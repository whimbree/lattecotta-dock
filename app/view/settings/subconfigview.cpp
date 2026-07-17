/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "subconfigview.h"

//local
#include <config-latte.h>
#include "../view.h"
#include "../../lattecorona.h"
#include "../../layouts/manager.h"
#include "../../plasma/extended/theme.h"
#include "../../settings/lengthoffsetclampbridge.h"
#include "../../settings/universalsettings.h"
#include "../../shortcuts/globalshortcuts.h"
#include "../../shortcuts/shortcutstracker.h"
#include "../../wm/abstractwindowinterface.h"
#include "../../wm/waylandlayershell.h"

// Qt
#include <QQmlEngine>

// KDE
#include <KLocalizedContext>
#include <KWindowSystem>

namespace Latte {
namespace ViewPart {

SubConfigView::SubConfigView(Latte::View *view, const QString &title, const bool &isNormalWindow)
    : QQuickView(nullptr),
      m_isNormalWindow(isNormalWindow)
{
    m_corona = qobject_cast<Latte::Corona *>(view->containment()->corona());

    //! setupWaylandIntegration() deliberately does not run here: it needs a
    //! valid m_latteView for the screen and location, and that is only
    //! assigned later through setParentView() -> initParentView(). The old
    //! plasma-shell surface could be created reactively on the
    //! SurfaceCreated platform event; a layer surface must be configured
    //! before the window is first shown, so initParentView() does it.

    if (KWindowSystem::isPlatformX11()) {
        m_corona->wm()->registerIgnoredWindow(WindowSystem::windowIdFromWId(winId()));
    } else {
        connect(this, &QWindow::windowTitleChanged, this, &SubConfigView::updateWaylandId);
        connect(m_corona->wm(), &WindowSystem::AbstractWindowInterface::latteWindowAdded, this, &SubConfigView::updateWaylandId);
    }

    m_validTitle = title;
    setTitle(m_validTitle);

    setScreen(view->screen());
    setIcon(qGuiApp->windowIcon());

    if (!m_isNormalWindow) {
        setFlags(wFlags());
        m_corona->wm()->setViewExtraFlags(this, true);
    }

    m_screenSyncTimer.setSingleShot(true);
    m_screenSyncTimer.setInterval(100);

    connections << connect(&m_screenSyncTimer, &QTimer::timeout, this, [this]() {
        if (!m_latteView) {
            return;
        }

        setScreen(m_latteView->screen());

        if (KWindowSystem::isPlatformX11()) {
            if (m_isNormalWindow) {
                setFlags(wFlags());
                m_corona->wm()->setViewExtraFlags(this, false, Latte::Types::NormalWindow);
            }
        }

        syncGeometry();
    });

    m_showTimer.setSingleShot(true);
    m_showTimer.setInterval(0);

    connections << connect(&m_showTimer, &QTimer::timeout, this, [this]() {
        syncSlideEffect();
        showWhenSized();
    });

    //! When the window is shown deferred (wayland, waiting for a real size),
    //! retry as soon as the content sizes it. With SizeViewToRootObject the
    //! view resizes to the QML root's implicit size during polish, after the
    //! 0 ms show timer has already fired.
    connections << connect(this, &QQuickView::widthChanged, this, [this]() {
        if (m_showDeferredUntilSized) {
            showWhenSized();
        }
    });
    connections << connect(this, &QQuickView::heightChanged, this, [this]() {
        if (m_showDeferredUntilSized) {
            showWhenSized();
        }
    });
}

bool SubConfigView::showWhenSized()
{
    if (isVisible()) {
        m_showDeferredUntilSized = false;
        return true;
    }

    //! Only the wlr-layer-shell backend rejects a zero-size surface; on X11 the
    //! window can always be shown and will size itself afterwards.
    if (KWindowSystem::isPlatformWayland() && size().isEmpty()) {
        m_showDeferredUntilSized = true;
        return false;
    }

    m_showDeferredUntilSized = false;
    show();
    return true;
}

SubConfigView::~SubConfigView()
{
    qDebug() << validTitle() << " deleting...";

    m_corona->dialogShadows()->removeWindow(this);

    m_corona->wm()->unregisterIgnoredWindow(KWindowSystem::isPlatformX11() ? WindowSystem::windowIdFromWId(winId()) : m_waylandWindowId);

    for (const auto &var : connections) {
        QObject::disconnect(var);
    }

    for (const auto &var : viewconnections) {
        QObject::disconnect(var);
    }
}

void SubConfigView::init()
{
    qDebug() << validTitle() << " : initialization started...";

    setDefaultAlphaBuffer(true);
    setColor(Qt::transparent);

    rootContext()->setContextProperty(QStringLiteral("viewConfig"), this);
    rootContext()->setContextProperty(QStringLiteral("shortcutsEngine"), m_corona->globalShortcuts()->shortcutsTracker());

    //! EX-18: the maxLength/minLength/offset clamp core's QML boundary;
    //! the maxLength ruler (canvas view) and the Appearance page (primary
    //! view) both call it, so it lives here where both engines initialize
    if (!m_lengthClampBridge) {
        m_lengthClampBridge = new Settings::LengthOffsetClampBridge(this);
    }
    rootContext()->setContextProperty(QStringLiteral("lengthClamp"), m_lengthClampBridge);

    if (m_corona) {
        rootContext()->setContextProperty(QStringLiteral("universalSettings"), m_corona->universalSettings());
        rootContext()->setContextProperty(QStringLiteral("layoutsManager"), m_corona->layoutsManager());
        rootContext()->setContextProperty(QStringLiteral("themeExtended"), m_corona->themeExtended());
    }

    //! KDeclarative's engine setup is gone in KF6; the i18n() context is the
    //! piece Latte needs from it
    auto *localizedContext = new KLocalizedContext(engine());
    localizedContext->setTranslationDomain(QStringLiteral("latte-dock"));
    engine()->rootContext()->setContextObject(localizedContext);

}

Qt::WindowFlags SubConfigView::wFlags() const
{
    return (flags() | Qt::FramelessWindowHint) & ~Qt::WindowDoesNotAcceptFocus;
}

QString SubConfigView::validTitle() const
{
    return m_validTitle;
}

Latte::WindowSystem::WindowId SubConfigView::trackedWindowId()
{
    //! wayland ids are compositor uuids and a uuid never parses as a
    //! number, so the Qt5-era 'toInt() <= 0' (id not yet assigned) test
    //! had become constant-true here: under wayland every call lazily
    //! re-resolves the id. Kept unconditional deliberately - the wm only
    //! emits latteWindowAdded for isAcceptableWindow() windows, which
    //! latte's own skip-taskbar config windows are not, so this lazy
    //! re-resolve is the reliable path. Tightening it to an isEmpty()
    //! check needs a live-session pass first.
    if (KWindowSystem::isPlatformWayland()) {
        updateWaylandId();
    }

    return !KWindowSystem::isPlatformWayland() ? WindowSystem::windowIdFromWId(winId()) : m_waylandWindowId;
}

Latte::Corona *SubConfigView::corona() const
{
    return m_corona;
}

Latte::View *SubConfigView::parentView() const
{
    return m_latteView;
}

void SubConfigView::setParentView(Latte::View *view, const bool &immediate)
{
    if (m_latteView == view) {
        return;
    }

    initParentView(view);
}

void SubConfigView::initParentView(Latte::View *view)
{
    for (const auto &var : viewconnections) {
        QObject::disconnect(var);
    }

    m_latteView = view;

    viewconnections << connect(m_latteView->positioner(), &ViewPart::Positioner::canvasGeometryChanged, this, &SubConfigView::syncGeometry);

    //! The config QML reads plasmoid.configuration, plasmoid.location and
    //! plasmoid.formFactor. On Plasma 5 the _plasma_graphicObject was one
    //! object carrying both the graphics and those data properties; Plasma 6
    //! splits them: AppletQuickItem::itemForApplet() is the graphics item and
    //! exposes .plasmoid (the applet) but NOT .configuration. The object that
    //! actually has configuration/location/formFactor is the Plasma::Applet
    //! (here the containment). Binding "plasmoid" to the graphics item left
    //! plasmoid.configuration undefined, so the canvas Loader
    //! (active: plasmoid && plasmoid.configuration && latteView) never loaded
    //! the blueprint and every settings binding read from undefined.
    rootContext()->setContextProperty(QStringLiteral("plasmoid"), m_latteView->containment());
    rootContext()->setContextProperty(QStringLiteral("latteView"), m_latteView);

    //! m_latteView is valid now, so the layer surface can be configured; it
    //! has to happen before the window is first shown, so skip it when a
    //! live config window is merely re-parented to another dock
    if (!isVisible()) {
        setupWaylandIntegration();
    }
}

void SubConfigView::requestActivate()
{
    if (KWindowSystem::isPlatformWayland()) {
        updateWaylandId();
        m_corona->wm()->requestActivate(m_waylandWindowId);
    } else {
        QQuickView::requestActivate();
    }
}

void SubConfigView::showAfter(int msecs)
{
    if (isVisible()) {
        return;
    }

    m_showTimer.setInterval(msecs);
    m_showTimer.start();

}

void SubConfigView::cancelDeferredShow()
{
    //! Both deferred-show mechanisms survive close(): the showAfter() timer
    //! keeps running and m_showDeferredUntilSized keeps re-arming show()
    //! from any later width/height change. Left alive across a deliberate
    //! hide they map the window again AFTER the configuring session ended -
    //! caught live as chrome windows lingering at wrong geometry across
    //! sessions and eating clicks aimed at the dock (the secondary
    //! type-chooser parked over the rearrange toggle). Every deliberate
    //! hide path must cancel them; transient hides (view retargeting,
    //! layer-surface remaps) must NOT, their re-show is the point.
    m_showTimer.stop();
    m_showDeferredUntilSized = false;
}

void SubConfigView::syncSlideEffect()
{
    if (!m_latteView || !m_latteView->containment()) {
        return;
    }

    auto slideLocation = WindowSystem::AbstractWindowInterface::Slide::None;

    switch (m_latteView->containment()->location()) {
    case Plasma::Types::TopEdge:
        slideLocation = WindowSystem::AbstractWindowInterface::Slide::Top;
        break;

    case Plasma::Types::RightEdge:
        slideLocation = WindowSystem::AbstractWindowInterface::Slide::Right;
        break;

    case Plasma::Types::BottomEdge:
        slideLocation = WindowSystem::AbstractWindowInterface::Slide::Bottom;
        break;

    case Plasma::Types::LeftEdge:
        slideLocation = WindowSystem::AbstractWindowInterface::Slide::Left;
        break;

    default:
        qDebug() << staticMetaObject.className() << "wrong location";
        break;
    }

    m_corona->wm()->slideWindow(*this, slideLocation);
}

void SubConfigView::setupWaylandIntegration()
{
    if (!KWindowSystem::isPlatformWayland() || !m_latteView || !m_latteView->containment()) {
        return;
    }

    //! Associate a layer surface but leave it unanchored and unsized. These are
    //! floating, content-sized config windows, not edge docks: seeding a size
    //! here (as configureView() does for the dock) lets the window be shown
    //! before its QML has laid out, and a wlr-layer surface committed at zero
    //! width without spanning both horizontal screen edges is a fatal protocol
    //! error. showWhenSized() holds the first commit until syncGeometry() has a
    //! real size, and each subclass places the surface from there (the settings
    //! windows stay unanchored/centered, the canvas overlays the dock geometry).
    //! Config windows take keyboard focus, unlike the dock.
    namespace LS = Latte::WindowSystem::LayerShell;
    LS::setUnanchored(this);
    LS::applyLayer(this, Latte::Types::AlwaysVisible); //! keep config windows on the top layer
    LS::setFocusPolicy(this, true);

    updateWaylandId();
    syncGeometry();
}

void SubConfigView::showEvent(QShowEvent *ev)
{
    QQuickView::showEvent(ev);

    if (KWindowSystem::isPlatformWayland()) {
        //! readd shadows after hiding because the window shadows are not shown again after first showing
        m_corona->dialogShadows()->addWindow(this, m_enabledBorders);
    }
}

void SubConfigView::updateWaylandId()
{
    Latte::WindowSystem::WindowId newId = m_corona->wm()->winIdFor("latte-dock", validTitle());

    if (m_waylandWindowId != newId) {
        if (!m_waylandWindowId.isNull()) {
            m_corona->wm()->unregisterIgnoredWindow(m_waylandWindowId);
        }

        m_waylandWindowId = newId;
        m_corona->wm()->registerIgnoredWindow(m_waylandWindowId);
    }
}

KSvg::FrameSvg::EnabledBorders SubConfigView::enabledBorders() const
{
    return m_enabledBorders;
}

}
}
