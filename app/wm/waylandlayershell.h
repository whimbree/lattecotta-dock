/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef WAYLANDLAYERSHELL_H
#define WAYLANDLAYERSHELL_H

// local
#include <coretypes.h>

// Qt
#include <QMargins>
#include <QRect>
#include <QRegion>
#include <QSize>

// Plasma
#include <Plasma/Plasma>

// LayerShellQt
#include <LayerShellQt/Window>

class QScreen;
class QWindow;

namespace Latte {
namespace WindowSystem {

//! Configures QWindows as wlr-layer-shell surfaces through LayerShellQt.
//! This replaces the plasma-shell surface path for dock positioning: KWin
//! ignores QWindow::setPosition() and PlasmaShellSurface::PanelBehavior for
//! layer surfaces, so anchors, margins and exclusive zones are the only
//! mechanism that actually places a dock and reserves its struts on wayland.
//!
//! The mapping functions are pure (no window access) and unit-tested in
//! tests/layershellmappingtest.cpp. The apply helpers push a mapping onto a
//! live QWindow; configureView() must run before the window is first shown,
//! because LayerShellQt can only turn a not-yet-created surface into a layer
//! surface.
namespace LayerShell {

//! dock edge + length-axis alignment -> anchor flags. @p windowSpansScreenLength
//! is true for a masked dock (behaveAsDockWithMask), whose window covers the
//! whole screen along its length axis and realises Left/Center/Right internally
//! through its mask: such a surface anchors BOTH length edges regardless of
//! @p alignment, so the compositor pins it corner to corner instead of centring
//! it inside the region other docks' exclusive zones leave free (the surface
//! drift fixed in this file's anchorsFor comment). A sized panel window
//! (behaveAsPlasmaPanel, shorter than the screen) passes false and its alignment
//! maps to near/far/both/single-edge anchoring.
LayerShellQt::Window::Anchors anchorsFor(Plasma::Types::Location location,
                                         Latte::Types::Alignment alignment,
                                         bool windowSpansScreenLength);

//! Latte visibility mode -> stacking layer
LayerShellQt::Window::Layer layerFor(Latte::Types::Visibility mode);

//! the single edge the dock pins to, for setExclusiveEdge()
LayerShellQt::Window::Anchor edgeFor(Plasma::Types::Location location);

//! perpendicular thickness (px) of the strut rect to reserve as the
//! exclusive zone; 0 for an empty rect
int exclusiveZoneFor(const QRect &strutRect, Plasma::Types::Location location);

//! The compositor rejects a layer surface that is 0-sized on an axis its
//! anchors do not span (a spanned axis may legally be 0 - the compositor
//! stretches it). Returns a legal size for the first commit: the length axis
//! is seeded from the screen, the thickness axis to 1px, and any axis that is
//! already sized or spanned stays untouched.
QSize seededLayerSize(LayerShellQt::Window::Anchors anchors, Plasma::Types::Location location,
                      const QSize &currentSize, const QSize &screenSize);

//! Turn @p window into a layer surface for @p location / @p alignment on
//! @p screen. Must be called before the window is first shown.
//! @p windowSpansScreenLength - see anchorsFor().
void configureView(QWindow *window, QScreen *screen,
                   Plasma::Types::Location location, Latte::Types::Alignment alignment,
                   bool windowSpansScreenLength);

//! Re-apply only the anchors, exclusive edge, screen and seeded size for a
//! new @p location / @p alignment. Anchors are what move a layer surface, so
//! this is the runtime path for dock edge or alignment changes; anchoring
//! only once at creation would weld the surface to its original edge.
//! Deliberately narrower than configureView(): re-running the full
//! configuration would reset the stacking layer (cover modes use LayerBottom)
//! and the keyboard policy. @p windowSpansScreenLength - see anchorsFor().
void updateAnchoring(QWindow *window, QScreen *screen,
                     Plasma::Types::Location location, Latte::Types::Alignment alignment,
                     bool windowSpansScreenLength);

//! update the stacking layer of an already-configured window from its
//! visibility mode
void applyLayer(QWindow *window, Latte::Types::Visibility mode);

//! set whether the layer surface may take keyboard focus
void setFocusPolicy(QWindow *window, bool takesFocus);

//! reserve struts on a layer surface; 0 releases them
void setExclusiveZone(QWindow *window, int zone);

//! Drop all anchors, margins and the exclusive edge so the compositor
//! centres the surface on its output. Settings windows use this: anchored to
//! the dock's edge they stay welded to whichever edge the dock had when they
//! opened.
void setUnanchored(QWindow *window);

//! anchors + margins that make a layer surface overlay a given rect exactly
struct CanvasPlacement {
    LayerShellQt::Window::Anchors anchors;
    QMargins margins;
};

//! Map a dock's canvas geometry (edit-mode overlay) to anchors and margins
//! that reproduce it exactly, so the edit grid lands on the dock instead of
//! at the compositor's default spot. The canvas sits on the edge itself, no
//! perpendicular offset. Horizontal docks span the full screen width: dock
//! edge + both length edges, zero margins. Vertical docks start at the
//! available area's top (below a top panel, say), not the screen top: dock
//! edge + top anchor, pushed down by a top margin of
//! canvasGeometry.y() - screen.y(). The top anchor is required because a
//! layer-shell margin only takes effect on an anchored edge; the surface's
//! explicit height carries the rest.
CanvasPlacement canvasPlacement(Plasma::Types::Location location,
                                const QRect &canvasGeometry, const QRect &screenGeometry);

//! anchor @p window so it overlays @p canvasGeometry exactly (see
//! canvasPlacement()); also clears the exclusive edge, which a canvas
//! overlay must never carry, and pins the surface to @p screen, which the
//! placement margins are relative to
void applyCanvasPlacement(QWindow *window, Plasma::Types::Location location,
                          QScreen *screen, const QRect &canvasGeometry, const QRect &screenGeometry);

//! Pin @p window to the top-left of @p screen, offset by margins so it lands
//! at @p geometry exactly, carrying no exclusive zone. KWin ignores
//! setPosition() for layer surfaces, so a pop-up config surface that wants a
//! specific spot (the widget explorer's left panel) must anchor to two edges
//! and drive the offset with margins; the surface's own size does the rest.
//! The margins are relative to @p screenGeometry, so the surface is pinned to
//! @p screen or the placement lands on the wrong monitor.
void applyFixedGeometry(QWindow *window, QScreen *screen, const QRect &geometry, const QRect &screenGeometry);

//! The input region (window-local, for QWindow::setMask) the edit-mode
//! canvas should catch pointer events in. The canvas overlays the dock and
//! sits above it (Wayland same-layer surfaces stack by mapping order and
//! cannot be raised), so whatever it grabs never reaches the widgets
//! beneath. In configure-applets mode only @p interactiveChrome keeps
//! grabbing (an invalid rect makes the whole canvas click-through); in
//! plain edit mode the canvas owns its surface MINUS @p dockStrip (the
//! dock's own rect in canvas-local coords), reproducing Qt5/X11's
//! dock-above-canvas stacking: widgets stay hoverable and right-clickable
//! in edit mode, the blueprint margin keeps wheel-opacity and the context
//! menu.
QRegion canvasInputRegion(bool inConfigureAppletsMode, const QSize &canvasSize,
                          const QRect &interactiveChrome, const QRect &dockStrip);

}
}
}

#endif
