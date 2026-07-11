/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "waylandlayershell.h"

// Qt
#include <QScreen>
#include <QWindow>

namespace Latte {
namespace WindowSystem {
namespace LayerShell {

using LSW = LayerShellQt::Window;

LSW::Anchors anchorsFor(Plasma::Types::Location location, Latte::Types::Alignment alignment)
{
    LSW::Anchors anchors;
    const bool horizontal = (location == Plasma::Types::TopEdge || location == Plasma::Types::BottomEdge);

    switch (location) {
    case Plasma::Types::TopEdge:
        anchors = LSW::AnchorTop;
        break;
    case Plasma::Types::BottomEdge:
        anchors = LSW::AnchorBottom;
        break;
    case Plasma::Types::LeftEdge:
        anchors = LSW::AnchorLeft;
        break;
    case Plasma::Types::RightEdge:
        anchors = LSW::AnchorRight;
        break;
    default:
        anchors = LSW::AnchorBottom;
        break;
    }

    const LSW::Anchor nearAnchor = horizontal ? LSW::AnchorLeft : LSW::AnchorTop;
    const LSW::Anchor farAnchor = horizontal ? LSW::AnchorRight : LSW::AnchorBottom;

    switch (alignment) {
    case Latte::Types::Justify:
        anchors |= nearAnchor;
        anchors |= farAnchor;
        break;
    case Latte::Types::Left:   //! horizontal docks
    case Latte::Types::Top:    //! vertical docks
        anchors |= nearAnchor;
        break;
    case Latte::Types::Right:  //! horizontal docks
    case Latte::Types::Bottom: //! vertical docks
        anchors |= farAnchor;
        break;
    case Latte::Types::Center:
    default:
        //! single-edge anchor: the compositor centres along the length axis
        break;
    }

    return anchors;
}

LSW::Layer layerFor(Latte::Types::Visibility mode)
{
    //! Only the two cover modes put the dock below windows; WindowsGoBelow
    //! means windows go below THE DOCK (the X11 backend gives it keep-above).
    //! latte-dock-qt6 mapped WindowsGoBelow to LayerBottom, which also turned
    //! its front-layer default into bottom - do not reintroduce that.
    //! NormalWindow (neither above nor below, WM-stacked) is not expressible
    //! for a layer surface; LayerTop keeps such a dock usable.
    switch (mode) {
    case Latte::Types::WindowsCanCover:
    case Latte::Types::WindowsAlwaysCover:
        return LSW::LayerBottom;
    default:
        return LSW::LayerTop;
    }
}

LSW::Anchor edgeFor(Plasma::Types::Location location)
{
    switch (location) {
    case Plasma::Types::TopEdge:
        return LSW::AnchorTop;
    case Plasma::Types::BottomEdge:
        return LSW::AnchorBottom;
    case Plasma::Types::LeftEdge:
        return LSW::AnchorLeft;
    case Plasma::Types::RightEdge:
        return LSW::AnchorRight;
    default:
        return LSW::AnchorBottom;
    }
}

int exclusiveZoneFor(const QRect &strutRect, Plasma::Types::Location location)
{
    if (strutRect.isEmpty()) {
        return 0;
    }

    switch (location) {
    case Plasma::Types::TopEdge:
    case Plasma::Types::BottomEdge:
        return strutRect.height();
    case Plasma::Types::LeftEdge:
    case Plasma::Types::RightEdge:
        return strutRect.width();
    default:
        return 0;
    }
}

QSize seededLayerSize(LSW::Anchors anchors, Plasma::Types::Location location,
                      const QSize &currentSize, const QSize &screenSize)
{
    const bool horizontal = (location == Plasma::Types::TopEdge || location == Plasma::Types::BottomEdge);
    const bool spansH = anchors.testFlag(LSW::AnchorLeft) && anchors.testFlag(LSW::AnchorRight);
    const bool spansV = anchors.testFlag(LSW::AnchorTop) && anchors.testFlag(LSW::AnchorBottom);

    int w = currentSize.width();
    int h = currentSize.height();

    //! only touch an axis the anchors do not span and that has no size yet;
    //! the length axis takes the screen extent, the thickness axis 1px
    if (!spansH && w <= 0) {
        w = horizontal ? screenSize.width() : 1;
    }

    if (!spansV && h <= 0) {
        h = horizontal ? 1 : screenSize.height();
    }

    return QSize(w, h);
}

void updateAnchoring(QWindow *window, QScreen *screen,
                     Plasma::Types::Location location, Latte::Types::Alignment alignment)
{
    LSW *ls = LSW::get(window);

    if (!ls) {
        return;
    }

    const LSW::Anchors anchors = anchorsFor(location, alignment);

    if (screen) {
        //! seed a legal size before the anchors commit; seededLayerSize
        //! leaves an already-sized window untouched, so re-running this on a
        //! runtime edge change is safe
        const QSize seeded = seededLayerSize(anchors, location, window->size(), screen->geometry().size());

        if (seeded != window->size()) {
            window->resize(seeded);
        }

        //! the screen must reach both objects: the QWindow so Qt renders on
        //! the right output, and the layer surface so the compositor maps it
        //! there - otherwise struts land on the wrong monitor in multi-screen
        //! setups. LayerShellQt::Window::setScreen(QScreen*) came and went
        //! upstream, hence the cmake probe.
        window->setScreen(screen);
#ifdef LATTE_LAYERSHELL_HAS_SET_SCREEN
        ls->setScreen(screen);
#endif
    }

    //! edgeFor(location) is one of anchorsFor(location, ...) by
    //! construction, so setting it right after the anchors keeps the
    //! committed state consistent - the compositor kills a surface whose
    //! exclusive edge is not among its anchors
    ls->setAnchors(anchors);
    ls->setExclusiveEdge(edgeFor(location));
}

void configureView(QWindow *window, QScreen *screen,
                   Plasma::Types::Location location, Latte::Types::Alignment alignment)
{
    LSW *ls = LSW::get(window);

    if (!ls) {
        return;
    }

    ls->setScope(QStringLiteral("dock"));
    updateAnchoring(window, screen, location, alignment);
    ls->setLayer(LSW::LayerTop);
    ls->setKeyboardInteractivity(LSW::KeyboardInteractivityNone);
}

void applyLayer(QWindow *window, Latte::Types::Visibility mode)
{
    if (LSW *ls = LSW::get(window)) {
        ls->setLayer(layerFor(mode));
    }
}

void setFocusPolicy(QWindow *window, bool takesFocus)
{
    if (LSW *ls = LSW::get(window)) {
        ls->setKeyboardInteractivity(takesFocus ? LSW::KeyboardInteractivityOnDemand
                                                : LSW::KeyboardInteractivityNone);
    }
}

void setExclusiveZone(QWindow *window, int zone)
{
    if (LSW *ls = LSW::get(window)) {
        ls->setExclusiveZone(zone);
    }
}

void setUnanchored(QWindow *window)
{
    if (LSW *ls = LSW::get(window)) {
        ls->setExclusiveEdge(LSW::AnchorNone);
        ls->setAnchors(LSW::Anchors());
        ls->setMargins(QMargins());
    }
}

CanvasPlacement canvasPlacement(Plasma::Types::Location location,
                                const QRect &canvasGeometry, const QRect &screenGeometry)
{
    CanvasPlacement placement;

    switch (location) {
    case Plasma::Types::TopEdge:
        placement.anchors = LSW::Anchors(LSW::AnchorTop) | LSW::AnchorLeft | LSW::AnchorRight;
        break;
    case Plasma::Types::BottomEdge:
        placement.anchors = LSW::Anchors(LSW::AnchorBottom) | LSW::AnchorLeft | LSW::AnchorRight;
        break;
    case Plasma::Types::LeftEdge:
        placement.anchors = LSW::Anchors(LSW::AnchorLeft) | LSW::AnchorTop;
        placement.margins.setTop(canvasGeometry.y() - screenGeometry.y());
        break;
    case Plasma::Types::RightEdge:
        placement.anchors = LSW::Anchors(LSW::AnchorRight) | LSW::AnchorTop;
        placement.margins.setTop(canvasGeometry.y() - screenGeometry.y());
        break;
    default:
        placement.anchors = LSW::Anchors(LSW::AnchorBottom) | LSW::AnchorLeft | LSW::AnchorRight;
        break;
    }

    return placement;
}

void applyCanvasPlacement(QWindow *window, Plasma::Types::Location location,
                          QScreen *screen, const QRect &canvasGeometry, const QRect &screenGeometry)
{
    if (LSW *ls = LSW::get(window)) {
        //! same output rule as applyFixedGeometry: the placement margins are
        //! relative to @p screenGeometry, so the surface must be on that
        //! output or the whole edit chrome lands on the wrong monitor
        //! (observed live: blueprint spanning the neighboring screen)
        if (screen) {
            window->setScreen(screen);
#ifdef LATTE_LAYERSHELL_HAS_SET_SCREEN
            ls->setScreen(screen);
#endif
        }

        const CanvasPlacement placement = canvasPlacement(location, canvasGeometry, screenGeometry);

        //! the canvas is an overlay, not a strut-reserving panel: it must not
        //! carry an exclusive edge. A stale one from an earlier configuration
        //! may not be among the overlay anchors, and the compositor kills the
        //! surface for that ("exclusive edge is not of the anchors").
        ls->setExclusiveEdge(LSW::AnchorNone);
        //! zone -1 opts out of OTHER surfaces' exclusive zones. With the
        //! default zone 0 the compositor pushes the canvas off the screen
        //! edge by the dock's own strut, so the edit chrome (ruler, header)
        //! floats above the blueprint instead of overlaying it.
        ls->setExclusiveZone(-1);
        ls->setAnchors(placement.anchors);
        ls->setMargins(placement.margins);
    }
}

void applyFixedGeometry(QWindow *window, QScreen *screen, const QRect &geometry, const QRect &screenGeometry)
{
    if (LSW *ls = LSW::get(window)) {
        //! the margins below are computed against @p screenGeometry, so the
        //! surface MUST sit on that output: config windows are reused across
        //! views and screens, and a stale output assignment lands the window
        //! on the wrong monitor with the other monitor's offsets (observed
        //! live: settings window cut off, widget explorer opening on the
        //! neighboring screen)
        if (screen) {
            window->setScreen(screen);
#ifdef LATTE_LAYERSHELL_HAS_SET_SCREEN
            ls->setScreen(screen);
#endif
        }

        //! a pop-up surface reserves nothing; a stale exclusive edge from the
        //! parent-dock anchoring would both hold a strut and, if not among the
        //! anchors below, make the compositor kill the surface
        ls->setExclusiveEdge(LSW::AnchorNone);
        //! zone -1 opts out of other surfaces' exclusive zones: the caller
        //! computed @p geometry in absolute screen coordinates, so letting
        //! the compositor shift the surface off a strut would misplace it
        ls->setExclusiveZone(-1);
        //! anchor the top-left corner and drive the position with margins; the
        //! two anchored edges are what let the margins take effect (a margin on
        //! an unanchored edge is ignored), and the window's explicit size
        //! carries width/height
        ls->setAnchors(LSW::Anchors(LSW::AnchorTop) | LSW::AnchorLeft);
        ls->setMargins(QMargins(geometry.x() - screenGeometry.x(),
                                geometry.y() - screenGeometry.y(), 0, 0));
    }
}

QRegion canvasInputRegion(bool inConfigureAppletsMode, const QSize &canvasSize,
                          const QRect &interactiveChrome)
{
    if (!inConfigureAppletsMode) {
        //! plain edit mode: the canvas owns its whole surface
        return QRegion(QRect(QPoint(0, 0), canvasSize));
    }

    if (interactiveChrome.isValid()) {
        return QRegion(interactiveChrome);
    }

    //! No chrome, so the canvas must grab nothing. An empty QRegion cannot
    //! express that: the Qt wayland plugin maps an empty setMask() to the
    //! infinite (grab-all) input region. A 1px region outside the surface
    //! keeps the on-surface input area empty, so every event falls through
    //! to the dock beneath.
    return QRegion(QRect(-1, -1, 1, 1));
}

}
}
}
