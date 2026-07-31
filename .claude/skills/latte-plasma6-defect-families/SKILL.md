---
name: latte-plasma6-defect-families
description: Recognizing and fixing the recurring Qt6/Plasma 6 porting bug classes in this codebase.
---

# Latte Plasma 6 defect families

Every family below was found live in this port, root-caused, and fixed
with a verifiable commit. When a new bug appears, match its fingerprint
against this catalog first: most porting defects here repeat one of
these eight mechanisms. For the diagnosis workflow (instrumenting,
driving the failure, reading the live process) see latte-debugging;
for proving a fix against the running dock see latte-live-verification.

## 1. Moved-one-hop applet chains

FINGERPRINT: `indexOfProperty("configuration")` is always -1 in C++;
QML property reads yield 0 or undefined; `Cannot read property ... of
undefined` TypeError floods on config pages (88 in one settings-window
lifetime for the Tasks page); `org.kde.sync ... was not established,
configuration object was missing!` storms at startup (six per dock);
whole config pages rendering defaults and applying nothing; applet
order silently wiped on every boot.

MECHANISM: Plasma 6 removed AppletQuickItem's static `configuration`
and `id` properties. They now live one hop away, on the Plasma::Applet
behind the item. Qt5-era code probing the quick item directly reads
nothing, usually without an error at the probe site, and the damage
surfaces far away.

FIX PATTERN:
- QML: `item.plasmoid.configuration.*` and `item.plasmoid.id`. Both
  are real Q_PROPERTYs (PlasmoidItem.plasmoid -> Applet), so bindings
  stay notify-connected.
- C++: `qobject_cast<PlasmaQuick::AppletQuickItem*>` and use
  `->applet()->id()`, or read the CONSTANT `configuration` Q_PROPERTY
  off the Applet (`Q_PROPERTY(KConfigPropertyMap* configuration READ
  configuration CONSTANT FINAL)`). Available from construction, so no
  retry timer is needed in the common path.
- Single-loader rule: always reach the ONE KConfigLoader-backed map
  the plasmoid's own bindings observe. Never construct a second
  KConfigLoader over the same file: two loaders cache independently
  and writes through one are invisible to the other. No dynamic
  property shims either (latte-dock-ng's setProperty route in
  eabf7c89a needed retry timers on top).

REAL EXAMPLES:
- 32df5b47: Tasks config page read all 75 options as
  `tasks.configuration.*`; every control showed defaults and applied
  nothing. Rerouted through `tasks.plasmoid.configuration.*`.
- c3d15966: appletConfiguration() probed the quick item, returned null
  for EVERY applet, always. All applet config syncing had silently
  never established since the port (change tracking, clone mirroring,
  the applet-order sync machinery). The 1s delayed retry could never
  succeed because it called the same dead probe.
- 9a6f8fb8: LayoutManager's save() collector read the id off the
  graphic item, got 0 for every child, serialized an EMPTY order every
  startup. Combined with family 6 this deleted appletOrder outright:
  manual rearrangements never survived a restart.

## 2. QQC2 control property shadowing

FINGERPRINT: `Cannot read property ... of undefined` TypeErrors only
in bindings sitting directly on QQC2 controls, while sibling bindings
on non-controls resolve the same name fine; toggles that visibly do
nothing (the handler writes to the wrong object). In 33fa17d7 only
checkbox rows misbehaved and sliders never did.

MECHANISM: QML scope resolution prefers the scope object's own
properties over context properties, inside bindings AND signal
handlers written on a control. QQC2 controls carry property names QQC1
never had. The indicator config API hands packages an `indicator`
context property, and QQC2 CheckBox has its own `indicator` (the check
glyph item), so every bare `indicator` reference on a CheckBox
resolved to the glyph. QQC2 Slider has no `indicator` property, which
is exactly why sliders were unaffected. Qt5 never hit this because
QQC1 CheckBox had no such name, so the bare reference fell through to
the context property. Watch for ANY collision of this shape, not just
`indicator`.

FIX PATTERN: Capture the context property once at the file root, whose
scope has no competing name, and use the alias inside control scopes:
`readonly property QtObject latteIndicator: indicator`.

DIAGNOSIS TRICK: log the receiver of the failing evaluation. It printed
`CheckIndicator_QMLTYPE` while the context property was simultaneously
valid at the root, pinning the mechanism in one measurement.

REAL EXAMPLES:
- 33fa17d7: five checkboxes in the default indicator plus one in the
  plasma indicator showed defaults and their onClicked writes went to
  the glyph item; zero indicator TypeErrors after the alias fix.

## 3. Read-only QQC2 properties abort handlers

FINGERPRINT: `Cannot assign to read-only property` in the log, plus
dead UI beyond the assignment site: statements after the write never
run because the TypeError aborts the WHOLE handler.

MECHANISM: Qt6 made previously writable QQC2 properties read-only.
ComboBox.pressed is the known case (writable form was already
deprecated in 5.15 in favor of `down`). onGenericPressed() assigned
`pressed` right before toggling `popup.visible`, so no LatteComponents
ComboBox popup could ever open; the settings screen selector appeared
completely dead.

FIX PATTERN: Drop the write and use the writable equivalent (`down`),
and migrate any visuals that keyed off `control.pressed` to
`control.down` so the pressed styling survives. Do not just delete the
write and leave the visuals reading a state nothing sets.

REAL EXAMPLES:
- 0474e20c: five `pressed` writes dropped, three visuals (background
  prefix, label color, button shadow) moved to `down`. Verified with
  wayland pointer injection: screen combobox opens and applies again.

## 4. Wayland layer-shell surface immutability

FINGERPRINT: a window resizes for a new screen but stays on the old
output; chimeras like a portrait-sized surface on a landscape output
(observed: a 2560-tall dock on a 1440-tall screen, compositor-clamped
to y=-113); edit chrome or config windows floating centered on the
wrong monitor; surfaces vanishing with a compositor "exclusive edge is
not of the anchors" kill.

MECHANISM: a mapped wlr-layer surface is bound to the output it was
created on and the protocol has no request to move it. Position
requests are ignored: QWindow::setScreen and setPosition are silent
no-ops while the surface is visible, while anchors, margins and size
still apply, so half the state lands and half does not.

FIX PATTERN: hide the window first (destroys the surface), retarget
both the QWindow screen and the LayerShellQt desired output, re-apply
the placement state, then show so a fresh surface maps on the right
output. Only remap when the window is visible and the screen actually
changes; otherwise plain setScreen suffices (startup, hidden views,
X11). See the shared `retargetScreen` helper and its comments in
`app/wm/waylandlayershell.cpp`.

RELATED CONSTRAINTS (all commented in app/wm/waylandlayershell.cpp,
read them before touching placement code):
- Margins are interpreted relative to the surface's own output, so the
  surface must be on the target output or the whole edit chrome lands
  on the wrong monitor.
- Overlays (canvas, popups) need exclusive zone -1 to opt out of other
  surfaces' exclusive zones; with the default zone 0 the compositor
  pushes them off the edge by the dock's own strut.
- The exclusive edge must be among the surface's anchors or the
  compositor kills the surface. Clear stale exclusive edges
  (`setExclusiveEdge(AnchorNone)`) when reconfiguring an overlay.
- Never discard computed geometry by falling back to setUnanchored;
  the compositor then centers the surface on whatever output it was
  created on.

REAL EXAMPLES:
- 793faad2: View::moveToScreen remaps the dock surface on relocation;
  setScreen alone kept the surface on the old output with the new
  screen's size.
- 1607d022: shared retargetScreen for visible config surfaces (canvas,
  secondary chooser) so cross-screen moves with edit mode open carry
  the whole chrome ensemble.
- 7ac419d1: the secondary config window computed correct Qt5 geometry
  then threw it away via setUnanchored; now applied through
  applyFixedGeometry pinned to the edited view's screen.

## 5. Async timing where Qt5 was synchronous

FINGERPRINT: correct code that fails only at startup or right after a
relocation; computations using zero or stale inputs (maxLength 0, the
previous screen's dimensions) with nothing left to re-trigger them;
100% CPU hangs whose interrupted backtrace sits in a loop fed a
degenerate value.

MECHANISM: Plasma 6/Wayland delivers window geometry, containment
screen ids and applet items later than Qt5/X11 did. Code that assumed
"by the time this runs, X is populated" now runs before X lands.
Seen live: the first autosize call arrived from visibilityChanged
before the window was sized, maxLength was 0, and a latent
upstream loop-termination bug (inherited from 747d4870) became an
infinite loop that starved the event loop. The containment's screen id
lands after relocation, so availability computations used the old
screen and the edit canvas kept the previous screen's length.

FIX PATTERN: two halves, both required. Guard the degenerate input
loudly (skip the computation, qWarning if it is genuinely unexpected)
AND wire the catching-up signal to re-run the computation when the
real value lands. Never just clamp: a clamp turns "runs too early"
into "runs once with garbage and never again with truth".

REAL EXAMPLES:
- ad9b823f: autosize loops clamp to their bounds and exit on
  inequality; updateIconSize() skips entirely while maxLength <= 0 and
  onAutomaticSizingMaximumLengthChanged re-runs it as soon as the real
  content budget exists.
- c5bdc239: Containment::screenChanged connected to
  Positioner::syncGeometry so the late screen id re-triggers the
  availability computation and the edit chrome re-places itself.

## 6. KConfig default-value deletion

FINGERPRINT: keys silently vanishing from layout files after a save
path runs with empty or default state. The appletOrder wipe in
9a6f8fb8 is the canonical case: save() serialized an empty order,
empty was the config default, so KConfigLoader deleted the stored key
outright.

MECHANISM: writing a value equal to the KConfigLoader default DELETES
the key from the file. This is by design (defaults are not persisted),
but it turns any upstream bug that degrades state to its default into
silent data loss on the next save.

FIX PATTERN: this family is usually a SYMPTOM AMPLIFIER, not the root
cause. When a key disappears, ask what wrote the default. Diagnose by
driving it: in 9a6f8fb8 a synthetic non-default order was planted and
watched being wiped to empty 30ms after restore() had read it
correctly, which located the broken collector. Interpretation rule: a
missing key after a round-trip is possibly normal (value at default);
a missing key after storing non-default data is a wipe bug.

## 7. MultiEffect needs real texture providers

FINGERPRINT: `ShaderEffect: Texture t1 is not assigned a valid texture
provider` (or `No QSGTexture provided from updateSampledImage()`)
warnings; effects rendering nothing (blank colorized applets, masks
that never apply); SIGSEGV in QSGBatchRenderer::buildRenderLists
during QSGRhiLayer::grab, typically on representation churn (popup
teardown, edit-mode toggles, applets rebuilding their content).

MECHANISM (corrected 2026-07-15 by source-read plus headless probe;
the earlier form of this section claimed Qt6 MultiEffect never
auto-wraps plain Items - that is WRONG and the corrected semantics
are pinned in tests/contracts/tst_multieffectcontracts.qml): Qt 6.11
MultiEffect DOES auto-wrap a plain Item source through its internal
source proxy (QGfxSourceProxyME). The real traps are narrower and
meaner: (1) the proxy decides direct-vs-wrapped at POLISH time and
NEVER repolishes when the source's layer.enabled flips - flip the
layer after the choice and the effect keeps sampling a dead layer;
(2) an effect node at opacity 0 still preprocesses its samplers every
scene repaint, so a faded-out effect with a dead or nulled source
warns and costs work per frame forever; (3) a source binding that
goes NULL is its own class - the warning's suffix is forensic:
a live class name means an unlayered/proxy problem, the literal
`(QQuickItem*)` means the bound source is a null variant; (4) mask
sources are NEVER proxied - masks must be real providers, unchanged;
(5) effects are not providers - sampling one MultiEffect from another
is still a defect. Related pipeline fact learned the hard way:
ItemGrabResult's itemgrabber: url is only valid while the result
object lives, and QML Canvas.loadImage cannot resolve that provider
at all - pixel analysis of grabbed items must be done in C++ from
QQuickItemGrabResult::image(). Historical implementations of that
analysis are preserved in commits 387343288 and 5c06b4970; the live
colorizer no longer classifies rendered applet pixels.

FIX PATTERN: hold the source's layer STABLE for the whole lifetime
the effect can sample it (never flip layer.enabled while the effect
node exists - the no-repolish trap); gate the effect's `visible` (not
just opacity) on BOTH the effect being wanted AND the source being
non-null and valid - visible:false removes the node before the next
material sync; gate the layer on a stable condition (a setting, not a
per-frame geometry predicate: layer create/destroy churn while grabs
are in flight is its own crash vector, see df747ebf); keep mask items
OUTSIDE the subtree the effect's layer grabs; shadows are layer.effect
never sibling copies (c7c46226); Qt5 Colorize semantics need
Qt5Compat ColorOverlay - MultiEffect.colorization multiplies by the
source's gray level and is a different effect (1f835402); or draw the
visual directly and drop the effect when layering the source would
feed the same crash class (e88af680 did this for a live pipewire
thumbnail). The autoPaddingEnabled ban (e3376405: it re-dirties every
frame, idle render storm) is enforced by the qmleffectrules ctest.

REAL EXAMPLES:
- 73da8400: colorizer and both applet-shadow paths sampled unlayered
  items; applet shadows and colorized icons rendered for the first
  time in this port once fixed. Reliable reproducer was the Comic
  Strip applet's representation churn.
- df747ebf: scroll-fade mask lived inside the grabbed subtree and its
  layer flipped on contentsExceed on every relayout; moved out and
  gated on the scrolling setting.
- e88af680: media-preview frosted glass masked by a plain Item; the
  mask had never applied and teardown raced the render thread. Drawn
  directly, MultiEffect dropped, deviation documented in place.
- c7200e3d: basic scenegraph render loop set as default while the port
  stabilizes; the threaded loop made every occurrence a cross-thread
  race with useless backtraces, the basic loop made the same
  corruption deterministic and debuggable.
- 5f8c10be: the clicked flash kept `source: compactRepresentation`
  while libplasma NULLED that property during the inline
  representation switch; the running flash (alwaysRunToEnd) warned
  per material sync - the `(QQuickItem*)` null-variant fingerprint.
- 230774d0: the colorizer's SourceProxy chose the direct path while
  the wrapper was layered and kept sampling the destroyed layer after
  every colorizing disengage - the no-repolish trap in the wild.
- 69baabf0/b634ef67: effect sources are texture providers only while
  an effect shows; task, badge and remove-ghost shadows became layer
  effects.

## 8. Environment and module resolution shadowing

FINGERPRINT: `module X is not installed` errors for applet QML
modules; a context menu or behavior silently switching to the stock
Plasma variant (the org.kde.taskmanager incident replaced the dock's
right-click menu with the stock task menu).

MECHANISM: appending a foreign-Qt QML root (the desktop session's
tree) to the import path lets a same-named module resolve from the
wrong build: it either fails to dlopen or silently replaces behavior.
An import-path append is never a narrow change.

FIX PATTERN: applet private QML modules come from the flake's own
pinned package set, never the session tree. Whole-package roots from
the SAME pin are safe (duplicated modules are the identical derivation
family and the staged Latte modules still win last); shared roots from
a different pin are not. Full detail and the staging mechanics live in
the latte-build-env skill.

REAL EXAMPLES:
- 4c9f3bc7: nine distinct applet modules failing with `is not
  installed`; owning packages added to LATTE_QML_MODULE_PATH from the
  flake pin, verified that the right-click menu stayed Latte's own.

## 9. Plasma 5 tree walks miss the Plasma 6 containment root

FINGERPRINT: a C++ walk over the containment's QML item tree finds
nothing and its dependents silently no-op, while the QML side renders
fine; invoke-by-name methods return false; a feature "works" only
because some fallback path serves it. No error anywhere at the walk
site (usually a silent early-return).

MECHANISM: on Plasma 6, containment roots must BE
ContainmentItem/PlasmoidItem types, so
`PlasmaQuick::AppletQuickItem::itemForApplet(containment)` hands back
the containment's OWN root - the item that carries the root
objectName itself - where Plasma 5 wrapped that root one level down
as a child. Any Qt5-era walk that scans childItems() for the root's
objectName and then descends matches nothing, forever.

FIX PATTERN: check the returned item ITSELF first, keep the child
scan as the safety fallback, and warn loudly when neither matches
(the walk failing is an environment/tree defect, not a normal state).
Grep candidates: every `itemForApplet` caller that follows with an
objectName comparison.

REAL EXAMPLES:
- Panel.qml viewLayout discovery (the round-five context-menu repair):
  right-click menus fell through to the stock task menu because the
  applet-position lookup never found the layout.
- a3d2afc7c: identifyShortcutsHost never found
  PositionShortcutsAbilityHost, so activate-entry, new-instance,
  shortcut badges and applet-id lookup were ALL dead since the port -
  Meta+number only appeared to work through view-iteration fallbacks.
  Found by the activateTaskAt D-Bus smoke, invisible for months
  because the invalid-method path returned false silently AND
  production logging swallowed the (then nonexistent) warnings.

## Smaller changes (one line each, verified in the plan and commits)

- Old-style `Connections` handlers (`onFoo: {...}`) still fire but are
  deprecated; prefer `function onFoo(params) {...}` or arrow handlers.
  Note the trap is different for plain objects: a `function
  onDrop(event) {...}` member on a DropArea does NOT connect in Qt6
  and silently becomes a dead method; use `onDrop: (event) => {...}`
  (b474adad).
- Signal-handler implicit parameter injection is removed: declare the
  parameters explicitly (`function(applet, rect)`), and verify each
  handler live; it is inconsistent across signal types (b474adad).
- `applet.action(name)` is removed: use `Plasmoid.internalAction(name)`
  or `applet.plasmoid.internalAction(name)`; the removed method throws
  a TypeError that aborts the whole handler, family 3 style (b474adad).
- Wayland window ids are QString UUIDs, not ints: WindowId is a
  NEWTYPE over QByteArray since 3c3f2d557 (explicit
  fromWaylandUuid()/fromX11WId() constructors, optional-returning
  toX11WId(), byte-wise ordering/hash - app/wm/windowid.h); empty
  means invalid, X11 ids ride as decimal strings; QML properties
  holding a winId must be `var`, not `int` (8e8cdf31 set the
  substrate, e9710e95 the wider port).
