# Known defects registry

Bugs found in the port, catalogued with evidence and status, so a found bug is
never invisible to the next session. A defect earns an entry when it is FOUND,
not only when fixed - "found, understood, not yet fixed" is a valid recorded
state. Fixed defects keep their entry with the fixing commit for the record.
This complements, not replaces, the per-fix commit body and the plan checklists:
the registry is the single flat list to scan for "what is known broken".

STATUS values: OPEN (found, not fixed) / FIXED (with the commit) / ACCEPTED
(checked to be intended, Qt5-faithful behavior - not a bug) / SUSPECTED (found
by code-reading, not yet reproduced under a driver).

How a defect is found is recorded because it calibrates confidence: a live repro
outranks a sanitizer abort outranks a code-reading hypothesis.

## Open / suspected

### D1 - Aborted task-reorder does not revert (Qt5-faithful live-move model)
- STATUS: ACCEPTED (resolved from SUSPECTED 2026-07-18; confirmed live and ruled
  Qt5-faithful, not a Qt6 regression - the C-I8/P7 task-reorder driver
  acceptance).
- FOUND: 2026-07-18, adversarial abort design (PR #31).
- SYMPTOM: dragging a task across a neighbour commits the reorder immediately;
  neither Escape nor a release-back reverts it. Only a drag that never crosses a
  neighbour's midpoint is a true no-op.
- EVIDENCE (live, tests/e2e/092-task-reorder.sh in the nested vehicle): a plain
  crossed drag and an Escape-held crossed drag (fakepointer `dragkey`, the key
  injected WITH the pointer button still held) landed the IDENTICAL crossed
  order - the committed move SURVIVED Escape; and a reverse-jitter returned to
  the exact origin still left the task moved (release-back does not revert
  either). A zero-cross hold-noop, by contrast, left the order AND the launchers
  config key byte-unchanged (the true no-op). Mechanism: tasksModel.move() runs
  LIVE inside onDragMove (MouseHandler.qml:184); the drag is a real compositor
  drag (dragHelper Drag.dragType Automatic -> QDrag/wl_data_device, main.qml:831)
  so Escape DOES cancel the drag, but dragHelper.Drag.onDragFinished only resets
  z and clears dragSource (main.qml:833) - nothing reverts the model move, and
  onDrop for an internal move is LeaveUnchanged (dropclassifier.h:263).
- DISPOSITION: Qt5-faithful, so ACCEPTED (not fixed). KDE's TasksModel is a
  live-move model with no drag transaction to revert, and BOTH reference forks
  carry the identical live pattern (latte-dock-ng MouseHandler.qml:296,
  latte-dock-qt6 MouseHandler.qml:180: tasksModel.move inside onDragMove, z=100,
  ignoreItemTimer). The C-A3 abort scenario therefore asserts the ACTUAL
  contract - a zero-cross is a true no-op (order + launchers key byte-unchanged),
  a crossed drag lands the NET crossings and does not revert - which is GREEN,
  not a standing RED for a wished revert. The 200ms ignoredItem timer
  (MouseHandler.qml:50) suppresses an immediate reverse re-cross, so a
  return-to-origin nets one crossing unless the reversal re-crosses after the
  timer expires.

### D2 - ConfigOverlay applet stranded over chrome on edit-exit mid-drag
- STATUS: OPEN (reproduced live 2026-07-18 by the C-I7 escape-in-held-drag
  driver + the G2 z readback; was SUSPECTED from adversarial code-reading).
- FOUND: 2026-07-18, adversarial abort design (PR #31); CONFIRMED live 2026-07-18
  (C-I7/P6, the applet-reorder driver).
- SYMPTOM: leaving edit mode WHILE an applet is mid-drag (here: Escape reaches
  the view during a held ConfigOverlay drag and exits edit mode) leaves the
  dragged applet's delegate stranded at the lift z (900), parented to root and
  drawn OVER the edit chrome. onEditModeChanged (main.qml) rescues the dndSpacer
  but NOT the in-flight ConfigOverlay currentApplet, and the onReleased restore
  never runs because inConfigureAppletsMode goes false first (the MouseArea is
  no longer live to receive the button release).
- EVIDENCE: tests/e2e/100-applet-reorder.sh DR-6 escape observation, BOTH axes:
  after `dragkey Escape` mid-drag, viewAppletsData reports the dragged applet at
  z=900 (`STRANDED 40@z900` on a horizontal view, `52@z900` on a vertical one)
  while editMode reads false. REFINEMENT of the original hypothesis: the residue
  manifests as the z=900 STRAND with `viewAppletsOrder` PRESERVED (order
  unchanged), not as a drop from appletOrder - the drop-from-order path needs
  save() to run, and this edit-exit path never calls it (onReleased does not
  fire). The G2 z field in viewAppletsData now makes the strand queryable
  (previously it would have been golden-only).
- DISPOSITION: the C-A2b marquee target (T4c). The fix rescues the ConfigOverlay
  currentApplet/placeHolder in main.qml onEditModeChanged, mirroring the dndSpacer
  rescue already there; out of scope for the C-I7 driver chunk that found it.

### D3 - Phantom ScreenConnectors entry on dropped-back cross-screen move
- STATUS: SUSPECTED (adversarial code-reading; C-A4 + the hardened residue
  detector will confirm).
- FOUND: 2026-07-18, adversarial abort design (PR #31).
- SYMPTOM: a cross-screen move dragged toward output B then dropped back on A
  can strand a phantom [ScreenConnectors] entry in lattedockrc (residue outside
  the layout file - the exact strand the C-I1 residue detector was hardened to
  catch, PR #35).
- EVIDENCE: app/screenpool.cpp:140-145 (insertScreenMapping / the connector
  group); positioner.cpp:843,890 (pending-move members).

### D4 - Maximize-length + autohide-hide sub-100ms mask over-capture race
- STATUS: OPEN (latent; found in the #24 re-review).
- FOUND: 2026-07-18, PR #24 independent re-review.
- SYMPTOM: the InputMaskFlush axis test keys off the currently-applied (possibly
  still-held) region, not the previous logical band. If a maximize-length settle
  is still pending (mask held wide) and an autohide HIDE lands within the 100ms
  window, the HIDE is misclassified as a length shrink and the full body is held
  as input mask while hidden - a re-appearance of the over-capture, confined to
  a sub-100ms race instead of every hide. Strictly better than pre-#24, exotic
  combo (maximize-length + autohide + timing).
- EVIDENCE: app/view/inputmaskflush.h:64.
- FIX DIRECTION: classify the shrink axis against the previous logical band
  while still unioning against the applied region for coverage.

### D15 - the Maximum ruler drags the Minimum (coupled-min side effect)
- STATUS: ACCEPTED (Bree 2026-07-18: KEEP the Qt5-faithful coupling - it keeps a
  fixed-length dock fixed as the ruler scrolls, easy to use). The real confusion
  was D16: the settings sliders did not update to SHOW the coupled min moving, so
  it looked broken. Fixing D16 makes the coupling legible. The CL-1 audit pins
  the coupling as intended behaviour, not a bug to remove.
- FOUND: 2026-07-18, edit-mode settings audit.
- EVIDENCE: shell/.../canvas/maxlength/RulerMouseArea.qml updateMaxLength()
  (47-63) writes minLength from the clamp result; app/settings/lengthoffsetclamp.h
  clampMaxLengthByStep (122-128) couples them when maxLength==minLength. Inherited
  Qt5 behaviour (latte-dock-qt6 carries the identical coupling).

### D16 - settings length sliders desync from the on-canvas ruler
- STATUS: FIXED (768fe8c99 re-sync + 6775d0850 cross-view guard; PR #43/CL-1).
  Was the real culprit behind the D15 confusion - the settings did not SHOW the
  coupled minimum moving because the handle binding was clobbered.
- FOUND: 2026-07-18, edit-mode settings audit.
- SYMPTOM: after a settings-window Max/Min slider is dragged once, changing the
  same length from the on-canvas ruler no longer moves the slider handle - the
  two views disagree.
- EVIDENCE: both config views share ONE config map (subconfigview.cpp:228, so not
  view isolation). ROOT: the declarative `value: plasmoid.configuration.maxLength`
  binding (AppearanceConfig.qml:264 / minLength :359) is CLOBBERED by the first
  imperative `value =` assignment (a drag) and never re-established. FIX: the
  offset slider's proxy-property + Binding{} re-sync pattern in the same file
  (:458-474). A QML regression test (drag, then external config change, assert the
  handle followed).

### D17 - the Maximum clamp floors by minLength even for Justify (alignment-blind)
- STATUS: FIXED (5baab3621; PR #43/CL-1). Landed via a distinct `Alignment::
  Justify` enum in the clamp core (both clamp functions skip the minLength floor
  for Justify only; geometry stays shared via `hasCenteredGeometry`). DELIBERATE
  QT5 DEVIATION, recorded at the site + commit + test: Qt5 floors Maximum by
  minLength for every alignment AND disables the Minimum slider for Justify, so
  the stranding is an upstream defect this port fixes (Justify effective min = 0).
- FOUND: 2026-07-18, edit-mode settings audit.
- SYMPTOM: on a Justify dock the Minimum slider is correctly disabled, but the
  Maximum cannot be lowered below the frozen stored minLength (stuck at an
  un-editable floor).
- EVIDENCE: AppearanceConfig.qml:347 disables Min for Justify, but
  lengthoffsetclamp.h floors maxLength by minLength unconditionally
  (clampMaxLengthByStep:130, clampMaxLengthToValue:154). The core Alignment enum
  is two-valued {Edge, Centered} (:48-51) and folds Justify into Centered (:43-47),
  so it cannot tell Justify apart. FIX: extend the core to carry Justify distinctly
  (third enum value or a minimumApplies flag) and skip the minLength floor for
  Justify. Touches core + bridge + both slider handlers.

### D18 - Widget-explorer drag flickers the containment enter/leave
- STATUS: OPEN (found live 2026-07-18 driving the C-I9/P8 explorer DnD, PR for
  C-I9). Correctness is NOT affected - the drop still adds exactly one and an
  abort adds zero - so it did not block C-I9; filed so the jitter is not lost.
- SYMPTOM: during a real explorer->containment drag, the containment DropArea
  receives a rapid onDragEnter / onDragLeave / onDragLeave cycle repeating on
  every motion step, and NO onDragMove ever fires. The dndSpacer therefore
  toggles live/parked many times per second instead of tracking the pointer
  smoothly; viewDropMarkerIndex flickers between the insert index and -1.
- EVIDENCE: temporary console.warn in DragDropArea.qml onDragEnter/Move/Leave/Drop
  during the C-I9 feasibility probe logged, per drag: [enter, leave, leave] x ~16,
  then a final enter + onDrop; onDragMove never logged. The drop coordinates were
  correct (window-local 800,340 = the aimed screen 800,956) and one applet was
  added.
- SUSPECTED ROOT: onDragEnter calls animations.needLength.addEvent(dragArea),
  which grows the view to make room for the spacer; the relayout momentarily
  moves the surface/item out from under the pointer, KWin sends a dnd leave, the
  view shrinks, re-enter - a resize/hit-test feedback loop. onDragLeave reparents
  the spacer to the containment each time, so the spacer never settles.
- DISPOSITION PENDING: is this Qt5-faithful (Qt5 draganddrop may debounce
  enter/leave differently) or a Qt6 regression in the needLength-on-enter grow?
  Qt5 Latte is the spec - compare its drag-hover behaviour before deciding FIX
  vs ACCEPTED. Not in C-I9 scope (the driver's job is to drive and observe the
  drop, which it does correctly); a future F2-add investigation owns it.

### D19 - About dialog keep-above is a silent X11-shaped no-op on wayland
- STATUS: OPEN (found 2026-07-18 by the X11 survivor-sweep code-read; the X11
  removal wave missed it because it is not textually an isPlatformX11 branch -
  it is an X11-shaped call the wayland interface silently drops).
- SYMPTOM: the About dialog is not actually raised keep-above on wayland. It may
  appear under other always-on-top windows instead of above them.
- EVIDENCE (code-read, lattecorona.cpp:882 + waylandinterface.cpp setKeepAbove):
  aboutApplication() calls
  `m_wm->setKeepAbove(WindowId::fromX11WId(aboutDialog->winId()), true)`
  unconditionally. `aboutDialog->winId()` is a Qt WId; fromX11WId wraps its
  decimal string. On wayland WaylandInterface::setKeepAbove does windowFor(wid),
  which resolves a WindowId against PlasmaWindow::uuid() values - a Qt WId
  decimal string can never equal a PlasmaWindow uuid, so windowFor() returns
  null and requestToggleKeepAbove() is never reached. Pairs with the skipTaskBar
  STUB one line above (waylandinterface.cpp:297), already a no-op with a Phase-4
  surface-management note.
- DISPOSITION PENDING: the intent (keep the About dialog above) is legitimate,
  so this is not a delete - it is a stub-or-wire decision. Either mark it a
  `// STUB` like skipTaskBar (defer to the PlasmaShellSurface/layer-shell
  surface-management work) or request keep-above through the wayland surface
  directly. Filed as proposal D2 in docs/tracking/x11-cleanup-audit.md; not fixed this
  pass (the survivor sweep executes removals only, surfaces behaviour changes).
### D20 - Right-click menu collapses in normal mode when the always-shown key is empty
- STATUS: the collapse-on-empty is ACCEPTED (Qt5-faithful: a genuinely empty
  `contextMenuActionsAlwaysShown` must hide every gated action in normal mode);
  the hypothesised UNASKED-FOR write-path emptying is DISPROVEN (not reproduced
  under a faithful driver); the real defect, that normal mode was never
  asserted, is FIXED by `tests/e2e/110-context-menu-normal-mode.sh` (6bcf55d62).
- FOUND: 2026-07-18, D20 audit item. Reported symptom: in NORMAL (non-edit)
  mode the dock right-click menu shows only the "Latte" section header and
  "Edit Dock...", every other action (Add Widgets, the Layouts submenu,
  Configure Latte, Add Dock/Panel, Duplicate, Export, Remove) hidden. EDIT mode
  shows them all.
- MECHANISM (confirmed): `menu.cpp:288` gates each normal-mode action on
  `m_actionsAlwaysShown.contains(action) || configuring`; `m_actionsAlwaysShown`
  is `contextMenuData` index 3, the `;;`-joined
  `UniversalSettings::contextMenuActionsAlwaysShown`. When that config value is
  empty the normal-mode menu collapses to the section header + Edit; edit mode
  (`|| configuring`) masks it, which is why the port's edit-mode-only menu
  verification (PORTING_PLAN menu check) never caught it.
- EVIDENCE (nested vehicle): a default/rich config drives `contextMenuData`
  index 3 to the full set
  `_layouts;;_preferences;;_quit_latte;;_separator1;;_add_latte_widgets;;_add_view`
  (menu full); seeding `contextMenuActionsAlwaysShown=` and restarting drives
  index 3 to `''` (menu collapsed). The new guard PASSES on the rich config and
  FAILS with `always-shown set is [], expected [...]` on the emptied one, so it
  observes the live failure, not just the pass.
- WRITE-PATH HYPOTHESIS DISPROVEN: the prime suspect was that the
  Preferences -> Actions `KActionSelector` fails to round-trip its selected
  column under KF6, so opening + saving persists an empty set. A faithful
  headless reproduction of the actual `loadItems()` -> `currentAlwaysData()`
  round-trip (the exact `app/settings/actionsdialog/actionshandler.cpp` logic
  against a real KF6 `KActionSelector`) leaves the selected column populated
  with all six actions in every case: construction-only, inside a shown dialog,
  and with a shuffled stored order. `currentAlwaysData()` is never empty for a
  rich seed, and OK stays disabled on a clean open (`hasChangedData()==false`),
  so a clean open + save cannot empty the key. Both reference forks carry
  byte-identical `loadItems`/`save` (upstream-unchanged), and the actionshandler
  history in this tree is upstream. The only in-tree writer of the persisted key
  is `TabPreferences::save` <- `ActionsHandler::save` <- `currentAlwaysData`; QML
  and D-Bus never write it. The only reachable empty state is a DELIBERATE user
  removal of every action, which the Qt5-faithful constraint requires to collapse
  the menu, so there is no unasked-for write-path emptying to fix, and no
  load-side or render-side default guard was added.
- SEPARATE LATENT (SUSPECTED, not D20, not fixed here): `menu.cpp` reads
  `m_data[index]` for indices 0..6 without checking `m_data.size()`; a failed
  `contextMenuData` D-Bus call leaves `m_data` empty (cleared at `menu.cpp:221`,
  never repopulated) and every access is then out-of-bounds, a silent broken
  menu instead of a loud failure. Not triggered in the D20 scenario (the reply
  carries seven elements); worth its own item.
- FIX: `tests/e2e/110-context-menu-normal-mode.sh` asserts every view's
  normal-mode always-shown feed carries the full set, closing the verification
  gap that let the collapse be invisible.

### D29 - Task-icon middle click appears to execute left-click behavior
- STATUS: ACCEPTED (resolved from OPEN 2026-07-20; inherited Qt5 behavior and a
  configuration-scope misunderstanding, not a Qt6 defect).
- EVIDENCE: at `origin/main` commit `5c2223a3e`, the default
  `middleClickAction=2` means `NewInstance`. A physical middle click on a pure
  Dolphin launcher reached `TaskMouseArea` as `Qt.MiddleButton`. The launcher
  branch ignored `middleClickAction`, called `activateTask()`, then
  `activateLauncher()`, and reached `TasksModel.requestActivate`. Independent
  KWin and model state changed from zero to one Dolphin window, and the row
  became the active window.
- CONTROL: the same click on the resulting single-window row used the
  non-launcher dispatch, selected `newInstance`, and reached
  `TasksModel.requestNewInstance`. Independent state changed from one to two
  Dolphin windows and the grouped row reported `childCount=2`. The complete
  sequence was reproduced twice.
- HISTORY: Qt5 and both reference forks retain the launcher exception. The
  configured task action applies after a launcher has become a window row; it
  does not replace pure-launcher activation.
- DISPOSITION: preserve the behavior with no fix or divergence. Existing D-Bus
  state cannot distinguish `requestActivate` from `requestNewInstance`, so
  SC-T3 (the D29 narrow dispatch readback) remains as a small observability
  follow-up before SC-T5 (the D29 permanent runtime-effect acceptance). SC-T4
  (the D29 root fix) is not applicable. Temporary instrumentation was removed.

### D30 - Behavior mouse actions expose fixed booleans instead of full choices
- STATUS: OPEN. SC-B1 (the D30 current-contract investigation) confirmed the
  Qt5/fork-parity contract; SC-B2 (the D30 product decision and sign-off gate)
  remains pending, with no action expansion approved.
- CURRENT CONTRACT: `BehaviorConfig.qml` binds two checkable buttons to
  `dragActiveWindowEnabled` and `closeActiveWindowEnabled`, with no action model
  or popup. The first boolean owns left drag or hold-to-move and left
  double-click maximize/restore. The second owns middle-click close. Left
  single-click is a no-op. Both booleans default to false, and `scrollAction`
  defaults to 0 (`ScrollNone`). Values 0 through 4 retain the existing none,
  desktop, activity, task, and minimize-toggle behavior. Task-icon actions remain
  a separate Tasks-page surface.
- EVIDENCE: nested runs covered enabled, disabled, and no-target configurations;
  move, maximize, and close; desktop and task wheel paths; activity refusal; and
  target history. Qt5 and both reference forks retain the same boolean controls
  and gesture ownership. This is inherited behavior, not a Qt6 popup regression.
- DISPOSITION: the evidence favors retain-and-clarify, but SC-B2 (the D30
  product decision and sign-off gate) remains pending. Typed core/API work,
  protocol operation families, migration, UI, observability, and nested gesture
  matrices remain separate units if a divergence is later approved.
- FINDINGS: D58 (close-only and minimize-toggle settings do not enable window
  tracking) is the confirmed root defect found by SC-B1. Separate plan findings
  cover Wayland close without an `isCloseable()` check, minimize without an
  `isMinimizeable()` check, and void operation APIs that cannot return typed
  refusal. Those seams require later decision units and are not part of D58.

### D56 - Pure-launcher task wheel uses inherited asymmetric activation
- STATUS: ACCEPTED (Qt5-faithful behavior, not a Qt6 routing regression).
  PR #89 landed SC-W1 (the D56 launcher-wheel regression guard) at `d2fa8bbd1`,
  `3b6930851`, and `c61ce8502`; the initial disposable nested capture remains at
  `6765b2320`.
- EVIDENCE: pure launchers receive wheel input directly in
  `TaskMouseArea`. A positive step calls `TaskItem.activateLauncher()`, then
  `TasksModel.requestActivate`; a negative step does nothing for `ScrollTasks`
  and `ScrollToggleMinimized`. `ScrollNone` refuses unless manual scrolling is
  enabled. With manual scrolling enabled and no overflow, the same positive
  launch occurs. Production does not call `TaskActions.scrollCommandFor` on
  this path. The permanent nested recipe drives real `TaskMouseArea` input,
  independently observes launcher processes, active KWin windows and task rows,
  and pins the 400 ms burst limiter while keeping launcher classification stable.
- HISTORY: `git blame` traces the handler and positive launcher call to Qt5
  commits `2d6b482d5f` and `e642087e31`. Both reference forks retain it.
- DISPOSITION: preserve the behavior. SC-W1 (the D56 launcher-wheel regression
  guard) provides permanent coverage for positive, negative, `ScrollNone`, manual
  scrolling, and no-overflow branches. This finding is separate from D29
  (task-icon middle click appears to execute left-click behavior).

### D57 - ConfigOverlay wheel threshold accepts nonnegative decrease deltas
- STATUS: SUSPECTED (code-reading only; no driven reproduction).
- FOUND: 2026-07-20, SC-F1 (the per-view source inventory and evidence ledger).
- SYMPTOM: zero, horizontal, and small positive wheel deltas can decrease a
  Latte-style applet's length.
- EVIDENCE: `containment/package/contents/ui/editmode/ConfigOverlay.qml` increases
  length for `angle > 12`, then decreases for `angle < 12` instead of
  `angle < -12`. The branch shape is inherited, but the real event path and
  independent applet-length effect have not been driven.
- NEXT: SC-CW1 (the D57 ConfigOverlay wheel-threshold reproduction) is approved
  to drive positive, negative, zero, horizontal, and sub-threshold controls. It
  is an evidence unit only; no fix is approved before reproduction.

### D58 - Close-only and minimize-toggle settings do not enable window tracking
- STATUS: OPEN (confirmed by SC-B1 nested evidence 2026-07-20).
- FOUND: 2026-07-20, SC-B1 (the D30 current-contract investigation).
- SYMPTOM: close-only and `ScrollToggleMinimized` configurations report
  `tracker.enabled=false`, leaving the configured close or minimize gesture with
  no active-window target and no effect.
- ROOT CAUSE: the `BindingsExternal.qml` active-window tracker expression enables
  tracking for `dragActiveWindowEnabled`, but omits
  `closeActiveWindowEnabled` and `scrollAction === ScrollToggleMinimized`.
- EVIDENCE: the nested current-contract matrix covered enabled, disabled, and
  no-target cases and independently observed the missing effects. The same run
  proved the move/maximize/close, desktop/task wheel, activity-refusal, and
  target-history paths when tracking was enabled by another requester.
- FIX DIRECTION: SC-WT1 (the D58 tracker-enablement root fix and regression) is
  approved to add only the two missing existing-contract dependencies and
  regress close-only and minimize-toggle effects. Wayland capability checks and
  typed-refusal API work remain separate plan findings, not part of D58.

### D59 - Invalid standalone AppStream identity and stale library provider
- STATUS: FIXED. PR #91 landed the source correction at final commits
  `94f8dc1e5`, `c5adbb863`, `cb659d480`, `477cdf70a`, `7246b4222`, `5c51ef221`,
  `696d383db`, `7463152e8`, and `625b6c2c0`. PR #92 repinned every remaining
  native recipe and deleted the Gentoo and Void patches at `dbba5ea48`, with
  tracking and acceptance finalized by `ba32d824c`, `72796622b`, and
  `4eb2e3d67`.
- FOUND: 2026-07-20, source-metadata audit before the first continuation
  release.
- SYMPTOM: AppStream 1.1.3 rejects the configured metadata with
  `cid-rdns-contains-hyphen`, and package metadata describes the standalone
  `latte-dock` executable as an addon of Plasma Shell. It also advertises
  `liblatte2plugin.so` as a public library even though that plugin no longer
  exists.
- ROOT CAUSE: the inherited component kept its addon-era
  `org.kde.latte-dock.desktop` identity and `<extends>org.kde.plasmashell</extends>`.
  The desktop suffix makes `latte-dock` a non-final reverse-DNS segment, where
  AppStream forbids the hyphen. Commit `507393933` removed the complete
  `liblatte2` plugin tree in 2020, but its provider declaration remained.
- EVIDENCE: direct validation of
  `build/app/org.kde.latte-dock.appdata.xml` failed under AppStream 1.1.3 while
  ECM's `appstreamtest` passed in 0.01 seconds without
  `build/install_manifest.txt`. The branch declares `desktop-application`
  component `org.kde.latte-dock`, retains the
  `org.kde.latte-dock.desktop` launchable, removes `extends`, and provides only
  binary `latte-dock`. Upstream tag `v0.10.8` at `28f39d65d` proves that the old
  component ID shipped, so an exact `<replaces>` relationship preserves its
  software-center history while `org.kde.latte-dock` remains the only live ID.
  AppStream 1.1.3 accepts this relationship. Direct validation and the
  configured-file CTest pass, and the Nix package declares AppStream in its
  native test closure.
  The installed-package gate additionally requires package-owned metainfo and
  structurally rejects each wrong identity field, a missing migration
  relationship, malformed or additional migration content, any extension, and
  the stale library without requiring AppStream at runtime.
  Five independent native-package lanes passed at exact pre-rebase PR #92 head
  `45c0d27cb`: every fresh install carried the corrected identity and migration,
  passed package integrity and the full nested-Wayland gate, and shut down with
  status 0. GitHub rewrote the recipe implementation to final commit
  `dbba5ea48`; no lane or current recipe has a live per-distribution AppStream
  patch.
- COMPATIBILITY: no continuation package has been released, so no continuation
  alias or migration is needed. The declarative `replaces` entry covers the
  inherited upstream release history and does not preserve the invalid ID as a
  live identity. Debian and RPM snapshot recipes consume current HEAD and no
  longer carry duplicate patches. PR #92 pins Arch, Gentoo, and Void to merged
  PR #91 head `804519254`; Gentoo and Void no longer carry their old patches,
  and Arch never carried one. The final tree has no
  per-distribution AppStream patch file or live recipe reference. The Void helper
  also rewrites its staged recipe to current HEAD without a patch; the package
  control requires exactly one matching staged `_commit` assignment and checks
  the corresponding archive metadata.

## Recorded elsewhere - indexed here so the flat scan is complete

These predate the registry and are detailed in their source docs; indexed here
so "what is known broken" is one scan. Full detail migrates on next touch.

### D10 - Tasks config page renders but does not apply its settings
- STATUS: RESOLVED - DOES NOT REPRODUCE in this port (CL-5, 09b59045f). The Tasks
  config APPLIES here: `tasks.plasmoid.configuration.*` resolves through the
  plasmoid's live KConfigPropertyMap (the ng eabf7c89a config-access root cause is
  avoided) and action dispatch is single-source via TaskActions.js (the second ng
  root cause avoided). Driven proof: 30 seeded tasks-page values reflect through
  appletConfigData, and a launchersGroup Unique->Global change alters the running
  bar. This resolves config transport only; it does not prove that every real
  Tasks control opens or that every configured action executes. The settings
  surface completion plan owns those interaction and runtime checks, so this
  port still needs neither the old wire-up fix nor a hidden Tasks page.
- HISTORY: the inherited upstream half-finished feature that latte-dock-ng hid
  (9faccabda). Detail: docs/archive/ng-upstream-audit.md:323 and CLAUDE.md's
  stub-tracking cautionary tale.

### D11 - Dev-dock env leak into child Qt apps
- STATUS: OPEN (re-evaluate at Phase 11 packaging).
- QML2_IMPORT_PATH and the stage-first XDG_DATA_DIRS leak into Qt apps LAUNCHED
  FROM the dev dock, so a child app can lose its platform plugin. Distinct from
  the #23 shadow fix (that is about what the dock ITSELF loads; this is about
  child processes the dev dock spawns). Detail: docs/tracking/PORTING_PLAN.md ~1724.

### D12 - Plasma lookup-by-id can silently fail on an id mismatch
- STATUS: OPEN/CHECK. An applet whose metadata embedded id mismatches makes
  Plasma's lookup-by-id silently fail. Detail: docs/tracking/PORTING_PLAN.md ~2362.

### D13 - Dock blank after display churn
- STATUS: SUSPECTED/UNCONFIRMED (could NOT reproduce as a monitor-sleep bug).
  Detail and the known-fix pointer for genuine hotplug (the guarded
  setScreenToFollow() recreate): docs/tracking/e2e-interaction-test-plan.md
  section 7.9 "Known fix pointer: dock blank on genuine hotplug".

NOTE: deferred/STUBBED features are NOT defects and are tracked separately by the
stub discipline (`grep -rn 'STUB:'`): app/infoview.cpp:165 +
app/wm/waylandinterface.cpp:299 (Phase 4 WId), app/layouts/synchronizer.cpp:507
(Phase 8 activity-stop). This registry is the flat defect index; each entry
carries its own detail or points into the plan and the reference docs.

## Fixed (kept for the record)

### D25 - Task icons stay stale after icon-theme changes
- STATUS: FIXED (PR #76, 8423fab40; coverage ratchet 6765b2320).
- FOUND: 2026-07-19, code-reading during the ng-upstream commit audit, then
  reproduced by the focused production-QML render test.
- SYMPTOM: task-manager and tooltip preview icons kept rendering the previous
  theme's pixmaps until the dock restarted.
- ROOT CAUSE: `Kirigami.Icon` cached the raster resolved from a stable
  task-model `QIcon` QVariant. `KIconLoader` updated the underlying theme data
  without changing that source, so the icon binding was never reevaluated.
- FIX: `Environment` forwards `iconLoaderSettingsChanged` only for real
  `KIconTheme::current()` transitions. `ThemeAwareIcon` retains the original
  QVariant in `iconSource`, then synchronously clears and rebinds the inherited
  source so Kirigami rebuilds its per-item raster without an empty rendered
  interval. The primary task icon and both tooltip preview icons use that
  component.
- PRIOR ART: latte-dock-ng commit `ef2989ec2` identified the missing refresh
  path and supplied the idea. Its global QIcon theme mutation, QPixmap cache
  clearing, file watching, and deferred rebind were not carried.
- EVIDENCE: the focused test was red with the named fixture icon still rendered
  red after its `QIcon` resolved blue. The fixed path renders blue without an
  `iconSourceChanged` emission or cache-key change; a nameless pixmap-backed
  icon stays green. The full build, QML compile gate, and qmllint ratchet pass.
  The coverage ratchet reports 94 ctest entries and 31 paired unit headers.

### D31 - Valid Justify splitter moves reset after restart
- STATUS: FIXED (PR #73: functional fix 91eff7c46; source-attribution commit
  3170dd4f9).
- FOUND: 2026-07-20, valid splitter moves restored the previous zones after
  restart; reproduced against the production `LayoutManager` with real
  `KConfigPropertyMap` and `KConfigLoader` state.
- SYMPTOM: moving either Justify splitter updates the current layout, but a
  restart restores the previous splitter positions and zone distribution while
  the applet order remains unchanged.
- ROOT CAUSE: `LayoutManager::saveOptions()` inserted each updated value under
  its live `splitterPosition` or `splitterPosition2` key, then emitted
  `KConfigPropertyMap::valueChanged` through absent `m_option` entries.
  `m_option` contains only the lock and color-option mappings, so both splitter
  lookups produced an empty key. The live map changed, but the backing
  `KConfigLoader` skeleton retained the old values for reconstruction.
- FIX: route both splitter positions through one equality-guarded writer that
  inserts and publishes the same explicit key. The unrelated lock and color
  option mappings remain unchanged. This is distinct from D5 (Justify splitter
  negative-insert UB), which repairs invalid positions before insertion; D31
  persists already-valid moved positions.
- EVIDENCE: restoring the empty-key path makes `layoutmanagerparkingtest` fail
  on the first notification key. The fixed path moves seeded positions
  `1,5 -> 2,5 -> 2,4`, observes each named notification independently,
  preserves applet order `7;8;9;10`, saves through `KConfigLoader`, reconstructs
  the complete fixture, and restores start/main/end zone counts `1/1/2`. A
  healthy seeded `2,4` save emits no notifications and remains byte-identical.
  The full gate passed at pre-merge `4f505ac5b`, including the sanitized nested
  dock; GitHub rewrote that tree-identical head to `3170dd4f9`.

### D26 - VisibilityManager inNormalState binding-loop warning
- STATUS: FIXED (PR #74, 4cc94a48f).
- FOUND: reproduced in this port's log during the ng-upstream commit audit,
  archived at `docs/archive/ng-upstream-audit.md`. latte-dock-ng commit
  `73d982f0b` addresses the same warning through imperative state recomputation.
- SYMPTOM: Qt logs `Binding loop detected for inNormalState` from
  `VisibilityManager`, causing the property to re-evaluate through a synchronous
  feedback cycle.
- ROOT CAUSE: `VisibilityManager.inNormalState` is declaratively bound to the
  animation tracker counts. Its true edge synchronously called
  `AutoSize.updateIconSize()`, which selected a new icon-size target, entered
  `inAutoSizeAnimation`, changed `animations.needBothAxis`, and fed the source
  tracker while the binding was still evaluating. The declarative binding was
  not itself the defect; the synchronous AutoSize continuation closed the loop.
- FIX: keep `inNormalState` declarative and defer only the AutoSize continuation
  with `Qt.callLater(sizer.updateIconSize)`. The execution-time normal-state
  check rejects stale work, and Qt coalesces duplicate calls to the same bound
  method. No code was transplanted from latte-dock-ng.
- EVIDENCE: restoring the direct call makes the focused production-QML test fail
  four assertions covering synchronous resize, uncoalesced rapid calls, stale
  resize after a final false state, and execution before Loader teardown. The
  deferred path passes all five focused scenarios, the complete qmlinteraction
  suite passes 232 cases, the QML compile gate compiles 129 files, and
  AutoSize's 24 curated qmllint warnings drop to zero.

### D55 - String service metadata passed containment-actions category checks
- STATUS: FIXED (PR #72, 3fb92a05a).
- FOUND: 2026-07-20, final independent review of the installed-package gate.
- ROOT CAUSE: jq `index()` searches both arrays and strings, so a scalar
  `ServiceTypes` value containing `Plasma/ContainmentActions` passed the category
  filter without the required metadata schema.
- FIX: require string IDs, an array `ServiceTypes` value with exact member
  equality, and explicit string types for indicator package structure and parent
  application metadata.
- EVIDENCE: a containment-actions plugin with string `ServiceTypes` and an
  indicator plugin with array package-structure metadata are rejected; valid
  typed metadata still passes.

### D54 - Qt inspector version probes could hang or accept unrelated text
- STATUS: FIXED (PR #72, 009c406dc).
- FOUND: 2026-07-20, final independent review of the installed-package gate.
- ROOT CAUSE: `qtplugininfo --version` had no deadline and accepted any 6.x
  substring, including unrelated diagnostic text from a Qt 5 candidate.
- FIX: bound every candidate probe, require one exact qplugininfo-family version
  line, parse its numeric major, and continue after timeout or malformed output.
- EVIDENCE: misleading multiline output and a 60-second candidate are skipped;
  the next real Qt 6 inspector is selected within the fixed bound.

### D53 - Optional indicator mapping terminated the runtime gate
- STATUS: FIXED (PR #72, 98f4ff797).
- FOUND: 2026-07-20, final independent review of the installed-package gate.
- ROOT CAUSE: mapped-artifact registration returned the status of its final
  `required == 1` comparison. The optional indicator returned 1 under `set -e`,
  stopping before map audit, shutdown, and PASS.
- FIX: make every successful registration return zero explicitly and preserve
  collision failure as a separate status-2 path.
- EVIDENCE: optional registration under active `set -e` reaches its following
  assertions with the expected map entry and an unchanged required set. The
  Arch package runtime at exact pre-merge source head `3ee077529`
  (post-rebase tree-equivalent `10b4c4565`) continued through mapping audit,
  clean shutdown, and both PASS lines.

### D52 - Selected package artifacts bypassed package-namespace resolution
- STATUS: FIXED (PR #72, c329eb138).
- FOUND: 2026-07-20, cross-check against the complete extraction-root contract.
- ROOT CAUSE: nested content used package-namespace link resolution, but selected
  executables, plugins, and metadata first dereferenced links against host `/`.
  Fixed runtime mapping keys also described link names rather than resolved
  target identities.
- FIX: resolve every selected file in the package namespace, require raw and
  resolved manifest ownership, constrain targets to their artifact trees, and
  use the resolved host paths for inspection, loading, launch, and mappings.
  Isolated roots reject literal absolute ELF search paths because the host loader
  cannot reinterpret them beneath `--root`; `$ORIGIN` remains supported.
- EVIDENCE: absolute selected executable, plugin, and metadata links pass inside
  their package trees; cross-tree and unowned targets fail. Renamed mapping
  targets remain exact, isolated absolute RUNPATH fails, and live-root absolute
  RUNPATH passes.

### D51 - Recursive package links could escape to foreign providers
- STATUS: FIXED (PR #72, dabaf058b).
- FOUND: 2026-07-20, implementation review of the installed-package gate.
- ROOT CAUSE: selected artifacts were canonicalized, but nested QML, shell,
  plasmoid, and indicator content links were not inspected.
- FIX: enumerate every nested link, reject broken and development-provider
  targets, and require resolution inside the installed package boundary.
- EVIDENCE: external, source, CMake build, `_qmlstage`, Nix, QML, and Latte data
  link controls all fail.

### D50 - Emergency dock cleanup could hang after failure
- STATUS: FIXED (PR #72, 728fdf675).
- FOUND: 2026-07-20, implementation review of the installed-package gate.
- ROOT CAUSE: after the 25-second shutdown contract failed, EXIT cleanup sent
  SIGTERM again and immediately waited, so a TERM-ignoring dock blocked forever.
- FIX: give emergency cleanup its own fixed TERM grace period and escalate a
  survivor to SIGKILL before reaping. D38 (signal cleanup could lose status or
  wait forever) later made every phase terminal and bounded.
- EVIDENCE: a TERM-ignoring process reaches SIGKILL and cleanup returns within
  the fixed bounds.

### D49 - Validation preflight omitted the environment launcher
- STATUS: FIXED (PR #72, ebcda72fa).
- FOUND: 2026-07-20, second independent review of the installed-package gate.
- ROOT CAUSE: plugin loading invoked `env`, but the validation preflight did
  not require it. Missing `env` was discovered only after package traversal.
- FIX: add `env` to validation preflight, remove unused `sort` and `grep`
  requirements, and align the self-test preflight with its real commands.
- EVIDENCE: a PATH containing every other validation dependency fails
  immediately with the missing-`env` diagnostic; all 67 focused controls pass.

### D48 - Plugin inspection could silently select a Qt 5 tool
- STATUS: FIXED (PR #72, 40ad5a245). Bounded exact parsing was completed by
  D54 (Qt inspector version probes could hang or accept unrelated text) in
  009c406dc.
- FOUND: 2026-07-20, second independent review of the installed-package gate.
- ROOT CAUSE: unsuffixed `qtplugininfo` was selected before Qt 6-specific names,
  and no version check established which Qt major supplied the command.
- FIX: prefer Qt 6-specific names and locations, deduplicate candidates, and
  accept an inspector only when `--version` reports major version 6.
- EVIDENCE: a Qt 6-specific fixture wins without invoking a competing
  unsuffixed Qt 5 tool, and a Qt 5-only candidate is rejected.

### D47 - Absolute nested-content links resolved against the host filesystem
- STATUS: FIXED (PR #72, 3b025df03).
- FOUND: 2026-07-20, second independent review of the installed-package gate.
- ROOT CAUSE: recursive audits passed package-absolute symlink targets directly
  to host `realpath`, so `/usr/...` inside an isolated root meant host `/usr`.
- FIX: walk links in the package namespace, restart absolute targets beneath
  the extraction root, bound chained links, and retain package/tree containment.
- EVIDENCE: isolated and live-root absolute in-tree links pass with their
  respective namespace semantics; absolute cross-tree and relative host escapes
  fail. D52 covers selected executable, plugin, and metadata paths.

### D46 - Executable wrappers contradicted runtime provenance
- STATUS: FIXED (PR #72, 9fe8ddd1d).
- FOUND: 2026-07-20, second independent review of the installed-package gate.
- ROOT CAUSE: check-only skipped ELF validation for executable wrappers, while
  runtime required `/proc/<pid>/exe` to equal that resolved wrapper pathname.
  A wrapper could pass static validation but could not satisfy runtime identity.
- FIX: require the installed `latte-dock` CMake target to be ELF. This replaces
  the earlier wrapper-compatible claim in `3074c6adf`; wrapper support would
  require a separate owned-target contract.
- EVIDENCE: the positive fixture carries ELF and an executable shell wrapper is
  rejected during check-only before runtime provenance can diverge.

### D45 - Process-group polling confused errors and zombies with live members
- STATUS: FIXED (PR #72, 02153ed63).
- FOUND: 2026-07-20, second independent review of the installed-package gate.
- ROOT CAUSE: every nonzero `pgrep` result meant no members, while every listed
  member, including an unreaped zombie, meant the group remained live.
- FIX: treat `pgrep` status 1 as absence, propagate operational errors, inspect
  procfs states, ignore zombie/dead members, and wait only after proven absence.
- EVIDENCE: simulated `pgrep` statuses 2 and 3 return failure without `wait`; a
  real zombie held unreaped outside the group counts as successfully stopped.

### D44 - Missing validation tools could yield partial package checks
- STATUS: FIXED (PR #72, cfe736213). The command audit was completed by D49
  (validation preflight omitted the environment launcher) in ebcda72fa.
- FOUND: 2026-07-20, implementation review of the installed-package gate.
- ROOT CAUSE: artifact parsing began before every external command was checked.
  A missing producer such as `awk` could leave an empty consumer result.
- FIX: preflight validation commands before argument handling and runtime
  commands before compositor startup; require one supported FUSE unmount tool.
- EVIDENCE: removing `awk` rejects the gate before package discovery. D49 covers
  the later audit's missed `env` dependency.

### D43 - A crashed dock could satisfy the shutdown gate
- STATUS: FIXED (PR #72, 14543f43f).
- FOUND: 2026-07-20, implementation review of the installed-package gate.
- ROOT CAUSE: shutdown checked only process disappearance and discarded the
  leader's wait status, so prompt aborts and other nonzero exits passed.
- FIX: capture and require status zero after the bounded disappearance proof,
  matching the dock's signal-handler path through normal application shutdown.
- EVIDENCE: a zero-status SIGTERM handler passes; prompt SIGABRT status 134 and
  a SIGTERM handler returning status 7 both fail.

### D42 - Nested compositor cleanup could block indefinitely
- STATUS: FIXED (PR #72, 1895c6c30).
- FOUND: 2026-07-20, implementation review of the installed-package gate.
- ROOT CAUSE: dock cleanup was bounded, but nested KWin cleanup still sent
  SIGTERM and waited on the session leader without a deadline.
- FIX: stop the complete nested session group through independent bounded TERM
  and KILL phases before invoking only the shared filesystem cleanup.
- EVIDENCE: a real TERM-ignoring session group requires SIGKILL; a simulated
  live group returns after fixed polling without entering `wait`.

### D41 - Corrupt or unloadable plugins passed installed checks
- STATUS: FIXED (PR #72, 3074c6adf).
- FOUND: 2026-07-20, implementation review of the installed-package gate.
- ROOT CAUSE: plugin existence was accepted even when ELF inspection failed,
  and the startup-lazy containment-actions plugin was never loaded.
- FIX: require valid ELF headers for all five plugin artifacts and open each
  exact installed pathname with immediate symbol binding. D35 adds identity
  metadata and bounds both inspection and loading.
- EVIDENCE: corrupt files in every plugin slot and a containment-actions plugin
  with an unresolved symbol are rejected.

### D40 - Symlinked runtime roots escaped recursive inspection
- STATUS: FIXED (PR #72, 035d38da8).
- FOUND: 2026-07-20, implementation review of the installed-package gate.
- ROOT CAUSE: each Latte runtime tree was canonicalized before traversal, making
  a root symlink's destination the accepted boundary while skipping the link.
- FIX: reject symlinked runtime-tree roots before canonicalization; continue to
  audit nested links against the physical tree boundary.
- EVIDENCE: a symlinked Latte data root and a nested QML directory link to an
  external provider are rejected.

### D39 - In-prefix cross-tree links escaped recursive audit
- STATUS: FIXED (PR #72, f08fbe2c4).
- FOUND: 2026-07-20, implementation review of the installed-package gate.
- ROOT CAUSE: nested links could target any path under the broad package prefix,
  including an unaudited sibling tree whose content could escape again.
- FIX: require each nested target to remain inside the specific Latte runtime
  tree being audited rather than merely inside the install prefix.
- EVIDENCE: an absolute QML link into an in-prefix data provider is rejected
  even though both endpoints share the package prefix.

### D38 - Signal cleanup could lose status or wait forever
- STATUS: FIXED (PR #72, 0032e17f2).
- FOUND: 2026-07-20, implementation review of the installed-package gate.
- ROOT CAUSE: one callback handled EXIT, INT, and TERM directly, allowing caught
  signals to resume, cleanup to replace an existing failure, and post-KILL
  reaping to wait without a disappearance bound.
- FIX: translate signals to 130/143, run cleanup once from EXIT, preserve the
  original status, and call `wait` only after bounded absence polling.
- EVIDENCE: status 37 survives a failing cleanup, INT and TERM terminate once,
  and a simulated unkillable group never reaches `wait`.

### D37 - Loader state and mapped paths could bypass installed provenance
- STATUS: FIXED (PR #72, 9a24b538d).
- FOUND: 2026-07-20, implementation review of the installed-package gate.
- ROOT CAUSE: only two loader variables were cleared, foreign ELF search paths
  were accepted, and `/proc` parsing split mapped pathnames on whitespace.
- FIX: clear the loader-control set, reject escaping RPATH/RUNPATH entries, audit
  all Latte mappings, and strip only the fixed `/proc/<pid>/maps` fields.
- EVIDENCE: LD_AUDIT injection, binary and plugin RUNPATH escapes, Nix/build
  mappings, and foreign mapped paths containing spaces are rejected.

### D36 - Installed dock cleanup left surviving descendants
- STATUS: FIXED (PR #72, 1d091efe8).
- FOUND: 2026-07-20, independent review of the installed-package gate.
- ROOT CAUSE: the dock was started with `setsid`, but normal shutdown and EXIT
  cleanup signalled only the leader PID. A descendant could survive after the
  leader exited while cleanup removed its private runtime.
- FIX: signal and poll the complete dock process group through bounded TERM and
  KILL phases, then reap the leader after no live group members remain. D45
  distinguishes polling failure and ignores zombie-only membership.
- EVIDENCE: a leader exited with status 0 on SIGTERM while its child ignored
  SIGTERM. Cleanup detected and killed the survivor; an unkillable-group control
  returned within its fixed bound without calling `wait` on a live group.

### D35 - Arbitrary shared libraries passed installed plugin validation
- STATUS: FIXED (PR #72, ce8950b11). Typed category validation was completed by
  D55 (string service metadata passed containment-actions category checks) in
  3fb92a05a.
- FOUND: 2026-07-20, independent review of the installed-package gate.
- ROOT CAUSE: valid ELF plus successful `dlopen` did not establish that a file
  was the expected QML, containment-actions, or KPackage plugin. The unbounded
  loader could also hang inside an ELF constructor.
- FIX: require exact Qt IID/class metadata for all five plugin slots, require
  category metadata for containment actions and indicator package structure,
  and bound metadata inspection and immediate-binding `dlopen`. The settled
  dock must map the three QML plugins and containment-actions plugin; the
  startup-inactive indicator package structure is validated by metadata and
  bounded loading.
- EVIDENCE: a generic library, valid plugins with wrong IID, class, or category,
  and a valid QML plugin with a TERM-ignoring constructor are rejected. The Arch
  package runtime at exact pre-merge source head `3ee077529` (post-rebase
  tree-equivalent `10b4c4565`) mapped all four startup plugin categories from
  the installed root.

### D34 - Partial artifact scanners could produce vacuous gate success
- STATUS: FIXED (PR #72, f8bd05d60).
- FOUND: 2026-07-20, independent review of the installed-package gate.
- ROOT CAUSE: process substitutions and pipelines reported consumer status, so
  failed `find`, `readelf`, `awk`, or `/proc` parsing could publish an
  empty or plausible partial result.
- FIX: capture and check each producer before publishing arrays or values;
  failed D-Bus polling samples are explicitly cleared.
- EVIDENCE: adversarial `find`, `readelf`, and maps parsers emitted plausible
  partial output before status 73. Each path failed before consuming that output.

### D33 - Live-root package checks accepted stale same-prefix artifacts
- STATUS: FIXED (PR #72, 484052179).
- FOUND: 2026-07-20, independent review of the installed-package gate.
- ROOT CAUSE: package-prefix containment becomes tautological when both the
  package root and artifact prefix cover the live filesystem. A file omitted by
  the package under test could be supplied by an older installation at the same
  path.
- FIX: `--root /` requires an explicit package manifest, and every selected or
  recursively audited Latte file must have exact ownership. Isolated extraction
  roots retain their root-as-package-boundary contract.
- EVIDENCE: a complete live-root manifest passes; the same filesystem with the
  tasks plugin omitted from its manifest is rejected even though the stale file
  remains present under the accepted prefix.

### D32 - Always Visible floating docks fail to track maximized windows when hiding the floating gap
- STATUS: FIXED (PR #71, 54572f495 + 33c72b34e).
- FOUND: 2026-07-20, the strengthened D27 (maximize transitions leave a stale
  floating-gap work area) nested acceptance fixture reported
  `trackerData.enabled=false` while KWin showed a maximized `1600x894` frame.
- ROOT CAUSE: the `View::WindowsTracker` enabled binding read nonexistent
  `root.screenEdgeMarginsEnabled`, while `main.qml` declares the singular
  `screenEdgeMarginEnabled`. In an Always Visible default fixture, every other
  tracking requester is false, so the hide-gap option never enabled tracking.
  Disabled tracking clears `existsWindowMaximized`, preventing all downstream
  maximize-length and floating-gap behavior. A richer real layout can mask the
  defect when another applet independently requests window tracking.
- FIX: read the declared singular property. This is the intended Qt5 behavior:
  upstream Qt5 commit `79705e9753edc45cfceccd432da86acbab6ae9b8`
  introduced the typo, and both reference forks retain it.
- EVIDENCE: a marker-scoped source guard isolates the tracker binding, requires
  the singular hide-gap arm, and rejects the plural spelling. Restoring the
  typo makes the focused test fail; restoring the fix passes. The QML compile
  gate loaded all 129 eligible package files, the qmllint ratchet held, and the
  complete fast gate passed.

### D27 - Maximize transitions leave a stale floating-gap work area
- STATUS: FIXED (PR #61 bounded continuous window-change starvation in
  983685c00 + f6d5271c4; PR #70 completed synchronous maximize/exclusive-zone
  delivery in 393d1f2bf + 7d3269011 + e61a70016 + 11861e947).
- FOUND: 2026-07-19, live on a floating top panel, then traced through the
  window-tracking and layer-shell publication paths.
- SYMPTOM: maximizing a window leaves the client below the floating panel's
  old gap for about one second. The panel has already removed its visible gap,
  but KWin still applies the old work area until Latte publishes the smaller
  layer-shell exclusive zone.
- ROOT CAUSE: two independent throttles sat in series. First,
  `AbstractWindowInterface::considerWindowChanged()` treated the discrete
  `PlasmaWindow::maximizedChanged()` edge like geometry/title churn. PR #61
  stopped same-window changes from moving the timer deadline forever, but
  still allowed the semantic maximize edge to wait up to 150 ms. Second,
  `VisibilityManager::strutsThicknessChanged` called
  `updateStrutsAfterTimer()`, the one-second geometry throttle retained to
  prevent a floating-panel feedback loop. Instrumentation measured the
  thickness changing `44 -> 26` at `1784527923148` and the new exclusive zone
  reaching `setViewStruts()` at `1784527924217`: a 1.069 s stale work area.
- FIX: `WindowChangeDelivery::{Coalesced,Immediate}` makes delivery policy
  explicit. `PlasmaWindow::maximizedChanged()` uses a dedicated immediate
  route; geometry, title, active, and the other noisy signals remain
  coalesced. An immediate change flushes an unrelated pending window first and
  cancels a same-window timer, preserving order without a duplicate delivery.
  `strutsThicknessChanged` now publishes directly through
  `updateStrutsBasedOnLayoutsAndActivities()`, while
  `absoluteGeometryChanged`, screen geometry, and off-screen churn retain the
  one-second throttle.
- EVIDENCE: `windowchangedebouncetest` and `sourceguardtest` passed 20
  consecutive repetitions. Negative controls rejected the old coalesced
  maximize route, a parallel direct geometry route, and a second throttled
  thickness route. `tests/e2e/071-maximized-window-length.sh` drove one uniquely
  tagged active Wayland Konsole through restore/maximize, correlated both
  tracker facts, observed a 284 ms reservation update, and verified KWin
  reapplied the complete screen-derived 88 px work area. The cleaned staged dock
  then passed two real-session Firefox runs at 114 ms with exact
  `0,26 1440x2534` KWin frame geometry. The full gate passed at pre-merge
  `29a7b63bf`, including the sanitized nested dock; GitHub rewrote that
  tree-identical head to `11861e947`. Temporary trace instrumentation was
  removed.

### D21 - Light/Layout applet contrast: clock has no text, show-desktop is white
- FIXED (#46, be2db3049). In "Light colors" (themeColors=LightThemeColors=4) and "Layout
  colors" (=5) the top panel's applets lost contrast: the digital clock showed NO
  text and the show-desktop applet rendered WHITE (invisible) on the light panel;
  "Dark colors" (=3) was fine. ROOT: Latte's ONLY applet-recolor path was a
  layer-FBO ColorOverlay (containment/.../applet/colorizer/Applet.qml fed by
  ItemWrapper.qml's layer.enabled, original hidden at opacity 0). Dark mode worked
  only because the decision core sets mustBeShown=false there (colorizer a no-op,
  applets native). In Light/Layout mustBeShown=true and the overlay ran, exposing
  two gaps: (a) the digital clock's label is Text.NativeRendering, which is NOT
  captured into a layer.enabled FBO, so the overlay sampled empty and the hidden
  original left the clock BLANK; (b) show-desktop is exempt from the overlay
  (isShowingInlineFullRepresentation / low-saturation icon), so it rendered its
  native Breeze-dark (light) icon = white on the light panel. The decision core
  (colorizerdecider.cpp + units/colorizerdecision.h) was CORRECT and unchanged -
  applyColor already resolved to the right colour. FIX (approach B, chosen by Bree
  2026-07-18, a DELIBERATE and APPROVED divergence from Qt5's flatten-everything
  overlay model): push the decided scheme into each stock applet's OWN
  Kirigami.Theme colour group (AppletItem.qml `_wrapper` Kirigami.Theme.inherit +
  the resolved colorizerManager colours, gated by colorizerPaletteActive), the way
  stock Plasma panels colour applets, so native content renders with correct
  contrast WITHOUT the FBO; the ColorOverlay is then retired (held inert at
  mustBeShown:false). Latte-aware applets keep their existing LatteBridge.colorPalette
  path (appletBlocksColorizing); colourful icons stay native (colorfulness probe).
  EVIDENCE (nested vehicle, dark plasma theme, LightThemeColors, isolating the
  push as the sole variable): CONTROL (overlay retired + push disabled via
  inherit:true) rendered the clock and show-desktop UNIFORM light - mean 0.994,
  std 0, min 0.988 - invisible native text on the light panel, faithfully
  modelling the real-system failure. TREATMENT (overlay retired + push enabled):
  clock "10:00 PM 7/18/26" and the show-desktop icon rendered DARK - clock std
  0.126, min 0.125 - visible/correct; the systray's symbolic icons also rendered
  dark AND kept their semantic accents (muted-volume's red strike), which the old
  flatten would have destroyed. The nested vehicle's compositor happens to capture
  NativeRendering into the FBO, so it does not reproduce the raw blank-clock
  symptom the real Plasma 6.6.5 desktop shows; the control/treatment isolation
  proves the mechanism instead. Observability: colorizerData now reports the
  resolved applyColor/textColor/backgroundColor + brightnesses; viewAppletsData
  reports per-applet colorizerActive + colorizerReason (applied / notEngaged /
  splitter / selfColored / userBlocked / inlineFull / colorful). Guard:
  tests/e2e/110-colorizer-applet-contrast.sh. Found on the real dock 2026-07-18.

### D22 - main.xml omits the LayoutThemeColors enum choice (enum range out of sync)
- FIXED (#46, be2db3049). containment/package/contents/config/main.xml listed only five
  themeColors choices (Plasma/Reverse/Smart/Dark/Light) while types.h and the
  settings UI define six - LayoutThemeColors=5 was missing, so the KConfigXT
  enum-by-name range was out of sync with the real enum: a config that stored the
  Layout mode by NAME (`themeColors=LayoutThemeColors`) had no choice to map to,
  and a Layout config held as the bare int `5` could not be re-serialized to its
  name. SURFACED 2026-07-18 during the D21 repro: the real top panel's
  `themeColors=LightThemeColors` was seen rewritten to a bare `themeColors=5`
  while the dock ran (the trigger was not isolated to a single write, but the
  enum range being out of sync is the class of bug that lets a name/int mismatch
  slip through). FIX: add `<choice name="LayoutThemeColors"/>` in enum order
  (matches types.h and the ng fork's main.xml). VERIFIED (nested vehicle): with
  the choice present, a `themeColors=LightThemeColors` panel round-trips a save
  cycle unchanged - after an edit-mode enter/exit the value stayed
  `LightThemeColors` and colorizerData read it as mode "light" (=4), not
  "layout" (=5).

### D23 - Colors dropdown collides Reverse and Layout on one index
- FIXED (#46, be2db3049). shell/.../pages/AppearanceConfig.qml colorsToIndex() mapped BOTH
  ReverseThemeColors and LayoutThemeColors to index 3, while Reverse was commented
  out of the dropdown model entirely (upstream's 2020 "combine Colors options"
  commit 2b5d19cfa; capt's port carries the same collision). A Reverse config
  therefore showed as "Layout Custom Colors" and, via onCurrentIndexChanged, was
  silently rewritten to Layout on open. FIX (ng-faithful): restore Reverse as its
  own dropdown row and give the six values distinct indices
  (Plasma0/Dark1/Light2/Layout3/Reverse4/Smart5), so the dropdown can show which
  mode is active and no value is clobbered. Found 2026-07-18, code-reading during
  the D21 investigation.

### D24 - TypeSelection Dock/Panel presets write two dead keys
- STATUS: OPEN (confirmed harmless-but-inert; tracked independently as
  SC-M1 (the D24 dead TypeSelection write cleanup)). Found by the CL-3 behavior-page
  audit (2026-07-19, AU-3d (the TypeSelection dead-key audit) and S-a (the
  TypeSelection dead-key check)).
- The Type-selection presets (shell/.../controls/TypeSelection.qml, four write
  sites) write `solidPanel` and `colorizeTransparentPanels` when picking Dock or
  Panel. Neither key exists in the containment schema (config/main.xml) and a
  tree-wide grep finds ZERO readers (the only solidPanel* symbols are the
  differently-named BackgroundStateResolver::solidPanelForced, fed by the REAL
  solidBackgroundForMaximized). So the two writes land nowhere and do nothing.
- INHERITED, not a port regression: a deprecated `solidPanel` schema key was
  removed upstream long ago and these writes were never cleaned up; the Qt6
  reference fork carries the identical dead writes.
- RECOMMENDED DIRECTION: remove the four dead write lines in
  `TypeSelection.qml` under SC-M1. This is an appearance/schema cleanup and is
  not part of D30 (Behavior mouse actions expose fixed booleans instead of full
  choices). No compatibility path is needed because no reader or schema entry
  exists.

### D28 - Show-desktop applet icon stays white on a light-colored panel
- FIXED (0390582fa). On a "Light colors" or "Layout colors" panel the
  show-desktop applet (org.kde.plasma.showdesktop) rendered with its native
  Breeze-dark icon, which is white, against the light panel background and was
  hard to see. ROOT: the D21 colorizer push is gated by
  `colorizerPaletteActive`, and that binding treated the colorfulness probe as a
  hard veto. On themes where the show-desktop icon registers as colorful the
  probe set `blocksColorizing=true`, so the resolved panel scheme was never
  pushed into the applet's Kirigami.Theme colour group and the native white
  icon remained. FIX: the show-desktop applet is a stock panel icon and is
  expected to follow the panel scheme like the digital clock and systray, so
  `AppletItem.qml` now explicitly exempts it from the colorfulness probe both in
  `colorizerPaletteActive` and in `colorizerExemptionReason`. This is a
  DELIBERATE Qt5-faithful behavior change: upstream Qt5 Latte never relied on
  this probe (it did not exist in the same form), and the flatten-everything
  overlay would have recoloured show-desktop anyway. EVIDENCE: the nested
  vehicle's `110-colorizer-applet-contrast.sh` recipe asserts show-desktop is
  never `colorizerReason="colorful"` and always `colorizerActive=true` with
  `reason="applied"`; a temporary revert of the carve-out produces the
  `colorful` reason and fails, while the carve-in passes.

### D14 - invalid-color qCriticals at every startup
- FIXED (#46, be2db3049). Startup logged a burst of `Tools.colorBrightness: invalid
  color from QML, returning 0 (dark)` qCriticals (80 in the nested-vehicle real
  config; ~46 on the config the defect was first noted against - the count
  tracks item/view count). ROOT: Kirigami's attached PlatformTheme (and the
  colorizer/colorPalette chain it feeds) serves a default-constructed invalid
  QColor on the FIRST evaluation of a creation-time binding, before its palette
  resolves; the theme's change notify recomputes the real color a beat later.
  The C++ boundary guard at declarativeimports/core/tools.cpp is CORRECT and
  stays (loud refuse of an invalid QColor); the fix is at the SOURCE - the QML
  call sites now guard the brightness/isLight call on color validity
  (`COLOR.valid ? Tools.f(COLOR) : <invalid-fallback>`), so the invalid interim
  is never handed to the boundary. All 13 LatteCore.Tools brightness/isLight
  call sites guarded (Manager.qml x5, LatteIndicator, ShortcutBadge,
  indicators/default main, plasmoid main, AddItem, AddingArea, SettingsOverlay,
  LatteDockConfiguration). EVIDENCE (nested vehicle, three runs): baseline 80
  invalid qCriticals, all spec=0; after the guard 0 qCriticals with per-site
  attribution summing to exactly 80 (no site missed); settled brightness values
  identical before/after (themeTextColorBrightness 37.445,
  backgroundColorBrightness 239.815, editModeTextColorIsBright true), so the
  fix changes only the premature invalid call, not the resolved result. The
  tools.cpp boundary comment flipped from "expect a burst" to "SILENT at
  startup; a refusal now is a genuine bug". Found live 2026-07-18; previously
  noticed at session-handoff.md:890 but never root-caused.

### D5 - Justify splitter negative-insert UB
- FIXED: #22 (c9f3f2427). splitterPosition=0 -> QList::insert(-1) = UB in the
  release dock. Repaired via the justifysplitters.h pure core. See
  ub-catching-plan.md B1. Found live (a splitter visibly vanished).

### D6 - Two decayed-vptr static_casts in destroyed() slots
- FIXED: #25 (ddb766df1). app/layout/genericlayout.cpp:790 (Containment),
  app/layouts/syncedlaunchers.cpp:65 (QQuickItem) - static_cast read a decayed
  (now-plain-QObject) vptr = UBSan -fsanitize=vptr abort. reinterpret_cast
  (identity-only, no vtable access). See ub-catching-plan.md B2. Found by the
  A3 driven sanitized gate on its first run.

### D7 - Maximize-length repaint: stale frosted band on shrink
- FIXED: #24 (83eaa0487 core, fbbf13a54 fix, c05f844c2 length-axis scoping).
  Qt6 wayland couples QWindow::mask() to submitted buffer damage; a length
  shrink dropped the vacated edge's clearing damage. Union-hold across the
  shrink, scoped to the length axis. Found live on a real top dock. Desk-check
  still owed (see session-handoff). Latent residual D4 above.

### D8 - Dev-loop shadow: staged dock loaded the packaged containment plugin
- FIXED: #23 (326aba06d). The nix Qt6 wrapper's NIXPKGS_QT6_QML_IMPORT_PATH
  carried the system-installed packaged latte-dock, shadowing the worktree
  build; containment/plugin changes "landed but never ran". lib-qml-env.sh
  strips only that leaf. Found via /proc/<dock>/maps.

### D9 - Edit-mode header hint ate the Rearrange click
- FIXED: #20. The edit-mode overlay tooltip swallowed clicks aimed at the
  Rearrange button when the panel was short. Found live.
