# Panel geometry fixes (Job A: panel-issues plan issues 1 + 2)

Branch: `panel-fix-geometry`. Two coupled-looking but SEPARATE root causes,
each fixed at origin with a deterministic regression guard.

## Issue 1 - floating gap grows the panel instead of offsetting it

ROOT CAUSE. For a `behaveAsPlasmaPanel` surface the floating gap
(screenEdgeMargin) was computed into the QWindow position by
`PositionerGeometry::dockPosition` (top edge: `y = screenGeometry.y() +
screenEdgeMargin`), but on Wayland the layer surface's position is owned by
layer-shell anchors and `QWindow::setPosition()` is ignored
(positioner.cpp:741, positionergeometry.h:19). `configureView` /
`updateAnchoring` (app/wm/waylandlayershell.cpp) anchored the panel to its
screen edge and set the exclusive edge but NEVER set a layer-shell margin, so
the compositor pinned the panel flush against the edge and the configured gap
never became a real offset. In edit mode the canvas blueprint uses
`View::editThickness()` which adds the margin, so the blueprint grew downward
by the gap - the "panel got taller instead of floating" appearance.

The strut side was already written for the offset: the visibility
`strutsThickness` binding reserves `gap + thickness` (BindingsExternal.qml
~338), matching Plasma's own floating-panel recipe (exclusive_zone =
thickness + gap, margin = gap). So the surface offset was the one missing
piece; the author had clearly intended the margin to be applied (X11 realised
it through setPosition, the Wayland plumbing was never added).

FIX. `LayerShell::edgeMarginsFor(location, edgeMargin)` maps the gap to a
layer-shell margin on the anchored (exclusive) edge; `updateAnchoring` /
`configureView` gained an `edgeMargin` parameter and always (re)set the
margin. `View::layerShellEdgeMargin()` feeds `screenEdgeMargin` for a floating
panel and 0 otherwise (a masked dock realises the gap through its mask, chrome
never floats). `screenEdgeMarginChanged` / `screenEdgeMarginEnabledChanged`
now route to `reanchorLayerShell` so a runtime gap change re-applies (or
clears) the offset.

GUARD. tests/layershellmappingtest.cpp:
- `floatingGapMapsToEdgeMargins` - the per-edge margin mapping and that the
  offset always lands on the exclusive/anchored edge; 0/negative/non-edge give
  no margin.
- `floatingPanelSurfaceIsLiftedOffItsEdge` - through REAL LayerShellQt: a
  floating top panel gets margins (0,16,0,0); disabling the gap brings it
  flush again (no stale margin welded on); a masked dock (edgeMargin 0) stays
  flush.

## Issue 2 - systray/applet popup opens over the panel, not under it

ROOT CAUSE (separate from Issue 1). Plasma 6's libplasma `PopupPlasmaWindow`
anchors an applet popup to `m_visualParent->window()->mask().boundingRect()`
so it opens OUTSIDE the panel band (popupplasmawindow.cpp updatePosition, the
`!m_floating` branch pads the anchor rect to that bounding rect). A Latte
`behaveAsPlasmaPanel` window carries NO QWindow mask: the pure input-mask core
returned `AcceptAllInput` for panels (maskgeometry.h computeInputMask), the
bridge maps that to the clear sentinel `QRect(0,0,-1,-1)`, and
`Effects::setInputMask` clears the mask (`m_view->setMask(QRegion())`). With an
empty mask, `mask().boundingRect()` collapses to `QRect(0,0,0,0)`, so libplasma
pads the anchor to a 1px strip at the window top and the popup opens OVER the
panel, covering the icon (a second click can't toggle it closed). A masked
DOCK exposes its visible band as a real mask, which is exactly why "a bottom
DOCK was fine but a top PANEL was broken."

FIX. `computeInputMask` now returns the full window rect for a
`behaveAsPlasmaPanel` (`AcceptInputWithin{full window}`) instead of
`AcceptAllInput`. Input is unchanged (the whole band still takes events) but
`QWindow::mask()` now exposes the band, so libplasma anchors the popup at the
real panel edge. Platform-forced deviation from Qt5 (which carried no panel
mask because X11 popup positioning did not depend on it), commented at the
site. The `!compositingActive` legacy branch still returns `AcceptAllInput`.

GUARD. tests/units/maskgeometrytest.cpp
`inputMaskPanelExposesFullWindowSoPopupsAnchorOutside` - a horizontal and a
vertical panel each expose the full window as the input band, and the band's
bottom is the real thickness (not a collapsed 0-height strip at the top). The
pre-existing `...WithoutCompositingOrAsPanel` test was split
(`inputMaskAcceptsEverywhereWithoutCompositing`) to reflect the new panel
behaviour.

## Verification

- Unit guards green: `layershellmappingtest` and `maskgeometrytest` both pass
  (the layer-shell one drives real LayerShellQt windows, all four edges).
- Nested vehicle (scripts/run-e2e.sh) with a crafted top-panel config: the
  vehicle came up and viewsData reported the gap as a REAL offset
  (`absoluteGeometry` y = 16 for a 16px gap), and a masked dock's surface (the
  non-panel case, unaffected by these fixes) stayed flush at the edge with its
  band offset via the mask - i.e. the dock path is untouched, as intended.
- KWin honouring layer-shell margins for placement is already proven in this
  tree by the settings-window / fixed-geometry path (applyFixedGeometry uses
  margins; settings-window-onscreen.sh guards it), so the same mechanism
  carries the panel offset.

## DESK CHECKS OWED (need a real plasma theme)

The nested vehicle's headless breeze theme yields a panel background too thin
for `background.isGreaterThanItemThickness`, so it always forms a masked DOCK,
never a `behaveAsPlasmaPanel` view - the two end-to-end panel checks below
could not be driven in the vehicle and are owed on the real desk (or Job C's
matrix once it can force panel mode with a real theme):

1. On a real top PANEL (behaveAsPlasmaPanel) with the floating gap enabled,
   dump the surface geometry (scripts/tools/dumpwins.sh): the panel surface
   must sit at `y = screenGeometry.top() + gap` (a real gap between the screen
   edge and the panel), not flush at `y = 0`, and NOT grow downward. Repeat on
   bottom/left/right (surface offset inward by the gap on each edge).
2. On a real top PANEL, click a systray icon (e.g. volume): the popup must
   open BELOW the panel band, leaving the icon uncovered and clickable to
   toggle the popup closed. Repeat on bottom (popup above), left/right (popup
   beside). A bottom DOCK was already fine and is the control.
