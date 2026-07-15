/*
    SPDX-FileCopyrightText: 2018 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "canvasconfigview.h"

#include <QQmlProperty>

// local
#include "primaryconfigview.h"
#include "secondaryconfigview.h"
#include "../panelshadows_p.h"
#include "../view.h"
#include "../../lattecorona.h"
#include "../../settings/universalsettings.h"
#include "../../wm/abstractwindowinterface.h"
#include "../../wm/waylandlayershell.h"

// Qt
#include <QQuickItem>
#include <QScreen>

// KDE
#include <KWindowSystem>

// Plasma
#include <KPackage/Package>

namespace Latte {
namespace ViewPart {

CanvasConfigView::CanvasConfigView(Latte::View *view, PrimaryConfigView *parent)
    : SubConfigView(view, QString("#canvasconfigview#"), false),
      m_parent(parent)
{
    setResizeMode(QQuickView::SizeRootObjectToView);

    //! the input region needs re-carving when the configure-applets mode
    //! flips (the click-through band appears/disappears) and after a resize
    //! (a resize resets the surface mask)
    if (m_corona && m_corona->universalSettings()) {
        connect(m_corona->universalSettings(), &Latte::UniversalSettings::inConfigureAppletsModeChanged,
                this, &CanvasConfigView::updateInputRegion);
    }
    connect(this, &QQuickView::widthChanged, this, &CanvasConfigView::updateInputRegion);
    connect(this, &QQuickView::heightChanged, this, &CanvasConfigView::updateInputRegion);

    //! the interactive chrome rect lives in the canvas QML
    //! (CanvasConfiguration.rearrangeToggleRect) and moves with layout and
    //! parabolic metrics, so the input region must follow it
    connect(this, &QQuickView::statusChanged, this, [this](QQuickView::Status status) {
        if (status == QQuickView::Ready && rootObject()) {
            connect(rootObject(), SIGNAL(rearrangeToggleRectChanged()),
                    this, SLOT(updateInputRegion()), Qt::UniqueConnection);
            updateInputRegion();
        }
    });

    setParentView(view);
    init();
}

void CanvasConfigView::init()
{
    SubConfigView::init();

    QByteArray tempFilePath = "canvasconfigurationui";

    updateEnabledBorders();

    auto source = QUrl::fromLocalFile(m_latteView->containment()->corona()->kPackage().filePath(tempFilePath));
    setSource(source);
    connectRootObject();
    syncGeometry();

    if (m_parent && KWindowSystem::isPlatformX11()) {
        m_parent->requestActivate();
    }
}

QRect CanvasConfigView::geometryWhenVisible() const
{
    return m_geometryWhenVisible;
}

void CanvasConfigView::connectRootObject()
{
    if (!rootObject()) {
        return;
    }

    //! The configure-applets input mask is carved to the published
    //! rearrangeToggleRect. That rect settles asynchronously (layouts run
    //! after the mode flips), so a single sample at mode-change time froze
    //! whatever garbage it held: observed live as a full-width 26px stripe
    //! that ate hover across every applet's middle. Re-carve whenever the
    //! published rect changes.
    QQmlProperty toggleRect(rootObject(), QStringLiteral("rearrangeToggleRect"));
    toggleRect.connectNotifySignal(this, metaObject()->indexOfMethod("updateInputRegion()"));
}

void CanvasConfigView::initParentView(Latte::View *view)
{
    const bool viewswitch = m_latteView && m_latteView != view;

    SubConfigView::initParentView(view);

    //! the dock strip is excluded from the canvas input region; re-carve
    //! whenever the dock's rect moves or breathes so the exclusion tracks it
    viewconnections << connect(m_latteView, &Latte::View::absoluteGeometryChanged,
                               this, &CanvasConfigView::updateInputRegion);

    //! RELOAD the canvas content when the shared chrome retargets to another
    //! view. The content's positional bindings (the rotated header, the
    //! ruler) branch on per-view context (form factor, location), and each
    //! branch captures DIFFERENT dependencies: a binding evaluated mid-switch
    //! through the old view's branch can strand on a transient value with no
    //! remaining dependency to wake it (observed live: the vertical header's
    //! y frozen at -13 while its width re-evaluated to 1440, putting the
    //! rearrange chip ~600px above the canvas and making rearrange unusable
    //! on vertical docks). A fresh instantiation against the settled context
    //! cannot strand.
    if (viewswitch && source().isValid()) {
        const QUrl src = source();
        setSource(QUrl());
        setSource(src);
        connectRootObject();
    }

    rootContext()->setContextProperty(QStringLiteral("primaryConfigView"), m_parent);

    updateEnabledBorders();
    syncGeometry();
}

void CanvasConfigView::syncGeometry()
{
    if (!m_latteView || !m_latteView->layout() || !m_latteView->containment() || !m_parent || !rootObject()) {
        return;
    }

    updateEnabledBorders();

    auto geometry = m_latteView->positioner()->canvasGeometry();

    if (m_geometryWhenVisible == geometry) {
        return;
    }

    m_geometryWhenVisible = geometry;

    if (KWindowSystem::isPlatformWayland()) {
        //! layer-shell ignores setPosition(), and the Center-anchored config
        //! from SubConfigView would drop the canvas centred on the dock's
        //! edge; anchor it so it overlays the dock's canvas geometry exactly
        Latte::WindowSystem::LayerShell::applyCanvasPlacement(this, m_latteView->location(), m_latteView->screen(), geometry, m_latteView->screenGeometry());
    } else {
        setPosition(geometry.topLeft());
    }

    setMaximumSize(geometry.size());
    setMinimumSize(geometry.size());
    resize(geometry.size());

    updateInputRegion();

    //! after placement request to activate the main config window in order to avoid
    //! rare cases of closing settings window from secondaryConfigView->focusOutEvent
    if (m_parent && KWindowSystem::isPlatformX11()) {
        m_parent->requestActivate();
    }
}

void CanvasConfigView::updateInputRegion()
{
    if (!m_latteView) {
        return;
    }

    //! never touch the surface mask before the wayland surface exists (same
    //! guard as the sibling views' updateEffects,
    //! https://bugs.kde.org/show_bug.cgi?id=392890)
    if (KWindowSystem::isPlatformWayland() && !isVisible()) {
        return;
    }

    const bool configuring = m_corona && m_corona->universalSettings()
            && m_corona->universalSettings()->inConfigureAppletsMode();

    //! In configure-applets mode the canvas goes click-through so pointer
    //! events reach the widgets beneath, EXCEPT the rearrange toggle's rect
    //! (published by CanvasConfiguration.qml in window coordinates). Without
    //! it the toggle cannot be unpressed: the click falls through to the
    //! dock, the settings window loses focus and the whole edit mode closes
    //! instead of just leaving the rearrange sub-mode.
    QRect chrome;

    if (configuring && rootObject()) {
        const QRectF published = rootObject()->property("rearrangeToggleRect").toRectF();

        if (!published.isEmpty()) {
            chrome = published.toAlignedRect();
        }
    }

    //! the dock's own strip in canvas-local coordinates: input there must
    //! always fall through to the dock (see canvasInputRegion's contract).
    //! m_geometryWhenVisible is the authoritative canvas placement; window
    //! position() lies on wayland layer-shell.
    const QRect dockStrip = m_latteView->absoluteGeometry().translated(-m_geometryWhenVisible.topLeft());

    setMask(Latte::WindowSystem::LayerShell::canvasInputRegion(configuring, size(), chrome, dockStrip));
}

bool CanvasConfigView::event(QEvent *e)
{
    bool result = SubConfigView::event(e);

    switch (e->type()) {
    case QEvent::Enter:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
        if (m_parent) {
            m_parent->requestActivate();
        }
        break;
    default:
        break;
    }

    return result;
}

void CanvasConfigView::showEvent(QShowEvent *ev)
{
    SubConfigView::showEvent(ev);

    if (!m_latteView) {
        return;
    }

    syncGeometry();

    //! syncGeometry() short-circuits on unchanged geometry and its init-time
    //! run happened before the wayland surface existed, so carve the input
    //! region explicitly now that the surface is up
    updateInputRegion();

    //! show Canvas on top of all other panels/docks and show
    //! its parent view on top afterwards
    m_corona->wm()->setViewExtraFlags(this, true);

    QTimer::singleShot(100, this, [this]() {
        //! delay execution in order to take influence after last Canvas on top call
        if (m_parent) {
            m_parent->requestActivate();
        }
    });

    m_screenSyncTimer.start();
    QTimer::singleShot(400, this, &CanvasConfigView::syncGeometry);

    Q_EMIT showSignal();
}

void CanvasConfigView::focusOutEvent(QFocusEvent *ev)
{
    Q_UNUSED(ev);

    if (!m_latteView) {
        return;
    }

    const auto *focusWindow = qGuiApp->focusWindow();

    if (focusWindow && (focusWindow->flags().testFlag(Qt::Popup)
                         || focusWindow->flags().testFlag(Qt::ToolTip))) {
        return;
    }

    //! same family rule as PrimaryConfigView::focusOutEvent: focus moving to
    //! the primary or chooser window is not a loss, and checking the new
    //! focus window's identity avoids racing its not-yet-landed isActive()
    if (focusWindow && m_parent
        && (focusWindow == m_parent || focusWindow == m_parent->secondaryWindow())) {
        return;
    }

    const auto parent = qobject_cast<PrimaryConfigView *>(m_parent);

    if (!parent->hasFocus()) {
        parent->hideConfigWindow();
    }
}

void CanvasConfigView::hideConfigWindow()
{
    if (KWindowSystem::isPlatformWayland()) {
        //!NOTE: Avoid crash in wayland environment with qt5.9
        close();
    } else {
        hide();
    }
}

//!BEGIN borders
void CanvasConfigView::updateEnabledBorders()
{
    if (!this->screen()) {
        return;
    }

    KSvg::FrameSvg::EnabledBorders borders = KSvg::FrameSvg::TopBorder;

    switch (m_latteView->location()) {
    case Plasma::Types::TopEdge:
        borders = KSvg::FrameSvg::BottomBorder;
        break;

    case Plasma::Types::LeftEdge:
        borders = KSvg::FrameSvg::RightBorder;
        break;

    case Plasma::Types::RightEdge:
        borders = KSvg::FrameSvg::LeftBorder;
        break;

    case Plasma::Types::BottomEdge:
        borders = KSvg::FrameSvg::TopBorder;
        break;

    default:
        break;
    }

    if (m_enabledBorders != borders) {
        m_enabledBorders = borders;
        m_corona->dialogShadows()->addWindow(this, m_enabledBorders);

        Q_EMIT enabledBordersChanged();
    }
}

//!END borders

}
}

