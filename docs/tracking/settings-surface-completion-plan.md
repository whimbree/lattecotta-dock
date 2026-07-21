# Settings surface completion plan

Planning artifact, split for review 2026-07-20. The dependent execution ledger
has landed, and authorization is limited to the exact units in section 1. This
initiative proves that Latte-owned settings can be reached, operated, persisted,
and observed through their real interfaces. It follows the completed
`edit-mode-settings-audit-plan.md` wiring audit without treating handler
transcription or direct config seeding as end-to-end evidence.

Production behavior changes are not approved by this document except SC-WT1
(the D58 tracker-enablement root fix and regression) at the exact existing-
contract boundary recorded below. No task-action expansion, empty-area action
expansion, persisted action enum, schema migration, or maintained-continuation
divergence is approved before the driven evidence and sign-off gates below.
Exact models, enum values, D-Bus records, and migrations are selected only after
source inventory and runtime evidence establish what is needed.

## 1. Approval boundary

The exact approved or completed boundary is:

- SC-F1 (the per-view source inventory and evidence ledger) and SC-F2 (the
  source-to-ledger coverage gate);
- SC-O1 (the read-only settings-control D-Bus registry) after SC-F1 defines the
  minimum evidence fields and SC-F2 closes the source-to-ledger coverage gate;
- SC-D1 (the pointer and keyboard control drivers) and SC-D2 (the popup and
  lifecycle driver helpers) after SC-O1;
- SC-C1 (the ComboBox and ComboBoxButton component family);
- SC-C2 (the slider and numeric-field component family);
- SC-C3 (the text-entry component family);
- SC-C4 (the checkable and grouped-button component family);
- SC-C5 (the color-control and dialog component family);
- SC-T1 (the D29 middle-click evidence capture), SC-T2 (the D29 disposition),
  and SC-B1 (the D30 current-contract investigation) are complete records;
- SC-T3 (the D29 narrow middle-click dispatch readback), followed by SC-T5 (the
  D29 permanent runtime-effect acceptance);
- SC-W1 (the launcher-wheel regression guard) for D56 (pure-launcher task wheel
  uses inherited asymmetric activation);
- SC-CW1 (the D57 ConfigOverlay wheel-threshold reproduction); and
- SC-WT1 (the D58 tracker-enablement root fix and regression).

Every other unchecked item remains sequenced but unauthorized for
implementation. The relevant evidence gate and explicit maintainer sign-off for
any Qt5 divergence must be recorded in this plan before an implementation agent
is farmed. SC-B2 (the D30 product decision and sign-off gate) remains pending. A
sign-off for one behavior does not approve adjacent action expansion.

## 2. Why the earlier audit is not completion evidence

The earlier audit established useful config readbacks, exact-key diffs, handler
tests, and a catalog of 121 logical settings entries. Its evidence is narrower
than this initiative's completion contract:

- `tests/behaviorwiringaudittest.cpp` and related tests execute transcribed
  handlers against stub maps. They prove expected writes, not real-control
  operability.
- `tests/e2e/032-behavior-live-reflect.sh` checks readback agreement without
  clicking each Behavior control.
- `tests/e2e/034-tasks-config-apply.sh` seeds Tasks config directly and observes
  one `launchersGroup` effect. It does not operate the Tasks page or prove the
  other labeled effects.
- `tests/qml/tst_taskactions.qml` checks action-to-token mapping. Production
  task wheel handling does not call `TaskActions.scrollCommandFor`, and the test
  does not prove pointer receipt, target classification, model requests, or
  independent effects.
- Popup selection, sliders, text entry, keyboard paths, disabled states,
  persistence, accessibility, and abort cleanup were not exhaustively driven.

The old plan remains the wiring/readback ledger. This plan owns actual control
operation and runtime-effect evidence.

## 3. Scope

### 3.1 Per-view settings

SC (per-view settings completion) covers every concrete affordance represented
by logical entries 1-121 in `edit-mode-settings-audit-plan.md`, plus affordances
grouped inside those entries:

- the canvas ruler, edit background, rearrange mode, and edge-stick controls;
- settings-window chrome, mode switch, tabs, resize handle, and view actions;
- Behavior, Appearance, Effects, and Tasks pages;
- the Dock/Panel chooser and Latte-owned ConfigOverlay actions;
- every bundled indicator configuration page and the third-party host contract;
- every pointer button, wheel path, hold path, split-button half, popup row,
  keyboard path, and repeated applet instance found by the source inventory;
- dock and panel modes, both axes, applicable alignments, and conditional
  enabled or visible states.

Third-party applet settings internals are out of scope. Latte's action for
opening an applet settings dialog remains in scope.

### 3.2 Global settings

GS (global settings completion) starts after the per-view page units and matrix
closure. It covers the Qt Widgets and mixed QML surfaces under `app/settings/`:

- the Settings dialog shell, Layouts table, and Preferences page;
- Actions, Views, Details, Screens, and Export Template dialogs;
- add, copy, import, export, remove, switch, apply, cancel, reset, and default
  paths;
- validation, persistence, restart, and destructive rollback behavior;
- keyboard and accessibility behavior, plus nested-KWin effects on live views.

GS-F1 (the global Qt Widgets source inventory) performs a fresh source pass. The
per-view count is not reused as a proxy for global coverage.

## 4. Completion contract

Each ledger row records separate evidence for C1-C9 (the nine
control-completion properties). One aggregate green flag is not sufficient.

- **C1 Reachable:** intended mode and state combinations expose the control;
  named conditions explain hidden and disabled states.
- **C2 Operable:** real pointer and keyboard input reaches the real control.
  Popups open and close, every enabled row can be selected, buttons invoke once,
  sliders reach boundaries and interior values, and fields validate and commit.
- **C3 Right write:** only the intended config or C++ state changes, including
  documented coupled writes. Dead and stray keys fail this property.
- **C4 Runtime effect:** the labeled dock, task, window, layout, or settings
  behavior changes. Config transport alone is not evidence.
- **C5 Reflects and syncs:** initial and external state changes appear in the
  control, and interaction does not destroy the binding.
- **C6 Persists:** persistent values survive close, reopen, and restart.
  Transient actions leave no persistent residue.
- **C7 Complete choices:** every displayed choice has a handled branch and every
  supported choice is represented or deliberately hidden. The source inventory,
  not a preselected action matrix, defines the set to check.
- **C8 Clean lifecycle:** close, tab change, edit exit, abort, and object
  destruction leave no popup, pressed state, focus grab, drag, or stale registry
  generation.
- **C9 Accessible:** useful role, name, value, actions, focus order, activation,
  and rendered focus state exist where applicable.

## 5. Evidence architecture

### 5.1 Independent source inventory

SC-F1 inventories interactive QML declarations and C++/Qt Widgets affordances
before the runtime registry exists. It includes accepted buttons, handlers,
popup actions, delegates, repeated instances, and explicit non-control
exemptions. Each ledger row records source location, stable audit identity,
reachability, input paths, expected write, runtime oracle, persistence,
accessibility, novel matrix cells, and C1-C9 evidence.

SC-F2 compares the independent inventory and checked ledger in both directions.
Later registry comparison is a third source. No source may define and certify
its own coverage universe.

The source side is derived independently from recursive shell configuration and
control roots, every public shared-component QML file, recursive bundled-
indicator configuration roots, and the explicit runtime QML and C++ surfaces
that can execute per-view actions. The shared-component `private` directory is
excluded deliberately. Its three live settings uses are visual-only:
`TextFieldFocus` and `ButtonShadow` decorate `ComboBox`, while `RoundShadow`
decorates `Slider`. A permanent scanner check permits only contracted lifecycle
members in those three declarations and verifies that the input-capable
`MobileCursor` and `MobileTextActionsToolBar` names remain absent from tokenized
live settings QML; their only current ComboBox references are commented out.
Any future live private input helper must become an explicit source root.

Schema 2 stores exact stable selectors instead of line anchors. QML ids and
named ancestry are preferred, anonymous nodes use normalized full-node SHA-256,
model rows carry value keys, and bounded dynamic menu construction, insertion,
and signal wiring carry semantic call selectors. Helper declarations never
count as invocations. `Component.onCompleted` and `Component.onDestruction` are
the exact lifecycle-member set and are candidates on every QML object without
promoting arbitrary `onFoo` notifications. C++ actions support qualified and
globally qualified types, aliases, inferred and typed copy initialization, and
lambda ancestry across `mutable`, `noexcept`, attributes, and trailing returns.
Lines remain diagnostics only. Intentional non-controls use exact
representatives that must resolve once; prefix, wildcard, or inherited coverage
is not accepted. Every affordance and exemption must also have a mapping in its
declared canonical source file, so an unrelated shared implementation cannot
certify the owner.

The fixed baseline is 734 candidates across 58 files: 237 QML objects, 357 QML
members, 57 static model elements, 57 semantic QML calls, 16 C++ constructions,
and 10 C++ calls. Sixteen representatives complete the reverse mapping to 271
affordances and 25 explicit exemptions through exactly 1,259 accepted coverage
relations across sites and representatives. Invalid mappings contribute no
relations. The 25th exemption is the shipped public `SpinBox`, which has no live
per-view settings instantiation; a future use in a scanned settings surface
creates its own unmapped candidate. Permanent red mutations cover malformed
schema shapes and field types, duplicate and wildcard links, source-universe
drift, stale measurements and selectors, candidates disguised as
representatives, globally uncovered identities, missing canonical ownership, a
new valid relation, a new `createNewItem` action, and lifecycle changes or
additions on noninteractive objects. Fixed assertions pin every count
independently of the JSON measurements.

SC-F2 is a hard gate for every later registry, driver, component, and page
registration. No production control registration or driver work starts through
a transitive shortcut before that gate lands.

### 5.2 Read-only control registry

SC-O1 adds only the state needed to locate and inspect inventoried controls.
The inventory selects the exact XML and record fields. At minimum, the design
must distinguish view, surface, load generation, optional applet, audit
identity, and repeated instance; expose mapped and clipped hit geometry; report
visible, enabled, focused, and current control state; describe popup rows when a
popup exists; and remove destroyed generations.

SC-O1 lands the registry transport, serializers, lifecycle, and a fixture-backed
vertical slice. It does not register every production page in one PR. Shared
component and page units add their own records as those surfaces become driven.

The approved interface is one read-only per-view D-Bus method,
`viewSettingsControlsData(u containmentId) -> s`. Corona validates that the
containment has a current view, then delegates to its runtime registry. The
registry is Corona-owned and remains alive until settings factories, layouts,
views, and their QML lifetime objects have torn down. A separate value-only
C++20 core owns the closed scalar domain, aggregate validation, deterministic
complete-identity ordering, popup-row ordering, and compact JSON.

The complete control identity is containment id, stable nonempty surface,
positive process-monotonic load generation, nullable applet id, exact nonempty
audit identity and kind, and nullable nonempty instance key. Generation
identities serialize as decimal strings. Each record snapshots live effective
visibility/enabled state, descendant keyboard focus, typed current state, and
one or more role-named hit rectangles. Global logical and ancestor/surface-
clipped geometry retain `QRectF` precision. `mapped:false` with null clipped
geometry is the fully clipped or unmapped state. Popup-capable controls add
open/mapped state, an open-generation identity, and semantic rows with stable
scalar values and the same live state/geometry fields. No labels, pointer or
window ids, setters, filters, registration, or execution enter public D-Bus.

Geometry comes from current QQuickItem visual ancestry plus an authoritative
registered global surface geometry provider, never cached registration
geometry or Wayland QWindow position. Every clipping parent and the supplied
surface bounds clip the hit. A transform or geometry that cannot truthfully be
represented as one axis-aligned rectangle refuses the complete aggregate.
Duplicate complete identities, malformed descriptors, unsupported live values,
and malformed surviving entries also warn and return `[]`, never a plausible
subset.

Generation allocation is registry-only. Replacing or retargeting a scope
retires its old generation before allocating a strictly newer one. Popup
close/reopen also advances its generation. Lifetime-object destruction removes
the generation synchronously; control or row destruction removes only that
entry. Retained QObject/QQuickItem references use QPointer, and destroyed
handlers capture numeric tokens rather than dereferencing dying objects. The
fixture QRC/QML remains outside SC-F2 production roots. Runtime-effect readbacks
and every production control registration remain later one-surface units.

The interface is read-only. Pointer and keyboard input drive controls at
reported geometry. No D-Bus setter may bypass a control. XML, serializer tests,
the observability design, the interface reference, and a usage recipe land in
the same PR.

Runtime-effect readbacks are not bundled into SC-O1. Each missing oracle found
by the ledger receives its own one-surface PR and plan item before the dependent
page or matrix unit starts.

### 5.3 Test layers

- Sanitized C++20 pure cores follow the step-2.5 law in
  `docs/reference/TESTING.md`.
- Component tests instantiate production control types and assert signals,
  popup state, rows, disabled behavior, focus, and cleanup.
- Page tests load one real page with production-shaped context and drive controls
  without screen-coordinate guesses.
- Nested tests use `scripts/run-e2e.sh`, fakepointer, keyboard injection, D-Bus,
  and compositor state. Pixels are reserved for visual effects or
  state-versus-render disagreement.
- Every runtime claim names a fixture, operation, requested state, independent
  effect, negative control, and permanent test ID before implementation.
- Every defect fix is reproduced red before the fix and green after it. Temporary
  probes and reverts remain uncommitted.

## 6. Evidence gates and defect dispositions

### D29 (task-icon middle click appears to execute left-click behavior)

Status is ACCEPTED as Qt5-faithful behavior and a configuration-scope
misunderstanding, not a Qt6 defect. At `origin/main` commit `5c2223a3e`, the
default `middleClickAction=2` means `NewInstance`. A physical middle click on a
pure Dolphin launcher reached `TaskMouseArea` as `Qt.MiddleButton`. The launcher
exception deliberately ignored `middleClickAction`, called `activateTask()`,
then `activateLauncher()`, and reached `TasksModel.requestActivate`. Independent
KWin and model state changed from zero to one Dolphin window, and that row became
the active window.

The same physical click on the resulting single-window row then used the
non-launcher dispatch, selected `newInstance`, and reached
`TasksModel.requestNewInstance`. Independent state changed from one to two
Dolphin windows and the grouped row reported `childCount=2`. The complete
sequence was reproduced twice. Qt5 and both reference forks retain the launcher
exception, so no fix or divergence is selected. Temporary instrumentation was
removed.

Existing D-Bus state proved the independent effects but could not distinguish
`requestActivate` from `requestNewInstance`. PR #99 landed SC-T3 (the D29 narrow
dispatch readback), which exposes only the latest middle-click row kind and
identity, configured action, dispatched operation, and monotonic sequence.
PR #101 landed SC-T5 (the D29 permanent runtime-effect acceptance) at
`382268a92`. Its permanent nested recipe pins exact-once dispatch for the pure
launcher and resulting task row, the zero-to-one active-window and one-to-two
grouped-child effects, and an action-None negative control.
SC-T4 (the D29 root fix) is not applicable. No action enum, persisted schema,
target/action matrix, group-policy change, or action-surface expansion is
approved.

### D30 (Behavior mouse actions expose fixed booleans instead of full choices)

SC-B1 confirms an inherited boolean contract, not a popup regression.
`dragActiveWindowEnabled` owns left drag or hold-to-move and left double-click
maximize/restore. `closeActiveWindowEnabled` owns middle-click close. Left
single-click is a no-op. Both booleans default to false, and `scrollAction`
defaults to 0 (`ScrollNone`). Values 0 through 4 retain their current none,
desktop, activity, task, and minimize-toggle behavior.

Nested evidence covered enabled, disabled, and no-target states; move, maximize,
and close; desktop and task wheel paths; activity refusal; and target history.
Qt5 and both reference forks retain the same two booleans and gesture ownership.
The evidence favors retain-and-clarify, but SC-B2 (the D30 product decision and
sign-off gate) remains pending and no product decision or action expansion is
approved.

SC-B1 also found D58 (close-only and minimize-toggle settings do not enable
window tracking). PR #94 fixed it under SC-WT1 (the D58 tracker-enablement root
fix and regression): `BindingsExternal.qml` now enables the active-window
tracker for `dragActiveWindowEnabled`, `closeActiveWindowEnabled`, and
`ScrollToggleMinimized`. Root commit `15f026887` added the two missing requesters;
`14da9e7ce` and `0a796e1ec` hardened the regression and complete requester guard.
Separate findings remain for Wayland close without an `isCloseable()` capability
check, minimize without an `isMinimizeable()` check, and void operation APIs that
cannot report typed refusal. Those seams require later decision units and are not
bundled into D58 or SC-B2.

### D56 (pure-launcher task wheel uses inherited asymmetric activation)

Status is ACCEPTED as Qt5-faithful behavior. A disposable nested run at
`origin/main` commit `6765b2320` proved that a pure launcher receives wheel
input directly in `TaskMouseArea`. A positive step calls
`TaskItem.activateLauncher()`, then `TasksModel.requestActivate`; a negative step
does nothing for `ScrollTasks` and `ScrollToggleMinimized`. `ScrollNone` refuses
the handler unless manual scrolling is enabled. With manual scrolling enabled
and no overflow to consume the step, the same positive launcher activation
occurs. Production does not call `TaskActions.scrollCommandFor` on this path.

`git blame` traces the handler and positive launcher call to Qt5 commits
`2d6b482d5f` and `e642087e31`; both reference forks retain the behavior. This is
not D29 and not a Qt6 routing regression. SC-W1 adds a permanent regression test
for the observed positive, negative, `ScrollNone`, manual-scroll, and no-overflow
branches without changing behavior.

### D57 (ConfigOverlay wheel threshold accepts nonnegative decrease deltas)

Status is OPEN. PR #96 landed SC-CW1 (the D57 ConfigOverlay wheel-threshold
reproduction) at `5ec57175f`, `aa6399b44`, `709c0946b`, and `9b0672cf9`.
`ConfigOverlay.qml` divides `wheel.angleDelta.y` by 8 but decreases for
`angle < 12` instead of `angle < -12`. Both view axes produced +120:+8,
-120:-8, +96:0, -96:-8, +90:-8, -90:-8, and horizontal +/-120:-8. Explicit
`axisstop` sends a zero fake-input axis; KWin forwards it as
`wl_pointer.axis_stop`, and Qt emits no `QWheelEvent` in this isolated sequence.
The following +120 still increases length. Only the complete cleaned-up matrix
exits 57; corrected behavior exits 0 as XPASS, and partial or harness failures
fail. SC-CW2 (the D57 signed decrease-threshold fix and regression promotion)
remains unchecked, approval-required, and unapproved.

### D58 (close-only and minimize-toggle settings do not enable window tracking)

Status is FIXED by PR #94. Root commit `15f026887` adds only the missing
`closeActiveWindowEnabled` and `scrollAction === ScrollToggleMinimized`
dependencies to the active-window tracker requester set. Regression hardening
at `14da9e7ce` rejects input, query, cleanup, restoration, and residue failures;
the complete requester-expression guard at `0a796e1ec` preserves every existing
requester arm. Capability checks and typed refusal remain separate findings.

### Existing defect boundaries

- D24 (TypeSelection Dock/Panel presets write two dead keys) remains OPEN on
  current main. SC-M1 owns only removal of the four schema-absent writes and the
  focused source/config guard. It is not part of D30 or any Behavior action
  expansion.
- D31 (valid Justify splitter moves reset after restart) is FIXED by PR #73 and
  is outside this plan. Existing persistence evidence may be reused, but no
  settings-completion unit owns or reopens it.

## 7. Per-view PR units

Every checklist item below is one PR unless it is explicitly an evidence or
sign-off gate. Each implementation PR has one surface or root cause, one focused
red control where a defect is fixed, and its own `Commits:` trace.

### Foundation and drivers

- [x] **SC-F1 (the per-view source inventory and evidence ledger):** inventory
      all concrete affordances, assign stable audit identities, and add C1-C9
      ledger rows and explicit exemptions. This is independent of runtime
      registration. Dependencies: none. Commits: 472711d11
- [x] **SC-F2 (the source-to-ledger coverage gate):** fail on an interactive
      declaration or handler without one ledger identity or exemption, and on a
      ledger source that no longer exists. PR #103 landed the syntax scanner,
      schema-2 inventory enforcement, documented coverage contract, and final
      review-state record. Dependencies: SC-F1. Commits: 7eda16ca0,
      6089fdea9, 14ecdf9dd, 31f9bb356
- [ ] **SC-O1 (the read-only settings-control D-Bus registry):** expose only
      inventory-required identity, instance, generation, geometry, state, and
      popup-row data through a fixture vertical slice; document and test
      destruction cleanup. Production registrations land with their component
      or page units. Dependencies: SC-F1 and SC-F2. Commits:
- [ ] **SC-D1 (the pointer and keyboard control drivers):** locate one registry
      record and operate buttons, rows, sliders, and fields through real input.
      Negative controls must catch disabled input, wrong geometry, a popup that
      did not open, and a no-op write. Dependencies: SC-F2 and SC-O1. Commits:
- [ ] **SC-D2 (the popup and lifecycle driver helpers):** wait for mapped popup
      generations, select by stable row value, abort through Escape/focus loss,
      and reject stale or clipped records. Dependencies: SC-F2 and SC-D1.
      Commits:
- [ ] **SC-O2 (the indicator configuration readback):** expose the selected
      bundled indicator's existing `[Indicator][<pluginId>]` values read-only,
      with applet and load-generation identity. Dependencies: SC-F1 and SC-F2.
      Commits:

### Shared component families

- [ ] **SC-C1 (the ComboBox and ComboBoxButton family):** test real popup
      mapping, stable row selection, disabled and separator rows, mouse and
      keyboard paths, RTL placement, focus return, and abort cleanup.
      Dependencies: SC-F2 and SC-D2. Commits:
- [ ] **SC-C2 (the slider and numeric-field family):** test pointer drag,
      keyboard steps, wheel, boundaries, fine-value fields, external reflection,
      and hold-repeat cleanup. Dependencies: SC-F2 and SC-D1. Commits:
- [ ] **SC-C3 (the text-entry family):** test editing, validation, commit,
      rejection, Escape, focus loss, and binding reflection. Dependencies:
      SC-F2 and SC-D1. Commits:
- [ ] **SC-C4 (the checkable and grouped-button family):** test exclusive and
      nonexclusive state, disabled activation, keyboard paths, exactly-once
      signals, and external reflection. Dependencies: SC-F2 and SC-D1. Commits:
- [ ] **SC-C5 (the color-control and dialog family):** test value selection,
      accept, reject, invalid input, focus return, and destruction cleanup.
      Dependencies: SC-F2 and SC-D2. Commits:

### Existing per-view pages and surfaces

- [ ] **SC-P1 (the Behavior page excluding D30 action expansion):** drive the
      existing location, visibility, environment, timer, filter, wheel, and
      dependent-state controls. The two fixed action booleans are recorded as
      current behavior until SC-B2. Dependencies: SC-F2 and SC-C1 through
      SC-C4. Commits:
- [ ] **SC-P2 (the Appearance page):** drive all appearance controls, including
      control 56 (`maximizeWhenMaximized`), with geometry/config readbacks and
      narrow visual checks only where rendering is the effect. Dependencies:
      SC-F2, SC-C1, SC-C2, SC-C4, and any ledger-opened oracle. Commits:
- [ ] **SC-P3 (the Effects page):** drive shadows, animations, colors, and
      indicator host selection without entering plugin-owned internals.
      Dependencies: SC-F2, SC-C1, SC-C4, and SC-C5. Commits:
- [ ] **SC-P4 (the Tasks page controls):** open and select every existing real
      control and verify config reflection. Direct config seeding is setup only.
      D29 production semantics are excluded. Dependencies: SC-F2 and SC-C1
      through SC-C4. Commits:
- [ ] **SC-R1 (the Tasks filtering runtime effects):** drive every inventoried
      task filter and assert live task-row membership through an independent
      model readback, including an unchanged-membership negative control.
      Dependencies: SC-F2, SC-P4, and any ledger-opened oracle. Commits:
- [ ] **SC-R2 (the Tasks grouping runtime effects):** change each grouping
      setting and assert the live parent/child topology rather than the stored
      value. Dependencies: SC-F2, SC-P4, and the grouped-task readback. Commits:
- [ ] **SC-R3 (the Tasks launcher runtime effects):** drive launcher scope,
      visibility, and ordering settings and assert the resulting live launcher
      rows. Dependencies: SC-F2, SC-P4, and the launcher readback. Commits:
- [ ] **SC-R4 (the Tasks badge runtime effects):** drive each badge setting and
      assert independent badge data or a narrowly scoped rendered effect.
      Dependencies: SC-F2, SC-P4, and any badge oracle opened by SC-F1. Commits:
- [ ] **SC-R5 (the Tasks animation runtime effects):** drive each animation
      setting and assert the runtime animation state or timing outcome, not the
      config value. Dependencies: SC-F2, SC-P4, and the animation oracle.
      Commits:
- [ ] **SC-R6 (the Tasks scrolling runtime effects):** drive manual overflow and
      task-action scrolling choices and assert list position or independent task
      state. Dependencies: SC-F2, SC-P4, SC-W1, and the scrolling oracle.
      Commits:
- [ ] **SC-R7 (the Tasks launcher-ownership interaction effects):** drive
      `isPreferredForDroppedLaunchers` and `isPreferredForPositionShortcuts` and
      assert the independent drop and shortcut routing targets. Dependencies:
      SC-F2, SC-P4, and the routing readbacks. Commits:
- [ ] **SC-R8 (the Tasks window-interaction effects):** drive
      `showWindowActions` and `previewWindowAsPopup` and assert the real menu
      actions and popup-preview surface behavior. Dependencies: SC-F2, SC-P4,
      and the menu/preview readbacks. Commits:
- [ ] **SC-R9 (the Tasks left-click action effects):** drive every configured
      left-click choice and assert its independent model, window, or compositor
      effect. Dependencies: SC-F2, SC-P4, and action-specific oracles. Commits:
- [ ] **SC-R10 (the Tasks hover action effects):** drive every hover choice and
      assert preview mapping and highlight targets through independent readbacks.
      Dependencies: SC-F2, SC-P4, and the preview/highlight readbacks. Commits:
- [ ] **SC-R11 (the Tasks modifier-button action effects):** drive every offered
      modifier and button combination and assert its action-specific independent
      effect. Dependencies: SC-F2, SC-P4, and action-specific oracles. Commits:

SC-P4 config reflection never closes C4. SC-T5 now provides its independent
runtime-effect evidence, while all SC-R1 through SC-R11 units must still provide
theirs before the Tasks page or per-view phase can close. Middle click remains
in the D29 chain; wheel remains in SC-R6.

- [ ] **SC-P5 (the third-party indicator host contract):** prove load,
      replacement, malformed-page refusal, and cleanup against a fixture plugin,
      without claiming arbitrary plugin internals. Dependencies: SC-F2, SC-O2,
      and SC-D2. Commits:
- [ ] **SC-P6 (the default indicator configuration page):** drive every
      inventoried control in `indicators/default`. Dependencies: SC-F2, SC-O2,
      SC-C1 through SC-C5, and SC-P5. Commits:
- [ ] **SC-P7 (the Plasma indicator configuration page):** drive every
      inventoried control in `indicators/org.kde.latte.plasma`. Dependencies:
      SC-F2, SC-O2, SC-C1 through SC-C5, and SC-P5. Commits:
- [ ] **SC-P8 (the tab-style indicator configuration page):** drive every
      inventoried control in `indicators/org.kde.latte.plasmatabstyle`.
      Dependencies: SC-F2, SC-O2, SC-C1 through SC-C5, and SC-P5. Commits:
- [ ] **SC-P9 (the editor chrome and canvas):** drive ruler, edit-background
      wheel, rearrange, stick edges, pin, mode switch, tabs, drag resize,
      double-click reset, and close. Dependencies: SC-F2, SC-D2, and applicable
      shared component units. Commits:
- [ ] **SC-P10 (the Dock/Panel chooser):** drive both presets, exact intended
      coupled writes, disabled state, reflection, and close cleanup. Dependencies:
      SC-D1 and SC-F2. Commits:
- [ ] **SC-P11 (the ConfigOverlay surface):** drive configure, colorize,
      lock-zoom, remove, popup, and abort paths for one and repeated applets.
      Dependencies: SC-F2 and SC-D2. Commits:
- [ ] **SC-A1 (the Add view action):** create one disposable view, prove its
      defaults and persistence, and clean it up. Dependencies: SC-P9. Commits:
- [ ] **SC-A2 (the Duplicate view action):** duplicate one disposable view and
      prove independent identity and config. Dependencies: SC-P9. Commits:
- [ ] **SC-A3 (the Remove view action):** prove confirm, cancel, delayed removal,
      restart, and no stale settings surfaces. Dependencies: SC-P9. Commits:
- [ ] **SC-A4 (the edit-mode Close action):** close the settings ensemble from
      each active sub-surface and prove lifecycle cleanup. Dependencies: SC-P9,
      SC-P11. Commits:
- [ ] **SC-A5 (the move-current-view-to-another-layout action):** drive the view
      context menu, select one other layout by stable identity, prove that the
      current view moves without switching the global layout, survives restart,
      excludes the current layout, and leaves no move or popup residue after an
      abort. Dependencies: SC-F2, SC-D2, and a disposable multi-layout fixture.
      Approval required; not approved. Commits:
- [ ] **SC-M1 (the D24 dead TypeSelection write cleanup):** remove only the
      `solidPanel` and `colorizeTransparentPanels` writes and replace the audit's
      expected-dead-write assertions with absence guards. Dependencies: SC-P10
      evidence. Commits:

### Evidence-gated task, empty-area, and focused defect work

- [x] **SC-T1 (the D29 middle-click evidence capture):** captured the pure
      Dolphin launcher and resulting single-window row with the stored action,
      event recipient, model requests, and independent effects. The sequence was
      reproduced twice and temporary instrumentation was removed. Dependencies:
      existing fakepointer, `appletConfigData`, and `viewTasksData`; no new
      settings scaffold. Commits: 327e2e9af
- [x] **SC-T2 (the D29 disposition and sign-off gate):** Qt5 and both forks
      retain the launcher exception. D29 is accepted as Qt5-faithful behavior
      and a configuration-scope misunderstanding, with no divergence or fix.
      Dependencies: SC-T1. Commits: 327e2e9af
- [x] **SC-T3 (the D29 narrow middle-click dispatch readback):** expose only the
      latest middle-click row identity and kind, configured action, dispatched
      operation, and monotonic sequence needed to distinguish launcher
      activation from new-instance dispatch. No setter, history, or action
      expansion. Dependencies: SC-T2. PR #99 landed this approved unit.
      Commits: e87deef58, bfd30f235, e190d03b0, 4dd51fdcd, cd959cb3a
- [x] **SC-T4 (the D29 root fix, if proven):** not applicable because SC-T2
      established Qt5-faithful behavior with no defect or selected divergence.
      Dependencies: SC-T2. Commits: N/A (accepted behavior requires no fix)
- [x] **SC-T5 (the D29 permanent runtime-effect acceptance):** always drive the
      pure launcher and resulting single-window row, assert the SC-T3 dispatch
      plus the zero-to-one active-window and one-to-two grouped-child effects,
      and include a negative control. Dependencies: SC-T2 and completed SC-T3;
      SC-T4 is not applicable. PR #101 landed the approved unit without changing
      product behavior or the D-Bus surface. Commits: 382268a92
- [x] **SC-W1 (the D56 launcher-wheel regression guard):** pin inherited pure
      launcher positive activation, negative no-op, `ScrollNone` refusal,
      manual-scroll enablement, and no-overflow behavior. Dependencies: existing
      task fixture and wheel driver only. Commits: d2fa8bbd1, 3b6930851,
      c61ce8502
- [x] **SC-CW1 (the D57 ConfigOverlay wheel-threshold reproduction):** drive a
      Latte-style applet through positive, negative, horizontal, boundary, and
      sub-threshold deltas plus an axis-stop control, recording independent
      applet-length effects. PR #95 supplied the generic prerequisites at
      `57bc03ce0`, `7f747f944`, `fb3466223`, `ce424574a`, `cd6d317b2`, and
      `240476b9c`. Reproduction only; no production behavior changed.
      Commits: 5ec57175f, aa6399b44, 709c0946b, 9b0672cf9
- [ ] **SC-CW2 (the D57 signed decrease-threshold fix and regression
      promotion):** SC-CW1 is merged, but explicit approval must still be
      recorded before changing the decrease comparison from `angle < 12` to
      `angle < -12`, removing the expected-failure marker, and retaining the
      both-axis matrix as a status-0 regression. Approval required; not approved.
      Commits:
- [x] **SC-B1 (the D30 current-contract investigation):** confirmed current
      gestures, event ownership, booleans/defaults, target lifecycle, requests,
      effects, and Qt5/fork parity across enabled, disabled, and no-target nested
      cases. No production behavior changed. Dependencies: existing fakepointer
      and window/view readbacks; no new settings scaffold.
      Commits: 327e2e9af
- [x] **SC-WT1 (the D58 tracker-enablement root fix and regression):** add only
      `closeActiveWindowEnabled` and `ScrollToggleMinimized` to the active-window
      tracker enablement expression, then regress close-only and minimize-toggle
      enabled, disabled, and no-target effects. Capability checks and typed
      refusal are excluded. Dependencies: SC-B1 and existing tracker/window
      readbacks. Commits: 15f026887, 91cfb2bac, 14da9e7ce, 0a796e1ec,
      8c6b1c826
- [ ] **SC-B2 (the D30 product decision and sign-off gate):** select retain and
      clarify, or a bounded action-choice divergence. Record exact gestures and
      choices only here after SC-B1. Evidence favors retain-and-clarify, but the
      explicit product decision remains pending. Dependencies: SC-B1. Commits:
- [ ] **SC-B3 (the D30 typed action decision core):** implement only the
      approved gesture/action decision table as a sanitized C++20 core. No
      schema, QML, or window protocol changes. Dependencies: SC-B2. Not
      approved. Commits:
- [ ] **SC-B4 (the D30 gesture arbitration core):** if SC-B2 requires competing
      click, double-click, drag, or hold paths, pin their ordering and abort
      behavior in a separate core. Remove this item if no arbitration change is
      approved. Dependencies: SC-B2. Not approved. Commits:
- [ ] **SC-B5 (the D30 window-target retention change, if required):** change
      only the proven `LastActiveWindow` selection/retention rule and test its
      replacement, invalidation, and scope exits. Dependencies: SC-B2. Not
      approved. Commits:
- [ ] **SC-B6 (the first D30 window-operation API family, if required):** add one
      approved operation and typed boundary result through the Wayland wrapper,
      with independent state evidence. Additional operation families receive
      new checklist items and PRs before farming. Dependencies: SC-B2. Not
      approved. Commits:
- [ ] **SC-B7 (the D30 schema migration):** add the chosen persisted model and
      one idempotent migration from the two shipped booleans. Migration tests
      cover every legacy combination and reopen. Dependencies: SC-B2 and SC-B3.
      Not approved. Commits:
- [ ] **SC-B8 (the D30 production runtime-dispatch integration):** wire the
      approved typed decision and persisted value into the production gesture
      executor without adding controls, schema, or protocol operations. A
      retain-and-clarify decision instead pins the existing boolean dispatch
      through the same production integration test. Dependencies: SC-B2 and
      SC-B3 through SC-B7 as retained. Not approved. Commits:
- [ ] **SC-B9 (the D30 Behavior UI):** replace or clarify only the two action
      controls selected by SC-B2, using explicit values rather than combo index
      assumptions. This unit owns UI only. Dependencies: SC-F2 and SC-B8. Not
      approved. Commits:
- [ ] **SC-B10 (the D30 action observability):** add a read-only record for the
      configured gesture, target kind, request or refusal, and sequence only if
      independent effects cannot identify dispatch. Dependencies: SC-B8. Not
      approved. Commits:
- [ ] **SC-B11 (the D30 left-drag nested matrix):** drive the approved left-drag
      choices against valid, invalid, and capability-refusing targets.
      Dependencies: SC-B8, SC-B9, SC-B10 as applicable, and SC-B4 through SC-B7
      as retained. Not approved. Commits:
- [ ] **SC-B12 (the D30 left-double-click nested matrix):** drive only the
      approved double-click choices and prove no single-click or drag double
      execution. Dependencies: SC-B8, SC-B9, SC-B10 as applicable, and SC-B4
      through SC-B7 as retained. Not approved. Commits:
- [ ] **SC-B13 (the D30 middle-click nested matrix):** drive only the approved
      middle-click choices and prove containment fall-through or consumption as
      selected. Dependencies: SC-B8, SC-B9, SC-B10 as applicable, and SC-B3
      through SC-B7 as retained. Not approved. Commits:

### Per-view matrix closure

SC-M1 is mandatory for every closure unit because schema-absent TypeSelection
writes leave C3 open.

- [ ] **SC-X1 (the mode and geometry e2e matrix):** cover basic/advanced,
      dock/panel, both axes, applicable edges, and alignments for novel ledger
      branches. Dependencies: all SC-P page units, SC-R1 through SC-R11, SC-M1,
      SC-T2, SC-T3, and SC-T5. Commits:
- [ ] **SC-X2 (the settings persistence e2e matrix):** close/reopen and restart
      persistent controls while proving transient actions leave no residue.
      Dependencies: all SC-P and SC-A units, SC-R1 through SC-R11, SC-M1,
      SC-T2, SC-T3, and SC-T5. Commits:
- [ ] **SC-X3 (the settings abort and lifecycle e2e matrix):** abort active
      popup, drag, field edit, dialog, page switch, and edit exit without stale
      surfaces or registry generations. Dependencies: SC-D2, all SC-P units,
      SC-R1 through SC-R11, SC-M1, SC-T2, SC-T3, and SC-T5.
      Commits:
- [ ] **SC-X4 (the settings keyboard and accessibility e2e matrix):** complete
      focus order, roles, names, values, actions, activation, popup focus return,
      and rendered focus checks. Record only announcement quality in
      `docs/reference/live-only.md`. Dependencies: all SC-P units, SC-R1 through
      SC-R11, SC-M1, SC-T2, SC-T3, and SC-T5. Commits:

## 8. Global Qt Widgets PR units

Global implementation starts only after SC-X1 through SC-X4 close, SC-R1 through
SC-R11 and SC-M1 land, and SC-T2, SC-T3, and SC-T5 land. GS-F1 may refine the
units below before any code is farmed.
Every schema migration or new runtime core found by GS-F1 receives a separate
`GS-M<n>` or `GS-R<n>` item, with a plain-English gloss, before implementation.

### Global scaffold and surfaces

- [ ] **GS-F1 (the global Qt Widgets source inventory):** inventory every
      widget, delegate, action, dialog result, validation branch, and persistence
      path in `app/settings/`; add independent C1-C9 ledger rows. Dependencies:
      SC-X1 through SC-X4, SC-R1 through SC-R11, SC-M1, SC-T2, SC-T3, and
      SC-T5. Commits:
- [ ] **GS-O1 (the read-only Qt Widgets control registry):** expose only the
      inventory-required object identity, dialog generation, geometry, state,
      model rows, selection, and enabled data. Dependencies: GS-F1. Commits:
- [ ] **GS-D1 (the Qt Widgets input drivers):** drive buttons, tabs, item views,
      delegates, combo rows, fields, and keyboard focus by registry identity.
      Dependencies: GS-O1. Commits:
- [ ] **GS-D2 (the mixed QML and Qt Widgets lifecycle drivers):** correlate
      global dialogs with affected views and detect stale modal, popup, or focus
      state after accept, reject, and close. Dependencies: GS-D1. Commits:
- [ ] **GS-P1 (the Settings dialog shell):** drive tabs, apply, cancel, reset,
      defaults, dirty state, close, and window lifecycle. Dependencies: GS-D2.
      Commits:
- [ ] **GS-P2 (the Layouts table surface):** drive selection, editing,
      delegates, activities, screens, ordering, and model reflection without
      invoking lifecycle actions. Dependencies: GS-D1. Commits:
- [ ] **GS-P3 (the Preferences page):** drive every inventoried preference and
      immediate-versus-apply behavior. Dependencies: GS-P1. Commits:
- [ ] **GS-P4 (the Actions dialog):** drive available/selected lists, ordering,
      defaults, accept, cancel, and empty-set behavior. Dependencies: GS-D2.
      Commits:
- [ ] **GS-P5 (the Views dialog):** drive its table, delegates, menus,
      validation, accept, cancel, and selection lifecycle. Dependencies: GS-D2.
      Commits:
- [ ] **GS-P6 (the Details dialog):** drive names, schemes, colors, pattern
      controls, validation, accept, and cancel. Dependencies: GS-D2. Commits:
- [ ] **GS-P7 (the Screens dialog):** drive screen assignments, delegate state,
      validation, accept, cancel, and unavailable-screen handling. Dependencies:
      GS-D2. Commits:
- [ ] **GS-P8 (the Export Template dialog):** drive selection, destination,
      overwrite, validation, refusal, accept, and cancel. Dependencies: GS-D2.
      Commits:

### Global runtime actions

- [ ] **GS-L1 (the Add layout action):** create one layout from the supported
      source path and prove model and storage identity. Dependencies: GS-P2.
      Commits:
- [ ] **GS-L2 (the Copy layout action):** copy one layout and prove independent
      identity and content. Dependencies: GS-P2. Commits:
- [ ] **GS-L3 (the Import layout action):** drive valid, malformed, duplicate,
      and cancellation paths. Dependencies: GS-P2. Commits:
- [ ] **GS-L4 (the Export layout action):** drive destination, overwrite,
      refusal, cancellation, and output validation. Dependencies: GS-P2.
      Commits:
- [ ] **GS-L5 (the Remove layout action):** prove confirmation, cancellation,
      active-layout constraints, rollback, and storage cleanup. Dependencies:
      GS-P2. Commits:
- [ ] **GS-L6 (the Switch layout action):** prove requested layout activation,
      affected views, failure refusal, and persisted selection. Dependencies:
      GS-P2 and any inventory-opened runtime readback. Commits:

### Global e2e matrices

- [ ] **GS-X1 (the global dialog lifecycle e2e matrix):** open, switch tabs,
      apply, cancel, reset, close, and reopen with no stale modal or popup.
      Dependencies: GS-P1 through GS-P8. Commits:
- [ ] **GS-X2 (the add, copy, and switch e2e matrix):** prove independent
      layout creation and live-view switching. Dependencies: GS-L1, GS-L2,
      GS-L6. Commits:
- [ ] **GS-X3 (the import and export e2e matrix):** prove valid round trips,
      malformed input, overwrite refusal, and cancellation. Dependencies: GS-L3,
      GS-L4, GS-P8. Commits:
- [ ] **GS-X4 (the destructive rollback e2e matrix):** prove remove confirm,
      abort, active-layout constraints, and restart consistency. Dependencies:
      GS-L5. Commits:
- [ ] **GS-X5 (the preferences and actions persistence e2e matrix):** prove
      immediate and apply-only settings, Actions dialog ordering, empty-set
      behavior, and restart persistence. Dependencies: GS-P3, GS-P4. Commits:
- [ ] **GS-X6 (the views, details, and screens e2e matrix):** prove each
      dialog's labeled effect against live layouts and views. Dependencies:
      GS-P5 through GS-P7 and required readbacks. Commits:
- [ ] **GS-X7 (the global restart e2e matrix):** restart after each novel
      persistent state and reject transient residue. Dependencies: GS-X2 through
      GS-X6. Commits:
- [ ] **GS-X8 (the global keyboard and accessibility e2e matrix):** close focus
      order, roles, names, values, actions, activation, and modal focus return.
      Dependencies: GS-P1 through GS-P8. Commits:

## 9. Merge and review contract

- One checklist item is one PR. If an investigation discovers multiple roots,
  new items are added before implementation rather than widening the PR.
- Dependencies are merged serially. File-disjoint page units may proceed in
  parallel only after their shared scaffold dependencies land.
- Temporary probes and experimental reverts remain uncommitted. Permanent
  readbacks are narrow, read-only production interfaces with XML, serializers,
  references, and tests.
- Every code PR receives a cold-context independent review and the applicable
  gate from `docs/prompts/orchestrator-prompt.md`.
- The plan and master checklist receive post-rebase commit hashes. Newly found
  defects are filed even when fixed in the same session.
- The ledger carries 91 checklist items and 91 corresponding `Commits:` slots.
  Both counts change together whenever an investigation splits another root or
  surface.
- No implementation agent may interpret this plan's reserved D29 or D30 units
  as approval for speculative action expansion.
