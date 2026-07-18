# Edit-mode settings audit plan

Planning artifact, written 2026-07-18. The edit-mode settings surface (the
on-canvas rulers plus the floating configuration window) is the port's largest
concentration of controls that each promise a behaviour: write a config key,
change the live dock, and reflect the dock's current state. Several of them do
not keep that promise. This is the same defect class as D10 (the inherited Tasks
config page that rendered but applied nothing) and the CLAUDE.md cautionary tale
about controls that silently do the wrong thing.

This document is a CATALOG of every edit-mode control, a driven AUDIT METHOD
that can prove or disprove each control's contract, and a DECOMPOSITION into
implementation clusters. It is a CHECKLIST, same discipline as
docs/e2e-interaction-test-plan.md and docs/ub-catching-plan.md: `- [ ]` items
with `Commits:` lines, ticked with post-rebase hashes at merge time. No fixes
are implemented here. This is the plan the implementation agents execute.

## 0. The mandate (acceptance criteria for the audit)

Every edit-mode control MUST satisfy three properties. The audit proves or
disproves each, per control:

- **P1 APPLIES.** Driving the control changes the live dock, not just a stored
  value nothing reads. A control that writes a key no subsystem consumes is the
  D10 class and is a defect.
- **P2 RIGHT KEY, RIGHT EFFECT.** The control writes the config key (or state
  object) it is labelled for, with that key's intended effect, and does NOT
  write other keys as an unintended side effect. Coupled writes are allowed only
  where a clamp legitimately couples values, and then the coupling is documented
  and intended, not accidental.
- **P3 REFLECTS STATE AND STAYS IN SYNC.** The control shows the dock's current
  value on open, and when the same underlying value is changed from a DIFFERENT
  surface (the on-canvas ruler versus the settings-window slider, or a config
  edit), the control updates to match. Two views of one value must never
  disagree.

The audit is READBACK-FIRST, GOLDEN-ONLY-WHERE-BLIND, inheriting HC2 from the
e2e plan: assert with a D-Bus/state readback wherever one can express the fact,
and reach for a render golden only for genuinely visual-only properties (colour,
blur, radius, shadow) where no readback exists. Where a readback SHOULD exist
but does not, the audit ADDS one (observability first) rather than goldening
around it. Section 3 lists the readback gaps this audit opens.

## 1. The three confirmed seed defects (D15-D17)

All three live in the length cluster (Maximum/Minimum/Offset), the most coupled
control group. They are filed in docs/known-defects.md as D15-D17 and are the
first audit items (cluster CL-1). Root-cause findings from reading the code:

### D15 - the Maximum ruler drags the Minimum (coupled-min side effect)

- shell/.../canvas/maxlength/RulerMouseArea.qml `updateMaxLength()` (lines
  47-63) delegates to `lengthClamp.clampMaxLengthByStep(...)` and then writes
  back THREE config keys from the clamp result: `minLength` (54-56), `maxLength`
  (58) and `offset` (60-62).
- The `minLength` write is not spurious in the handler; it faithfully mirrors
  the core. The ROOT is in app/settings/lengthoffsetclamp.h
  `clampMaxLengthByStep` (114-134): `minimumIsCoupled = (state.maxLength ==
  state.minLength)` (122), and when coupled it drags the minimum up with the
  maximum, `state.minLength = std::max(30.0, value)` (126-128). So once the
  Maximum ruler is scrolled DOWN far enough to meet the Minimum, the pair
  becomes equal, and every further down-scroll of "Maximum" now also lowers
  "Minimum". One control was touched; two values moved.
- This coupling is Qt5-FAITHFUL. The reference fork latte-dock-qt6 carries the
  identical `updateminimumlength = (maxLength === minLength)` guard and the same
  coupled write (RulerMouseArea.qml, verified 2026-07-18). So D15 is not a port
  regression; it is an inherited behaviour flagged as surprising.
- DISPOSITION PENDING (the D1 precedent): the coupling has a defensible purpose
  (a fixed-length dock where `minLength == maxLength` stays fixed-length as the
  ruler scrolls) and a surprising face (scrolling "Maximum" silently lowers
  "Minimum"). The audit DRIVES the ruler and pins the exact trigger (the
  `max == min` boundary); the disposition (FIX as a deliberate deviation - the
  Maximum ruler floors at the minimum instead of dragging it - versus ACCEPTED
  as documented Qt5-faithful behaviour) is an open question for Bree (section
  8). The audit item stays RED until the disposition is recorded either way.

### D16 - the settings sliders desync from the on-canvas ruler

- The Maximum and Minimum sliders in shell/.../pages/AppearanceConfig.qml bind
  their handle declaratively: `value: plasmoid.configuration.maxLength` (264)
  and `value: plasmoid.configuration.minLength` (359).
- The two config views SHARE ONE config map: SubConfigView::init() sets
  `plasmoid` to the containment for both PrimaryConfigView (which hosts the
  settings pages) and CanvasConfigView (which hosts the ruler) -
  app/view/settings/subconfigview.cpp:228. So a ruler write to
  `plasmoid.configuration.maxLength` DOES fire the slider binding; view
  isolation is ruled out.
- The ROOT is the declarative binding being CLOBBERED. `LatteComponents.Slider`
  is a plain QtQuick.Templates Slider (declarativeimports/components/Slider.qml)
  whose `value` is an ordinary property. The moment the handle is dragged (or
  the handler's drag branch runs `value = Math.max(...)`, AppearanceConfig.qml
  290-293), an imperative assignment DESTROYS the `value: plasmoid.configuration
  .maxLength` binding. Nothing re-establishes it. From then on, ruler-driven (or
  any external) changes to the config no longer move the slider handle: the two
  views disagree. Before the first drag the binding is intact and the ruler DOES
  update the slider, so the desync appears only after the slider is touched,
  which matches "clobbered once touched".
- The offset slider in the SAME file does NOT have this bug: it avoids the
  declarative-value binding entirely, driving `value` through a proxy property
  (`offsetValue`, 458) plus `Binding {}` elements for `from`/`to` (460-474) and
  explicit re-sync in `updateOffset` (479-492). That is the fix pattern already
  present in-tree.
- FIX DIRECTION: give the Maximum and Minimum sliders the same re-syncing
  binding the offset slider uses (an explicit `Binding {}` or a proxy-property +
  `Qt.binding` restore), so the handle re-tracks the config after any
  interaction. The regression guard is a QML test that simulates a drag then an
  external config change and asserts the handle followed (section 2, tier B/D).

### D17 - the Maximum clamp floors by minLength even for Justify (alignment-blind)

- AppearanceConfig.qml:347 disables the Minimum slider for Justify alignment
  (`enabled: plasmoid.configuration.alignment !== LatteCore.Types.Justify`) -
  intended, a justify dock has no independent minimum length.
- But the Maximum clamp floors `maxLength` by the stored `minLength`
  UNCONDITIONALLY, regardless of alignment: lengthoffsetclamp.h
  `clampMaxLengthByStep` line 130 (`value = std::max(state.minLength, value)`)
  and `clampMaxLengthToValue` line 154 (`std::max({requestedMaxLength, minLength,
  1.0})`). So a Justify dock whose stored `minLength` is, say, 50 cannot have
  its Maximum lowered below 50, and the Minimum slider that would let the user
  lower it is disabled. The Maximum is stuck at or above a frozen, un-editable
  floor.
- ROOT plus a structural implication for the fix: the clamp core CANNOT
  currently tell Justify from Center. Its `Alignment` enum is two-valued {Edge,
  Centered} (lengthoffsetclamp.h:48-51) and the bridge maps BOTH Center and
  Justify onto `Centered` (lengthoffsetclamp.h:43-47,
  app/settings/lengthoffsetclampbridge). The alignment parameter today only
  steers the off-screen offset math, never the minLength floor. Making the floor
  alignment-aware therefore needs the core to REPRESENT Justify distinctly (a
  third enum value, or a `minimumApplies` flag on the call), not just a
  call-site guard. The fix touches the core, the bridge, and both slider
  handlers, and needs new core unit cases.
- FIX DIRECTION: for Justify, treat the effective minimum as 0 (skip the
  minLength floor) in both `clampMaxLengthByStep` and `clampMaxLengthToValue`.
  Extend the core alignment representation to carry Justify. Add core tests that
  a Justify request below the stored minLength is honoured and an Edge/Center
  request is still floored.

## 2. The audit method

Each control is audited through the layers below. Not every control needs every
layer; the catalog (section 5) names the layers per control. The method is a
SUPERSET of the e2e plan's F1 edit-mode family and REUSES its infra; section 4
reconciles the two so nothing is duplicated.

- **Tier A - core math (unit).** For controls backed by a pure core (the length
  clamp), assert the math in the sanitized unit tier (the existing
  lengthoffsetclamp tests). D15 and D17 fixes add cases here. This tier is where
  "only the right keys change, coupled where the clamp legitimately couples" is
  pinned deterministically.
- **Tier B - wiring (config-snapshot diff).** In the nested vehicle, snapshot
  the WHOLE containment config, drive the control, snapshot again, and assert
  the EXACT set of keys that changed. This is the decisive test for P2 (right
  key, no stray side effect): it catches D15's minLength side effect,
  TypeSelection's writes to schema-absent keys (section 6, S-a), and any
  wrong-key bug directly. The snapshot must read the in-process config map
  through a new readback (section 3), not the on-disk file, because KConfig
  deletes keys whose value returns to default - a file diff cannot distinguish
  "key written to default" from "key deleted" (the config round-trip caveat, e2e
  plan section 2).
- **Tier C - live effect (readback or golden).** After the config change, assert
  the DOCK changed (P1). Prefer a readback: length/thickness/margin/edge/
  alignment/screen/struts changes show in `viewsData()` geometry
  (absoluteGeometry, localGeometry, publishedStruts, strutsThickness, maskRect);
  visibility mode and delay show in `viewsData()`/`trackerData()`. Use a render
  golden ONLY for visual-only keys with no readback: palette/window colours,
  blur, radius, shadow size/opacity/colour, edit-background opacity. A control
  whose only observable effect is a pixel fact is flagged so the golden set
  stays deliberate.
- **Tier D - cross-view sync (P3).** Drive the value from surface 1 (the ruler,
  or a config write) and assert surface 2 (the slider handle, or the ruler
  overlay) agrees, and vice versa. Because the config map is shared (D16
  finding), the config VALUE always agrees; what can disagree is a control's
  DISPLAYED value after its binding was clobbered. Two probes:
  - a QML integration test (qmltestrunner) that reproduces the slider binding
    pattern against a shared QQmlPropertyMap, simulates a drag, then changes the
    map externally and asserts the handle followed - the cheap deterministic
    D16 regression guard;
  - a live confirmation in the vehicle: drive the ruler via fakepointer scroll
    (the `030-wheel-ruler-maxlength` recipe is the seed), read the config value
    back, and confirm the settings window renders the slider at the new value
    (a cropped golden of the slider row where no value readback exists, or the
    new control-value readback if section 3 adds one).

Driving surfaces available: the on-canvas rulers and the edit-background wheel
take fakepointer `scroll` (proven by `030-wheel-ruler-maxlength`); the settings
window is a mapped surface `setViewEditMode` brings up, so its buttons and
checkboxes take fakepointer `click` and its sliders take `drag`; combos and text
fields are pointer-drivable but fiddly, so where a control's WIRING (not its
pixel feel) is the question, a targeted QML-handler harness that invokes the
handler against a stub config map is the cheaper decisive driver. Each new
driver ships an acceptance test that proves it can OBSERVE A REJECTION / a
no-change, not only a green happy path (HC3 from the e2e plan).

## 3. Observability gaps this audit opens (prerequisites)

The current `viewsData()` exposes geometry, edit-mode, alignment, edge, screen
and visibility, but NOT the raw length/appearance config VALUES (maxLength,
minLength, offset, iconSize, zoomLevel, panelSize, panelTransparency,
thickMargin, lengthExtMargin, screenEdgeMargin, backgroundRadius,
backgroundShadowSize, the background toggles, the colour enums). Tier B and the
"reflects current state" half of P3 need those values pull-queryable. Two
additions, designed as one reviewed surface (Phase 10 discipline):

- a config-value readback (either extend `viewsData()` or a new
  `viewConfigData(u containmentId) -> s` JSON) that returns the containment
  General-group keys the audit asserts on, READ FROM THE IN-PROCESS map so the
  KConfig default-deletion caveat does not corrupt the diff. Reads state only,
  no execution, no secrets - within the observability rules.
- the Tasks page writes `tasks.plasmoid.configuration.*` (a DIFFERENT applet's
  config); auditing P1/P2 there needs the tasks applet's config values readable
  too (a `viewAppletsData` extension or a tasks-config readback). This is also
  what settles the D10 disposition (does the Tasks page apply at all).

These land in cluster CL-0 before the per-surface audits, exactly as the e2e
plan lands its C-I infra before its scenario chunks.

## 4. Relationship to the e2e interaction plan (no duplication)

The e2e plan's F1 edit-mode family (docs/e2e-interaction-test-plan.md sections
F1, C-S1..C-S3) drives edit mode and asserts CHROME GEOMETRY (ruler and toggle
rects, blueprint placement) across the edge x alignment matrix. This audit is
the SEMANTIC complement, not a re-run: it asserts each control's CONFIG EFFECT
and cross-view sync, a dimension F1 does not cover. Concretely:

- REUSE, do not rebuild: the nested vehicle, `setViewEditMode`, fakepointer
  (`scroll`/`click`/`drag`/`key`), `viewsData()` geometry readback, the
  chrome-geometry helpers, and the `030-wheel-ruler-maxlength` seed recipe. The
  config-value readback (section 3) is new and shared back to F1.
- The audit's driven ruler/slider tests MAY BE promoted into F1 assertions (a
  ruler-versus-slider agreement check is a natural F1 addition), or feed F1's
  matrix as the "control actually moved the dock" leg. They do not fork the
  infra.
- Division of labour: F1 owns "the chrome is drawn at the right place per
  edge/alignment"; this audit owns "each control writes the right key, moves the
  dock, and both views agree". The Justify-alignment interaction (D17) is where
  they meet: the audit sets alignment (by config write) and asserts the length
  clamp behaves, while F1 asserts the justify chrome renders.

## 5. The control catalog

Six surfaces. Counts: roughly 108 interactive controls mapping to about 86
distinct config keys or C++ state targets. Suspected-broken at plan time: the
length cluster (controls 1, 50, 52, and the interaction with 54 - D15/D16/D17),
the Palette combo (60, S-b), the TypeSelection preset (13, S-a), and the ENTIRE
31-control Tasks page (91-121) pending the D10 applies-at-all verdict. Legend for
TARGET CLASS:

- `cfg` = writes `plasmoid.configuration.<key>` (containment config, main.xml).
- `tcfg` = writes `tasks.plasmoid.configuration.<key>` (Tasks plasmoid config,
  plasmoid/.../config/main.xml) - the D10-class surface.
- `vis` = writes a `latteView.visibility.<prop>` C++ property.
- `pos` = calls `latteView.positioner.setNextLocation(...)` (edge/alignment/
  screen/group).
- `ind` = writes `latteView.indicator.<prop>` (C++).
- `us` = writes `universalSettings.<prop>`.
- `view` = writes another `latteView.<prop>` (byPassWM, isPreferredForShortcuts).
- `act` = a one-shot action (add/duplicate/remove/close/newView), no persistent
  key.

READBACK legend: `geo` = viewsData geometry; `struts` = publishedStruts/
strutsThickness; `vd` = a viewsData scalar field (edge/alignment/screen/
visibilityMode/inConfigureAppletsMode/keyboardNavigation); `cfg?` = needs the
new config-value readback (section 3); `golden` = visual-only, render golden;
`td` = trackerData; `new-rb` = needs a new C++-state readback (visibility timers,
byPassWM, indicator type, etc.).

### 5.1 On-canvas controls (CanvasConfigView, drawn on the dock in edit mode)

| # | Control (file:line) | Writes | Class | Expected dock effect | Readback | Sync check |
|---|---|---|---|---|---|---|
| 1 | maxLength Ruler wheel - RulerMouseArea.qml:47 | maxLength (+minLength coupled, +offset) | cfg | dock length changes | geo + cfg? | vs Maximum slider (D16); vs Ruler overlay |
| 2 | edit-background wheel - CanvasConfiguration.qml:139 | editBackgroundOpacity | cfg | edit-mode grid opacity (NOT panelTransparency - the CLAUDE.md fork-rewire trap; verify it stays editBackgroundOpacity) | golden (edit grid) + cfg? | reflects on re-open |
| 3 | Rearrange toggle - HeaderSettings.qml:108 | inConfigureAppletsMode | us | enters configure-applets mode (widgets draggable) | vd.inConfigureAppletsMode | vs settings state |
| 4 | Stick On Top (vertical only) - HeaderSettings.qml:68 | isStickedOnTopEdge | cfg | vertical view claims top-edge space | geo/struts + cfg? | reflects on re-open |
| 5 | Stick On Bottom (vertical only) - HeaderSettings.qml:132 | isStickedOnBottomEdge | cfg | vertical view claims bottom-edge space | geo/struts + cfg? | reflects on re-open |

### 5.2 Settings-window chrome (LatteDockConfiguration.qml)

| # | Control (file:line) | Writes | Class | Effect | Readback | Sync |
|---|---|---|---|---|---|---|
| 6 | Pin button - :213 | configurationSticker (+viewConfig.setSticker) | cfg | settings window stops auto-closing on focus loss | cfg? + behavioural | reflects on re-open |
| 7 | Advanced switch / label - :246,277 | inAdvancedModeForEditSettings | us | advanced-only rows/pages appear | behavioural (rows visible) | vs label state |
| 8 | Drag corner - controls/DragCorner.qml:108 | universalSettings.setScreenScales | us | settings-window size per screen | behavioural | reflects on re-open |
| 9 | Tab bar (Behavior/Appearance/Effects/Tasks) - :298-354 | page navigation | act | switches page | n/a | n/a |
| 10 | Add... / Duplicate combo-button - :487 | latteView.newView / duplicateView | act | new/duplicated view | viewsData count | n/a |
| 11 | Remove button - :613 | latteView.removeView() | act | view removed (undo window) | viewsData count | n/a |
| 12 | Close button - :626 | viewConfig.hideConfigWindow() | act | closes chrome | vd.editMode | n/a |

### 5.3 Behavior page (BehaviorConfig.qml)

| # | Control (file:line) | Writes | Class | Effect | Readback | Sync |
|---|---|---|---|---|---|---|
| 13 | Type selection (inline dual) - controls/TypeSelection.qml:57 | BATCH cfg (alignment, useThemePanel, panelSize, zoomLevel, ...) incl. `solidPanel` + `colorizeTransparentPanels` which are ABSENT from main.xml | cfg | switches dock<->panel preset | cfg? per key | SUSPECT: dead-key writes (S-a) |
| 14 | Screen combo - :138 | positioner.setNextLocation(screen/group) | pos | moves view to screen/group | vd.screen | reflects current screen |
| 15-18 | Location Bottom/Left/Top/Right - :182,200,218,236 | setNextLocation(edge) | pos | moves view to edge | vd.edge + geo | `checked` reflects plasmoid.location |
| 19-22 | Alignment Left-or-Top/Center/Right-or-Bottom/Justify - :278,294,310,327 | setNextLocation(alignment) | pos | re-anchors length | vd.alignment + geo | `checked` reflects config alignment; Justify feeds D17 |
| 23-25 | Visibility Always/AutoHide/DodgeActive - :368,384,399 | visibility.mode | vis | visibility behaviour | vd.visibilityMode + td | `checked` reflects live mode |
| 26 | Dodge mode (Maximized/AllWindows) - CustomVisibilityModeButton | visibility.mode + lastDodgeVisibilityMode | vis+cfg | dodge sub-mode | vd.visibilityMode + cfg? | reflects live mode |
| 27 | Windows mode (GoBelow/CanCover/AlwaysCover) - CustomVisibilityModeButton | visibility.mode + lastWindowsVisibilityMode | vis+cfg | windows sub-mode | vd.visibilityMode + cfg? | reflects live mode |
| 28 | Sidebar mode (OnDemand/AutoHide) - CustomVisibilityModeButton | visibility.mode + lastSidebarVisibilityMode | vis+cfg | sidebar sub-mode | vd.visibilityMode + cfg? | reflects live mode |
| 29 | Show timer - :548 | visibility.timerShow | vis | show delay | new-rb | reflects live timer |
| 30 | Hide timer - :579 | visibility.timerHide | vis | hide delay | new-rb | reflects live timer |
| 31 | Track active window combo - :625 | activeWindowFilter | cfg | active-window scope | cfg? | reflects value (S-d combo round-trip) |
| 32 | Left button: drag active window - :657 | dragActiveWindowEnabled | cfg | empty-area drag | cfg? | `checked` reflects value |
| 33 | Middle button: close active window - :683 | closeActiveWindowEnabled | cfg | empty-area close | cfg? | `checked` reflects value |
| 34 | Mouse wheel action combo - :711 | scrollAction | cfg | empty-area wheel | cfg? | reflects value (S-d) |
| 35 | Thin title tooltips - :751 | titleTooltips | cfg | Latte tooltips | cfg? | reflects value |
| 36 | Expand popup via wheel - :763 | mouseWheelActions | cfg | applet-popup wheel | cfg? | reflects value |
| 37 | Adjust size automatically - :776 | autoSizeEnabled | cfg | auto icon resize | cfg? + geo | reflects value |
| 38 | Position global shortcuts - :789 | isPreferredForShortcuts (+layout.preferredForShortcutsTouched) | view | shortcut targeting | new-rb | reflects live state |
| 39 | Always use floating gap - :817 | floatingInternalGapIsForced | cfg | interaction gap | cfg? | reflects value |
| 40 | Hide floating gap for maximized - :830 | hideFloatingGapForMaximized | cfg | gap under maximized | cfg? | reflects value |
| 41 | Delay floating-gap hiding - :841 | floatingGapHidingWaitsMouse | cfg | gap hide delay | cfg? | reflects value |
| 42 | Mirror floating gap - :853 | floatingGapIsMirrored | cfg | gap mirroring | cfg? | reflects value |
| 43 | Activate KWin edge after hiding - :887 | visibility.enableKWinEdges | vis | KWin edge hint | new-rb | reflects live |
| 44 | Can be above fullscreen - :902 | latteView.byPassWM | view | bypass-WM flag | new-rb | reflects live |
| 45 | Raise on desktop change - :913 | visibility.raiseOnDesktop | vis | raise on desktop | new-rb | reflects live |
| 46 | Raise on activity change - :923 | visibility.raiseOnActivity | vis | raise on activity | new-rb | reflects live |

### 5.4 Appearance page (AppearanceConfig.qml)

| # | Control (file:line) | Writes | Class | Effect | Readback | Sync |
|---|---|---|---|---|---|---|
| 47 | Absolute size slider - :75 | iconSize (+syncGeometry) | cfg | icon size, dock thickness | geo + cfg? | reflects value |
| 48 | Relative size slider - :125 | proportionIconSize | cfg | proportional icon size | geo + cfg? | reflects value |
| 49 | Zoom on hover slider - :189 | zoomLevel | cfg | parabolic zoom factor | cfg? + hover golden | reflects value |
| 50 | Maximum slider - :260 | maxLength (+offset) | cfg | dock length | geo + cfg? | D16 target; vs ruler |
| 51 | Maximum value scroll/click - :315 | maxLength (fractional/round) | cfg | fine length | geo + cfg? | vs slider/ruler |
| 52 | Minimum slider - :355 (disabled for Justify at :347) | minLength | cfg | dynamic min length | geo + cfg? | D16/D17 target; vs ruler |
| 53 | Minimum value scroll/click - :398 | minLength | cfg | fine min | cfg? | vs slider |
| 54 | Offset slider - :438 | offset (+maxLength via clampOffset) | cfg | length offset (the correct re-sync pattern; the D16 reference) | geo + cfg? | reflects value |
| 55 | Offset value scroll/click - :526 | offset | cfg | fine offset | cfg? | vs slider |
| 56 | Maximize in presence of maximized - :561 | maximizeWhenMaximized | cfg | dynamic length | cfg? + geo | reflects value |
| 57 | Margin Length slider - :608 | lengthExtMargin | cfg | length margin | geo + cfg? | reflects value |
| 58 | Margin Thickness slider - :653 | thickMargin | cfg | thickness margin | geo + cfg? | reflects value |
| 59 | Floating gap slider - :700 | screenEdgeMargin | cfg | screen-edge gap | geo + cfg? | reflects value |
| 60 | Palette combo - :757 | themeColors | cfg | colour palette | golden + cfg? | SUSPECT: colorsToIndex collides Reverse/Layout onto index 3 (S-b) |
| 61 | From Window combo - :807 | windowColors | cfg | window-based colours | golden + cfg? | reflects value |
| 62 | Background switch - :848 | useThemePanel | cfg | background on/off | golden + cfg? | reflects value |
| 63 | Thickness slider - :881 | panelSize | cfg | background thickness | golden/geo + cfg? | reflects value |
| 64 | Opacity slider - :931 | panelTransparency | cfg | background opacity | golden + cfg? | reflects value |
| 65 | Radius slider - :980 | backgroundRadius | cfg | corner radius | golden + cfg? | reflects value |
| 66 | Shadow slider - :1023 | backgroundShadowSize | cfg | background shadow | golden + cfg? | reflects value |
| 67 | Blur button - :1063 | blurEnabled | cfg | background blur | golden + cfg? | `checked` reflects value |
| 68 | Shadows button - :1082 | panelShadows | cfg | background shadow on/off | golden + cfg? | `checked` reflects value |
| 69 | Outline button - :1101 | panelOutline | cfg | border line | golden + cfg? | `checked` reflects value |
| 70 | All Corners button - :1117 | backgroundAllCorners | cfg | corner drawing | golden + cfg? | `checked` reflects value |
| 71 | Prefer opaque when touching - :1146 | solidBackgroundForMaximized | cfg | dynamic opacity | golden + cfg? | reflects value |
| 72 | Hide background when not needed - :1160 | backgroundOnlyOnMaximized | cfg | dynamic visibility | golden + cfg? | reflects value |
| 73 | Hide shadow for maximized - :1174 | disablePanelShadowForMaximized | cfg | dynamic shadow | golden + cfg? | reflects value |
| 74 | Prefer Plasma bg for popups - :1195 | plasmaBackgroundForPopups | cfg | expanded-applet bg | golden + cfg? | reflects value |

### 5.5 Effects page (EffectsConfig.qml)

| # | Control (file:line) | Writes | Class | Effect | Readback | Sync |
|---|---|---|---|---|---|---|
| 75 | Applet shadows switch - :48 | appletShadowsEnabled | cfg | applet shadows | golden + cfg? | reflects value |
| 76 | Shadow size slider - :75 | shadowSize | cfg | applet shadow size | golden + cfg? | reflects value |
| 77 | Shadow opacity slider - :123 | shadowOpacity | cfg | applet shadow opacity | golden + cfg? | reflects value |
| 78 | Default shadow colour - :175 | shadowColorType | cfg | shadow colour source | cfg? | `checked` reflects value |
| 79 | Theme shadow colour - :194 | shadowColorType | cfg | shadow colour source | cfg? | `checked` reflects value |
| 80 | User shadow colour (+dialog) - :214,265 | shadowColorType + shadowColor | cfg | custom shadow colour | golden + cfg? | swatch reflects value |
| 81 | Animations switch - :299 | animationsEnabled | cfg | all animations | behavioural + cfg? | reflects value |
| 82-84 | Duration x1/x2/x3 - :330,344,358 | durationTime | cfg | animation speed (CAUTION: label is SPEED, stored value is the INVERSE time factor - x1->3, x2->2, x3->1) | cfg? | `checked` compares raw value (S-c) |
| 85 | Indicators switch - :382 | latteView.indicator.enabled | ind | indicators on/off | new-rb | reflects live |
| 86-88 | Indicator style Latte/Plasma/Custom - :416,427,439 | latteView.indicator.type | ind | indicator plugin | new-rb | tab reflects live type |
| 89 | Custom indicator button - controls/CustomIndicatorButton.qml:78 | latteView.indicator.type/customType (+install) | ind | custom indicator | new-rb | reflects live |
| 90 | Indicator sub-options (per-plugin StackView) - :499 | per-indicator config keys (dynamic) | cfg/ind | indicator-specific | cfg? per key | reflects value; own sub-audit |

### 5.6 Tasks page (TasksConfig.qml) - writes `tasks.plasmoid.configuration.*` (D10 class)

The whole page writes the Tasks PLASMOID's config, not the containment. Whether
those writes reach the running tasks applet is the D10 question and must be
proven for the page as a whole BEFORE the per-control audit (P1 first). Controls
91-121, all `tcfg`:

- Badges (91-95): showInfoBadge, showProgressBadge, showAudioBadge,
  infoBadgeProminentColorEnabled, audioBadgeActionsEnabled.
- Interaction (96-99): isPreferredForDroppedLaunchers, showWindowActions,
  previewWindowAsPopup, isPreferredForPositionShortcuts.
- Filters (100-105): showOnlyCurrentScreen, showOnlyCurrentDesktop,
  showOnlyCurrentActivity, showWindowsOnlyFromLaunchers, hideAllTasks,
  groupTasksByDefault.
- Animations (106-110): animationLauncherBouncing, animationWindowInAttention,
  animationNewWindowSliding, animationWindowAddedInGroup,
  animationWindowRemovedFromGroup.
- Launchers group (111-113): Unique/Layout/Global (launchersGroup).
- Scrolling (114-116): scrollTasksEnabled, manualScrollTasksType,
  autoScrollTasksEnabled.
- Actions (117-121): leftClickAction, middleClickAction, hoverAction,
  taskScrollAction, and the modifier/modifierClick/modifierClickAction cluster.

Readback needs the tasks-config readback (section 3); the effect is a
tasks-applet behaviour proven by `viewTasksData` where observable (grouping,
filtering, launcher visibility, badges) and by behavioural drive (click/hover/
wheel actions) elsewhere.

## 6. Additional suspected defects surfaced while cataloging

Filed here as SUSPECTED (code-reading only); the audit confirms or clears them.
Promote to a D-number in docs/known-defects.md when the driven audit reproduces
one:

- **S-a TypeSelection dead keys.** controls/TypeSelection.qml writes
  `plasmoid.configuration.solidPanel` and
  `plasmoid.configuration.colorizeTransparentPanels`; neither exists in
  containment/.../config/main.xml (verified 2026-07-18). Those writes land in a
  key nothing reads - a P2 violation (writes the wrong/nonexistent key) exactly
  in the D10 family. Audit: config-snapshot diff after a Type switch shows keys
  with no schema entry.
- **S-b Palette combo index collision.** AppearanceConfig.qml `colorsToIndex`
  (:786) maps BOTH `ReverseThemeColors` and `LayoutThemeColors` to index 3, and
  the model's index 3 is "Layout Custom Colors". A dock stored with the (legacy,
  commented-out-in-model) Reverse palette shows "Layout" selected and, on any
  change, would be rewritten as Layout - a P3 (reflect-state) failure. Also the
  main.xml `themeColors` enum choices do not include a Layout choice, so verify
  the QML `LatteContainment.Types` values and the KConfig enum agree numerically.
- **S-c durationTime label/value inversion.** The x1/x2/x3 buttons store 3/2/1;
  the label reads as SPEED, the stored value is an inverse time factor. Likely
  Qt5-faithful, but confirm nothing downstream reads durationTime by the schema
  choice NAME (None/x1/x2/x3 = 0/1/2/3), which would disagree with the stored
  3/2/1. If consistent, ACCEPT with a comment; if not, it is a real mismatch.
- **S-d bidirectional combos write-on-read.** Several combos pair
  `currentIndex: <from config>` with `onCurrentIndexChanged: config = <mapped>`.
  When the config changes externally the handler re-fires and writes back;
  usually idempotent, but where index != stored value (leftClickAction 0/1/2 ->
  6/7/8, hoverAction, etc.) the round-trip must be proven identity. Audit: change
  the key externally, assert the combo re-selects the right row and does not
  rewrite a different value.

## 7. Decomposition into implementation clusters

Each cluster is sized for one Opus implementation agent and lands as its own PR
with an independent review. CL-0 is the shared prerequisite; the rest can run
mostly in parallel once CL-0 is merged, with the noted ordering.

- **CL-0 - audit infrastructure and observability (PREREQ, lands first).** The
  config-value readback and tasks-config readback (section 3); the
  config-snapshot-diff harness and its HC3 acceptance test; the settings-window
  fakepointer drive helpers; the QML-handler stub harness for wiring tests. No
  control audit can assert P1/P2/P3 without these. Blocks every other cluster.
- **CL-1 - length cluster (D15, D16, D17; controls 1, 50-55).** The
  highest-value cluster: the three seed defects and their interactions all live
  here. Includes the clamp-core extension for D17 (Justify representation), the
  D15 disposition drive, and the D16 slider re-sync fix pattern. Land right after
  CL-0. Depends on CL-0 only; the alignment precondition for D17 is set by config
  write, so no hard dependency on CL-3.
- **CL-2 - appearance non-length (controls 47-49, 56-74; S-b).** Items, Margins,
  Colors, Background. Mostly `cfg` with golden-verified visual effects. Depends
  on CL-0.
- **CL-3 - behavior (controls 13-46; S-a).** Location/Alignment/Visibility/Delay/
  Actions/Environment. Mixes `pos`/`vis`/`cfg`/`view` writers; needs the
  visibility and view-property readbacks. Carries the TypeSelection dead-key
  finding (S-a). Depends on CL-0.
- **CL-4 - effects (controls 75-90; S-c).** Shadows/Animations/Indicators. The
  indicator controls are `ind` (C++), needing an indicator readback (fold into
  CL-0 if small, else CL-4 adds it). Depends on CL-0.
- **CL-5 - tasks page and the D10 disposition (controls 91-121).** First prove
  whether `tasks.plasmoid.configuration.*` writes apply at all (settles D10),
  then audit each control. Its own large cluster; may pause for a Bree decision
  on D10 (wire-up versus hide) before the per-control sweep. Depends on CL-0 (the
  tasks-config readback).
- **CL-6 - chrome and on-canvas non-length (controls 2-12).** Pin, advanced,
  drag-corner scale, add/duplicate/remove/close, rearrange toggle, stick buttons,
  the edit-background-opacity wheel. Mostly `us`/`act`/a few `cfg`. Depends on
  CL-0.

Merge order: CL-0 -> CL-1 -> {CL-2, CL-3, CL-4, CL-6 in any order} -> CL-5 (or
CL-5 in parallel once its readback lands and D10 is dispositioned). Each cluster
files any newly reproduced defect as a D-number the same session, per the
known-defects discipline.

## 8. Decisions (resolved by Bree 2026-07-18)

- **D15 (max/min coupling): ACCEPTED - keep the coupling.** It keeps a
  fixed-length dock fixed as the ruler scrolls (easy to use) and is Qt5-faithful.
  The confusion it caused was really D16 (the settings did not update to SHOW the
  coupled min moving); CL-1 fixes D16 so the coupling is legible, and pins the
  coupling as intended behaviour rather than removing it.
- **D10 (Tasks page): WIRE IT UP.** If CL-5 finds the tasks-config writes do not
  apply, make them apply (the ng eabf7c89a/94f87ba66/ed0afd054 pattern), not hide.
  A config tab that renders should do what it shows.
- **Config-value readback: a DEDICATED viewConfigData().** Keep viewsData() lean
  (it already carries geometry/masks/struts/applets/tasks); the config values get
  their own read surface. Built in CL-0.
- **P3 for C++-property controls: ADD the readbacks now** (visibility timers,
  byPassWM, indicator type, ...), do not defer - observability-first, a control
  that cannot be verified to reflect state is only half-audited. Folded into CL-0.

## 9. Checklist

Ticked with post-rebase hashes at merge time. Grouped by cluster.

### CL-0 - infrastructure and observability (prereq)
- [x] **AU-0a** Config-value readback via a dedicated `viewConfigData(u) -> s`
      (General-group config values + the C++ `view` half: visibility timers,
      byPassWM, indicator type), read from the in-process `KConfigPropertyMap`
      so the KConfig default-deletion trap cannot corrupt the diff. XML +
      adaptor + dbusreportstest pin + docs/dbus-* update. Commits: 931b900fe
- [x] **AU-0b** Tasks-config readback `appletConfigData(u,u) -> s` for
      `tasks.plasmoid.configuration.*` (and any plain applet). Commits: 931b900fe
- [x] **AU-0c** Config-snapshot-diff harness (tests/units/configsnapshotdiff.h +
      tests/e2e/audit/audit-lib.sh): snapshot all keys, drive, snapshot, assert
      exact key delta. HC3 proven - a wrong-key, a stray coupled write (D15
      shape) and a no-op (D10 shape) each report FAIL; a default-deleted key is a
      loud change, never a silent pass. Commits: ad00dba0c
- [x] **AU-0d** Settings-window fakepointer drive helpers (audit_settings_*) + a
      QML-handler stub harness (tests/settingswiringharnesstest.cpp) driving a
      real handler body against a stub QQmlPropertyMap. HC3 rejection caught
      through a real handler, not crafted JSON. Commits: ad00dba0c

CL-0 review follow-up nits (from the PR #40 independent review, non-blocking):
- [ ] **AU-0e** `configValueToJson` (app/dbusreports.h + the mirror in
      settingswiringharnesstest.cpp) falls back to a bare `toString()` for a
      non-scalar, non-color type. Latent false-PASS: a future General-group key
      of a type with no JSON scalar and no distinguishing string (QFont,
      QVariantMap) would collapse the way the QColor branch already guards
      against. No such key exists today. Harden: warn/refuse on an unserializable
      non-color type rather than silently stringify. Commits:
- [ ] **AU-0f** `appletConfigData` reads `target->property("configuration")`
      directly; a subcontainment applet (system tray) keeps its real config on
      the subcontainment (the containmentinterface.cpp:1069 pattern), so a
      subcontainment id silently returns the wrapper's near-empty config. The
      tasks plasmoid is plain, so CL-5 is unaffected; add a one-line warn if the
      surface ever generalizes to subcontainments. Commits:
- [ ] **AU-0g** `appletConfigData` reads any applet id on the containment,
      including a user-added third-party widget - broader than the docs' "D10
      tasks plasmoid" framing. Low severity (the caller's own session over their
      own bus, within the read-surface safety contract). Commits:

### CL-1 - length cluster (seed defects)
- [ ] **AU-1a D15** Drive the Maximum ruler; assert via snapshot-diff which keys
      change at and away from the `max == min` boundary; record the disposition
      (FIX-deviation or ACCEPTED) with Bree. Commits:
- [ ] **AU-1b D16** QML integration test reproducing the slider binding: simulate
      a drag then an external config change, assert the handle re-tracks. RED
      until the re-sync fix lands. Commits:
- [ ] **AU-1c D16** Live cross-view test: ruler scroll (fakepointer) -> config
      readback -> settings slider renders the new value (golden crop or
      control-value readback). Commits:
- [ ] **AU-1d D17** Core test: a Justify request below the stored minLength is
      honoured; Edge/Center still floored. Requires the core Justify
      representation. RED until the alignment-aware clamp lands. Commits:
- [ ] **AU-1e** Offset slider audit (control 54): confirm it is the correct
      re-sync reference and stays P3-correct after the D16 fix generalizes its
      pattern. Commits:
- [ ] **AU-1f** Fine value scroll/click rows (controls 51, 53, 55): P2/P3 for the
      ctrl-wheel and click-to-round paths. Commits:

### CL-2 - appearance non-length
- [ ] **AU-2a** Items (47-49): iconSize/proportionIconSize/zoomLevel apply to geo
      and hover; P2/P3. Commits:
- [ ] **AU-2b** Margins (57-59): lengthExtMargin/thickMargin/screenEdgeMargin geo
      effect; P2/P3. Commits:
- [ ] **AU-2c** Colors (60-61) incl. S-b Palette index collision; golden effect;
      P2/P3. Commits:
- [ ] **AU-2d** Background group (62-74): golden effects; P2/P3 for every toggle
      and slider. Commits:

### CL-3 - behavior
- [ ] **AU-3a** Location + Alignment + Screen (14-22): setNextLocation effect via
      vd.edge/alignment/screen; P3 `checked` reflects live. Commits:
- [ ] **AU-3b** Visibility + Delay (23-30, 43-46): mode + timers via readback
      (adds the visibility readbacks); P3 reflects live. Commits:
- [ ] **AU-3c** Actions + Environment + Items checkboxes (31-42, 44) incl. the
      view-property controls (byPassWM, isPreferredForShortcuts); S-d combo
      round-trips. Commits:
- [ ] **AU-3d** S-a TypeSelection dead-key check (control 13): snapshot-diff shows
      `solidPanel`/`colorizeTransparentPanels` land nowhere; file/clear the
      finding. Commits:

### CL-4 - effects
- [ ] **AU-4a** Shadows (75-80): golden effects; P2/P3. Commits:
- [ ] **AU-4b** Animations (81-84) incl. S-c durationTime inversion check.
      Commits:
- [ ] **AU-4c** Indicators (85-90): latteView.indicator readback; P1/P3; the
      per-plugin sub-options own sub-audit. Commits:

### CL-5 - tasks page (D10)
- [ ] **AU-5a D10** Prove whether `tasks.plasmoid.configuration.*` writes apply
      at all (page-level P1); settle the D10 disposition with Bree. Commits:
- [ ] **AU-5b** Badges + Interaction + Filters (91-105): P1/P2/P3 via tasks-config
      readback and viewTasksData where observable. Commits:
- [ ] **AU-5c** Animations + Launchers + Scrolling + Actions (106-121): P1/P2/P3;
      behavioural drive for the click/hover/wheel actions. Commits:

### CL-6 - chrome and on-canvas non-length
- [ ] **AU-6a** On-canvas non-length (2-5): edit-background wheel keeps
      editBackgroundOpacity (the fork-rewire trap), stick buttons, rearrange
      toggle. Commits:
- [ ] **AU-6b** Settings chrome (6-12): pin/advanced/drag-corner/add/duplicate/
      remove/close; behavioural + universalSettings readback. Commits:
