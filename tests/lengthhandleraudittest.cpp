/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The length cluster's real-handler WIRING audit (CL-1 in
// docs/tracking/edit-mode-settings-audit-plan.md). It is the settingswiringharnesstest
// pattern (a control's handler body run as a QML fragment against a stub
// QQmlPropertyMap, then the Tier B diff predicates in
// tests/units/configsnapshotdiff.h over the delta), extended with the one thing
// the length handlers need that the CL-0 harness does not register: the
// LengthOffsetClampBridge exposed as the `lengthClamp` context property, exactly
// as SubConfigView::init() registers it live. The maxLength ruler and the
// Appearance-page sliders all route their math through that bridge, so a
// faithful wiring test must drive the REAL clamp, not a re-implementation.
//
// It answers three length-cluster questions deterministically:
//   - D15 (AU-1a): which config keys the Maximum ruler changes at and away from
//     the max==min boundary - pinning the min-coupling as ACCEPTED, intended,
//     Qt5-faithful behaviour (Bree's decision, section 8), not a bug to remove.
//   - D16 (AU-1b): the slider-handle binding clobber - a plain `value:` binding
//     desyncs after an imperative assignment; the proxy-property re-sync pattern
//     re-tracks. The regression guard for the fix.
//   - AU-1e/AU-1f: the offset slider is the correct re-sync reference, and the
//     fine-value ctrl-wheel / click-to-round rows write only their own key.

// Qt
#include <QJsonObject>
#include <QJsonValue>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlPropertyMap>
#include <QScopedPointer>
#include <QTest>
#include <QVariant>

#include "configsnapshotdiff.h"
#include "lengthoffsetclampbridge.h"

using namespace Latte::AuditHarness;

//! the maxLength ruler's real write-back body (RulerMouseArea.qml
//! updateMaxLength), adapted only in that `plasmoid.configuration` becomes the
//! stub map `configuration` and the wheel step is the injected local `step`.
//! Everything that decides WHICH keys move - the coupled min write guard, the
//! always-write of maxLength, the offset write guard - is verbatim, so the diff
//! it produces is the diff the live ruler produces.
static const QString kRulerUpdateMaxLengthBody = QStringLiteral(
    "var clamped = lengthClamp.clampMaxLengthByStep(configuration.maxLength,\n"
    "                                               configuration.minLength,\n"
    "                                               configuration.offset,\n"
    "                                               step,\n"
    "                                               configuration.alignment);\n"
    "if (configuration.minLength !== clamped.minLength) {\n"
    "    configuration.minLength = clamped.minLength;\n"
    "}\n"
    "configuration.maxLength = clamped.maxLength;\n"
    "if (configuration.offset !== clamped.offset) {\n"
    "    configuration.offset = clamped.offset;\n"
    "}\n");

class LengthHandlerAuditTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    //! AU-1a D15: the accepted min-coupling, pinned by its exact key delta
    void rulerAwayFromBoundaryMovesOnlyMaximum();
    void rulerAtCoupledBoundaryAlsoMovesMinimum();
    void rulerFloorCatchMovesOnlyMaximumNotCoupling();

    //! AU-1b D16: the slider-handle binding clobber and its proxy re-sync fix
    void plainValueBindingDesyncsAfterClobber();
    void proxyResyncReTracksAfterClobber();

    //! AU-1e: the offset slider (control 54) is the correct re-sync reference
    void offsetWritePathWritesOnlyOffsetWhenInRange();
    void offsetWritePathHealsMaxLengthOnlyWhenCorrupt();

    //! AU-1f: the fine value scroll/click rows (controls 51, 53, 55)
    void maxLengthCtrlWheelWritesOnlyMaximumEvenWhenCoupled();
    void maxLengthClickRoundsOnlyMaximum();
    void minAndOffsetFineRowsWriteOnlyTheirOwnKey();

private:
    static void seed(QQmlPropertyMap &config,
                     std::initializer_list<std::pair<QString, QVariant>> pairs);

    //! run a handler body against the stub map, with BOTH the map (as
    //! `configuration`) and a real LengthOffsetClampBridge (as `lengthClamp`)
    //! in scope. Returns the QML error string (empty on success) so a malformed
    //! fragment fails loudly instead of reading as a silent no-op.
    static QString runHandler(QQmlPropertyMap &config, const QString &handlerBody);

    //! the stub map's values as a "config" JSON object, the same shape
    //! viewConfigData()'s "config" carries (dbusreports.h), so the diff
    //! predicates see the same thing live and stubbed.
    static QJsonObject snapshot(const QQmlPropertyMap &config);
};

void LengthHandlerAuditTest::seed(QQmlPropertyMap &config,
                                  std::initializer_list<std::pair<QString, QVariant>> pairs)
{
    for (const auto &pair : pairs) {
        config.insert(pair.first, pair.second);
    }
}

QString LengthHandlerAuditTest::runHandler(QQmlPropertyMap &config, const QString &handlerBody)
{
    QQmlEngine engine;
    Latte::Settings::LengthOffsetClampBridge clamp;
    engine.rootContext()->setContextProperty(QStringLiteral("configuration"), &config);
    engine.rootContext()->setContextProperty(QStringLiteral("lengthClamp"), &clamp);

    QQmlComponent component(&engine);
    const QString qml = QStringLiteral(
        "import QtQml\n"
        "QtObject { Component.onCompleted: { %1 } }\n").arg(handlerBody);
    component.setData(qml.toUtf8(), QUrl());

    if (component.isError()) {
        return component.errorString();
    }

    QScopedPointer<QObject> root(component.create());

    if (!root) {
        return component.errorString().isEmpty()
            ? QStringLiteral("handler fragment produced no object") : component.errorString();
    }

    return QString();
}

QJsonObject LengthHandlerAuditTest::snapshot(const QQmlPropertyMap &config)
{
    QJsonObject json;
    const QStringList keys = config.keys();

    for (const QString &key : keys) {
        const QVariant value = config.value(key);
        const QJsonValue direct = QJsonValue::fromVariant(value);

        if (direct.type() != QJsonValue::Null && direct.type() != QJsonValue::Undefined) {
            json[key] = direct;
        } else {
            json[key] = value.toString();
        }
    }

    return json;
}

//! AU-1a: away from the boundary (maxLength > minLength) the ruler moves ONLY
//! maxLength - the min-coupling does not fire, so a normal-length dock's
//! Minimum is never touched. This is the P2 baseline the accepted coupling is
//! measured against.
void LengthHandlerAuditTest::rulerAwayFromBoundaryMovesOnlyMaximum()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("maxLength"), 100}, {QStringLiteral("minLength"), 30},
                  {QStringLiteral("offset"), 0}, {QStringLiteral("alignment"), 0}}); // Center

    const QJsonObject before = snapshot(config);
    QCOMPARE(runHandler(config, QStringLiteral("var step = -6;\n") + kRulerUpdateMaxLengthBody), QString());
    const QJsonObject after = snapshot(config);

    QVERIFY(controlApplies(before, after, QStringLiteral("maxLength")));
    QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("maxLength")}));
    QVERIFY(valueReflects(after, QStringLiteral("maxLength"), 94));
    QVERIFY2(!changedConfigKeys(before, after).contains(QStringLiteral("minLength")),
             "away from the max==min boundary the ruler must not touch minLength");
}

//! AU-1a D15 ACCEPTED (Bree, section 8): at the max==min boundary the ruler
//! moves BOTH maxLength and minLength - the coupling keeps a fixed-length dock
//! (max==min) fixed as the ruler scrolls. This is intended, Qt5-faithful
//! behaviour (latte-dock-qt6 carries the identical `updateminimumlength =
//! (maxLength === minLength)` guard), pinned here as a passing regression that
//! DOCUMENTS the coupling rather than a bug the audit fails on.
void LengthHandlerAuditTest::rulerAtCoupledBoundaryAlsoMovesMinimum()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("maxLength"), 100}, {QStringLiteral("minLength"), 100},
                  {QStringLiteral("offset"), 0}, {QStringLiteral("alignment"), 0}}); // Center

    const QJsonObject before = snapshot(config);
    QCOMPARE(runHandler(config, QStringLiteral("var step = -6;\n") + kRulerUpdateMaxLengthBody), QString());
    const QJsonObject after = snapshot(config);

    QVERIFY(controlApplies(before, after, QStringLiteral("maxLength")));
    QVERIFY(controlApplies(before, after, QStringLiteral("minLength")));
    QVERIFY(onlyExpectedKeysChanged(before, after,
                                    {QStringLiteral("maxLength"), QStringLiteral("minLength")}));
    //! the pair stays equal (the fixed-length invariant the coupling protects)
    QVERIFY(valueReflects(after, QStringLiteral("maxLength"), 94));
    QVERIFY(valueReflects(after, QStringLiteral("minLength"), 94));
}

//! AU-1a boundary precision: crossing INTO the boundary (maxLength above
//! minLength, stepped down onto it) is the min FLOOR catching maxLength, not
//! the coupling dragging minLength. Only maxLength moves; minLength is a floor
//! the wheel lands on, not a value it edits. This distinguishes the two
//! mechanisms the plan (section 1, D15) folds together in prose.
void LengthHandlerAuditTest::rulerFloorCatchMovesOnlyMaximumNotCoupling()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("maxLength"), 45}, {QStringLiteral("minLength"), 40},
                  {QStringLiteral("offset"), 0}, {QStringLiteral("alignment"), 1}}); // Left / Edge

    const QJsonObject before = snapshot(config);
    QCOMPARE(runHandler(config, QStringLiteral("var step = -6;\n") + kRulerUpdateMaxLengthBody), QString());
    const QJsonObject after = snapshot(config);

    QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("maxLength")}));
    QVERIFY(valueReflects(after, QStringLiteral("maxLength"), 40));   // floored at min, not below
    QVERIFY2(!changedConfigKeys(before, after).contains(QStringLiteral("minLength")),
             "the floor catch must leave minLength unedited - it is a floor, not a coupled write");
}

//! AU-1b D16, the bug half: a plain declarative `value: configuration.maxLength`
//! binding (what maxLengthSlider carried) TRACKS config until `value` is
//! assigned imperatively - the first drag, or the drag-snap - which DESTROYS
//! the binding. After that, config-driven changes (the canvas ruler, an
//! external edit) no longer move the handle: the ruler and the slider desync.
//! This is the mechanism the fix removes; pinned as a passing document of why
//! the plain binding is wrong, not as a target state.
void LengthHandlerAuditTest::plainValueBindingDesyncsAfterClobber()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("maxLength"), 100}});

    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("configuration"), &config);
    QQmlComponent component(&engine);
    //! clobber() is a QML-side assignment on purpose: a drag / the drag-snap
    //! writes `value` from WITHIN QML, which is what destroys a QML binding. A
    //! C++ setProperty would not - the binding would re-assert - so the clobber
    //! must originate in QML to reproduce D16 faithfully.
    component.setData(
        "import QtQml\n"
        "QtObject {\n"
        "  property real value: configuration.maxLength\n"
        "  function clobber(v) { value = v; }\n"
        "}\n", QUrl());
    QScopedPointer<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));

    //! live binding: config drives the handle before any imperative write
    QCOMPARE(root->property("value").toDouble(), 100.0);
    config.insert(QStringLiteral("maxLength"), 80);
    QCOMPARE(root->property("value").toDouble(), 80.0);

    //! an imperative QML assignment (a drag / the drag-snap) destroys the binding
    QMetaObject::invokeMethod(root.data(), "clobber", Q_ARG(QVariant, QVariant(50.0)));
    QCOMPARE(root->property("value").toDouble(), 50.0);

    //! and now the ruler cannot move the handle - the desync D16 is about
    config.insert(QStringLiteral("maxLength"), 90);
    QVERIFY2(root->property("value").toDouble() != 90.0,
             "a plain value binding must NOT re-track after being clobbered - "
             "this is the D16 desync the proxy pattern fixes");
    QCOMPARE(root->property("value").toDouble(), 50.0);
}

//! AU-1b D16, the fix half: the shipped pattern (maxLengthSlider's
//! maxLengthValue proxy + resyncMaxLengthHandle, mirroring the offset slider).
//! `value` is never declaratively bound, so an imperative write cannot clobber
//! a binding; a proxy property STAYS bound to config and re-syncs the handle
//! whenever config changes, except while pressed so a live drag owns the value.
void LengthHandlerAuditTest::proxyResyncReTracksAfterClobber()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("maxLength"), 100}});

    QQmlEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("configuration"), &config);
    QQmlComponent component(&engine);
    component.setData(
        "import QtQml\n"
        "QtObject {\n"
        "  property real value: 0\n"
        "  property bool pressed: false\n"
        "  property real proxy: configuration.maxLength\n"
        "  function resync() { if (!pressed) { value = proxy; } }\n"
        "  function clobber(v) { value = v; }\n"
        "  onProxyChanged: resync()\n"
        "  Component.onCompleted: resync()\n"
        "}\n", QUrl());
    QScopedPointer<QObject> root(component.create());
    QVERIFY2(root, qPrintable(component.errorString()));

    QCOMPARE(root->property("value").toDouble(), 100.0);   // initial sync

    //! clobber value the way a drag does (QML-side assignment) - the proxy
    //! binding is separate and survives it
    QMetaObject::invokeMethod(root.data(), "clobber", Q_ARG(QVariant, QVariant(50.0)));
    QCOMPARE(root->property("value").toDouble(), 50.0);

    //! the ruler now DOES move the handle again: re-sync fired off the proxy
    config.insert(QStringLiteral("maxLength"), 90);
    QCOMPARE(root->property("value").toDouble(), 90.0);

    //! while pressed the drag owns the value: re-sync is suppressed so config
    //! changes do not yank the handle mid-drag
    root->setProperty("pressed", true);
    QMetaObject::invokeMethod(root.data(), "clobber", Q_ARG(QVariant, QVariant(42.0)));
    config.insert(QStringLiteral("maxLength"), 77);
    QCOMPARE(root->property("value").toDouble(), 42.0);

    //! and the handle re-tracks again once released
    root->setProperty("pressed", false);
    config.insert(QStringLiteral("maxLength"), 63);
    QCOMPARE(root->property("value").toDouble(), 63.0);
}

//! the offset slider's config-driven write path (offsetSlider.updateOffset,
//! the not-pressed re-sync branch): bound the config offset into range, then
//! shrink maxLength only if the pair still leaves the screen. This is the P3
//! re-sync branch (requested = plasmoid.configuration.offset), verbatim except
//! for the stub-map rename, so its key delta is the live delta.
static const QString kOffsetResyncBody = QStringLiteral(
    "var requested = configuration.offset;\n"
    "var clamped = lengthClamp.clampOffset(configuration.maxLength,\n"
    "                                      requested,\n"
    "                                      configuration.alignment);\n"
    "configuration.offset = clamped.offset;\n"
    "if (configuration.maxLength !== clamped.maxLength) {\n"
    "    configuration.maxLength = clamped.maxLength;\n"
    "}\n");

//! AU-1e: the offset slider is the in-tree re-sync REFERENCE the D16 fix
//! copied to Maximum/Minimum, so it must itself stay P2-correct. On a sane
//! (in-range) maxLength it writes ONLY offset - never a stray maxLength - which
//! is exactly the "writes its own key and nothing else" property the whole
//! audit checks.
void LengthHandlerAuditTest::offsetWritePathWritesOnlyOffsetWhenInRange()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("maxLength"), 80}, {QStringLiteral("minLength"), 30},
                  {QStringLiteral("offset"), 200}, {QStringLiteral("alignment"), 0}}); // Center

    const QJsonObject before = snapshot(config);
    QCOMPARE(runHandler(config, kOffsetResyncBody), QString());
    const QJsonObject after = snapshot(config);

    //! the out-of-range request is pulled into the centered bound [-10, 10]
    QVERIFY(controlApplies(before, after, QStringLiteral("offset")));
    QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("offset")}));
    QVERIFY(valueReflects(after, QStringLiteral("offset"), 10));
    QVERIFY2(!changedConfigKeys(before, after).contains(QStringLiteral("maxLength")),
             "an in-range offset re-sync must not resize the dock");
}

//! AU-1e: the offset slider's ONE legitimate coupled write is healing a
//! corrupt (out-of-range) maxLength back on-screen (the catalog's "+maxLength
//! via clampOffset"). That coupling is intended and documented, not a stray
//! side effect: it fires only when the stored maxLength is already invalid.
void LengthHandlerAuditTest::offsetWritePathHealsMaxLengthOnlyWhenCorrupt()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("maxLength"), 150}, {QStringLiteral("minLength"), 30},
                  {QStringLiteral("offset"), 0}, {QStringLiteral("alignment"), 0}}); // Center, corrupt max

    const QJsonObject before = snapshot(config);
    QCOMPARE(runHandler(config, kOffsetResyncBody), QString());
    const QJsonObject after = snapshot(config);

    //! both move: the inverted bounds land offset on `to` and the shrink
    //! branch pulls maxLength back on-screen (matches the core's
    //! clampOffset_outOfRangeMaxLengthSelfHeals)
    QVERIFY(onlyExpectedKeysChanged(before, after,
                                    {QStringLiteral("maxLength"), QStringLiteral("offset")}));
    QVERIFY(valueReflects(after, QStringLiteral("maxLength"), 50));
    QVERIFY(valueReflects(after, QStringLiteral("offset"), -25));
}

//! AU-1f: the Maximum value's ctrl-wheel row (control 51) writes maxLength
//! RAW - it does not route through the clamp - so it can never drag minLength,
//! not even at the max==min boundary where the RULER wheel does (AU-1a). Pinned
//! at that exact boundary to contrast the two Maximum paths: the ruler couples,
//! the fine value does not.
void LengthHandlerAuditTest::maxLengthCtrlWheelWritesOnlyMaximumEvenWhenCoupled()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("maxLength"), 90}, {QStringLiteral("minLength"), 90},
                  {QStringLiteral("offset"), 0}, {QStringLiteral("alignment"), 0}}); // coupled at 90

    const QJsonObject before = snapshot(config);
    //! the ScrollArea onScrolledUp ctrl-modifier body, smallStep 0.1
    QCOMPARE(runHandler(config,
             QStringLiteral("configuration.maxLength = configuration.maxLength + 0.1;")), QString());
    const QJsonObject after = snapshot(config);

    QVERIFY(controlApplies(before, after, QStringLiteral("maxLength")));
    QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("maxLength")}));
    QVERIFY2(!changedConfigKeys(before, after).contains(QStringLiteral("minLength")),
             "the fine-value ctrl-wheel bypasses the clamp, so it must not drag "
             "minLength even at the coupled boundary");
}

//! AU-1f: the Maximum value's click row (control 51) rounds maxLength to a
//! whole percent and writes only maxLength (P2 + P3).
void LengthHandlerAuditTest::maxLengthClickRoundsOnlyMaximum()
{
    QQmlPropertyMap config;
    seed(config, {{QStringLiteral("maxLength"), 90.4}, {QStringLiteral("minLength"), 30},
                  {QStringLiteral("offset"), 0}, {QStringLiteral("alignment"), 0}});

    const QJsonObject before = snapshot(config);
    QCOMPARE(runHandler(config,
             QStringLiteral("configuration.maxLength = Math.round(configuration.maxLength);")), QString());
    const QJsonObject after = snapshot(config);

    QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("maxLength")}));
    QVERIFY(valueReflects(after, QStringLiteral("maxLength"), 90));
}

//! AU-1f: the Minimum (control 53) and Offset (control 55) fine rows each write
//! only their own key - the same raw-write shape, no coupling, no clamp.
void LengthHandlerAuditTest::minAndOffsetFineRowsWriteOnlyTheirOwnKey()
{
    {
        QQmlPropertyMap config;
        seed(config, {{QStringLiteral("maxLength"), 100}, {QStringLiteral("minLength"), 30},
                      {QStringLiteral("offset"), 0}, {QStringLiteral("alignment"), 0}});
        const QJsonObject before = snapshot(config);
        QCOMPARE(runHandler(config,
                 QStringLiteral("configuration.minLength = configuration.minLength + 0.1;")), QString());
        const QJsonObject after = snapshot(config);
        QVERIFY(controlApplies(before, after, QStringLiteral("minLength")));
        QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("minLength")}));
    }
    {
        QQmlPropertyMap config;
        seed(config, {{QStringLiteral("maxLength"), 100}, {QStringLiteral("minLength"), 30},
                      {QStringLiteral("offset"), 5.4}, {QStringLiteral("alignment"), 0}});
        const QJsonObject before = snapshot(config);
        QCOMPARE(runHandler(config,
                 QStringLiteral("configuration.offset = Math.round(configuration.offset);")), QString());
        const QJsonObject after = snapshot(config);
        QVERIFY(onlyExpectedKeysChanged(before, after, {QStringLiteral("offset")}));
        QVERIFY(valueReflects(after, QStringLiteral("offset"), 5));
    }
}

QTEST_GUILESS_MAIN(LengthHandlerAuditTest)

#include "lengthhandleraudittest.moc"
