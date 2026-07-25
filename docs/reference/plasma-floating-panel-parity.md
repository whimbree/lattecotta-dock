# Plasma floating-panel parity

Research date: 2026-07-24.

This record fixes the architecture target for Lattecotta's floating panel
transition. The inspected upstream sources are Plasma 6.7.3:

- plasma-workspace `4c3ace3dfc7b06b3107b52b6e09508be14e73e8a`
- plasma-desktop `e42363ebc7c83489f8a6536265377f646794eac8`
- KWin `45ec9a6d0ed312a803ff5658a2a3e61f221566c6`
- libplasma `c702ca94707a1000dc8c5cc7381f7275d48b0847`

The accepted implementation is a stable per-view surface with an internal
visual transition. It follows Plasma's geometry invariant while retaining
Lattecotta's separate layer-shell reservation transport.

## Upstream source map

- plasma-desktop
  [`Panel.qml`](https://invent.kde.org/plasma/plasma-desktop/-/blob/v6.7.3/desktoppackage/contents/views/Panel.qml)
  owns the TaskManager filter, touch region, target floatingness, internal
  background placement, and containment hints.
- plasma-workspace
  [`panelview.cpp`](https://invent.kde.org/plasma/plasma-workspace/-/blob/v6.7.3/shell/panelview.cpp)
  owns the fixed surface envelope, qreal animation, layer-shell reservation,
  masks, borders, shadows, and Fitts event forwarding.
- libplasma
  [`popupplasmawindow.cpp`](https://invent.kde.org/plasma/libplasma/-/blob/v6.7.3/src/plasmaquick/popupplasmawindow.cpp)
  derives popup placement from the parent mask bounds.
- KWin
  [`workspace.cpp`](https://invent.kde.org/plasma/kwin/-/blob/v6.7.3/src/workspace.cpp)
  and
  [`window.cpp`](https://invent.kde.org/plasma/kwin/-/blob/v6.7.3/src/window.cpp)
  own client areas, electric maximize preview, and interactive move
  completion or cancellation.

## The invariant

`floatingness` may change the background rectangle, paint mask, input routing,
shadow, corners, and popup spacing. It must not change:

- the dock view's QWindow geometry;
- its layer-shell perpendicular margin;
- the applet row's resting measurements and primary-axis span;
- the window-touch trigger geometry;
- the normal-mode reservation depth; or
- the view's output, edge, or stable primary-axis span.

This split is the reason a dragged window can reverse smoothly and a committed
maximize does not cause a second work-area resize.

## How Plasma implements it

Plasma keeps a panel surface large enough to contain both the attached and
fully floated presentations. The surface stays anchored to the screen edge.
QML interpolates a `floatingness` value between the two background rectangles
inside that surface. The containment follows the background inside the stable
surface, but its layout measurements do not refit during the transition.

`PanelView` owns a `QPropertyAnimation` for the qreal progress. A reversal
starts from the current fractional value. Attaching uses an inward easing and
floating uses an outward easing. Lattecotta will select direction from the
qreal target, not Plasma 6.7.3's `toInt()` comparison, because truncating a
fractional reversal can select the wrong easing.

Plasma's task model filters to the current desktop and activity, excludes
hidden and minimized windows, and does not filter only by screen. A window
spanning outputs can therefore affect every panel whose exact trigger it
intersects. The trigger is derived from stable panel geometry and overlaps the
output by one logical pixel where edge adjacency would otherwise fail
`QRect::intersects()`. A 10 ms delayed evaluation filters transient geometry
noise without routing the interaction through the general 150 ms window
coalescer.

The visible mask follows the internal background. Blur, contrast, borders, and
shadows consume that same shape. The input mask additionally reaches from the
background through the floating gap to the physical screen edge. Pointer and
wheel events in that invisible bridge are remapped into the containment. This
preserves Fitts' law without a second interactive Wayland surface.

The layer-shell exclusive zone remains the attached panel thickness. It does
not include the floating gap and does not animate. Edit mode may reserve its
separate expanded thickness. Non-reserving visibility modes use their existing
no-reservation policy.

Popup placement uses the visible mask rather than the oversized QWindow. The
containment also publishes the floating-applet hint so compact applet popups
can animate their visual spacing with the panel.

## Current Lattecotta mismatch

The current panel path gives the floating gap to three authorities that change
together:

- `PositionerGeometry` subtracts `slideOffset` from the physical edge margin;
- `VisibilityManager.qml` animates `Positioner.slideOffset`;
- the layer-shell path applies the result as a real surface margin; and
- `BindingsExternal.qml` changes `strutsThickness` between gap-plus-panel and
  panel-only values.

The trigger currently intersects window geometry with a view rectangle that
can move during the same transition. Maximize delivery was made immediate by
D27 (maximize transitions leave a stale floating-gap work area), but that fix
does not remove the geometry feedback or the changing reservation.

The stable-surface migration supersedes the old panel-issues hypothesis that a
floating gap should be a real layer-shell offset. That offset is the mechanism
that must be removed from the presentation transition.

## Lattecotta ownership model

### Per view

Every panel view owns:

- one stable QWindow canvas;
- one transition controller and qreal `floatingness`;
- one attached background rectangle;
- one floated background rectangle;
- one current visible-background rectangle;
- one current internal content-translation offset;
- one stable window-touch trigger;
- one tight paint/effects mask;
- one edge-reaching input bridge over its exact primary-axis span; and
- one popup anchor derived from the visible mask.

No controller, rectangle, or mutable transition state is shared between
independent views or linked per-output views.

### Per output edge

One reservation coordinator owns the ordinary client work-area publisher for a
Latte output identity and edge. Every eligible view contributes its attached
depth. The coordinator publishes the maximum depth, never a sum.

This is not inward dock stacking. Multiple separated partial-length docks or
panels retain independent visual surfaces and independent exact input regions.
An activation coordinator may own several disjoint rectangles, but it must not
widen them into one continuous input surface. Stable primary-axis overlap
remains invalid and requires the separate span validator recorded in
`DOCK_IDENTITY_HARDENING.md`.

KWin owns one rectangular work area per output. It receives the output-edge
maximum reservation. Lattecotta owns the finer per-view geometry used to keep
its own perpendicular views from displacing each other.

### Across outputs

Output-edge membership uses persistent Latte output identity plus edge. It
does not use monitor adjacency. Portrait, landscape, disconnected, partially
touching, fully touching, and overlapping-coordinate arrangements therefore
use the same per-output calculation.

## Initial policy

The attach-on-window-touch transition initially applies to floating
`AlwaysVisible` panels. Other visibility modes retain their existing
visibility policy and remain visually floated unless a separately specified
policy says otherwise. `WindowsGoBelow` is not silently reclassified as an
ordinary reserving panel.

## Required observability

The atomic dock-system readback must expose, per view:

- eligibility and configured policy;
- target and current floatingness;
- transition phase and direction;
- stable canvas, attached, floated, current visible, trigger, paint-mask, and
  input-bridge rectangles;
- current internal content-translation offset;
- stable applet measurement bounds and primary-axis span;
- touching-window count;
- stable layer-shell edge margin;
- requested and effective reservation depth; and
- output-edge reservation group identity.

The output-edge readback must expose every contributor, the selected maximum
depth, publisher surface state, and generation. A move or teardown must not
leave a view registered under an old output or edge.

## Acceptance

Deterministic coverage must prove:

- stable QWindow, layer-shell margin, trigger, applet span, and reservation
  through attach, float, and mid-flight reversal;
- all four edges and Start, Center, End, and Justify placement;
- exact input bridging without widening separated partial spans;
- correct masks, borders, shadows, and popup anchors at progress 0, fractional
  progress, and progress 1;
- real-window drag in, drag out, Escape cancellation, and committed maximize;
- same-edge maximum-depth reservation without accumulated zones;
- portrait and landscape outputs in disconnected and touching topologies;
- no stale coordinator membership after output, edge, visibility, or lifecycle
  changes; and
- identical persistence before and after restart.

Pixels are evidence where paint is the assertion. Geometry, membership,
transition state, and reservations are asserted through D-Bus.
