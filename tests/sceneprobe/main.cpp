// SPDX-FileCopyrightText: 2026 Latte Dock contributors
// SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, derived)
// SPDX-FileCopyrightText: 2026 Bree Spektor
// SPDX-License-Identifier: GPL-2.0-or-later
//
// latte-sceneprobe: render a QML scene offscreen on Vulkan for N frames.
// Runs under a nested kwin_wayland session (wayland QPA) so QVulkanInstance works.
//
// Adopted from David Goree's latte-dock-qt6 (tests/sceneprobe/main.cpp at
// 52c11b36, github.com/CaptSilver/latte-dock-qt6; see
// docs/archive/captsilver-testability-adoption.md, P1). Local changes:
//  - LATTE_QML_IMPORT_PATH is a colon-separated list (our staging hands the
//    probe the same resolved import list the QML gates use), not one dir.
//  - The validation layer's presence is verified before instance creation:
//    QVulkanInstance drops unsupported layers SILENTLY, which would turn the
//    whole validation gate into a no-op on a machine without the layer.
//  - Missing goldens are device-aware: a hard failure on lavapipe (the CI
//    device), a loud golden-compare-skipped notice on opt-in devices (dgpu),
//    so a desktop GPU run is useful without a blessed dgpu set. (The compare
//    RIGOR is a separate axis, SCENEPROBE_TIER; see GoldenTier in
//    imagecompare.h - the device keys the golden filename, the tier keys how
//    tightly it compares.)
#include <QGuiApplication>
#include <QAnimationDriver>
#include <QScopeGuard>
#include <QVulkanInstance>
#include <QQuickRenderControl>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickRenderTarget>
#include <QImage>
#include <QFileInfo>
#include <rhi/qrhi.h>
#include <vulkan/vulkan.h>
#include <atomic>
#include <cstdio>
#include <fstream>
#include <optional>
#include <set>
#include <string>
#include "imagecompare.h"

static std::atomic_bool g_shaderError{false};

static void messageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    if (msg.contains(QLatin1String("Failed to deserialize QShader"))
        || msg.contains(QLatin1String("shader preparation failed"))
        || (ctx.category && QLatin1String(ctx.category) == QLatin1String("qt.scenegraph.general")
            && type >= QtWarningMsg)) {
        g_shaderError = true;
    }
    std::fprintf(stderr, "[qt] %s\n", qPrintable(msg)); // keep Qt output visible
}

static std::atomic_bool g_validationError{false};
static bool g_outputError = false;
static std::set<std::string> g_vkSuppress;

static VKAPI_ATTR VkBool32 VKAPI_CALL vkDebugCb(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT *data, void *)
{
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        const std::string id = data->pMessageIdName ? data->pMessageIdName : "";
        for (const std::string &s : g_vkSuppress) {
            if (!s.empty() && id.find(s) != std::string::npos) {
                std::fprintf(stderr, "[vk-validation SUPPRESSED] %s\n", data->pMessage);
                return VK_FALSE;
            }
        }
        g_validationError = true;
        std::fprintf(stderr, "[vk-validation ERROR] %s\n", data->pMessage);
    }
    return VK_FALSE;
}

// Read the rendered colour texture back into an 8-bit RGBA QImage. Runs a dedicated
// offscreen frame after the render loop; the texture persists from the last rendered frame.
static QImage readbackTexture(QRhi *rhi, QRhiTexture *tex)
{
    QRhiCommandBuffer *cb = nullptr;
    if (rhi->beginOffscreenFrame(&cb) != QRhi::FrameOpSuccess) return {};
    QRhiResourceUpdateBatch *u = rhi->nextResourceUpdateBatch();
    QRhiReadbackResult rb;
    bool done = false;
    rb.completed = [&done] { done = true; };
    u->readBackTexture(QRhiReadbackDescription(tex), &rb);
    cb->resourceUpdate(u);
    rhi->endOffscreenFrame(); // submits and waits; completed fires
    if (!done || rb.data.isEmpty()) return {};
    QImage img(reinterpret_cast<const uchar *>(rb.data.constData()),
               rb.pixelSize.width(), rb.pixelSize.height(), QImage::Format_RGBA8888);
    return img.copy(); // deep-copy out of rb.data
}

// Deterministic animation clock: advance a fixed step per rendered frame so animated
// scenes (e.g. multieffect_degenerate) read back reproducibly regardless of wall-clock.
class StepAnimationDriver : public QAnimationDriver {
public:
    void advance() override { m_elapsed += 16; advanceAnimation(); }
    qint64 elapsed() const override { return m_elapsed; }
private:
    qint64 m_elapsed = 0;
};

int main(int argc, char **argv)
{
    qInstallMessageHandler(messageHandler);
    qputenv("QSG_RHI_BACKEND", "vulkan");
    QGuiApplication app(argc, argv);

    if (app.arguments().size() < 2) { std::fprintf(stderr, "usage: latte-sceneprobe scene.qml\n"); return 2; }
    const QString scenePath = app.arguments().at(1);
    const int frames = 5;
    const QSize size(256, 256);

    // Golden-compare rigor is chosen by SCENEPROBE_TIER, decoupled from the
    // render device: the device keys the golden filename, the tier keys how
    // tightly the compare gates. Parse once at startup and refuse an unknown
    // value loudly here at the boundary - a typo'd tier must never silently
    // fall through to bit-exact (masking a real regression) or to tolerance
    // (hiding one). Unset defaults to bit-exact, so the NixOS/dev merge gate
    // is byte-unchanged when nothing sets it.
    const std::optional<LatteProbe::GoldenTier> tier =
        LatteProbe::parseGoldenTier(qgetenv("SCENEPROBE_TIER"));
    if (!tier) {
        std::fprintf(stderr, "FATAL: SCENEPROBE_TIER='%s' is not a known golden tier "
                             "(bitexact|tolerance)\n", qgetenv("SCENEPROBE_TIER").constData());
        return 2;
    }

    QVulkanInstance inst;
    // Refuse to run without the validation layer instead of letting
    // QVulkanInstance drop it silently (gate 2 would vanish without a trace).
    if (!inst.supportedLayers().contains(QByteArrayLiteral("VK_LAYER_KHRONOS_validation"))) {
        std::fprintf(stderr, "FATAL: VK_LAYER_KHRONOS_validation is not available to the Vulkan loader.\n"
                             "Set VK_LAYER_PATH to the validation layer manifests (the flake devShell\n"
                             "exports LATTE_VK_LAYER_PATH; run through scripts/sceneprobe-gate.sh).\n");
        return 2;
    }
    inst.setLayers({ QByteArrayLiteral("VK_LAYER_KHRONOS_validation") });
    inst.setExtensions({ QByteArrayLiteral("VK_EXT_debug_utils") });
    if (!inst.create()) { std::fprintf(stderr, "FATAL: QVulkanInstance::create failed (err %d)\n", inst.errorCode()); return 2; }

    auto createMsgr = (PFN_vkCreateDebugUtilsMessengerEXT)
        inst.getInstanceProcAddr("vkCreateDebugUtilsMessengerEXT");
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    if (createMsgr) {
        VkDebugUtilsMessengerCreateInfoEXT ci{};
        ci.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT
                           | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT;
        ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                       | VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT;
        ci.pfnUserCallback = vkDebugCb;
        createMsgr(inst.vkInstance(), &ci, nullptr, &messenger);
    }
    auto messengerGuard = qScopeGuard([&] {
        if (messenger != VK_NULL_HANDLE) {
            auto destroyMsgr = (PFN_vkDestroyDebugUtilsMessengerEXT)
                inst.getInstanceProcAddr("vkDestroyDebugUtilsMessengerEXT");
            if (destroyMsgr) destroyMsgr(inst.vkInstance(), messenger, nullptr);
        }
    });

    QQuickRenderControl renderControl;
    QQuickWindow window(&renderControl);
    window.setVulkanInstance(&inst);
    window.setColor(Qt::black);

    if (const QByteArray supPath = qgetenv("LATTE_VK_SUPPRESSIONS"); !supPath.isEmpty()) {
        std::ifstream f(supPath.constData());
        std::string line;
        while (std::getline(f, line)) {
            const auto a = line.find_first_not_of(" \t\r\n");
            if (a == std::string::npos || line[a] == '#') continue;
            const auto b = line.find_last_not_of(" \t\r\n");
            g_vkSuppress.insert(line.substr(a, b - a + 1));
        }
    }

    if (!renderControl.initialize()) { std::fprintf(stderr, "FATAL: renderControl.initialize failed\n"); return 2; }
    QRhi *rhi = renderControl.rhi();
    if (!rhi) { std::fprintf(stderr, "FATAL: no QRhi (backend not vulkan?)\n"); return 2; }

    const QRhiDriverInfo di = rhi->driverInfo();
    std::printf("render device: %s\n", di.deviceName.constData());

    QScopedPointer<QRhiTexture> tex(rhi->newTexture(QRhiTexture::RGBA8, size, 1,
        QRhiTexture::RenderTarget | QRhiTexture::UsedAsTransferSource));
    tex->create();
    QScopedPointer<QRhiRenderBuffer> ds(rhi->newRenderBuffer(QRhiRenderBuffer::DepthStencil, size, 1));
    ds->create();
    QRhiTextureRenderTargetDescription rtDesc(QRhiColorAttachment(tex.data()));
    rtDesc.setDepthStencilBuffer(ds.data());
    QScopedPointer<QRhiTextureRenderTarget> rt(rhi->newTextureRenderTarget(rtDesc));
    QScopedPointer<QRhiRenderPassDescriptor> rp(rt->newCompatibleRenderPassDescriptor());
    rt->setRenderPassDescriptor(rp.data());
    rt->create();
    window.setRenderTarget(QQuickRenderTarget::fromRhiRenderTarget(rt.data()));

    QQmlEngine engine;
    // Colon-separated list; later entries must win, and QQmlEngine prepends on
    // addImportPath, so feed them in reverse to preserve the list's order.
    const QList<QByteArray> extraImports = qgetenv("LATTE_QML_IMPORT_PATH").split(':');
    for (auto it = extraImports.rbegin(); it != extraImports.rend(); ++it)
        if (!it->isEmpty()) engine.addImportPath(QString::fromLocal8Bit(*it));
    QQmlComponent component(&engine, QUrl::fromLocalFile(scenePath));
    QObject *root = component.create();
    if (!root) {
        for (const QQmlError &e : component.errors()) std::fprintf(stderr, "QML: %s\n", qPrintable(e.toString()));
        return 2;
    }
    QQuickItem *rootItem = qobject_cast<QQuickItem *>(root);
    if (!rootItem) { std::fprintf(stderr, "FATAL: scene root is not a QQuickItem\n"); return 2; }
    rootItem->setParentItem(window.contentItem());
    window.contentItem()->setSize(size);
    window.setGeometry(0, 0, size.width(), size.height());
    rootItem->setSize(size);

    StepAnimationDriver animDriver;
    animDriver.install();

    for (int i = 0; i < frames; ++i) {
        animDriver.advance();
        renderControl.polishItems();
        renderControl.beginFrame();
        renderControl.sync();
        renderControl.render();
        renderControl.endFrame();
        QCoreApplication::processEvents();
    }

    QImage frame = readbackTexture(rhi, tex.data());
    if (frame.isNull()) { std::fprintf(stderr, "FATAL: texture read-back failed\n"); return 2; }

    {
        const QString inv = LatteProbe::checkInvariants(frame, 0.005);
        if (!inv.isEmpty()) {
            std::fprintf(stderr, "OUTPUT FAIL (invariants): %s\n", qPrintable(inv));
            g_outputError = true;
        }
        const QVariantList exps = root->property("probeExpect").toList();
        const QString exp = LatteProbe::checkExpectations(frame, exps);
        if (!exp.isEmpty()) {
            std::fprintf(stderr, "OUTPUT FAIL (probeExpect): %s\n", qPrintable(exp));
            g_outputError = true;
        }
    }

    // Self-test scenes (selftest-*) prove the gate's own pass/fail wiring via exit codes,
    // not pixel stability - they intentionally ship no reference, so skip the compare for
    // them. Without this guard the "missing reference fails" rule would trip selftest-good.
    if (!QFileInfo(scenePath).fileName().startsWith(QLatin1String("selftest-"))) {
        const QByteArray dev = qgetenv("SCENEPROBE_DEVICE");
        const QString device = dev.isEmpty() ? QStringLiteral("lavapipe") : QString::fromLocal8Bit(dev);
        const bool blessing = !qgetenv("SCENEPROBE_BLESS").isEmpty();
        QString base = scenePath; base.chop(4);
        const QString refPath = base + QStringLiteral(".expected.") + device + QStringLiteral(".png");
        const QString artDir = QString::fromLocal8Bit(qgetenv("SCENEPROBE_ARTIFACTS"));
        const QString stem = artDir.isEmpty() ? base : artDir + QStringLiteral("/") + QFileInfo(scenePath).completeBaseName();

        if (blessing) {
            frame.save(stem + QStringLiteral(".actual.png"));
            std::fprintf(stderr, "bless: candidate written for %s (%s)\n",
                         qPrintable(QFileInfo(scenePath).fileName()), qPrintable(device));
        } else {
            QImage ref(refPath);
            if (ref.isNull()) {
                frame.save(stem + QStringLiteral(".actual.png"));
                if (device == QLatin1String("lavapipe")) {
                    // the CI tier: a missing golden is a hard failure, or a
                    // deleted golden would silently gut the compare
                    std::fprintf(stderr, "OUTPUT FAIL: no reference for %s (%s) - run the gate with --bless to create it\n",
                                 qPrintable(QFileInfo(scenePath).fileName()), qPrintable(device));
                    g_outputError = true;
                } else {
                    // Opt-in devices (dgpu) are exploratory: their golden
                    // sets are blessed separately if ever, and a desktop GPU
                    // run must be useful without a blessing ceremony. The
                    // absence is reported loudly (never skipped in silence);
                    // the shader/validation/blank/probeExpect gates still
                    // verdict this run.
                    std::fprintf(stderr, "NOTE: no goldens blessed for device '%s' (%s) - golden compare skipped, all other gates still apply\n",
                                 qPrintable(device), qPrintable(QFileInfo(scenePath).fileName()));
                }
            } else {
                // The TIER (not the device) selects the compare rigor: every
                // matrix distro renders the lavapipe device and compares the
                // same .expected.lavapipe.png goldens, but a tolerance-tier
                // distro gates at a bounded delta rather than bit-exact. The
                // per-scene probeTolerance override below still layers on top.
                LatteProbe::CompareTolerance tol = LatteProbe::toleranceForTier(*tier);
                const QVariantMap pt = root->property("probeTolerance").toMap();
                if (pt.contains(QLatin1String("delta")) || pt.contains(QLatin1String("budget"))) {
                    tol.perChannelDelta = pt.value(QStringLiteral("delta"), tol.perChannelDelta).toInt();
                    tol.maxExceedFraction = pt.value(QStringLiteral("budget"), tol.maxExceedFraction).toDouble();
                }
                const LatteProbe::CompareResult r = LatteProbe::compareImages(frame, ref, tol);
                std::fprintf(stderr, "%s\n", qPrintable(LatteProbe::verdictLine(QFileInfo(scenePath).fileName(), device, r)));
                if (!r.match) {
                    std::fprintf(stderr, "  diff bbox: (%d,%d %dx%d)\n",
                                 r.diffBounds.x(), r.diffBounds.y(), r.diffBounds.width(), r.diffBounds.height());
                    frame.save(stem + QStringLiteral(".actual.png"));
                    ref.save(stem + QStringLiteral(".expected.png"));
                    LatteProbe::amplifiedDiff(frame, ref).save(stem + QStringLiteral(".diff.png"));
                    g_outputError = true;
                }
            }
        }
    }

    if (g_validationError) { std::fprintf(stderr, "GATE FAIL: Vulkan validation error\n"); return 1; }
    if (g_shaderError)     { std::fprintf(stderr, "GATE FAIL: Qt shader/scene-graph error\n"); return 1; }
    if (g_outputError)     { std::fprintf(stderr, "GATE FAIL: rendered output assertion failed\n"); return 1; }
    std::printf("rendered %d frames of %s - clean\n", frames, qPrintable(scenePath));
    return 0;
}
