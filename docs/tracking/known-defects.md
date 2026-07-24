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
- DISPOSITION: preserve the behavior with no fix or divergence. PR #99 landed
  SC-T3 (the D29 narrow dispatch readback), which distinguishes
  `requestActivate` from `requestNewInstance`. PR #101 landed SC-T5 (the D29
  permanent runtime-effect acceptance) at `382268a92`, pinning exact-once
  dispatch, the zero-to-one active-window and one-to-two grouped-child effects,
  and an action-None negative control. SC-T4 (the D29 root fix) is not
  applicable. Temporary instrumentation was removed.

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
  tracking) was the confirmed root defect found by SC-B1 and is fixed by PR #94.
  Separate plan findings cover Wayland close without an `isCloseable()` check,
  minimize without an `isMinimizeable()` check, and void operation APIs that
  cannot return typed refusal. Those seams require later decision units and are
  not part of D58.

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
- STATUS: OPEN. PR #96 landed SC-CW1 (the D57 ConfigOverlay wheel-threshold
  reproduction) at reproduction commit `5ec57175f`, tracking commit
  `aa6399b44`, tracking trim `709c0946b`, and evidence qualifier `9b0672cf9`.
- FOUND: 2026-07-20, SC-F1 (the per-view source inventory and evidence ledger).
- SYMPTOM: delivered horizontal +/-120, vertical +/-90, and the vertical -96
  boundary decrease a Latte-style applet's length on either view axis.
- ROOT CAUSE: `containment/package/contents/ui/editmode/ConfigOverlay.qml`
  divides `wheel.angleDelta.y` by 8 but decreases for `angle < 12` instead of
  `angle < -12`; horizontal events enter that arm with `angle == 0`.
- EVIDENCE: repeated nested runs observed +120:+8, -120:-8, +96:0, -96:-8,
  +90:-8, -90:-8, and horizontal +/-120:-8 on both view axes. Normal mode was a
  no-op. Explicit `axisstop` sends a zero fake-input axis; KWin forwards it as
  `wl_pointer.axis_stop`, and Qt emits no `QWheelEvent` in this isolated sequence.
  The following +120 still increases length. Status 57 means this complete matrix
  after cleanup; status 0 is XPASS, and partial signatures or harness failures
  remain FAIL.
- NEXT: SC-CW2 (the D57 signed decrease-threshold fix and regression promotion)
  remains unchecked, approval-required, and unapproved. Merged SC-CW1 evidence
  does not authorize the production fix.

### D58 - Close-only and minimize-toggle settings do not enable window tracking
- STATUS: FIXED. PR #94 landed the root fix at `15f026887`, initial tracking at
  `91cfb2bac`, e2e harness hardening at `14da9e7ce`, the complete
  requester source guard at `0a796e1ec`, and review tracking at `8c6b1c826`.
- FOUND: 2026-07-20, SC-B1 (the D30 current-contract investigation).
- SYMPTOM: before PR #94, close-only and `ScrollToggleMinimized` configurations
  reported `tracker.enabled=false`, leaving the configured close or minimize
  gesture with no active-window target and no effect.
- ROOT CAUSE: the `BindingsExternal.qml` active-window tracker expression enabled
  tracking for `dragActiveWindowEnabled`, but omitted
  `closeActiveWindowEnabled` and `scrollAction === ScrollToggleMinimized`.
- EVIDENCE: the SC-B1 current-contract matrix independently observed the missing
  effects. SC-WT1 (the D58 tracker-enablement root fix and regression) then went
  RED both in the source guard and at the close-only nested tracker readback.
  Root commit `15f026887` and hardened regression `14da9e7ce` passed three
  complete nested production-path runs.
  Every run observed disabled close/minimize with `tracker.enabled=false` and a
  normal KWin window, close-only and minimize-toggle with
  `tracker.enabled=true`, harmless no-target input, independent KWin window
  removal for close, and independent KWin minimized state for negative wheel.
- REVIEW HARDENING: KWin and input failures now fail loudly, successful cleanup
  proves zero fixture residue and byte-identical config restoration before PASS,
  and the source guard compares the complete normalized requester expression.
  A false injector and an OR-to-AND mutation were both rejected; three hardened
  nested repetitions passed after the temporary probes were removed.
- FIX: `BindingsExternal.qml` adds only the two missing existing-contract
  dependencies. The source guard preserves visibility, applet, move/maximize,
  dynamic-background, and floating-gap requesters. Wayland capability checks
  and typed-refusal API work remain separate plan findings, not part of D58.

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

### D60 - Tasks QML type metadata omits accessibility composer methods
- STATUS: OPEN (confirmed by generated-metadata comparison 2026-07-21). The
  defect record landed at `faceecd35`; the repair remains separate from SC-T3
  (the D29 narrow middle-click dispatch readback).
- FOUND: SC-T3 (the narrow dispatch readback for D29 (task-icon middle click
  appears to execute left-click behavior)) type-metadata check.
- SYMPTOM: QML tooling cannot discover
  `TooltipTextComposer.composeAccessibleDescription(QVariantMap)` or
  `TooltipTextComposer.muteToggleLabel()`, although production QML calls both
  methods and the plugin exports them at runtime.
- EVIDENCE: regenerating `org.kde.latte.private.tasks` metadata from the built
  plugin adds exactly those two method declarations beyond the tracked
  `plasmoid/plugin/plugins.qmltypes` after the new SC-T3 Backend property,
  signal, and method are matched. The runtime methods exist in
  `tooltiptextcomposer.h/.cpp`; the tracked tooling metadata is stale.
- NEXT: regenerate and review the complete tasks `plugins.qmltypes` file in a
  separate tooling-metadata change. No unrelated metadata fix is included in
  SC-T3.

### D61 - Middle-click aggregate could expose an older plausible event
- STATUS: FIXED. PR #99 landed the fail-closed aggregate fix at `bfd30f235`.
- FOUND: independent pre-PR review of SC-T3 (the D29 narrow middle-click
  dispatch readback).
- ROOT: `collectMiddleClickDispatchData` skipped malformed candidates and only
  compared a sequence with the current maximum. A valid older record could
  therefore survive malformed state, and a nonadjacent duplicate such as
  5/10/5 was not detected.
- FIX: the live collector feeds every readable tasks-applet candidate into one
  pure selector. Any malformed nonempty state, containment mismatch, or
  globally repeated sequence refuses the complete aggregate as `{}`. Empty
  maps remain legitimate no-event state; startup-transient missing quick items
  remain loudly unavailable; applets in Plasma's removal undo window remain
  queryable until actual destruction.
- EVIDENCE: sanitizer-backed `dbusreportstest` covers multiple applets, newest
  selection, exact JSON, mixed no-event candidates, malformed-plus-valid
  refusal, requested-containment mismatch, and 5/10/5 duplicate refusal.
  Source guards at `e190d03b0` and `4dd51fdcd` pin the complete QML reporter
  and live collector bridges, including the undo-window lifecycle contract.

### D62 - Middle-click readback accepted inconsistent action-operation pairs
- STATUS: FIXED. PR #99 landed the exhaustive action-operation mapping at
  `bfd30f235`.
- FOUND: independent pre-PR review of SC-T3 (the D29 narrow middle-click
  dispatch readback).
- ROOT: the backend and D-Bus parser validated enum ranges and the launcher
  exception independently, but neither required a task row's operation to
  match its configured action. They also accepted TaskAction values outside
  the six-value middle-click set.
- FIX: one exhaustive C++ mapping defines the six offered actions and their
  task-row operations for both boundaries. Launchers may carry any offered
  configured action but must report `RequestActivate`; task rows must report
  the mapped operation exactly.
- EVIDENCE: `tasksbackendtest` and sanitizer-backed `dbusreportstest` cover all
  six task pairs, all six launcher exceptions, every known unoffered action,
  unknown values, and mismatched row/action/operation combinations. The exact
  production reporter forwarding is pinned at `e190d03b0`.

### D63 - Task settings-inventory anchors did not follow middle-click QML
- STATUS: FIXED. PR #99 landed the inventory-anchor correction at `cd959cb3a`.
- FOUND: canonical full gate for SC-T3 (the D29 narrow middle-click dispatch
  readback) before the anchor correction.
- ROOT: `TaskMouseArea.qml` accepted-buttons moved from line 19 to 20 because it
  precedes the inserted reporter helpers. Subsequent pointer-handler anchors
  shifted by 25 lines, while wheel and timer anchors shifted by 26 because they
  also follow launcher dispatch recording. The inventory retained its pre-SC-T3
  line numbers; most still landed on unrelated nonempty lines, while hover-exit
  landed on line 100's blank separator and made `settingsinventorytest` fail.
- FIX: all nine task-row anchors and the drag-and-drop exemption now point to
  their exact accepted-buttons, handler, wheel, timer, and drag-handler lines.
- EVIDENCE: focused `settingsinventorytest` passes at 270 affordances and 21
  exemptions. The final canonical full gate passed and stamped exact pre-rebase
  head `2fd23a08e34a10eebeab11e7cbb02c919478b8d4`, whose tree matches final
  tracking commit `f2c2ba089` after GitHub's rebase merge.

### D64 - Distro-gate fakepointer build omits the xkbcommon link dependency
- STATUS: OPEN (confirmed by the exact helper link command 2026-07-21). The
  defect record landed at `611824a68`; no repair landed with it.
- FOUND: SC-T5 (the permanent runtime-effect acceptance for D29, task-icon
  middle click appears to execute left-click behavior) local fakepointer build.
- ROOT: `ci/build-and-gate.sh` compiles `scripts/tools/fakepointer.c` with only
  `pkg-config --cflags --libs wayland-client`. The source calls
  `xkb_keysym_from_name` for its key and drag-key verbs, so the binary also
  requires `xkbcommon`. The live-verification build recipe already names both
  packages.
- EVIDENCE: running the helper's exact compiler and linker arguments against
  the generated fake-input protocol failed with
  `undefined reference to xkb_keysym_from_name`. Adding
  `pkg-config --cflags --libs wayland-client xkbcommon` produced the
  fakepointer binary used by the passing SC-T5 nested runs.
- FIX DIRECTION: B2a (the D64 distro-gate fakepointer xkbcommon link repair) in
  `multi-distro-ci-plan.md` adds the missing package to the helper and every
  container dependency set, then exercises the helper build in the focused
  container self-test. This remains separate from SC-T5 and does not require a
  dock behavior change.

### D65 - Popup row stable values were not unique at the wire level
- STATUS: FIXED (`523c6f468`).
- FOUND: 2026-07-21, independent review of SC-O1 (the read-only
  settings-control D-Bus registry).
- ROOT: row registration rejected duplicate semantic identity and visual index
  but did not reject duplicate stable locator values. Distinct C++ scalar
  alternatives can also serialize to the same JSON bytes, including integer
  `1` and double `1.0`.
- FIX: one pure helper derives the compact serialized scalar locator used by
  both registration and aggregate validation. Each popup accepts exactly one
  row per locator, including only one null.
- EVIDENCE: sanitizer-backed `settingscontrolrecordstest` pins exact locator
  bytes and rejects null/null and integer `1`/double `1.0`. The registry fixture
  rejects the same pairs and retires their complete load generations.

### D66 - Settings-control descriptors accepted foreign-thread objects
- STATUS: FIXED (`523c6f468`; post-registration migration completion
  `015200981`).
- FOUND: 2026-07-21, the SC-O1 independent review.
- ROOT: registry calls were GUI-thread-only, but the QObject and QQuickItem
  descriptors themselves had no affinity check. Their destruction could
  therefore queue cleanup or run object access across threads.
- FIX: scope lifetime, surface and geometry objects, control and popup state,
  popup and row items, and every hit item must share the registry GUI thread at
  registration.
  `Qt::AutoConnection` keeps cleanup synchronous while affinity holds and queues
  it safely to the registry thread after illegal post-registration migration.
- EVIDENCE: `settingscontrolregistrytest` moves each descriptor category to a
  live worker thread, verifies loud refusal and complete generation retirement,
  then returns every object to the GUI thread before destruction. Separate
  post-registration cases prove migrated signal and destruction delivery does
  not mutate registry counts until GUI-thread event processing.

### D67 - Logical registry removal left lifecycle callbacks connected
- STATUS: FIXED (`523c6f468`; count-based cleanup proof `015200981`).
- FOUND: 2026-07-21, the SC-O1 independent review.
- ROOT: destroyed and popup-notify connections were not stored, so replacement
  removed entries but left callbacks attached. Popup routing cleanup also read
  a QPointer after destruction had begun, leaving stale raw-key entries after
  pointer decay.
- FIX: scopes, controls, and rows own every connection. Removal captures the
  entry, erases its owning and secondary route/row bookkeeping, then disconnects
  the captured handles. Popup-state destruction removes routing by captured raw
  identity and numeric token; the raw pointer is never dereferenced.
- EVIDENCE: repeated generation replacement followed by old owner, control,
  row, and popup-state notification/destruction leaves the replacement intact.
  Current popup-state notification still advances generation, and destruction
  removes the current control synchronously. Count diagnostics remain constant
  across five replacements and reach zero after explicit retirement.

### D68 - Popup rows accepted unrelated surface items
- STATUS: FIXED (`523c6f468`).
- FOUND: 2026-07-21, the SC-O1 independent review.
- ROOT: row and row-hit ancestry was checked against the settings surface, not
  the popup item. An unrelated surface descendant could therefore become a
  plausible popup target.
- FIX: every row item and row hit must be the popup item or its visual
  descendant.
- EVIDENCE: `settingscontrolregistrytest` rejects both an unrelated row item and
  an unrelated row hit, retiring each affected load generation.

### D69 - Failed settings-control registration exposed a plausible subset
- STATUS: FIXED (`523c6f468`; cross-scope completion `015200981`).
- FOUND: 2026-07-21, the SC-O1 independent review.
- ROOT: malformed or duplicate registration returned failure but retained
  controls already accepted for the same load. A query could therefore expose
  a partial array that looked complete.
- FIX: any control or popup-row registration refusal immediately poisons the
  complete affected load generation. An exact-scope tombstone now also blocks
  other surviving scopes in the containment until matching valid replacement
  or owner destruction. Attempts through retired generation or control tokens
  warn and refuse.
- EVIDENCE: the registry fixture starts from valid controls and rows, injects
  duplicate and malformed registrations, verifies `[]`, and proves later use
  of each retired token refuses. A two-scope fixture proves a sibling cannot
  escape the tombstone.

### D70 - Corona settings-control changes omitted current copyright attribution
- STATUS: FIXED (`523c6f468`).
- FOUND: 2026-07-21, the SC-O1 independent review.
- ROOT: `app/lattecorona.h` and `app/lattecorona.cpp` gained the registry
  boundary without adding the current author's SPDX line.
- FIX: both files now retain every existing line and add
  `SPDX-FileCopyrightText: 2026 Bree Spektor`.

### D71 - Invalid settings scope did not poison sibling scopes
- STATUS: FIXED (`015200981`).
- FOUND: 2026-07-21, warranted second independent review of SC-O1 (the
  read-only settings-control D-Bus registry).
- ROOT: removing the failed generation erased all evidence of its invalid load,
  so a surviving surface or applet scope could still look like the complete
  containment.
- FIX: an exact containment/surface/applet tombstone blocks the aggregate until
  matching valid replacement or captured-owner destruction. Unrelated scope
  replacement cannot clear it.
- EVIDENCE: the two-scope fixture covers sibling survival, unrelated
  replacement, matching restoration, owner cleanup, and stale
  invalid-generation refusal.

### D72 - Forced direct cleanup could mutate the registry from a foreign thread
- STATUS: FIXED (`015200981`).
- FOUND: 2026-07-21, the warranted second SC-O1 review.
- ROOT: forced `Qt::DirectConnection` ignored receiver affinity after an object
  illegally migrated following valid registration.
- FIX: lifecycle and popup-notify connections use `Qt::AutoConnection`; migrated
  objects fail queries closed and queue cleanup to the GUI thread.
- EVIDENCE: parentless popup-state and lifetime objects migrate, signal, and die
  on a worker without changing counts before GUI event delivery.

### D73 - Popup integer locators exceeded interoperable JSON precision
- STATUS: FIXED (`015200981`).
- FOUND: 2026-07-21, the warranted second SC-O1 review.
- ROOT: qint64 row values beyond IEEE-754 exact integer precision could change
  identity in common JSON consumers.
- FIX: integer locators are restricted to
  `[-9007199254740991, 9007199254740991]`; other scalar wire-uniqueness rules
  remain unchanged.
- EVIDENCE: sanitizer-backed boundary tests accept both limits and reject each
  adjacent outside value; registry refusal poisons the load.

### D74 - Settings-control cleanup claims lacked a state-count oracle
- STATUS: FIXED (`015200981`).
- FOUND: 2026-07-21, the warranted second SC-O1 review.
- ROOT: output-only assertions could pass while stale route or connection
  bookkeeping accumulated for process lifetime.
- FIX: a non-D-Bus internal diagnostics value exposes counts only for
  generations, controls, rows, routes, owned connections, and tombstones.
- EVIDENCE: five replacements hold counts constant; row and popup-state
  destruction reduce them; explicit generation retirement reaches all zeroes.

### D75 - Handoff ended the review sequence before required major follow-up
- STATUS: FIXED (`a5b086d29`).
- FOUND: 2026-07-21, applying the refined review-severity heuristic to the
  initial SC-O1 findings.
- ROOT: the handoff classified the initial fixes as requiring no further review,
  although ownership, lifecycle, invariant, test meaning, and traceability
  findings require one second independent review under the current rule.
- FIX: the handoff records the initial major findings, the warranted second
  review and its MERGE AFTER FIXES result, the correction commits, and the end
  of the review sequence without a third review.

### D81 - Installed-package audit crossed its isolated package-root boundary
- STATUS: FIXED (PR #108; `7148a54d8`, `fcb71e8b4`, `ff732466e`;
  standalone package-provenance and fixture corrections).
- FOUND: 2026-07-21, the C0 (atomic dock-system observability snapshot) branch's
  required fast gate under a `/tmp` worktree.
- ROOT: recursive package-link validation first proved a target belonged to the
  isolated package root, then scanned development-provider markers beyond that
  boundary up to `/`. An unrelated `/tmp/.git` therefore classified every
  synthetic package beneath `/tmp` as a source tree.
- FIX: development-provider traversal stops after checking the isolated
  package root. Live `--root /` validation still reaches `/`, and the existing
  direct source/build markers inside a package remain refusals. The live-root
  fixture starts below an explicit marker-free parent, defaults to
  `XDG_RUNTIME_DIR` or `/var/tmp`, and refuses source/build-marked ancestry
  before exercising the production host-root walk. Its preflight inspects the
  current ancestor before the stop condition, including `/`, to match the
  production live-root traversal.
- EVIDENCE: the focused installed-package self-test places a valid internally
  linked package beneath an external parent carrying `CMakeLists.txt` and
  requires acceptance. All 91 provenance, parser, link, ELF, loader, mapping,
  signal, and cleanup controls pass. A predicate that marks only `/` drives the
  exact fixture-preflight refusal without modifying the host root. The same run
  also requires a host-absolute live-root symlink to retain host semantics from
  real marker-free ancestry.

### D82 - TaskItem Connections syntax exceeded the curated Qt 6 lint ratchet
- STATUS: FIXED (PR #108; `728d69a62`; standalone QML syntax correction).
- FOUND: 2026-07-21, the C0 (atomic dock-system observability snapshot) branch's
  required fast gate under pinned Qt 6.11.1.
- ROOT: `TaskItem.qml` retained the deprecated implicit `Connections` handler
  form for PulseAudio stream changes. The pinned linter reproducibly counted
  212 curated findings against the checked-in 211-warning ceiling even though
  the QML file, both imported static qmltypes files, the lint script, and the
  baseline were byte-identical to the branch point.
- FIX: declare `onStreamsChanged` as an explicit function while preserving its
  target, optional-signal contract, and `updateAudioStreams()` action.
- EVIDENCE: the QML compile gate accepts every staged package file and the
  qmllint gate returns `TaskItem.qml` to the checked-in 211-warning ceiling.

### D84 - Runtime token assignment depended on QHash traversal
- STATUS: FIXED IN PR #110 (`5591b66d7`).
- FOUND: 2026-07-22, initial independent C0 review.
- ROOT: the first runtime identity-registry lookups happened while traversing
  Synchronizer's QHash-derived view collection. A fresh process could therefore
  assign different opaque tokens to the same persistent dock ordering.
- FIX: read containment ids first, sort by persistent id, then perform every
  runtime view and shared-controller lookup in that order.
- EVIDENCE: shuffled inputs with fresh registries produce identical view and
  shared-controller token assignments; a controlled source guard requires the
  ordering call before the first registry lookup.

### D85 - Runtime identity tests missed retirement timing and thread affinity
- STATUS: FIXED IN PR #110 (`85e59ee07`).
- FOUND: 2026-07-22, required cold C0 follow-up review.
- ROOT: exact-address reuse still passed through the lazy cleared-QPointer
  fallback if the destruction connection was removed. No test drove the
  registry's GUI-thread precondition from a foreign object or caller.
- FIX: a count-only internal oracle proves immediate retirement, and one
  side-effect-free predicate is shared by registration, destruction, and the
  oracle. It checks application, registry, caller, and object affinity.
- EVIDENCE: destruction reduces the count before reconstruction at the same
  address; worker-thread tests reject both affinity violations; controlled
  mutations reject queued cleanup, missing erase, and a removed caller check.

### D86 - Dock-system schema tests left most field types unchecked
- STATUS: FIXED IN PR #110 (`f9c5af8df`).
- FOUND: 2026-07-22, required cold C0 follow-up review.
- ROOT: the serializer test pinned every key but asserted types for only a
  subset, so numbers, booleans, rectangles, and nullable fields could drift
  while the key-set check remained green.
- FIX: assert the JSON type of every top-level, view, geometry, and object field
  in a populated record, plus every documented null in a default record.

### D87 - C0 and per-dock configure isolation shared one commit
- STATUS: FIXED IN PR #110 (`c11c77ed2`, `5591b66d7`).
- FOUND: 2026-07-22, initial independent C0 review.
- ROOT: D76 (global applet-configure readback marked unrelated docks active)
  and the new C0 read surface were grouped despite having independent causes.
- FIX: D76 is a dedicated fix commit before the C0 feature commit.

### D88 - Initial C0 documentation omitted identity and geometry semantics
- STATUS: FIXED IN PR #110 (`9767ea4fb`).
- FOUND: 2026-07-22, initial independent C0 review.
- ROOT: the first public text collapsed current duplication behavior and did
  not define geometry coordinate spaces, logical-pixel units, or required
  versus optional runtime authorities.
- FIX: both D-Bus references define those contracts and expose stacking as an
  explicit unavailable capability instead of an inferred order.

### D89 - Dock-system enum mappings lacked exhaustive tests
- STATUS: FIXED IN PR #110 (`5591b66d7`).
- FOUND: 2026-07-22, initial independent C0 review.
- ROOT: orientation, screen-group, and relationship name switches lacked one
  data row per enumerator, weakening their Q_UNREACHABLE exhaustiveness claim.
- FIX: data-driven tests cover every current enumerator.

### D90 - Malformed clone lineage yielded plausible partial snapshots
- STATUS: FIXED IN PR #110 (`30ecf6bfc`, `e853e196a`).
- FOUND: 2026-07-22, required cold C0 follow-up review.
- ROOT: lineage was classified one record at a time. A bad record was skipped,
  missing originals produced null screen-group policy, and clone-to-clone edges
  were never checked, despite the API promising a complete atomic snapshot.
- FIX: validate the complete persistent-id graph before any identity lookup.
  Every clone must target a present screens-group original directly. Any
  missing or standalone target, duplicate id, chain, or cycle refuses the whole
  query with critical diagnostics and an empty D-Bus string.
- EVIDENCE: the pure graph matrix covers each valid and invalid shape; source
  guards pin both validation-before-identity and the wrapper's fail-closed path.

### D91 - C0 review defects lacked flat-registry and checklist traceability
- STATUS: FIXED IN PR #110 (`4fa619870`).
- FOUND: 2026-07-22, required cold C0 follow-up review.
- ROOT: the handoff summarized the initial findings, but the mandatory flat
  defect registry and Phase 10 checklist did not name them individually. The
  plan also still described a completed but later-invalidated gate as pending.
- FIX: D84-D92 are recorded in the flat registry and as checked Phase 10 work;
  the handoff distinguishes the first gate from the required final rerun.

### D92 - Const-touched View files omitted current copyright attribution
- STATUS: FIXED IN PR #110 (`cd478bb06`).
- FOUND: 2026-07-22, required cold C0 follow-up review.
- ROOT: making two observational View accessors const modified both source
  files without adding the current 2026 modification copyright line.
- FIX: retain all existing SPDX lines and add Bree Spektor to both files.
### D77 - Dock duplication retains clone lineage and edit ownership
- STATUS: FIXED IN PR #109 (`8f2c3073d`; implementation `d9ca7bcfb`, `0234aba66`,
  `896f8e20b`, `a2a93b965`, `2d5184665`, `0f04cb7ef`, `5585c708a`,
  `b99bbe4be`, `2695d2355`, `2e97f88b6`, `5d9166ed9`; focused caller
  contract `bce41d191`; shared layouts-dialog correction `ebb517a67`; runtime
  recipes `3a7b01f25`, `2e97f88b6`).
- FOUND: 2026-07-21, dock duplication and edit-mode identity investigation.
- SYMPTOM: Duplicate Dock on an ordinary All Screens source created another
  linked multi-output ensemble instead of one independent dock. Duplicating a
  linked replica through a non-menu action could retain its source, while the
  menu hid the operation. Rapid edit retargets could leave an older containment
  configuring, replica edit exit could miss the original session, and runtime
  recreation could temporarily expose relation-owned context-menu actions on a
  persistent replica.
- ROOT CAUSE: the template-copy path also copied `screensGroup`, so an imported
  All Screens original immediately created new persisted `isClonedFrom`
  members and runtime `ClonedView` synchronization. A replica capture could
  separately overlay its old `isClonedFrom` after Storage orphaned the unmapped
  source. Relationship capabilities were presentation policy rather than one
  runtime policy. The shared edit chrome queued unversioned retarget callbacks,
  and entry and exit did not resolve one authoritative target. Replica
  membership and several Wayland registrations also lacked explicit lifetime
  ownership.
- FIX: Duplicate Dock is a relation-breaking snapshot from either an original
  or a linked replica. It clears `isClonedFrom`, normalizes `screensGroup` to
  `SingleScreenGroup`, stays visible on replica menus, and creates one fresh
  containment and applet identity set. Export, move, and remove remain owned by
  the relationship original. Existing linked layouts remain linked. Edit
  retargeting is cancelable and generation-checked, ends the old configuring
  session before rebind, and resolves linked-member entry and exit to the same
  original. Replica membership is destruction-safe. Exact Wayland title
  matching, owner-counted ignored windows, replaceable output subscriptions,
  and persistent containment identity close the related runtime ownership gaps.
- EVIDENCE: sanitizer-backed `viewactionpolicytest`,
  `retargetrequeststatetest`, `windowtrackingpredicatestest`, and
  `ignoredwindowregistrytest` pass. `dockidentitycontracttest` pins the
  production boundaries. `latte-dock` and the containment-actions plugin
  compile successfully. Nested KWin canceled the B edit request in 178 ms and
  kept both docks out through the old timeout. The dual-output acceptance made
  All Screens original 1 and linked replica 12 each produce exactly one fresh
  nonlinked dock (13 and 14), found no containment or applet ID overlap, kept
  the original relation intact, propagated a visibility-mode change from the
  original only to its linked replica, and preserved all four identities across
  restart. The final canonical full gate passed at exact pre-merge head
  `defaa0c7ad1a0e376937bf07f035430ecc977407` after both cold review
  corrections and the commit-message cleanup. GitHub rebased the validated
  source and test tree through `b6ba7ab15`.

### D83 - Removed duplicate containment survives the undo window in persistent layout state
- STATUS: FIXED in PR #113 (`adb11b11f`, `b92fafb56`).
- FOUND: 2026-07-21, baseline `duplicate-view-idremap` acceptance run at
  `16eb58ea4` before the D77 implementation.
- SYMPTOM: removing a newly created independent duplicate destroys its runtime
  containment, but the duplicate's persistent containment group remains in the
  layout after the removal window has expired.
- EVIDENCE: duplicate containment 12 was independent with `IsClonedFrom: -1`.
  The log recorded `dock containment destroyed changed!!!!` at 21:31:13, while
  the containment group was still present after the recipe's 120-second poll.
- ROOT CAUSE: two independent persistence boundaries failed. MultipleLayouts
  synchronization copied every live containment QObject, including Plasma's
  transient destroyed objects, back into the per-layout file. In the nested
  session, the notification service was absent, so libplasma's removal
  notification disappeared without emitting `KNotification::closed`; its
  cleanup callback never committed final destruction.
- FIX: Storage treats destroyed containments and their owned subcontainment
  trees as persistence tombstones and validates the retained projection before
  replacing the prior config group. GenericLayout owns a per-containment
  fallback transaction for Plasma's same 60-second Undo window; Undo, a newer
  request, and final destruction cancel the old timer.
- EVIDENCE: storage, identity, and libplasma signal-order contracts pass. The
  seeded nested stress observed the removed member leave runtime and disk,
  recreated a direct-root member, and restored exactly the five intended docks
  after restart.

### D98 - Dock-system sizing diagnostics read the edit controller
- STATUS: FIXED in PR #113 (`bfee4170e`).
- FOUND: 2026-07-22, exact linked-dock sizing reproduction.
- SYMPTOM: every settled dock reported null `availablePrimaryLength`, so the
  first cross-dock sizing transition could not be attributed.
- ROOT: `dockSystemData` read `maxLength` from the containment edit root. The
  orientation-specific value consumed by AutoSize belongs to that view's live
  Metrics object.
- FIX: expose the Metrics value and collect both effective icon size and
  available primary length from that same per-view sizing authority.
- EVIDENCE: the D-Bus schema and production source contract pass. The linked
  acceptance reports non-null independent sizing values for every member and
  keeps the portrait vertical member unchanged while the bottom root changes
  alignment.

### D99 - Programmatic applet creation did not notify linked members
- STATUS: FIXED in PR #113 (`4b9bbb1ba`).
- FOUND: 2026-07-22, Create Linked Dock acceptance.
- SYMPTOM: adding an applet by plug-in ID changed only the addressed dock even
  though every linked member subscribed to `appletCreated`.
- ROOT: the successful creation path never emitted the declared relationship
  signal.
- FIX: split announcing external creation from local coordinator fanout. The
  first path emits once; member copies use the local-only path and cannot feed
  the event back into the group.
- EVIDENCE: the source contract pins the single announcing boundary. Nested
  KWin observed the same plug-in multiset and disjoint applet IDs in the root
  and both members.

### D100 - Startup cleanup deleted explicitly placed linked docks
- STATUS: FIXED in PR #113 (`8adc09a88`).
- FOUND: 2026-07-22, first explicit-linked persistence reload.
- SYMPTOM: linked containment records were correct on disk, but restart removed
  the explicitly placed members before their views were constructed.
- ROOT: cleanup treated every `isClonedFrom` record as disposable
  screen-group fanout. Enumeration order could also construct a member before
  its relationship root.
- FIX: only `ScreenGroupDerived` members are regenerated and cleaned. Explicit
  members remain ordinary screen-map participants, and startup stable-partitions
  roots before members.
- EVIDENCE: storage fixtures and both nested linked recipes restore direct-root
  members with the same persistent IDs, outputs, edges, and distinct runtime
  objects.

### D101 - Rapid placement changes lost relocation ownership
- STATUS: FIXED in PR #113 (`15dbcbea1`).
- FOUND: 2026-07-22, seeded linked-dock operation storm.
- SYMPTOM: a second move could remain forever in relocation animation, or the
  model could report its new output and edge while local geometry still
  described the previous orientation.
- ROOT: an older reveal animation's `onStopped` cleared the newer hide
  transaction. Delayed completion callbacks also carried no placement
  generation, and `inRelocationAnimation` excluded the deferred screen and
  geometry reconciliation queues.
- FIX: stop the old reveal before claiming a new hide, version every placement
  request, reject superseded completion callbacks, and expose requested versus
  applied generations plus `geometrySettled` after every owned queue drains.
- EVIDENCE: identity and D-Bus contracts pass. Seed 127934575 completed 28
  placements and seven edit transitions, then held all geometry and sizing
  fields unchanged for two seconds before an equivalent persistence reload.

### D102 - Viewless containments missed the removal fallback
- STATUS: FIXED in PR #113 (`5353a9e94`).
- FOUND: 2026-07-22, removal-ownership review after D83.
- SYMPTOM: a removed embedded containment could remain transient forever when
  the notification backend failed, even though dock containments retired.
- ROOT: GenericLayout armed its notification-independent commit only inside the
  `Latte::View` parking branch. Embedded subcontainments are registered but have
  no dock View.
- FIX: every registered containment owns the fallback transaction. Only the
  view-map parking remains conditional.
- EVIDENCE: the source contract requires the timer arm after the view-only
  branch; production compiles and the focused identity contract passes.

### D103 - Linked-dock controls escaped the settings inventory
- STATUS: FIXED in PR #113 (`ba6267def`).
- FOUND: 2026-07-22, canonical gate for Create Linked Dock.
- SYMPTOM: `settingsinventorytest` rejected the new linked popup, output and
  edge selectors, context-menu target actions, and changed action-model
  signatures. The dialog's accept and cancel lifecycle was absent from the
  source candidates entirely.
- ROOT: the feature added the first ordinary `Dialog` under the scanned
  settings roots, but the scanner classified only `ColorDialog` as
  interactive. The feature also changed exact action signatures without adding
  their new semantic rows to the checked ledger.
- FIX: classify ordinary dialogs as interactive and catalog all seven new
  linked-view affordances, including accept and cancel. Exact coverage now maps
  278 affordances to 742 candidates through 1274 relations.
- EVIDENCE: the scanner unit test proves that a dialog and its accepted handler
  become candidates. `settingssourcescannertest` and `settingsinventorytest`
  pass with the expanded checked counts.

### D104 - Linked member mutations depended on applet position
- STATUS: FIXED in PR #113 (`7d4245f80`).
- FOUND: 2026-07-22, mandatory cold review of Create Linked Dock.
- SYMPTOM: an add, drop, remove, reorder, or configuration edit originating in
  a linked member could update only that member or address the wrong root applet
  after local order changed.
- ROOT: structural entry paths bypassed the runtime View relationship boundary,
  and member-to-root translation fell back to positional order instead of the
  established applet identity map.
- FIX: every structural mutation enters through the addressed View, resolves to
  the direct root, and fans out through stable root-to-member ID mappings with
  feedback guards. A member-local ID is never treated as a root ID.
- EVIDENCE: the source contract pins all QML, context-menu, D-Bus, add, drop,
  remove, reorder, and configuration boundaries. The dual-output acceptance
  drives each operation from the root and every member and observes equal
  plug-in order with disjoint local IDs.

### D105 - Programmatic applet order changes were not published
- STATUS: FIXED in PR #113 (`c90721575`).
- FOUND: 2026-07-22, mandatory cold review of Create Linked Dock.
- SYMPTOM: a linked reorder could settle visually in the addressed containment
  without notifying the root coordinator on a non-Justify layout.
- ROOT: the low-level layout method updated the order property but emitted the
  relationship-facing order signal only from the pointer-driven path.
- FIX: publish every successful programmatic order change through the same
  signal. Coordinator feedback guards prevent the projection from looping.
- EVIDENCE: the identity source contract requires publication from the
  low-level setter, and the all-members acceptance holds equal plug-in order.

### D106 - Malformed linked graphs reached startup construction
- STATUS: FIXED in PR #113 (`3e89143fb`).
- FOUND: 2026-07-22, mandatory cold review of Create Linked Dock.
- SYMPTOM: a missing root, member chain, cycle, duplicate persistent ID, or bad
  placement policy could reach startup as a plausible partial relationship and
  a null root cast.
- ROOT: D-Bus observation validated the complete table, but startup constructed
  runtime views record by record without applying the same graph contract.
- FIX: validate the full persisted ViewsTable before constructing any runtime
  member and refuse the layout with the concrete validation error.
- EVIDENCE: storage fixtures cover valid direct roots and every rejected graph
  shape; the production source contract pins validation before construction.

### D107 - Linked applet removal left member projections persistent
- STATUS: FIXED in PR #113 (`8e9540f64`, `8d341260b`).
- FOUND: 2026-07-22, real-notification Undo acceptance added after mandatory
  cold review.
- SYMPTOM: removal from a linked member created or mirrored independent Plasma
  transactions. Shutdown inside the root Undo window could resurrect the
  member applet from its still-present configuration group.
- ROOT: scheduled-destruction state was mirrored as if it were ordinary applet
  configuration. It is transaction ownership, and only the root notification
  was authoritative for the logical content operation.
- FIX: the root owns one reversible Plasma transaction. Member projections are
  destroyed immediately; Undo recreates them from the live root with fresh
  member-local IDs and copied configuration.
- EVIDENCE: the dual-output recipe removes through a member, invokes the fake
  freedesktop notification service's real Undo action, removes again, restarts
  inside the Undo window, and rejects runtime or persisted resurrection in
  either containment.

### D108 - Single-layout dock Undo lacked a complete restoration source
- STATUS: FIXED in PR #113 (`c758f08a4`, `9748aa152`).
- FOUND: 2026-07-22, executable dock-removal Undo acceptance.
- SYMPTOM: the forward persistence tombstone correctly removed a linked member,
  but Undo could restore only a partial containment group and lose relationship,
  placement, subcontainment, or applet state on the next reload.
- ROOT: the deleted layout-file subtree was the only complete persistence
  source. Plasma revived in-memory objects but did not reconstruct that subtree.
- FIX: snapshot the exact owned subtree before triggering removal, refuse the
  operation if the reversible snapshot cannot be prepared, and replace Plasma's
  partial groups from the snapshot on Undo.
- EVIDENCE: `storagetest` proves partial-group replacement. The nested
  notification recipe restores the same member containment ID and direct-root
  relationship, reloads successfully, and then proves restart tombstones.

### D109 - Linked-dock source changes lacked current copyright attribution
- STATUS: FIXED in PR #113 (`cf1cb6d7c`).
- FOUND: 2026-07-22, mandatory cold review of Create Linked Dock.
- ROOT: six modified source files retained prior authors but omitted the current
  2026 modification copyright line.
- FIX: preserve every existing SPDX line and add Bree Spektor to every C++,
  header, and QML path changed by the branch.
- EVIDENCE: a complete changed-source scan reports no missing attribution.

### D110 - Widget explorer delegate bypassed its mutation injection
- STATUS: FIXED in PR #113 (`627c25008`).
- FOUND: 2026-07-22, final canonical gate for Create Linked Dock.
- SYMPTOM: the Add Widgets accessibility press action stopped adding an applet,
  and the strict-on-touch qmllint ratchet increased in three changed files.
- ROOT: the relationship-aware add path made the reusable AppletDelegate read
  the production-only `latteView` context object directly. Its component test
  intentionally supplies the page contract instead. New injected QML reads were
  also left implicit rather than documenting their context boundary.
- FIX: keep `latteView` at the WidgetExplorer page boundary and expose one
  replaceable `addApplet` interface to every delegate. Document the injected
  edit-overlay and Tasks-plasmoid boundaries for qmllint.
- EVIDENCE: `qmlinteraction` passes all 231 assertions, including the real
  shipped delegate's accessible press action. The canonical `qmllintgate`
  passes at 5,831 curated warnings, one fewer than the full-stage baseline
  before the correction.

### D111 - Linked-root removal was not one reversible transaction
- STATUS: MITIGATED in PR #113 (`39122837c`). Persistent
  corruption is refused. A group-wide removal transaction remains open.
- SEVERITY: KNOWN ISSUE for the first linked-dock release. RELEASE BLOCKER for
  enabling root removal while explicit members remain.
- FOUND: 2026-07-22, final independent review of Create Linked Dock.
- SYMPTOM: removing a relationship root eventually cascaded through
  `OriginalView::cleanClones()`, but each persistent member entered a separate
  containment removal. Plasma Undo owned only the initiating root transaction,
  so a partial replay could restore a root or member without its relationship.
- ROOT: root and explicit members are separate persistent containments, while
  the legacy teardown assumed derived replicas with no independent persistence
  lifetime. No group transaction snapshot or notification owner existed.
- MITIGATION: one ViewsTable predicate identifies only `ExplicitTarget`
  members. The live view, active and inactive layout removal, layouts dialog,
  settings button, and context menu all refuse root removal until those members
  are removed individually. Derived All Screens members remain removable with
  their root. The disabled surfaces explain the required order.
- EVIDENCE: the identity contract pins every refusal before containment
  destruction. The real-notification nested recipe attempts root removal and
  observes the same two live views, direct-root graph, persisted member record,
  and notification delivery count.
- NEXT: implement one relationship snapshot, tombstone, notification owner,
  and Undo restore covering the root and every persistent explicit member.

### D112 - Startup accepted malformed dock identity roles
- STATUS: FIXED in PR #113 (`824a7c8b6`).
- FOUND: 2026-07-22, final independent review of Create Linked Dock.
- SYMPTOM: an alphabetic or zero containment identity could pass full-graph
  validation, and an explicitly placed linked member could also claim a
  multi-output screen group.
- ROOT: relationship validation compared opaque ID strings for duplication and
  graph edges without checking the containment ID domain. Placement validation
  checked enum ranges but not the incompatible combination of `ExplicitTarget`
  with a shared screen group.
- FIX: require canonical positive decimal containment IDs and
  `SingleScreenGroup` for every explicit linked member before runtime view
  construction.
- EVIDENCE: value-layer cases and real KConfig fixtures reject alphabetic,
  zero, leading-zero, and explicit multi-output records. The focused data and
  storage tests pass.

### D113 - Hidden applet remove actions resurfaced in the wrapper
- STATUS: FIXED in PR #113 (`a2270b0ce`).
- FOUND: 2026-07-22, final independent review of Create Linked Dock.
- SYMPTOM: an applet that hid its internal Remove action could receive a visible
  relationship-aware Remove entry in Latte's context menu.
- ROOT: the wrapper copied text, icon, and enabled state, but a new QAction
  defaults to visible and the source visibility was omitted.
- FIX: copy visibility before inserting the wrapper into the menu.
- EVIDENCE: the identity contract requires the exact visibility transfer and
  passes with the focused context-menu source checks.

### D114 - Linked-source removal controls raised the QML warning baseline
- STATUS: FIXED in PR #113 (`dca5067eb`).
- FOUND: 2026-07-22, post-review canonical gate for Create Linked Dock.
- SYMPTOM: `qmllintgate` reported five additional curated unqualified-access
  warnings in `LatteDockConfiguration.qml`.
- ROOT: the new removal policy bindings read shell-provided `latteView` and
  `i18n` context properties without marking that structural boundary for the
  static analyzer.
- FIX: apply the existing context-property annotation to the complete touched
  removal binding block. This also retires three inherited warnings already in
  that block.
- EVIDENCE: the per-file count falls from 94 to 91. The final canonical gate
  passes the 5,828-warning ratchet across 234 eligible QML files.

### D115 - Cross-layout moves could split explicit linked relationships
- STATUS: FIXED in PR #113 (`d485f78c4`, `f72d0c651`).
- FOUND: 2026-07-22, post-review relationship-boundary audit.
- SYMPTOM: a root or explicit linked member could be moved to another layout
  without the rest of its relationship. A stale layouts-dialog Cut could also
  import a destination after the root gained a member, then fail source removal
  and silently become Copy.
- ROOT: the legacy move transaction coordinates only an original and its
  screen-group-derived fanout. Move eligibility was inferred independently at
  UI and runtime entry points, and Cut was not revalidated at the save boundary.
- FIX: one persistent `ViewsTable` predicate governs runtime actions, menus,
  layouts-dialog selection, and `Layouts::Manager`. Save re-reads the current
  origin graph and cancels before any destination import if the relationship
  changed after Cut.
- EVIDENCE: value tests cover independent, explicit, and derived-only roles.
  The production contract pins final revalidation before both import paths.

### D116 - Runtime root replacement stranded or deleted linked members
- STATUS: FIXED in PR #113 (`49e22845c`, `f72d0c651`).
- FOUND: 2026-07-22, final independent review and executable recreation pass.
- SYMPTOM: custom-indicator runtime reload replaced only the root, leaving live
  members with null root pointers. Runtime root teardown could also remove
  persistent members through destructor cleanup.
- ROOT: runtime and persistent relationship lifetimes shared the same
  `cleanClones()` path, while recreation tracked only one containment.
- FIX: runtime destruction is nonpersistent. Root recreation collects the
  complete live relationship, queues members before the root, reconstructs the
  root first, rebinds eligible members, and reconciles only after the complete
  group exists. The reload driver is gated by `LATTE_DEBUG_DBUS=1`.
- EVIDENCE: the two-output recipe observes fresh runtime IDs for the root and
  every linked member, unchanged persistent IDs and content, and an unchanged
  runtime ID for the independent Duplicate.

### D117 - Output disconnect remapped a linked member to primary
- STATUS: FIXED in PR #113 (`72006f07c`, `f72d0c651`).
- FOUND: 2026-07-22, executable output disconnect and reconnect pass.
- SYMPTOM: disabling the portrait output left its explicit linked member live
  and remapped to the primary output instead of parking it.
- ROOT: output eligibility read live View or layout-overlaid containment screen
  state. Qt and Plasma temporarily reported the surviving primary while the
  removed output's window was being retired, overwriting placement authority.
- FIX: pending explicit placement wins while a transaction is active;
  otherwise eligibility reads persisted containment KConfig directly. Runtime
  screen fallback is never an ownership source. Delayed recreation rechecks the
  same eligibility before constructing a view.
- EVIDENCE: disconnect removes the member runtime but preserves its exact
  `isClonedFrom`, output, edge, and containment record. Reconnect restores it to
  the separated portrait output.

### D118 - Offline linked members missed root applet changes
- STATUS: FIXED in PR #113 (`b76ed462a`, `f72d0c651`).
- FOUND: 2026-07-22, executable output reconnect pass.
- SYMPTOM: a member recreated after its output returned retained stale applet
  structure or configuration from before disconnect.
- ROOT: live signals had no recipient while the runtime was absent, and
  initialization could complete before AppletQuickItem configuration maps were
  ready. No event guaranteed a later full projection comparison.
- FIX: member feedback pauses behind an event-driven reconciliation barrier.
  Once both endpoint inventories are ready, stale projections are pruned,
  missing ones receive fresh local IDs, shared configuration is copied, and
  order plus per-applet lists are reapplied.
- EVIDENCE: the root gains applets while the remote member is offline.
  Reconnect converges exact plug-in order and shared KConfig values with
  disjoint applet IDs.

### D119 - Linked applet length crossed orientation boundaries
- STATUS: FIXED in PR #113 (`b76ed462a`, `f72d0c651`).
- FOUND: 2026-07-22, exact cross-orientation content convergence pass.
- SYMPTOM: changing a horizontal dock could overwrite the Tasks applet length
  in a linked vertical dock and force a different internal icon fit.
- ROOT: ConfigOverlay writes `length` from the local width or height resize
  handle, but blanket linked configuration forwarding treated that geometry as
  shared content. Dock-level icon and available-length metrics remained stable;
  applet `length` was the first divergent value.
- FIX: a compile-time policy classifies `length` as per-view. Linked template
  import, both live signal directions, Undo restoration, and reconnect
  reconciliation exclude it. Independent Duplicate Dock still copies it as
  part of its unrelated snapshot.
- EVIDENCE: the portrait member's local length stays stable through root
  alignment, shared launcher edits, output reconnect, and whole-group runtime
  recreation while launcher configuration continues to synchronize.

### D120 - Copy preserved stale linked lineage
- STATUS: FIXED in PR #113 (`c135664b1`, `02809355a`,
  `b1c6d0573`).
- FOUND: 2026-07-22, final independent linked-dock review.
- SYMPTOM: copying an explicit linked member through the layouts dialog could
  paste a record whose root was absent or was an unrelated dock with the same
  numeric ID. Restart could reject the destination relationship graph.
- ROOT: Copy exported one selected containment but retained `isClonedFrom`,
  `ExplicitTarget`, and the transient Cut/Paste move flags. Storage reapplied
  the relationship metadata after remapping local containment and applet IDs;
  a copied unsaved move destination could also masquerade as a later Cut.
- FIX: normalize every Copy record through `toIndependentSnapshot()` before
  clipboard publication. The value operation clears linked lineage and both
  move-transaction flags. Cut alone retains checked origin identity.
- EVIDENCE: the identity contract pins normalization before clipboard
  publication; the value contract proves ordinary configuration survives while
  relationship and move roles reset. `datatypestest` passes 47/47 and
  `dockidentitycontracttest` passes 24/24.

### D121 - Late move refusal left relocation pending
- STATUS: FIXED in PR #113 (`427b97d68`, `02809355a`).
- FOUND: 2026-07-22, final independent linked-dock review.
- SYMPTOM: if a dock relationship gained an explicit member after relocation
  hiding began, the manager refused the now-partial cross-layout move but the
  dock remained hidden with a pending layout forever.
- ROOT: `Manager::moveView()` returned no result. The positioner cleared its
  pending layout only after `layoutChanged`, which a refused move never emits.
- FIX: the manager returns checked success and refuses before unassignment. A
  failed positioner move clears every pending placement component and schedules
  the ordinary delayed reveal and generation settlement path.
- EVIDENCE: the pinned build completes. The identity contract requires refusal
  before unassignment and full pending-state cancellation before reveal.

### D122 - Same-edge edit canvas retarget lost layer anchors
- STATUS: FIXED in PR #115 (`c4cdd03b2`).
- FOUND: 2026-07-22, live vertical-dock edit-mode acceptance.
- SYMPTOM: the edit header for a left dock appeared near the middle of the
  output even though the dock and its reported canvas geometry stayed on the
  left edge.
- ROOT: shared config chrome clears its old layer-shell anchors during a
  retarget. `CanvasConfigView::syncGeometry()` returned early when the target
  canvas rectangle matched its cache. Separate same-edge docks legitimately
  have identical rectangles, so the cache suppressed both compositor
  placement and the target view's input-mask refresh. KWin centered the
  unanchored vertical surface.
- FIX: reassert Wayland canvas placement and the view-owned input mask on every
  synchronization. Keep rectangle equality only as the condition for resize
  work and the non-Wayland `setPosition()` path.
- EVIDENCE: the live dock reported `[1440,425,140,1440]` while KWin mapped the
  surface at `[2650,425,140,1440]`. The pre-fix nested same-edge retarget
  reported `0,0 146x912` while KWin mapped `727,44 146x912`. Two fresh
  corrected runs mapped the first and retargeted canvases at the reported
  `0,88 146x824`; `layershellmappingtest` also passes.

### D123 - Same-edge regression did not pin the cache key
- STATUS: FIXED in PR #115 (`f7b125f35`).
- FOUND: 2026-07-22, independent review of PR #115.
- SYMPTOM: the edit-canvas regression could pass without exercising the
  unchanged-rectangle branch that caused D122 (same-edge edit canvas retarget
  lost layer anchors).
- ROOT: placing both docks on the left edge was assumed to produce identical
  canvas rectangles, but the recipe never asserted that equality.
- FIX: read both published canvas rectangles after settlement and refuse the
  test unless the complete cache keys are identical before edit mode opens.
- EVIDENCE: two fresh runs report both peers at exactly `0,88 146x824` before
  either canvas mapping is checked.

### D124 - Canvas regression accepted an ambiguous layer surface
- STATUS: FIXED in PR #115 (`f7b125f35`).
- FOUND: 2026-07-22, independent review of PR #115.
- SYMPTOM: a same-sized dock or stale config surface could satisfy the
  compositor assertion before the intended edit canvas was examined.
- ROOT: the recipe selected the first top-layer `latte-dock` surface whose
  width and height matched the reported canvas.
- FIX: require exactly one matching live KWin surface, compare its complete
  geometry, retain its compositor UUID, and require a different UUID after
  the hide-and-remap retarget.
- EVIDENCE: two fresh runs find one exact canvas per pass and observe distinct
  KWin UUIDs across each same-geometry retarget.

### D125 - Failed duplicate discovery leaked fixture state
- STATUS: FIXED in PR #115 (`f7b125f35`).
- FOUND: 2026-07-22, independent review of PR #115.
- SYMPTOM: if duplication succeeded but ID discovery timed out, cleanup could
  stop the fixture with the extra dock still persisted for later recipes.
- ROOT: teardown removed only the discovered `view_b`; an empty discovery
  result discarded ownership of every newly created ID.
- FIX: snapshot all pre-test IDs and remove every ID absent from that snapshot
  during teardown, including partial-failure paths.
- EVIDENCE: teardown recomputes every ID absent from the original snapshot and
  submits each one for removal before the clean fixture stop.

### D126 - Side docks resized from intermediate layout frames
- STATUS: FIXED on `fix/vertical-autosize-animation-tracker` (`e5930c301`).
- FOUND: 2026-07-22, live side-dock acceptance after the edit-canvas repair.
- SYMPTOM: a side dock repeatedly expanded and contracted while top and bottom
  docks remained stable. A 10 Hz atomic snapshot caught the effective icon size
  animating `24 -> 26 -> 28 -> 30 -> 32 -> 30 -> 28 -> 26 -> 24` while the
  691 px available length, 1440 px view window, output, edge, and geometry
  generation remained unchanged.
- ROOT: upstream commit `6a558df10` migrated both axes from the old
  `slotAnimationsNeedLength(1)` counter to the event-owned animation tracker.
  The horizontal branch correctly became `needLength.addEvent`, but the
  vertical branch became `needLength.removeEvent`. Side docks therefore stayed
  in `inNormalState` while their content height was animating. `AutoSize` could
  consume intermediate height frames and feed a new icon-size target back into
  the same layout animation.
- FIX: both axes call one `registerLengthAnimation()` function. It registers the
  layout owner once; the existing settle timer performs the one matching
  removal. This restores the pre-refactor semantics without changing automatic
  sizing thresholds or adding vertical-only tolerances.
- EVIDENCE: the focused source contract rejects the old remove-at-start
  operation on either axis and requires one shared registration paired with one
  settle-time removal. `sourceguardtest`, `qmlcompilegate`, and `qmllintgate`
  pass. The fixed live dock held 24 px through 200 samples spanning edit-mode
  entry and exit plus two hover cycles; only the edit flag changed, and all
  placement geometry remained constant. A fresh staged process remained at the
  same settled size for 180 additional samples. Temporary telemetry was removed.

### D127 - Automatic sizing stranded usable length in modulo-8 buckets
- STATUS: FIXED on `fix/vertical-autosize-animation-tracker` (`eee511c62`).
- FOUND: 2026-07-22, live automatic-size acceptance on horizontal and vertical
  docks.
- SYMPTOM: a dock could remain visibly smaller even though another valid icon
  size fit its Maximum Length. The selected result also depended on the
  configured icon-size ceiling's remainder modulo eight.
- ROOT: the inherited search tested only `current +/- 8` candidates. Each
  configured ceiling therefore searched one remainder class and skipped valid
  integer sizes between buckets.
- FIX: solve the linear projection directly for the largest fitting integer,
  then correct the floating-point boundary by at most one pixel using the real
  inclusive shrink or strict grow comparison.
- EVIDENCE: the pure regression gives identical geometry the same result under
  ceilings 31, 50, 64, 68, and 127. The live-shaped 44 px case selects the
  valid intermediate fit. `autosizeenginetest` passed 23/23 under ASan+UBSan,
  and live docks converged without post-input movement.

### D128 - Task artwork painted smaller than its autosized slot
- STATUS: FIXED on `fix/vertical-autosize-animation-tracker` (`b1d993279`).
- FOUND: 2026-07-22, live inspection after the integer automatic-size search.
- SYMPTOM: the layout could consume a computed non-standard slot while the
  visible task icon remained at the next smaller standard icon size, making
  correctly occupied space look empty.
- ROOT: `Kirigami.Icon.roundToIconSize` rounded task artwork to standard theme
  sizes. A 63 px task slot could paint only 48 px of icon artwork.
- FIX: disable standard-size rounding at the shared task icon and both task
  icon copies that bypass it. The fitted layout slot remains authoritative.
- EVIDENCE: `themeawareicontest` renders a 63 px named icon and requires the
  painted dimensions and both corner pixels to occupy the complete slot.

### D129 - Automatic sizing reserved a full hovered icon
- STATUS: FIXED on `fix/vertical-autosize-animation-tracker` (`25390b5d1`,
  completed by D135 in `d8faf2d49`).
- FOUND: 2026-07-22, live comparison of settled row length, Maximum Length,
  and hover zoom.
- SYMPTOM: automatic sizing could discard roughly one full icon of available
  edge length. A temporary hover presentation could also force the persistent
  resting row smaller.
- ROOT: both fit limits subtracted a complete zoomed item even though the
  settled row already included that item's base extent. The base icon was
  counted twice and hover state participated in shrink decisions.
- FIX: shrink and grow only from settled row geometry. D129 first removed hover
  from the shrink decision but retained an approximate incremental-hover
  reserve for growth. D135 removed zoom from the sizing API entirely and kept
  only two logical pixels of total rounding slack. Prediction history records
  settled geometry.
- EVIDENCE: the live-shaped 1114 px row inside a 1228 px budget grows from 50
  to 55 px; its 1225.4 px stable projection fits the 1226 px growth boundary,
  while 56 px does not. `qmlinteraction` and `autosizeenginetest` pass.

### D130 - Settings bars ignored or stole wheel input
- STATUS: FIXED on `fix/vertical-autosize-animation-tracker` (`711391bb5`).
- FOUND: 2026-07-22, live Appearance settings acceptance with a real mouse.
- SYMPTOM: settings sliders ignored the wheel. A first always-enabled repair
  then changed Screen height while the settings page was being scrolled,
  persisting 2.7 percent and unexpectedly disabling Absolute Size.
- ROOT: every Appearance and Effects slider explicitly disabled Qt's native
  wheel path. Enabling it for unfocused hovered controls made a page-scroll
  gesture mutate configuration.
- FIX: a settings slider accepts native wheel events only after a click gives
  it active focus. Unfocused bars leave the wheel event to the page.
- EVIDENCE: the interaction regression proves page scrolling over an unfocused
  slider is non-mutating, a real handle click arms wheel input, and opposite
  notches apply and restore exactly one declared step. `qmlinteraction` and
  `qmlcompilegate` pass.

### D131 - Screen-relative sizing obscured its meaning and mode
- STATUS: FIXED on `fix/vertical-autosize-animation-tracker` (`0e7693bce`).
- FOUND: 2026-07-22, live recovery from the persisted D130 wheel mutation.
- SYMPTOM: `Relative Size: 2.7%` did not say what the percentage referenced,
  and Absolute Size appeared permanently broken while the mutually exclusive
  mode was active.
- ROOT: the label hid that the percentage used the output's full screen height,
  showed the stored percentage instead of its resolved size, and provided no
  explanation beside the disabled Absolute Size row.
- FIX: name the control Screen height, show the resolved pixel ceiling by
  default, expose the stored percentage on hover, label its sentinel Off, and
  state that turning it off restores Absolute Size.
- EVIDENCE: `appearancehandleraudittest` pins all labels and display semantics;
  `qmlcompilegate` passes. Live D-Bus readback distinguished the affected dock's
  2.7 percent configuration from the other docks' `-1` Off sentinel.

### D132 - Length-control inventory anchors depended on source hashes
- STATUS: FIXED on `fix/vertical-autosize-animation-tracker` (`2e931284d`).
- FOUND: 2026-07-22, first canonical gate after the settings wheel repair.
- SYMPTOM: `settingsinventorytest` rejected the Maximum, Minimum, and Offset
  fine-adjust areas after a behavior-neutral edit changed their anonymous
  parent `RowLayout` hashes.
- ROOT: those semantic controls were identified through hashes of incidental
  parent source text rather than stable QML ids.
- FIX: give all three rows semantic ids and map both their sliders and their
  fine-adjust areas through those anchors.
- EVIDENCE: `settingsinventorytest` passes with every source candidate resolved
  exactly once.

### D133 - Screen-height guidance exceeded the QML lint baseline
- STATUS: FIXED on `fix/vertical-autosize-animation-tracker` (`06df46103`).
- FOUND: 2026-07-22, first canonical gate after adding the Screen height
  explanation.
- SYMPTOM: `qmllintgate` reported that `AppearanceConfig.qml` increased from
  243 to 245 curated warnings.
- ROOT: the new translated instruction added direct inherited-context accesses
  for its text and width in a file still awaiting complete context typing.
- FIX: retain the translated instruction, expose the dialog width through one
  typed page property, and qualify the three touched width bindings through it.
- EVIDENCE: `appearancehandleraudittest`, `qmlcompilegate`, and `qmllintgate`
  pass with the previous exact per-file warning count.

### D134 - Autosize ignored background end padding
- STATUS: FIXED on `fix/vertical-autosize-animation-tracker` (`71a8081ab`).
- FOUND: 2026-07-22, live side-dock acceptance at 100 percent Maximum Length.
- SYMPTOM: the side dock chose an overly large stable icon size and clipped its
  rounded background at both ends. Its 1240 px canvas carried an effects
  rectangle from y=-14 through y=1254.
- ROOT: AutoSize compared the applet row with raw `maxLength`, but the
  background added primary-axis end padding outside that row. The layouter
  already calculated the correct post-padding content budget, but the solver
  bypassed it.
- FIX: solve against `layouter.contentsMaxLength` on every orientation and
  publish that same authority as `availablePrimaryLength` over D-Bus.
- EVIDENCE: a live-shaped QML regression subtracts 28 px of end padding before
  selecting the largest fit. The rebuilt right dock settled at 54 px with its
  complete y=25, height=1190 effects rectangle inside the 1240 px canvas.
  `qmlinteraction`, `autosizeenginetest`, `dockidentitycontracttest`,
  `qmlcompilegate`, and `qmllintgate` pass.

### D135 - Hover presentation reduced the stable autosize fit
- STATUS: FIXED on `fix/vertical-autosize-animation-tracker` (`d8faf2d49`).
- FOUND: 2026-07-22, independent review of PR #116 and live acceptance of the
  stable-resting-layout requirement.
- SYMPTOM: the D129 growth repair still left usable resting space because it
  reserved one icon's approximate incremental zoom extent. That proxy neither
  described the complete parabolic curve nor matched the intended persistent
  sizing semantics.
- ROOT: `AutoSizeInput` treated transient hover presentation as ownership input
  for the persistent applet-row fit.
- FIX: remove zoom from the stepper API and pure-core input. Both shrink and
  grow solve only the settled row, with one logical pixel of rounding margin at
  each primary-axis end.
- EVIDENCE: the live-shaped 965 px row grows from 44 to 55 px, and the 1114 px
  row grows from 50 to 55 px. Both largest-fit cases stop before their next
  integer projection crosses the stable boundary. The 227-case QML interaction
  suite and `autosizeenginetest` pass.

### D136 - Padding changes left autosize on a stale budget
- STATUS: FIXED on `fix/vertical-autosize-animation-tracker` (`4387f0210`).
- FOUND: 2026-07-22, independent review of PR #116.
- SYMPTOM: changing background margins, rounding, indicators, or theme extents
  after settlement could leave the previous automatic icon size in place.
- ROOT: AutoSize consumed `layouter.contentsMaxLength` but listened only for
  changes to outer `containment.maxLength`. The content budget can change while
  that outer span remains constant.
- FIX: observe `contentsMaxLength` directly and defer refitting through the
  existing normal-state and animation gates so dependent geometry bindings
  settle first.
- EVIDENCE: the shipped-ability integration test settles at 63 px, changes only
  end padding to shrink to 60 px, then releases the padding and regrows to
  63 px without changing `containment.maxLength`. The QML interaction suite
  passes.

### D137 - D-Bus references described stale raw-length semantics
- STATUS: FIXED on `fix/vertical-autosize-animation-tracker` (`b18a3c0cf`).
- FOUND: 2026-07-22, independent review of PR #116.
- SYMPTOM: both public D-Bus references said `availablePrimaryLength` was raw
  containment `maxLength` after D134 changed the live authority.
- ROOT: the observability implementation and contract test changed with D134,
  but its prose references did not.
- FIX: define the field as layouter `contentsMaxLength`, the applet span after
  primary-axis background end padding is removed.
- EVIDENCE: both public interface references now match the QML binding and the
  source contract that rejects the old edit-controller `maxLength` read.

### D138 - Sub-floor icon ranges entered the autosize core
- STATUS: FIXED on `fix/vertical-autosize-animation-tracker` (`eb7168c`).
- FOUND: 2026-07-22, independent review of PR #116.
- SYMPTOM: positive sizes below the 16 px floor, current sizes above their
  configured ceiling, and invalid applied-size state reached the pure search.
  Some were normalized into plausible output instead of identifying the caller
  defect.
- ROOT: the QML boundary checked positivity but not the engine's complete range
  invariant.
- FIX: refuse the invalid external measurement with a complete `qCritical`
  state dump and assert the same floor, ceiling, and applied-size contract at
  the pure core.
- EVIDENCE: the staged QML shell rejects sub-floor current and ceiling values,
  an above-ceiling current value, and applied values outside the valid range.
  `qmlinteraction` and `autosizeenginetest` pass.

### D139 - Touched inherited QML omitted adaptation attribution
- STATUS: FIXED on `fix/vertical-autosize-animation-tracker` (`2c4e99430`).
- FOUND: 2026-07-22, independent review of PR #116.
- SYMPTOM: `LayoutsContainer.qml` and `EffectsConfig.qml` were modified on the
  branch without recording the current adaptation copyright.
- ROOT: the functional edits preserved the inherited authors but omitted the
  additional attribution required for modified files.
- FIX: add the current adaptation line beside every preserved original author.
- EVIDENCE: both touched headers retain their inherited copyright lines and add
  the 2026 Bree Spektor line.

### D140 - Zoomed side-dock chrome clipped at both ends
- STATUS: FIXED on `fix/vertical-autosize-animation-tracker` (`1228ecf8c`,
  `d19a1805c`, `921bf089b`, `a0ab006f8`), with its transient-span ownership
  corrected by D150.
- FOUND: 2026-07-22, live first-and-last-icon zoom acceptance on a side dock.
- SYMPTOM: a centered 1240 px side surface expanded its solid effects rectangle
  to y=-34, height=1307 during parabolic zoom. Bounding only that solid
  rectangle to the surface still cut off the drop shadow at both ends.
- ROOT: the background added its resting end padding to the transiently expanded
  applet row, then applied the unconstrained parabolic centering offset. The
  shadow item adds separate length-axis margins outside the solid rectangle, so
  a solid rectangle that filled the surface still requested an oversized
  complete visual.
- FIX: let transient zoom borrow resting end padding without entering the
  persistent icon-size solver. Reserve the renderer's actual length-axis
  shadow margins, then constrain centered movement using the complete
  background visual and its owning output canvas. D150 corrects the initial
  implementation's mistaken use of the configured resting span as that
  transient boundary. The same primary-axis calculation handles horizontal
  and vertical docks.
- EVIDENCE: after the independent-review corrections, the rebuilt 1240 px side
  surface settles at 54 px with a 1126 px post-chrome applet budget and reports
  a y=25, height=1190 solid rectangle. Each end retains its complete 20 px drop
  shadow plus 5 px slack. The D150 live acceptance additionally pins a
  landscape row expanding from [152,2399] to [54,2499], with its complete
  background expanding from [78,2481] to [20,2540] inside the [0,2560]
  per-output canvas.

### D141 - Bounded background movement shifted the applet row
- STATUS: FIXED on `fix/vertical-autosize-animation-tracker` (`d19a1805c`).
- FOUND: 2026-07-22, independent review of PR #116 after the D140 chrome fit.
- SYMPTOM: clamping a centered background's -34 px parabolic offset to zero
  shifted the applet row by +34 px during end-icon hover.
- ROOT: centered `mainLayout.offset` subtracted parabolic movement from
  `background.offset`. That canceled the original unbounded background motion,
  but fed any later visual clamp delta back into content placement.
- FIX: keep the centered applet row on the configured placement offset. The
  background owns its parabolic presentation movement and clamp independently.
- EVIDENCE: `sourceguardtest` reads the production QML, requires the stable
  content offset, and rejects a controlled mutation that restores the visual
  feedback expression.

### D142 - Stable autosize omitted background shadow margins
- STATUS: FIXED on `fix/vertical-autosize-animation-tracker` (`921bf089b`).
- FOUND: 2026-07-22, independent review of PR #116 after the D140 chrome fit.
- SYMPTOM: AutoSize could accept a resting row against the post-padding span,
  then the background fit shortened the solid rectangle by the additional
  shadow length. Stable content could therefore outgrow its resting chrome.
- ROOT: `layouter.contentsMaxLength` subtracted primary-axis end padding but
  omitted the shadow margins that are also part of stable background geometry.
- FIX: define the authoritative applet span after both padding and shadow
  margins. The existing `contentsMaxLength` observer refits when either inset
  changes; transient zoom remains outside the persistent solver.
- EVIDENCE: the shipped AutoSize integration shrinks 63 px to 60 px when only a
  50 px shadow inset appears, then regrows to 63 px after release. The rebuilt
  side dock settles at 54 px with `availablePrimaryLength=1126` and complete
  chrome inside its 1240 px surface. All 241 QML interaction cases and
  `qmllintgate` pass.

### D143 - Dock-mode Justify bypassed the complete chrome fit
- STATUS: FIXED on `fix/vertical-autosize-animation-tracker` (`a0ab006f8`).
- FOUND: 2026-07-22, independent review of PR #116 after the D140 chrome fit.
- SYMPTOM: a Justify dock returned `root.maxLength` before shadow reservation,
  so its complete visual could remain longer than its owning surface.
- ROOT: Justify had a dedicated early return instead of sharing the dock-mode
  background path. Its alignment states masked the corresponding offset
  ownership rather than expressing the zero-offset contract directly.
- FIX: preserve the composited Plasma-panel path, route every dock alignment
  through the shadow-aware fit, and make Justify's presentation offset zero.
- EVIDENCE: production-source guards reject direct-length and offset bypasses.
  The QML shell pins both full-span Justify and shorter content-driven dock
  requests. `sourceguardtest`, all 242 QML interaction cases, and the improved
  183-warning `MultiLayered.qml` lint baseline pass.

### D144 - Aspect-scaled background shadow clipped side docks
- STATUS: FIXED on `fix/vertical-autosize-animation-tracker` (`b03a68005`,
  `545e79c34`).
- FOUND: 2026-07-22, live first-and-last-item hover acceptance after D140.
- SYMPTOM: the solid background stayed inside the side-dock canvas, but the
  visible drop shadow remained tight against or clipped by the output ends.
  Increasing zoom made the missing end blur easier to see.
- ROOT: Kirigami 6.27 `ShadowedRectangle` expands its scene-graph rectangle by
  `shadow.size` multiplied by the source aspect ratio. A 74 by 1190 vertical
  background with a 20 px configured shadow therefore requested about 322 px
  beyond each length-axis end. Latte reserved the configured 20 px because a
  shadow size is a pixel radius, so the renderer and geometry owner described
  different paint footprints.
- FIX: replace the aspect-scaled background renderer with Qt 6.9
  `RectangularShadow`, a dedicated rounded-shadow item with an exact pixel
  footprint. It remains a sibling behind the background, preserving shadow
  opacity independently of background translucency. The renderer publishes
  its `EffectMetrics` blur-plus-spread margin directly to placement.
- EVIDENCE: the 1240 px live side dock settled at 54 px. First-item hover kept
  the effects rectangle at y=25, height=1190; last-item hover kept it at y=22,
  height=1196. Both complete visuals remain inside the canvas and both captures
  retain visible end shadow. A 5:1 scene-probe fixture pins equal fixed-pixel
  reach and an independent shadow behind a 25 percent opaque background. QML
  interaction tests pin the renderer-owned margin, including the zero-size
  case. Production-source mutations reject the Kirigami renderer, opacity
  coupling, a missing module import, private padding math, or disconnected
  placement geometry.

### D145 - Translucent backgrounds attenuated custom shadows
- STATUS: FIXED on `fix/vertical-autosize-animation-tracker` (`727f94ded`).
- FOUND: 2026-07-22, mandatory cold review of the D144 renderer replacement.
- SYMPTOM: reducing background opacity also weakened the custom shadow, and a
  fully transparent background erased it. Qt5 Latte kept those values
  independent.
- ROOT: `painter.opacity` is composited after an item layer effect. Applying
  `MultiEffect` as that layer therefore multiplied the rectangle and shadow by
  the same opacity even though the removed sibling renderer did not.
- FIX: use Qt 6.9 `RectangularShadow` as a sibling behind the painter. The
  dedicated item renders only the rounded shadow with an exact pixel footprint,
  so the painter retains its independent opacity binding. Raise the Qt floor to
  the first release that provides this API.
- EVIDENCE: the 5:1 scene probe places a 25 percent opaque rectangle over a
  full-strength red shadow and pins both the halo and center composite. Source
  mutations reject reconnecting the shadow as the painter layer. QML compile,
  interaction, source-guard, and scene-probe gates pass.

### D146 - Zero-size custom shadows reserved empty geometry
- STATUS: FIXED on `fix/vertical-autosize-animation-tracker` (`166342ca1`).
- FOUND: 2026-07-22, mandatory cold review of the D144 renderer replacement.
- SYMPTOM: a valid zero-pixel custom shadow reserved two pixels at every
  eligible edge even though the renderer was disabled.
- ROOT: background placement reused `MultiEffect` padding math, including its
  two-pixel post-blur guard, instead of the background renderer's own footprint.
- FIX: publish the live `RectangularShadow` blur-plus-spread margin through
  `CustomBackground` and consume that value in `MultiLayered`. Zero blur and
  zero spread now produce zero paint and zero placement margin.
- EVIDENCE: QML interaction coverage pins 20 px to 20 px and 0 px to 0 px.
  Production-source guards pin the renderer-owned route and reject disconnected
  placement or missing metrics imports.

### D147 - Shadow renderer cleanup improved the QML warning ratchet
- STATUS: FIXED on `fix/vertical-autosize-animation-tracker` (commit below).
- FOUND: 2026-07-22, first canonical gate after the D145 and D146 review fixes.
- SYMPTOM: all other registered tests passed, but `qmllintgate` rejected a
  two-warning improvement in `MultiLayered.qml` because its exact baseline still
  recorded 183 curated warnings.
- ROOT: removing the obsolete Kirigami availability predicates eliminated two
  unqualified references without shrinking the per-file warning ledger.
- FIX: reduce only the `MultiLayered.qml` baseline entry from 183 to 181.
- EVIDENCE: the focused gate matches 151 files with 5817 curated warnings. The
  final canonical rerun provides whole-tree evidence.

### D148 - Shadow regressions bypassed production ownership guards
- STATUS: FIXED on `fix/vertical-autosize-animation-tracker` (`3d775a0a2`).
- FOUND: 2026-07-22, second cold review of the D145 and D146 corrections.
- SYMPTOM: the render and metric tests exercised `BackgroundShadow` directly,
  while the production matcher did not require the `CustomBackground` sibling
  order, opacity independence, or live paint-margin alias. Regressions in those
  integration bindings could pass.
- ROOT: the source guard checked renderer selection and downstream consumption
  but omitted three properties that stitch the renderer into production.
- FIX: parse the production shadow block, require it behind the painter with no
  opacity binding, and require `CustomBackground.shadowPaintMargin` to remain an
  alias of the renderer value.
- EVIDENCE: controlled mutations for opacity coupling, front stacking, and a
  constant replacement of the alias are all rejected by `sourceguardtest`.

### D149 - Qt 6.9 floor stopped at CMake
- STATUS: FIXED on `fix/vertical-autosize-animation-tracker` (`b8f492b01`).
- FOUND: 2026-07-22, second cold review of the D145 renderer correction.
- SYMPTOM: CMake required Qt 6.9 for `RectangularShadow`, but every native
  package recipe and several current installation or CI references still
  permitted Qt 6.6.
- ROOT: the API-floor change updated the build-system authority and one distro
  plan heading without auditing its packaging and documentation consumers.
- FIX: propagate Qt 6.9 through Arch, Debian, RPM, Gentoo, and Void build and
  runtime constraints, generated package metadata, current container notes,
  both CI prompts, the distro plan, and the public requirements.
- EVIDENCE: no current Qt 6.6 floor remains under packaging, CI, prompts, tests,
  or README. All shell-form package recipes pass syntax checks; the final
  canonical gate supplies the repository-wide package-contract evidence.

### D150 - Hovered applet row escaped its resting background
- STATUS: FIXED on `fix/vertical-autosize-animation-tracker` (`3219a1761`,
  `45092dca8`).
- FOUND: 2026-07-23, live landscape-dock acceptance after the side-dock shadow
  corrections.
- SYMPTOM: zooming the first or last item moved the clock and end applets
  outside the rounded background even though the complete applet row remained
  inside its output.
- ROOT: the D140 correction reused `root.maxLength`, the stable resting-layout
  budget, as the transient background clipping plane. The applet row correctly
  kept hover presentation out of stable autosize, but the background could not
  follow that presentation beyond the resting span.
- FIX: keep `maximumLength` as the stable autosize and Justify contract. For a
  content-driven dock, derive the transient background request from the live
  row and bound its complete visual against the view's own primary-axis canvas
  after reserving renderer-owned shadow margins. The calculation remains local
  to each view and output, including unrelated portrait and landscape outputs.
- EVIDENCE: the live landscape dock changes from row [152,2399] and background
  [78,2481] at rest to row [54,2499] and background [20,2540] under hover,
  all inside canvas [0,2560]. The presentation oracle rejects the captured bad
  shape, row [54,2499] against background [225,2335], and accepts the corrected
  shape. C++ and QML cases pin resting, expanded, capped, and vertical inputs.

### D151 - Nested hover preview did not exercise parabolic expansion
- STATUS: OPEN on `fix/vertical-autosize-animation-tracker`.
- FOUND: 2026-07-23, deterministic presentation-coverage work for D150.
- SYMPTOM: the nested `parabolic-hover-preview` recipe mapped the expected
  layer-6 preview, but the measured applet span stayed 843 px before and during
  the gesture despite an 80 percent configured zoom.
- ROOT: the recipe treated preview mapping as proof that parabolic expansion
  had rendered. Preview activation and row expansion are separate signals. A
  temporary boundary trace showed the synthetic gesture reaching the task
  `MouseArea.onEntered` with factor 1.59375, while neither
  `parabolicEntered` nor `parabolicMove` arrived from the view bridge.
- REQUIRED FIX: make the nested vehicle exercise observable parabolic
  expansion through the production view-motion bridge, then require a larger
  transient row before applying the background-coverage oracle. Preserve a
  screenshot and geometry payload on failure.
- EVIDENCE: repeated synthetic glides mapped the preview while reporting
  `843 -> 843`. Instrumentation recorded `basic onEntered 3 1.59375 null` and
  no corresponding parabolic entry or move. The D150 pure and live-state
  oracles remain valid, but this nested recipe does not yet provide
  deterministic rendered-zoom coverage.

### D152 - Linked portrait dock overflowed with automatic sizing off
- STATUS: OPEN on `fix/vertical-autosize-animation-tracker`.
- FOUND: 2026-07-23, all-view live presentation watcher introduced for D150.
- SYMPTOM: the linked dock on the 1440 px portrait output paints a stable
  applet row beyond both ends of its canvas. The saved workspace image shows
  the clock-side and trailing items cropped.
- ROOT: the linked views correctly share a configured 106 px icon size, but
  disabling automatic sizing also bypasses a per-view safety fit. The
  landscape member can consume that size while the shorter portrait member
  applies it unchanged as its effective size.
- REQUIRED FIX: keep the linked configured size authoritative while deriving a
  bounded effective size for each runtime view and output. Turning automatic
  growth off must not permit an unrenderable stable layout.
- EVIDENCE: `dockSystemData` and `viewAppletsData` report content
  [-408,1839], background [20,1420], and canvas [0,1440] for persistent dock
  12. `watch-dock-presentation.sh` rejected all four escaped boundaries and
  preserved the D-Bus payloads plus the workspace screenshot.

### D153 - Partial bottom reservation moved a separated side dock
- STATUS: FIXED locally on `fix/vertical-autosize-animation-tracker`
  (`25c74a6a3`, `6608a1d39`); real-layout visual acceptance is pending.
- FOUND: 2026-07-23, live comparison of a partial bottom dock in Always
  Visible and dodge visibility modes beside a right dock.
- SYMPTOM: enabling Always Visible on the partial-width bottom dock shortened
  and moved the right dock upward even though the two stable dock rectangles
  did not intersect. Dodge mode restored the right dock to the output bottom.
- ROOT: three ownership errors stacked. KWin applies each positive layer-shell
  exclusive zone to one rectangular per-output work area, so attaching the
  zone to the visual dock surface also placed that visual inside an
  output-wide bottom band. Latte independently reconstructed an occupied
  footprint from the larger masked QWindow instead of consuming the already
  solved stable background rectangle. `View::updateAbsoluteGeometry()` then
  compared the new rectangle after assignment, suppressing ordinary peer
  notifications when the occupied footprint changed.
- FIX: make `absoluteGeometry` the sole occupied-footprint authority and
  publish its changes before perpendicular peers solve again. Keep every Latte
  visual surface at Positioner's exact per-output rectangle with layer-shell
  zone -1. Publish ordinary client work-area reservation through a separate
  transparent, inputless one-pixel surface whose length matches the occupied
  span. `dockSystemData` schema 3 reports the requested state of both surfaces.
- EVIDENCE: the pure region case keeps a right rectangle at
  [1512,0,88,1000] beside bottom occupancy [378,912,844,88]. The 061 nested
  KWin replay passed three times with the actual right surface unchanged at
  [1216,0,384,1000] across the visibility transition. The existing 060
  geometry-agreement replay also passes.

### D154 - Dock resize speed varied with slider distance
- STATUS: FIXED locally on `fix/vertical-autosize-animation-tracker`
  (`ee405a940`); real-layout visual acceptance is pending.
- FOUND: 2026-07-23, live Absolute Size slider acceptance.
- SYMPTOM: changing the size by a large amount animated at a visibly different
  rate from a small change, and repeated large changes produced jitter.
- ROOT: `iconSize` used a fixed-duration `NumberAnimation`, so distance
  directly changed pixels per second. Length margin, thickness margins, and
  padding each owned another animation whose target derived from the changing
  icon value, causing several nested animations to retarget every frame.
- FIX: give icon size one velocity-preserving `SmoothedAnimation`. Derive
  margin and padding values directly from that animated authority instead of
  starting dependent animations.
- EVIDENCE: the source contract rejects fixed-duration icon resizing and
  dependent margin or padding animations. The QML compile gate and all 244
  interaction tests pass. Removing the redundant behaviors reduces
  `MetricsPrivate.qml` from 18 to 16 curated qmllint warnings.

### D155 - Small icons doubled the theme background minimum
- STATUS: FIXED locally on `fix/vertical-autosize-animation-tracker`
  (`2322b0349`); real-layout visual acceptance is pending.
- FOUND: 2026-07-24, live vertical dock sizing at 24, 26, and 28 px.
- SYMPTOM: the dock background became extremely thick at 24 and 26 px, then
  abruptly returned to normal at 28 px.
- ROOT: the inherited `MultiLayered.qml` formula added the complete item row to
  the theme minimum while that row stayed at or below the minimum. Once the row
  crossed the threshold it began subtracting the minimum first, so increasing
  icon size could make the background shrink by one complete theme-minimum
  unit.
- FIX: interpolate only the row's nonnegative excess above the theme minimum.
  One constexpr pure core now owns the calculation for current and maximum
  metrics, with validated QML boundary inputs.
- EVIDENCE: the exact 24, 26, 28, and 30 px transition, every integer row size
  from 0 through 64, five configured fractions, invalid boundary inputs, and
  controlled production-QML mutations pass in `backgroundstatetest` and
  `sourceguardtest`.

### D156 - Layouts submenu collapsed to its radio-button column
- STATUS: FIXED locally on `fix/vertical-autosize-animation-tracker`
  (`16baf03c1`); real-menu visual acceptance is pending.
- FOUND: 2026-07-24, live Latte context menu.
- SYMPTOM: opening Layouts showed only the radio and color controls in a narrow
  strip; layout names were not visible.
- ROOT: `LayoutMenuItemWidget` paints its icon and label outside its child
  layout. Qt 6 asks a `QWidgetAction` default widget for `sizeHint()`, but the
  delegate overrode only `minimumSizeHint()`, so the menu measured the child
  radio button instead of all painted content.
- FIX: route both size-hint contracts through one const painted-content
  calculation.
- EVIDENCE: the production delegate test failed before the fix because its
  width did not contain its painted label. It now verifies the complete
  delegate width and the containing menu's adoption of that width.

### D157 - Layouts submenu regression was absent from the coverage ratchet
- STATUS: FIXED locally on `fix/vertical-autosize-animation-tracker`
  (`8daf1f804`).
- FOUND: 2026-07-24, first canonical gate after adding
  `layoutmenuitemwidgettest`.
- SYMPTOM: all 105 CTest entries passed, then the coverage ratchet rejected the
  new target because its committed inventory still contained 104 entries.
- ROOT: the production delegate test was registered with CTest but its exact
  target identity was omitted from the removal-detection baseline.
- FIX: add the target to the sorted coverage inventory and raise the expected
  entry count to 105.
- EVIDENCE: the first gate reached 105 of 105 passing tests before reporting
  the one-entry inventory diff.

### D158 - Same-edge placement notes overstated the OG Latte UI contract
- STATUS: FIXED locally on `fix/vertical-autosize-animation-tracker`
  (`5ff991d8e`).
- FOUND: 2026-07-24, source-history verification of the no-stacking placement
  decision.
- SYMPTOM: the identity record described separated same-edge views as a normal
  upstream workflow.
- ROOT: the runtime's ability to load same-edge records was conflated with the
  ordinary UI contract. Upstream `GenericLayout::freeEdges()` has removed an
  edge after its first view since commit `bbddfd3d48`, so creation and movement
  did not expose that composition as a first-class workflow.
- FIX: describe separated same-edge spans as a deliberate Lattecotta extension.
  OG Latte and Lattecotta both lack inward stack semantics, but only Lattecotta
  intends to support non-overlapping same-edge spans explicitly.
- EVIDENCE: blame and history trace both `freeEdges()` overloads and their
  `edges.removeOne(view->location())` rule to upstream `bbddfd3d48`.

### D159 - Stacking diagnostics claimed an unenforced overlap invariant
- STATUS: FIXED locally on `fix/vertical-autosize-animation-tracker`
  (`707d1778a`, regression assertion `313eedba0`).
- FOUND: 2026-07-24, cold review of the no-inward-stacking contract.
- SYMPTOM: `dockSystemData` said stable spans must not overlap even though the
  same snapshot could contain overlapping views.
- ROOT: `Create Linked Dock` deliberately accepts an occupied edge, while no
  stable-span validator exists yet. The typed negative capability was written
  as if the intended placement invariant were already enforced.
- FIX: report that inward stacking is unsupported and stable-span overlap is
  not yet rejected. Public D-Bus references now state that `available=false`
  is not validation success, and the creation path names validation as missing
  work.
- EVIDENCE: the runtime snapshot reports the corrected reason; source history
  contains no validator or overlap-refusal path at linked creation. The
  serializer test compares the complete public reason, so replacing it with a
  different nonempty claim fails.

### D160 - Same-edge maximum reservation depth was described as implemented
- STATUS: FIXED locally on `fix/vertical-autosize-animation-tracker`
  (`9dcf27dd8`).
- FOUND: 2026-07-24, cold review of the no-inward-stacking contract.
- SYMPTOM: the placement record said separated same-edge members contribute
  only their maximum depth.
- ROOT: each Always Visible view currently publishes its own positive
  layer-shell exclusive zone. KWin processes those surfaces independently, so
  same-edge zones may accumulate. No maximum-depth aggregator exists.
- FIX: retain maximum depth as the intended policy, assign it to the missing
  output-edge reservation aggregator, and keep the gap as a beta blocker.
- EVIDENCE: `VisibilityManager` publishes per-view struts and
  `WaylandLayerShell` applies each view's independent positive zone.

### D161 - Layouts submenu sizing test omitted painted control columns
- STATUS: FIXED locally on `fix/vertical-autosize-animation-tracker`
  (`81fbf1ed3`, odd-height correction `bebe0a9f4`).
- FOUND: 2026-07-24, cold review of the D156 production regression.
- SYMPTOM: a size hint only one pixel wider than the label could satisfy the
  test while still clipping the manually painted radio and icon slots.
- ROOT: the assertion compared the complete hint only with text width.
- FIX: require room for the label, the height-derived radio column, and the
  icon plus both icon length margins. Derive the icon width through the same
  integer arithmetic as production, since an odd style height produces a
  17 px icon rather than a 16 px icon.
- EVIDENCE: the strengthened production delegate test forces an odd-height
  style and passes offscreen under Qt 6.11. The containing menu still adopts
  the resulting hint.

### D162 - Justify applets occupied shadow-only margins
- STATUS: FIXED locally on `fix/vertical-autosize-animation-tracker`
  (`cf50d7845`, cycle correction `4edcd203d`, asymmetric-margin correction
  `6cd8ff860`, mutation correction `3feb54939`); real-layout visual acceptance
  is pending.
- FOUND: 2026-07-24, live top-dock rendering at 22 px icon size.
- SYMPTOM: the first and last applets extended past the solid rounded
  background, so the ends looked clipped and the shadow resembled a second
  plate.
- ROOT: the solid Justify background was fitted inside the configured complete
  visual span, including its two length-axis shadows. `LayoutsContainer`
  continued to use the outer `root.maxLength` for its physical origin and
  length, placing endpoint wrappers in the two shadow-only margins.
- FIX: resolve the background against an independent full-view canvas. Center
  the complete visual, then derive the applet origin and length from the actual
  tail and head shadow margins.
- EVIDENCE: the live solid background remained `[126,18,1189,26]`. The first
  wrapper moved from x=121 to x=131, and the last wrapper moved from
  x=1286..1319 to x=1276..1309. Filtered live logging produced no binding-loop
  warning after the one-way correction. Controlled source mutations that
  restore the outer length, old origin, applet-owned canvas, or equal-shadow
  assumption fail.

### D163 - Native background shadows retained Kirigami alpha compensation
- STATUS: FIXED locally on `fix/vertical-autosize-animation-tracker`
  (`92fab9745`); real-layout visual acceptance is pending.
- FOUND: 2026-07-24, live comparison of the thin top-dock shadow.
- SYMPTOM: the shadow was much darker than the theme color and appeared as a
  detached background behind the dock.
- ROOT: the former Kirigami `ShadowedRectangle` path added 0.336 to the theme
  shadow alpha as an explicit renderer-matching workaround. D145 (background
  shadows used a height-distorting renderer) replaced that renderer with
  `RectangularShadow` but retained the old formula even though the new effect
  consumes its supplied color directly.
- FIX: pass the theme shadow color directly to `RectangularShadow`.
- EVIDENCE: a controlled source mutation that restores the Kirigami formula
  fails. Source, QML compile, QML lint, image-comparison helper, and complete
  scene-probe gates pass. Live comparison retains a soft shadow without the
  detached dark plate.

### D164 - The first D162 correction formed a Justify geometry cycle
- STATUS: FIXED locally on `fix/vertical-autosize-animation-tracker`
  (`4edcd203d`).
- FOUND: 2026-07-24, mandatory cold review of the thin-dock correction.
- SYMPTOM: the corrected endpoint positions could settle live while remaining
  vulnerable to binding loops, stale geometry, or collapse.
- ROOT: the background host filled `layoutsContainer`, while
  `layoutsContainer` read `background.length`. Positive shadows made each
  object's primary-axis length depend on the other.
- FIX: resolve the background's primary axis against the complete view canvas.
  Preserve only the independent perpendicular hide-animation relationship.
- EVIDENCE: a viable mutation restores `anchors.fill: layoutsContainer` and
  fails the production source guard. Filtered live logging produced no
  binding-loop warning after the correction.

### D165 - The first D162 correction assumed equal end shadows
- STATUS: FIXED locally on `fix/vertical-autosize-animation-tracker`
  (`6cd8ff860`).
- FOUND: 2026-07-24, mandatory cold review of the thin-dock correction.
- SYMPTOM: themes with unequal tail and head shadow margins could displace the
  applet row relative to the solid rounded background.
- ROOT: the first correction centered the solid length directly. The complete
  visual is centered instead, and the solid begins after the actual tail
  margin.
- FIX: center the resolved complete visual, add the tail shadow to its origin,
  and subtract the independent tail and head shadows from its length.
- EVIDENCE: the source guard pins both margins independently. A viable mutation
  that substitutes the tail margin for the head margin fails.

### D166 - The first D162 origin mutation produced invalid QML
- STATUS: FIXED locally on `fix/vertical-autosize-animation-tracker`
  (`3feb54939`).
- FOUND: 2026-07-24, mandatory cold review of the thin-dock correction.
- SYMPTOM: the regression test failed after an origin mutation, but the
  replacement expression referenced a variable outside its scope.
- ROOT: the mutation rewrote the two consumer returns instead of the
  authoritative origin property.
- FIX: restore the former compilable `root.maxLength` origin formula at the
  property definition. Add the independent equal-shadow mutation.
- EVIDENCE: the production matcher passes, while both viable semantic
  regressions fail it.

### D167 - Thin-dock tracking used a bare D145 codeword
- STATUS: FIXED locally on `fix/vertical-autosize-animation-tracker`
  (`e8ca33c2f`).
- FOUND: 2026-07-24, mandatory cold review of the thin-dock correction.
- SYMPTOM: the handoff and D163 root used `D145` without its plain-English
  description.
- ROOT: the local defect reference was treated as sufficient context in prose
  that must remain readable without the registry.
- FIX: describe D145 as the background-shadow height-distortion correction at
  first use in both records.
- EVIDENCE: the corrected prose contains the codeword and its description
  together.

### D168 - Thin-dock tracking commit omitted explicit verification evidence
- STATUS: OPEN on `fix/vertical-autosize-animation-tracker`; correct during
  pre-merge history cleanup.
- FOUND: 2026-07-24, mandatory cold review of commit `5318aec02`.
- SYMPTOM: the commit body described what the records contained but did not
  state the focused checks that had passed.
- ROOT: documentation content evidence was mistaken for commit verification
  evidence.
- REQUIRED: preserve the current commit sequence while development continues,
  then rewrite that body before final PR landing to name the source, QML, and
  scene-probe results.
- EVIDENCE: `git show --format=fuller 5318aec02` contains no explicit
  verification paragraph.

### D93 - Duplicate submenu change left a stale settings-inventory identity
- STATUS: FIXED IN PR #109 (`feea7158f`).
- FOUND: 2026-07-22, canonical gate on the rebased identity branch.
- SYMPTOM: `settingsinventorytest` rejected one new `addSeparator()` source
  candidate and one ledger identity that no longer resolved.
- ROOT: making the Duplicate Dock submenu entry conditional also changed its
  preceding structural separator from an unused QAction binding to a direct
  `m_addViewMenu->addSeparator()` call. The exact audited settings inventory
  still named the removed statement shape.
- FIX: replace only that structural exemption identity with the scanner's new
  direct-receiver identity. The inventory coverage and ownership rules remain
  unchanged.
- EVIDENCE: the first full gate passed the other 103 CTest entries and failed
  only this exact ledger mismatch. The focused inventory test and final
  canonical rerun provide the correction evidence.

### D94 - Dock identity tests were absent from the coverage ratchet
- STATUS: FIXED IN PR #109 (`f31d14c49`).
- FOUND: 2026-07-22, second canonical gate on the rebased identity branch.
- SYMPTOM: all 104 CTest entries passed, then the coverage ratchet rejected four
  additions relative to its committed 100-target ledger.
- ROOT: PR #109 registered `dockidentitycontracttest`,
  `ignoredwindowregistrytest`, `retargetrequeststatetest`, and
  `viewactionpolicytest` in CMake without recording those targets in the
  coverage baseline.
- FIX: add all four targets in sorted order and update the exact count to 104.
- EVIDENCE: the focused ratchet passes with 104 CTest entries and 35 paired unit
  headers. The final canonical rerun provides whole-tree evidence.

### D95 - Layouts-dialog Duplicate preserves linked relationship state
- STATUS: FIXED IN PR #109 (`ebb517a67`).
- FOUND: 2026-07-22, mandatory cold review of PR #109.
- SYMPTOM: Duplicate in the layouts dialog could create another linked ensemble
  from an All Screens or All Secondary Screens source, even though Duplicate in
  the live dock created one independent snapshot.
- ROOT: `Views::duplicateSelectedViews()` was a distinct import path that copied
  `Data::View` directly. It never cleared `isClonedFrom` or normalized
  `screensGroup`, so it bypassed `View::createViewFromTemplate()` and its
  independent-import branch.
- FIX: `Data::View::toIndependentSnapshot()` is the single const value
  transformation for relationship breaking. Both live-view and layouts-dialog
  Duplicate paths call it before import. Runtime assertions pin the live import
  precondition without carrying side effects.
- EVIDENCE: `datatypestest` proves the source is unchanged, every unrelated
  field survives, and both relationship fields normalize. The production source
  contract proves both callers transform before import. The final canonical
  gate passed at exact pre-merge head
  `defaa0c7ad1a0e376937bf07f035430ecc977407`.

### D96 - Duplicate settings inventory still claims linked exclusion
- STATUS: FIXED IN PR #109 (`f755d9008`).
- FOUND: 2026-07-22, mandatory cold review of PR #109.
- ROOT: the settings ledger retained the old noncloned precondition and cloned
  exclusion matrix after Duplicate became valid from a linked member.
- FIX: the row now covers original and linked sources and requires one
  independent result with the relationship severed.
- EVIDENCE: `settingsinventorytest` passes with the corrected semantic row.

### D97 - Independent snapshot test ignores transient view fields
- STATUS: FIXED IN PR #109 (`f207d6560`).
- FOUND: 2026-07-22, mandatory second cold review of PR #109.
- SYMPTOM: `datatypestest` claimed that relationship normalization preserved
  every unrelated `Data::View` field, but the main equality assertion could not
  observe five transient fields.
- ROOT: `Data::View::operator==` intentionally excludes `isActive`, both move
  flags, `errors`, and `warnings` because they do not participate in settings
  persistence. Reusing that operator made the snapshot test weaker than its
  stated value-copy contract.
- FIX: seed all five omitted fields and compare them directly on the returned
  snapshot. The persistence-oriented equality assertion remains in place for
  the fields it is designed to cover.
- EVIDENCE: the focused `datatypestest` and the final canonical gate at exact
  pre-merge head `defaa0c7ad1a0e376937bf07f035430ecc977407` pass with the
  direct transient-field assertions.

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

### D76 - Global applet-configure readback marked unrelated docks active
- STATUS: FIXED IN PR #110 (`c11c77ed2`).
- FOUND: 2026-07-21, multi-dock observability code reading.
- SYMPTOM: `viewsData.inConfigureAppletsMode` copied the one global rearrange
  toggle into every dock record. Entering applet configuration on one edited
  dock therefore reported unrelated docks as configuring applets, even though
  containment QML requires both that dock's `editMode` and the global toggle.
- ROOT: `collectViewRecord()` accepted only the global bit and assigned it
  directly. The readback did not apply the per-view QML expression
  `editMode && universalSettings.inConfigureAppletsMode`.
- FIX: derive the compatibility field through one constexpr value-layer helper
  from per-view edit mode and the global bit. Compile-time truth-table checks
  pin all four inputs. A production source guard pins the live collector route
  and proves that restoring the direct global assignment fails.

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
