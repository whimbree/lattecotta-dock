/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
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

namespace {

[[nodiscard]] constexpr bool isScreenEdge(Plasma::Types::Location location) noexcept
{
    switch (location) {
    case Plasma::Types::TopEdge:
    case Plasma::Types::BottomEdge:
    case Plasma::Types::LeftEdge:
    case Plasma::Types::RightEdge:
        return true;
    default:
        return false;
    }
}

}

static bool retargetScreen(QWindow *window, LSW *ls, QScreen *screen);

LSW::Anchors anchorsFor(Plasma::Types::Location location, Latte::Types::Alignment alignment,
                        bool windowSpansScreenLength)
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

    //! A masked dock (behaveAsDockWithMask) keeps a window that spans the whole
    //! screen along its length axis and realises Left/Center/Right INSIDE that
    //! window through its mask - the window itself never moves. Such a surface
    //! must anchor BOTH length edges so the compositor pins it corner to corner
    //! across the full screen length. The alternative - a single-edge anchor
    //! that leans on the compositor to centre a Center dock - drifts the surface
    //! off its reported geometry: wlr-layer-shell centres a single-edge-anchored
    //! surface inside the region left FREE by other docks' exclusive zones, not
    //! the whole output. A bottom dock beside a left dock (48px zone) and a
    //! right dock (88px zone) then lands (48-88)/2 = -20px off the screen centre
    //! its own geometry math assumes (PositionerGeometry::dockPosition is
    //! full-screen for horizontal docks), so icons render 20px off where input
    //! regions and task centres expect them. Both length edges make the surface
    //! ignore the perpendicular zones and fill the whole screen length, matching
    //! the full-width window. Reproduced and isolated in
    //! docs/agent-logs/2026-07-17-phase8-surface-drift.md; guarded by
    //! tests/e2e/060-geometry-agreement.sh.
    if (windowSpansScreenLength) {
        anchors |= nearAnchor;
        anchors |= farAnchor;
        return anchors;
    }

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
        //! a sized panel window (behaveAsPlasmaPanel) is shorter than the screen
        //! and its alignment moves the whole window; a single-edge anchor lets
        //! the compositor centre it along the length axis
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

QMargins edgeMarginsFor(Plasma::Types::Location location, int edgeMargin)
{
    if (edgeMargin <= 0) {
        return QMargins();
    }

    switch (location) {
    case Plasma::Types::TopEdge:
        return QMargins(0, edgeMargin, 0, 0);
    case Plasma::Types::BottomEdge:
        return QMargins(0, 0, 0, edgeMargin);
    case Plasma::Types::LeftEdge:
        return QMargins(edgeMargin, 0, 0, 0);
    case Plasma::Types::RightEdge:
        return QMargins(0, 0, edgeMargin, 0);
    default:
        return QMargins();
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
                     Plasma::Types::Location location, Latte::Types::Alignment alignment,
                     bool windowSpansScreenLength, int edgeMargin)
{
    LSW *ls = LSW::get(window);

    if (!ls) {
        return;
    }

    const LSW::Anchors anchors = anchorsFor(location, alignment, windowSpansScreenLength);

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

    //! the floating gap is a real offset off the anchored edge, not surface
    //! thickness: a behaveAsPlasmaPanel dock is lifted screenEdgeMargin px off
    //! its edge here. Masked docks and chrome pass 0 and stay flush. Always
    //! (re)setting this keeps a gap-disabling change (edgeMargin back to 0)
    //! from leaving a stale margin welded on.
    ls->setMargins(edgeMarginsFor(location, edgeMargin));
}

LSW *configureView(QWindow *window, QScreen *screen,
                   Plasma::Types::Location location, Latte::Types::Alignment alignment,
                   bool windowSpansScreenLength, int edgeMargin)
{
    LSW *ls = LSW::get(window);

    if (!ls) {
        return nullptr;
    }

    ls->setScope(QStringLiteral("dock"));
    updateAnchoring(window, screen, location, alignment, windowSpansScreenLength, edgeMargin);
    ls->setLayer(LSW::LayerTop);
    ls->setKeyboardInteractivity(LSW::KeyboardInteractivityNone);
    return ls;
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

ViewPlacement viewPlacement(Plasma::Types::Location location,
                            const QRect &viewGeometry, const QRect &screenGeometry)
{
    Q_ASSERT(isScreenEdge(location));
    Q_ASSERT(screenGeometry.isValid());
    Q_ASSERT(viewGeometry.isValid());
    Q_ASSERT(screenGeometry.contains(viewGeometry));

    ViewPlacement placement;
    placement.anchors = LSW::Anchors(LSW::AnchorTop) | LSW::AnchorLeft;
    placement.margins = QMargins(viewGeometry.left() - screenGeometry.left(),
                                 viewGeometry.top() - screenGeometry.top(), 0, 0);
    return placement;
}

void applyViewPlacement(QWindow *window, Plasma::Types::Location location,
                        const QRect &viewGeometry, const QRect &screenGeometry)
{
    if (!window) {
        qCritical() << "LayerShell::applyViewPlacement refused a null window";
        return;
    }

    if (!screenGeometry.isValid() || !viewGeometry.isValid()
            || !screenGeometry.contains(viewGeometry)) {
        qCritical() << "LayerShell::applyViewPlacement refused geometry outside its output"
                    << "view=" << viewGeometry << "screen=" << screenGeometry;
        return;
    }

    if (!isScreenEdge(location)) {
        qCritical() << "LayerShell::applyViewPlacement refused non-edge location"
                    << static_cast<int>(location);
        return;
    }

    LSW *const ls = LSW::get(window);
    if (!ls) {
        qCritical() << "LayerShell::applyViewPlacement found no attached layer-shell state";
        return;
    }

    const ViewPlacement placement = viewPlacement(location, viewGeometry, screenGeometry);

    //! Geometry synchronization can run repeatedly with an unchanged
    //! solution. Avoiding redundant layer-shell setters prevents needless
    //! compositor configure events from feeding another geometry pass.
    if (ls->exclusionZone() != -1) {
        ls->setExclusiveZone(-1);
    }
    if (ls->exclusiveEdge() != LSW::AnchorNone) {
        ls->setExclusiveEdge(LSW::AnchorNone);
    }
    if (ls->anchors() != placement.anchors) {
        ls->setAnchors(placement.anchors);
    }
    if (ls->margins() != placement.margins) {
        ls->setMargins(placement.margins);
    }
}

ReservationPlacement reservationPlacement(Plasma::Types::Location location,
                                          const QRect &strutGeometry,
                                          const QRect &screenGeometry)
{
    Q_ASSERT(isScreenEdge(location));
    Q_ASSERT(screenGeometry.isValid());
    Q_ASSERT(strutGeometry.isValid());
    Q_ASSERT(screenGeometry.contains(strutGeometry));

    const bool horizontal = location == Plasma::Types::TopEdge
            || location == Plasma::Types::BottomEdge;
    const int lengthOffset = horizontal
            ? strutGeometry.left() - screenGeometry.left()
            : strutGeometry.top() - screenGeometry.top();

    ReservationPlacement placement;
    placement.exclusiveEdge = edgeFor(location);
    placement.anchors = LSW::Anchors(placement.exclusiveEdge)
            | (horizontal ? LSW::AnchorLeft : LSW::AnchorTop)
            | (horizontal ? LSW::AnchorRight : LSW::AnchorBottom);
    placement.surfaceSize = horizontal
            ? QSize(strutGeometry.width(), 1)
            : QSize(1, strutGeometry.height());
    placement.exclusiveZone = exclusiveZoneFor(strutGeometry, location);

    if (horizontal) {
        placement.margins.setLeft(lengthOffset);
        placement.margins.setRight(-lengthOffset);
    } else {
        placement.margins.setTop(lengthOffset);
        placement.margins.setBottom(-lengthOffset);
    }

    return placement;
}

LSW *applyReservationPlacement(QWindow *window, QScreen *screen,
                               Plasma::Types::Location location,
                               const QRect &strutGeometry,
                               const QRect &screenGeometry)
{
    if (!window || !screen) {
        qCritical() << "LayerShell::applyReservationPlacement refused a null window or screen";
        return nullptr;
    }

    if (!isScreenEdge(location) || !screenGeometry.isValid()
            || screen->geometry() != screenGeometry
            || !strutGeometry.isValid() || !screenGeometry.contains(strutGeometry)) {
        qCritical() << "LayerShell::applyReservationPlacement refused invalid geometry"
                    << "location=" << static_cast<int>(location)
                    << "strut=" << strutGeometry << "screen=" << screenGeometry;
        return nullptr;
    }

    const bool touchesEdge =
            (location == Plasma::Types::TopEdge && strutGeometry.top() == screenGeometry.top())
            || (location == Plasma::Types::BottomEdge && strutGeometry.bottom() == screenGeometry.bottom())
            || (location == Plasma::Types::LeftEdge && strutGeometry.left() == screenGeometry.left())
            || (location == Plasma::Types::RightEdge && strutGeometry.right() == screenGeometry.right());
    if (!touchesEdge) {
        qCritical() << "LayerShell::applyReservationPlacement refused a strut away from its edge"
                    << "location=" << static_cast<int>(location)
                    << "strut=" << strutGeometry << "screen=" << screenGeometry;
        return nullptr;
    }

    LSW *const ls = LSW::get(window);
    if (!ls) {
        qCritical() << "LayerShell::applyReservationPlacement could not create layer-shell state";
        return nullptr;
    }

    const bool remap = retargetScreen(window, ls, screen);
    const ReservationPlacement placement = reservationPlacement(
        location, strutGeometry, screenGeometry);

    window->setMinimumSize(placement.surfaceSize);
    window->setMaximumSize(placement.surfaceSize);
    if (window->size() != placement.surfaceSize) {
        window->resize(placement.surfaceSize);
    }
    if (ls->scope() != QStringLiteral("dock-reservation")) {
        ls->setScope(QStringLiteral("dock-reservation"));
    }
    if (ls->layer() != LSW::LayerTop) {
        ls->setLayer(LSW::LayerTop);
    }
    if (ls->keyboardInteractivity() != LSW::KeyboardInteractivityNone) {
        ls->setKeyboardInteractivity(LSW::KeyboardInteractivityNone);
    }
    if (ls->anchors() != placement.anchors) {
        ls->setAnchors(placement.anchors);
    }
    if (ls->exclusiveEdge() != placement.exclusiveEdge) {
        ls->setExclusiveEdge(placement.exclusiveEdge);
    }
    if (ls->margins() != placement.margins) {
        ls->setMargins(placement.margins);
    }
    if (ls->exclusionZone() != placement.exclusiveZone) {
        ls->setExclusiveZone(placement.exclusiveZone);
    }

    if (remap) {
        window->setVisible(true);
    }
    return ls;
}

void clearReservation(QWindow *window)
{
    if (!window) {
        qCritical() << "LayerShell::clearReservation refused a null window";
        return;
    }

    LSW *const ls = LSW::get(window);
    if (!ls) {
        qCritical() << "LayerShell::clearReservation found no attached layer-shell state";
        return;
    }

    if (ls->exclusionZone() != 0) {
        ls->setExclusiveZone(0);
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

//! a mapped wlr-layer surface is bound to the output it was created on (the
//! protocol has no request to move it), so retargeting a VISIBLE window to
//! another screen requires destroying and recreating the surface: hide,
//! apply the new state, show. Observed live without this: a dock moved
//! DP-2 -> DP-3 with edit mode open re-synced the canvas to the portrait
//! geometry while the surface stayed on the landscape output. Same
//! constraint View::moveToScreen handles for the dock window itself.
static bool retargetScreen(QWindow *window, LSW *ls, QScreen *screen)
{
    if (!screen) {
        return false;
    }

    const bool remap = window->isVisible() && window->screen() != screen;

    if (remap) {
        window->setVisible(false);
    }

    window->setScreen(screen);
#ifdef LATTE_LAYERSHELL_HAS_SET_SCREEN
    ls->setScreen(screen);
#endif

    return remap;
}

void applyCanvasPlacement(QWindow *window, Plasma::Types::Location location,
                          QScreen *screen, const QRect &canvasGeometry, const QRect &screenGeometry)
{
    if (LSW *ls = LSW::get(window)) {
        //! same output rule as applyFixedGeometry: the placement margins are
        //! relative to @p screenGeometry, so the surface must be on that
        //! output or the whole edit chrome lands on the wrong monitor
        //! (observed live: blueprint spanning the neighboring screen)
        const bool remap = retargetScreen(window, ls, screen);

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

        if (remap) {
            window->setVisible(true);
        }
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
        const bool remap = retargetScreen(window, ls, screen);

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

        if (remap) {
            window->setVisible(true);
        }
    }
}

QRegion canvasInputRegion(bool inConfigureAppletsMode, const QSize &canvasSize,
                          const QRect &interactiveChrome, const QRect &dockStrip)
{
    if (!inConfigureAppletsMode) {
        //! Plain edit mode: the canvas owns its surface EXCEPT the dock's own
        //! strip. Qt5/X11 got this by stacking: the canvas was raised above
        //! surrounding docks and then the edited dock was raised above the
        //! canvas, so applet hover, right-click and wheel over the dock hit
        //! the dock, while the blueprint margin around it kept the canvas's
        //! wheel-opacity and context-menu areas. Wayland layer surfaces in
        //! the same layer cannot be restacked, so the same contract is
        //! expressed through the input region instead.
        return QRegion(QRect(QPoint(0, 0), canvasSize)) - QRegion(dockStrip);
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
