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
  corrected by D150 (hovered applet row escaped its resting background), D169
  (panel shadows consumed the stable panel and applet span), and D170 (the
  first D169 correction weakened end-hover shadow bounds).
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
  persistent icon-size solver. D150 makes the output canvas the transient
  boundary. D169 keeps shadow paint from shrinking the solid, while D170
  keeps the stable solid as the placement authority and clips external paint
  that cannot fit around it. The same primary-axis calculation handles
  horizontal and vertical docks.
- EVIDENCE: D150 live acceptance pins a landscape row expanding from
  [152,2399] to [54,2499], with its solid background expanding from
  [78,2481] to [20,2540] inside the [0,2560] per-output canvas. D170's
  behavioral core pins the stable solid under shadow changes, and its
  production source guard rejects complete-visual clamping. All 245 QML
  interaction cases pass.

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

### D142 - Stable autosize charged shadow paint against the applet budget
- STATUS: FIXED locally on `fix/vertical-autosize-animation-tracker`
  (`921bf089b`, corrected by D169 (panel shadows consumed the stable panel and
  applet span) at `ae10800dc`).
- FOUND: 2026-07-22, independent review of PR #116 after the D140 chrome fit.
- SYMPTOM: enabling a background shadow could select smaller resting icons even
  though the solid panel and its available applet span had not changed.
- ROOT: the first correction classified external shadow paint as a stable
  content inset and subtracted it from `layouter.contentsMaxLength`.
- FIX: subtract only the solid background's internal primary-axis padding.
  Shadow margins remain presentation paint and cannot trigger a stable refit.
- EVIDENCE: the production QML integration keeps both its applet budget and
  63 px effective icon size unchanged while a 50 px shadow appears and
  disappears. All 245 QML interaction cases pass.

### D143 - Dock-mode Justify charged shadow paint against configured length
- STATUS: FIXED locally on `fix/vertical-autosize-animation-tracker`
  (`a0ab006f8`, corrected by D169 at `ae10800dc`).
- FOUND: 2026-07-22, independent review of PR #116 after the D140 chrome fit.
- SYMPTOM: enabling 42 px end shadows shortened an 84 percent Justify panel by
  84 px, even though its configured length did not change.
- ROOT: the first correction treated the configured maximum as a
  complete-visual budget and removed both external shadow margins before
  resolving the solid background.
- FIX: route every alignment through one solid-background fit against its
  output-owned canvas. Center and Justify compensate asymmetric outer paint
  without changing the centered solid.
- EVIDENCE: the shadow-off and shadow-on live layouts both settle at
  `[115,18,1209,26]` on the 1440 px output. Controlled mutations reject shadow
  subtraction, visual-length placement clamps, and missing asymmetric
  compensation.

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
  content-driven dock, derive the transient solid request from the live row and
  bound it against the view's own primary-axis canvas. D169 keeps external
  shadow paint out of that solid length. D170 preserves the bounded solid for
  every alignment and clips external paint that cannot fit around it. The
  calculation remains local to each view and output, including unrelated
  portrait and landscape outputs.
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
- STATUS: FIXED in PR #118 (`0a4407f30`, `e8adfb96e`, atomic correction
  `63497b3ac`). The earlier
  tracking correction is `9dcf27dd8`.
- FOUND: 2026-07-24, cold review of the no-inward-stacking contract.
- SYMPTOM: the placement record said separated same-edge members contribute
  only their maximum depth.
- ROOT: before FP-1 (the output-edge maximum reservation authority), each
  Always Visible view published its own positive layer-shell exclusive zone.
  KWin processed those surfaces independently, so same-edge zones could
  accumulate. No maximum-depth aggregator existed.
- FIX: one Corona-owned coordinator groups contributions by persistent Latte
  output identity and edge, then publishes exactly one positive zone at the
  maximum requested depth. Visual views keep independent zone -1 surfaces.
- EVIDENCE: the pure ledger matrix covers order, maximum selection, fallback,
  migration, output isolation, and teardown. Nested recipe 061 observes one
  shared publisher, moves a member from output 13 to output 14, restarts, and
  returns to a publisher-free state without an orphan.

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
  `6cd8ff860`, mutation correction `3feb54939`, shadow-ownership correction
  `ae10800dc`).
- FOUND: 2026-07-24, live top-dock rendering at 22 px icon size.
- SYMPTOM: the first and last applets extended past the solid rounded
  background, so the ends looked clipped and the shadow resembled a second
  plate.
- ROOT: `LayoutsContainer` used `root.maxLength` while the Justify background
  used a different, shadow-reduced length. The first correction aligned the
  applets with that smaller solid but preserved the incorrect shadow charge.
- FIX: resolve the background against an independent full-view canvas and use
  its stable solid length for the applet container. External shadows neither
  contribute to that length nor move its origin.
- EVIDENCE: with shadows off and on, the live solid remains
  `[115,18,1209,26]`, its applet span remains `[128,18,1183,26]`, and endpoint
  wrappers remain at x=124 and x=1283..1316. Controlled source mutations that
  restore shadow-derived length, shadow-derived origin, or an applet-owned
  canvas fail.

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
  (`6cd8ff860`, corrected by D169 at `ae10800dc`).
- FOUND: 2026-07-24, mandatory cold review of the thin-dock correction.
- SYMPTOM: themes with unequal tail and head shadow margins could displace the
  applet row relative to the solid rounded background.
- ROOT: the first correction centered the solid directly. Its replacement
  centered the complete visual and derived a smaller solid, which restored the
  shadow-ownership error fixed by D169.
- FIX: center the stable solid and offset its shadow-bearing visual parent by
  half the head-minus-tail margin difference. Unequal paint can then extend
  independently without moving or shrinking the solid.
- EVIDENCE: production-source guards pin the compensation formula on horizontal
  and vertical center and Justify states. Dropping the compensation or clamping
  placement with the complete visual fails.

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
- STATUS: FIXED IN PR #116 (`c9ed2de4b`).
- FOUND: 2026-07-24, mandatory cold review of commit `5318aec02`.
- SYMPTOM: the commit body described what the records contained but did not
  state the focused checks that had passed.
- ROOT: documentation content evidence was mistaken for commit verification
  evidence.
- FIX: preserve the commit sequence and rewrite the body to name the focused
  source guard, QML compile and lint gates, image-comparison helper,
  scene-probe gate, and stable live coordinates.
- EVIDENCE: `git show --format=fuller c9ed2de4b` carries the explicit
  verification paragraph while retaining the original tree.

### D169 - Panel shadows consumed the stable panel and applet span
- STATUS: FIXED IN PR #116 (`ae10800dc`, end-hover corrections `c7c13cf14`
  and `f0d8578f3`); real-layout visual
  acceptance is pending.
- FOUND: 2026-07-24, live top-panel shadow toggle after the thin-background
  correction.
- SYMPTOM: enabling background shadows made a Justify panel visibly shorter.
  The same ownership model also made automatic sizing choose smaller icons.
- ROOT: D142 (stable autosize charged shadow paint against the applet budget)
  and D143 (Dock-mode Justify charged shadow paint against configured length)
  classified external paint as stable geometry. D162 and D165 then aligned
  applets and asymmetric placement to that already-shrunken solid.
- FIX: make the configured solid background and its internal padding the stable
  layout authority. Shadows are external presentation paint. Remove them from
  the fit and autosize APIs, keep applets on the solid span, and compensate
  asymmetric paint only in the outer visual's placement. D170 (the first D169
  correction weakened end-hover shadow bounds) preserves that solid placement
  when shadows change and clips paint outside the canvas.
- EVIDENCE: before the fix, 42 px shadows changed the live panel from
  `[125,18,1189,26]` without custom shadows to `[157,18,1125,26]` with them.
  After the fix, isolated shadow-on and real shadow-off runs both settle at
  `[115,18,1209,26]`, with applets `[128,18,1183,26]` and endpoint wrappers at
  x=124 and x=1283..1316. Focused C++ geometry and source-contract tests, all
  130 QML compile probes, and all 245 QML interaction cases pass. The canonical
  gate passed all 105 CTest entries, scene probes, nested ASan/UBSan replay, and
  the output matrix before the D170 stable-solid correction. The final
  corrected gate passes the same complete set at merged PR #116 head
  `6f6c33d9a`.

### D170 - The first D169 correction weakened end-hover shadow bounds
- STATUS: FIXED IN PR #116 (`c7c13cf14`, stable-solid correction
  `f0d8578f3`); real-layout visual acceptance is pending.
- FOUND: 2026-07-24, correction review against D140 (zoomed side-dock chrome
  clipped at both ends).
- SYMPTOM: the first shadow-independent solid fix used solid length for every
  centered placement clamp. End-hover paint could therefore cross the output
  boundary even when the complete visual had enough room to remain inside.
- ROOT: stable sizing and transient presentation clipping were treated as one
  decision. Shadows must not reduce the former, but can constrain the latter.
- ROOT CORRECTION: mandatory cold review found that "the complete visual can
  fit somewhere" did not mean it could fit around the requested stable solid.
  A 90 px solid with a 10 px head shadow in a 100 px canvas moved five pixels
  when shadows became active, while Justify preserved the same solid.
- FIX: make the stable solid the only placement authority for Center and
  Justify. Bound its requested center against its own length, then apply only
  the head-minus-tail compensation to the visual parent. External paint clips
  when it cannot fit around the preserved solid.
- EVIDENCE: the sanitizer-backed behavioral core covers the exact asymmetric
  failure, reversed ends, fit-capable and clipped bounds, both end clamps, full
  and near-full canvases, and shadow toggles. The production source guard pins
  horizontal left/right and vertical top/bottom mapping while rejecting
  complete-visual clamping and bridge bypass. Focused CTest passes 2 of 2, QML
  compilation passes 130 of 130 files, the qmllint baseline matches, and QML
  interaction passes 245 of 245 cases.

### D171 - Centered shadow offsets raised the QML warning ratchet
- STATUS: FIXED IN PR #116 (`1221d8919`).
- FOUND: 2026-07-24, canonical gate after the D169 shadow-ownership correction.
- SYMPTOM: all 104 other CTest entries passed, but `qmllintgate` reported that
  `MultiLayered.qml` increased from 181 to 182 curated warnings.
- ROOT: the new two-condition placement path repeated the injected `myView`
  alignment lookup. The offset binding already repeated that unqualified access
  across its alignment branches.
- FIX: capture alignment once in a local const and reuse it. Name the document
  root directly in center and Justify state offsets instead of routing through
  the inherited background context name.
- EVIDENCE: `sourceguardtest` and `qmllintgate` pass. The touched file improves
  from 181 to 176 curated warnings, and the ratchet records the lower count.
  The canonical gate passes all 105 CTest entries.

### D173 - Theme-aware icon render test deadlocked during final view teardown
- STATUS: FIXED IN PR #116 (`879eb35c8`).
- FOUND: 2026-07-24, PR #116 canonical fast gate.
- SYMPTOM: `themeawareicontest` reached the end of
  `nonStandardSlotPaintsAtComputedSize()` after its size and pixel assertions,
  then the QtTest watchdog terminated it at 300 seconds. The other 104 CTest
  entries passed.
- ROOT: the process registered one `Environment` instance with
  `qmlRegisterSingletonInstance`, but each test case default-constructed a
  `QQuickView` with a separate `QQmlEngine`. Qt permits a registered singleton
  instance in only one engine, so the second and third views logged that
  `Environment` was unavailable. During final-window destruction, the GUI
  thread then waited in `QSGSoftwareThreadedRenderLoop::handleResourceRelease`
  while the software render thread remained asleep in
  `processEventsAndWaitForMore`.
- FIX: own one `QQmlEngine` for the complete test object and construct every
  view against it. Select the basic Qt Quick render loop before
  `QGuiApplication` so this offscreen software-render regression releases
  resources synchronously; scene-graph threading is outside its contract.
- EVIDENCE: focused `themeawareicontest` completes all six cases in 236 ms with
  no multi-engine singleton warning or null-`Environment` critical.
  `sourceguardtest` requires every view construction to use the shared engine
  and requires the basic loop in both direct and CTest execution. Controlled
  default-engine and threaded-loop mutations fail that source contract. The
  corrected-head canonical fast gate passes all 105 CTest entries, QML lint,
  scene probes, native package checks, and the fixture matrix.

### D174 - Theme-aware icon lifecycle guard ignored ordering and target scope
- STATUS: FIXED IN PR #116 (`6f6c33d9a`).
- FOUND: 2026-07-24, independent review of the D173 (theme-aware icon render
  test deadlocked during final view teardown) correction.
- SYMPTOM: moving `QSG_RENDER_LOOP=basic` after `QGuiApplication`, assigning
  the CTest environment to another target, or changing a view's local variable
  name did not test the lifecycle rule the source guard claimed to enforce.
- ROOT: the matcher normalized both complete files and searched for fixed token
  strings. It did not model the `main()` ordering, the
  `themeawareicontest` CMake block, or view construction as syntax independent
  of one local name.
- FIX: extract and inspect `main()`, require the direct render-loop selection
  before application construction, isolate the target's CMake section before
  checking its environment, and match every local `QQuickView` construction
  with variable-name-independent regular expressions.
- EVIDENCE: `sourceguardtest` passes in 0.04 seconds. Controlled mutations
  replace one shared-engine view with a differently named default-engine view,
  move direct render-loop selection after `QGuiApplication`, and move the
  CTest setting to another target; all three fail the corrected matcher.

### D175 - Reservation moves committed policy before publication succeeded
- STATUS: FIXED in PR #118 (`63497b3ac`).
- FOUND: 2026-07-24, cold review of FP-1 (the output-edge maximum reservation
  authority).
- SYMPTOM: a failed edge or output migration could leave the ownership ledger
  naming the new group after only part of its publisher state had changed.
- ROOT: the first coordinator draft mutated committed membership before every
  affected output-edge projection was known to be publishable.
- FIX: build a copied candidate ledger, stage every old and new group
  projection, and replace the committed ledger, publishers, and generation
  only after all staging succeeds. Failed staging destroys only replacements
  and retains the previous complete graph.
- EVIDENCE: the ledger tests discard a failed candidate without changing the
  committed state. Nested migration verifies one generation across both
  affected groups.

### D176 - Dock-system observability omitted reservation group ownership
- STATUS: FIXED in PR #118 (`7d452e789`).
- FOUND: 2026-07-24, cold review of FP-1 (the output-edge maximum reservation
  authority).
- SYMPTOM: per-view publisher fields could not prove which contributors,
  maximum depth, output edge, or transaction generation formed one shared
  reservation.
- ROOT: schema 3 described the former one-publisher-per-view transport and had
  no authoritative group graph.
- FIX: schema 4 adds one coordinator state generation and canonical
  output-edge group records. Collection refuses missing publishers, duplicate
  contributors, invalid group geometry, stale membership, and projection
  disagreement as an empty complete query.
- EVIDENCE: the exact serializer test pins root and per-view wire types,
  canonical order, publisher identity, and empty last-member teardown.

### D177 - Reservation replay could skip output migration and orphan cleanup
- STATUS: FIXED in PR #118 (`cb353022f`, mandatory dual-output correction
  `3f70b7224`).
- FOUND: 2026-07-24, two cold reviews of FP-1 (the output-edge maximum
  reservation authority).
- SYMPTOM: recipe 061 initially stopped after a same-output edge move. Its
  first extension made the secondary-output leg conditional, so a one-output
  fixture could still pass without testing the claim.
- ROOT: the fixture treated output migration as optional environment coverage
  instead of a required contract.
- FIX: require exactly two nested outputs, move the contribution
  unconditionally, restart the dock, remove both selected contributions, and
  reject every stale contributor or orphan group.
- EVIDENCE: the mandatory replay moves output 13 to 14, observes the persisted
  group after restart, and converges to an empty group array at a newer
  generation.

### D178 - Reservation validation assumed the compositor kept requested window size
- STATUS: FIXED in PR #118 (`e11081694`).
- FOUND: 2026-07-24, nested FP-1 replay with perpendicular reservations.
- SYMPTOM: the atomic snapshot rejected a valid side reservation after KWin
  shortened its mapped QWindow inside another exclusive band.
- ROOT: validation required the compositor-sized window geometry to equal the
  full-edge requested reservation rectangle.
- FIX: validate the requested reservation geometry and applied layer-shell
  anchors, margins, edge, and zone. The mapped publisher remains observable
  but is not required to retain its requested primary-axis size.
- EVIDENCE: nested recipe 061 retains the perpendicular publishers and one
  bottom maximum-depth group while the separated right dock remains fixed.

### D179 - Cross-output staging validated a lagging QWindow screen
- STATUS: FIXED in PR #118 (`1f82307da`).
- FOUND: 2026-07-24, mandatory two-output FP-1 migration replay.
- SYMPTOM: a correct output 13 to output 14 move was rejected synchronously,
  leaving the previous group committed.
- ROOT: `QWindow::screen()` can retain the old output until the first
  configure event. LayerShellQt's explicit screen already names the output
  that controls compositor placement.
- FIX: validate `LayerShellQt::Window::screen()` together with coordinator
  membership and the applied layer-shell policy.
- EVIDENCE: nested recipe 061 observes output 14 before and after restart and
  finds no output 13 residue.

### D180 - Reservation snapshot validation accepted divergent mirrored fields
- STATUS: FIXED in PR #118 (`27519ddb5`, residue corrections `9e8907870` and
  `ae529c166`).
- FOUND: 2026-07-24, independent rereview of schema 4 reservation
  observability.
- SYMPTOM: a group record and its contributing view could disagree in
  publisher-window geometry or applied layer-shell fields while the
  value-level consistency pass still accepted the snapshot. A noncontributing
  view could also retain reservation residue.
- ROOT: the first consistency pass compared membership, logical geometry,
  generation, and publisher identity but did not exhaustively compare every
  mirrored publication field or every no-membership empty state.
- FIX: compare the complete group projection against every member, require
  each contributor's published strut depth to equal its own contribution, and
  reject all reservation state on a view with no group. Canonical emptiness is
  equality with `QRect()`, not the wider zero-area `isEmpty()` predicate.
- EVIDENCE: controlled one-field mutations cover every mirrored scalar,
  contributor list, rectangle, layer-shell field, publisher token, and
  no-membership residue.

### D181 - Immediate migration snapshots reused the lagging QWindow output
- STATUS: FIXED in PR #118 (`266f11d0f`, committed-boundary replay
  `cdb9c6d20`).
- FOUND: 2026-07-24, independent rereview of the FP-1 (the output-edge
  maximum reservation authority) migration transaction.
- SYMPTOM: the first `dockSystemData` read after a valid cross-output
  reservation commit could return an empty complete query until the
  compositor configured the publisher window.
- ROOT: the coordinator staged against LayerShellQt's synchronous explicit
  output, but the D-Bus collector subsequently validated the same publisher
  against lagging `QWindow::screen()`. Recipe 061 retried empty snapshots and
  hid the disagreement.
- FIX: collect the publisher output from the applied layer-shell assignment.
  Poll only the older per-view readback until its published rectangle belongs
  to the target output, then require the next complete snapshot to expose the
  matching schema 4 membership without a retry.
- EVIDENCE: the dual-output nested replay observes the first complete
  coordinator snapshot at the committed boundary, persists output 14 through
  restart, removes both contributions, and leaves no orphan group.

### D182 - Coordinator rollback did not roll back member publication state
- STATUS: FIXED in PR #118 (`21ea8c61e`).
- FOUND: 2026-07-24, independent rereview of the FP-1 (the output-edge
  maximum reservation authority) failure transaction.
- SYMPTOM: a rejected update or removal retained the coordinator's old group
  while `VisibilityManager` already exposed the new or empty
  `publishedStruts`. An equal-valued retry could then be suppressed.
- ROOT: the publication API returned `void`, and the member assigned its
  acknowledged rectangle before the coordinator transaction completed.
- FIX: propagate `[[nodiscard]] bool` through the window-system, view, and
  coordinator boundary. Commit member state only on success, preserve a dirty
  retry after failure, and key equality by rectangle, persistent output
  identity, and edge using LayerShellQt's explicit output.
- EVIDENCE: focused tests cover success, failed update, failed removal,
  equal-valued retry, and same-geometry migration between different outputs.
  Source mutations reject discarded results, old-candidate publication,
  missing visibility-mode gating, and lagging QWindow output lookup.

### D183 - Reservation contributor ordering was normalized after disagreement
- STATUS: FIXED in PR #118 (`a1035aabf`).
- FOUND: 2026-07-24, independent rereview of the FP-1 (the output-edge
  maximum reservation authority) schema 4 consistency pass.
- SYMPTOM: reordered group and per-view contributor lists could pass
  validation and serialize different wire orderings.
- ROOT: the verifier sorted temporary copies before comparing them even though
  group serialization canonicalized its list and per-view serialization
  preserved the supplied list.
- FIX: require every group contributor list to arrive in canonical sorted
  order and compare every per-view mirror exactly with it.
- EVIDENCE: the valid two-member graph passes. Independent mutations reorder
  one view, then identically reorder a group and all of its mirrors; both are
  rejected.

### D184 - Reservation publication core bypassed the coverage inventory
- STATUS: FIXED in PR #118 (`21ea8c61e`).
- FOUND: 2026-07-24, final cold review of FP-1 (the output-edge maximum
  reservation authority).
- SYMPTOM: the sanitizer-backed publication-state test was registered in
  CMake, but the committed CTest count remained 106 and the app-subtree
  pure-core list omitted its header.
- ROOT: the helper and test landed without the two independent coverage
  inventory updates required for app-subtree pure cores.
- FIX: add the header-to-test pairing and raise the committed CTest inventory
  to 107.
- EVIDENCE: the coverage ratchet reports 107 CTest entries and 37 paired unit
  headers.

### D185 - Visibility header extension omitted adapting copyright
- STATUS: FIXED in PR #118 (`21ea8c61e`).
- FOUND: 2026-07-24, final cold review of FP-1 (the output-edge maximum
  reservation authority).
- SYMPTOM: `visibilitymanager.h` gained the member publication-state
  authority without the adapting author copyright line.
- ROOT: the source file received the line, but the materially extended header
  retained only its two upstream copyright lines.
- FIX: add the adapting author line without replacing either existing line.
- EVIDENCE: the final header carries all three lines in its SPDX block.

### D186 - Reservation commits recorded insufficient verification evidence
- STATUS: FIXED in PR #118 (`59146fd4e`, `cdb9c6d20`).
- FOUND: 2026-07-24, final cold review of FP-1 (the output-edge maximum
  reservation authority) commit claims.
- SYMPTOM: the dead-lookup removal recorded only a rebuild, while the
  committed-boundary replay recorded syntax and diff checks plus a literal
  escaped paragraph break. Neither body recorded the evidence required by its
  behavior claim.
- ROOT: intermediate commit messages were written before the independent
  caller sweep and corrected nested replay completed.
- FIX: record the tree-wide no-caller result in the removal commit and the
  observed maximum-depth migration, restart, teardown, and fixed perpendicular
  dock geometry in the replay commit.
- EVIDENCE: the caller search returns no `findMembership` occurrence. The
  corrected dual-output replay passes with depth 88, output 14 persistence,
  orphan-free teardown, and the right dock fixed at `1216,0 384x1000`.

### D187 - Full-span End floating panels extended one pixel beyond their output
- STATUS: FIXED in PR #120 (`853e6e359`).
- FOUND: 2026-07-24, while routing panel placement through the fail-closed
  stable-canvas solver.
- SYMPTOM: a Right-aligned horizontal panel or Bottom-aligned vertical panel
  at `maxLength=1` and `offset=0` begins one pixel after the output origin and
  ends one pixel outside the output. The stable geometry boundary refuses the
  resulting surface instead of mutating its QWindow.
- ROOT: reversed primary-axis placement adds one to the output origin even
  though QRect width and height already express the complete exclusive
  length. The increment is unrelated to both screen-edge reservation +1
  conventions.
- FIX: remove the reversed-alignment primary-axis increment for all four edges.
  Full-span End placement now equals the output span exactly. Both reservation
  +1 gap-prevention conventions remain unchanged.
- EVIDENCE: sanitizer-backed `positionergeometrytest` drives full-span End
  placement on top, bottom, left, and right edges and requires every complete
  surface to remain output-contained.

### D188 - Stable-canvas acceptance staged a Dock for a Panel-only transition
- STATUS: FIXED in PR #120 (`0dabbb516`).
- FOUND: 2026-07-25, recipe 071's first schema-integrated nested-KWin run.
- SYMPTOM: the recipe selected a horizontal Dock, then required the
  Panel-only stable floating transition to become eligible. Setting a positive
  screen-edge margin also retained the default client-side internal-gap
  ownership, which correctly kept the effective view type in Dock mode.
- ROOT: the fixture was inferred from whichever view happened to exist instead
  of declaring the view type and floating-gap owner that the contract needed.
- FIX: stage the matrix harness's deterministic
  `panel-bottom-justify-1out` fixture and explicitly set
  `floatingInternalGapIsForced=false`, matching the shipped panel templates.
  Restore the complete pristine nested config on exit.
- EVIDENCE: the uncorrected run failed with a configured but ineligible
  controller. The corrected fixture realizes as `type=panel`, reports an
  eligible controller with complete stable geometry, and reaches the first
  in-flight progress assertion.

### D189 - KWin script collection delay consumed the transition under test
- STATUS: FIXED in PR #120 (`b122ef88c`).
- FOUND: 2026-07-25, recipe 071's first eligible nested-KWin transition.
- SYMPTOM: maximizing the fixture reached the correct attached endpoint, but
  the recipe never observed a qreal midpoint. The claimed eight rapid
  reversals also waited for a settled endpoint between every target.
- ROOT: `e2e_kwin_js` slept for 500 ms before returning. The floating
  transition is shorter than that fixed log-collection delay, so the driver
  could not observe its in-flight state.
- FIX: retain the 500 ms default for existing KWin scripts, accept an explicit
  collection delay, and use 10 ms for recipe 071's immediate maximize
  mutations. Progress sampling and target-reversal checks then run while the
  controller is active.
- EVIDENCE: the fixed-delay run ended at
  `target=attached phase=resting progress=0`. With the scoped 10 ms delay, the
  nested recipe observes qreal midpoints in both directions and eight target
  reversals before settlement, while all stable geometry and revision
  assertions remain unchanged.

### D190 - Stable-canvas fixture cleanup started after destructive staging
- STATUS: FIXED in PR #120 (`34af636d7`).
- FOUND: 2026-07-24, independent FP-2 review.
- SYMPTOM: a failure while staging recipe 071's matrix fixture could leave the
  nested compositor running the modified dock configuration.
- ROOT: `matrix_stage` stops the dock, replaces its configuration, and restarts
  it before the recipe installed its EXIT cleanup trap.
- FIX: initialize cleanup state first, capture the pristine configuration,
  mark restoration necessary, and install the EXIT trap before fixture
  staging. Partial staging failures now take the same complete restoration
  path as failures later in the recipe.
- EVIDENCE: `sourceguardtest` requires the cleanup trap to precede
  `matrix_stage` and requires restoration state to become active immediately
  after the pristine snapshot.

### D191 - Stable-canvas reversal storm accepted settled transitions
- STATUS: FIXED in PR #120 (`29f992ef0`).
- FOUND: 2026-07-24, independent FP-2 review.
- SYMPTOM: recipe 071 claimed eight rapid in-flight reversals after observing
  only the requested target. A transition that had already settled could pass
  every storm iteration.
- ROOT: the storm helper did not require the matching active phase,
  `transitionRunning=true`, or fractional qreal progress before requesting the
  opposite target.
- FIX: require every alternating target to be observed in its matching
  attaching or floating phase with `0 < progress < 1`. Require the
  transition-geometry, surface-publication, and layer-shell-configure
  revisions to remain equal to their pre-storm values at every observation.
- EVIDENCE: recipe 071 now refuses a selected or settled target unless the
  transition is actively in flight. `sourceguardtest` pins the phase,
  running-state, fractional-progress, revision, and alternating-call
  requirements. The corrected nested-KWin run observed all eight alternating
  targets in flight and passed with the 88 px maximum-depth reservation and
  every stable geometry and revision assertion unchanged.

### D192 - Zero-gap panels exposed conflicting floating eligibility
- STATUS: FIXED in PR #120 (`73ad81186`).
- FOUND: 2026-07-24, second independent FP-2 review.
- SYMPTOM: a Panel with the legal `screenEdgeMargin=0` setting could make
  `dockSystemData()` return an empty string.
- ROOT: QML treated every enabled margin, including zero, as transition
  eligible. C++ `View::isFloatingPanel()` requires a positive gap, so schema 5
  correctly refused the contradictory eligible-but-unconfigured record.
- FIX: expose `View::isFloatingPanel()` as the notified
  `floatingPanelConfigured` property and derive QML eligibility from that same
  authority. Changes to panel mode, margin enablement, and margin size notify
  the derived property only when its value changes.
- EVIDENCE: `sourceguardtest` pins the one-authority route and rejects the old
  enabled-margin QML predicate. Recipe 071 restarts its deterministic
  Panel/Always Visible fixture at a 0 px gap and requires a nonempty snapshot
  with both configured and eligible false at the floated resting endpoint.

### D193 - Extended floating-background files omitted adapting attribution
- STATUS: FIXED in PR #120 (`86576fc52`).
- FOUND: 2026-07-24, second independent FP-2 review.
- SYMPTOM: two QML files materially extended for stable floating geometry
  retained the original copyright line but omitted the adapting author's
  required SPDX line.
- ROOT: the implementation changed each file's runtime contract without
  applying the repository's preserve-and-add attribution rule.
- FIX: preserve Michail Vourlakos's existing lines and add Bree Spektor's 2026
  SPDX copyright line to `BackgroundProperties.qml` and `Totals.qml`.
- EVIDENCE: the two changed headers now carry both original and adapting
  attribution. The canonical REUSE gate remains the final repository-wide
  license check.

### D194 - Zero-gap endpoint proof split state across snapshots
- STATUS: FIXED in PR #120 (`e1504a097`).
- FOUND: 2026-07-24, third independent FP-2 review.
- SYMPTOM: recipe 071 established the zero-gap Panel, visibility, configured,
  and eligible values in one snapshot, then established floated resting
  progress in later snapshots. The evidence did not prove the complete
  endpoint state atomically.
- ROOT: the boundary check reused the generic endpoint waiter after a separate
  configuration read instead of defining the exact combined state required by
  the D192 (zero-gap panels exposed conflicting floating eligibility)
  regression.
- FIX: poll one `dockSystemData` record until the same snapshot reports Panel,
  Always Visible, configured false, eligible false, floated, resting, not
  running, and progress 1.
- EVIDENCE: `sourceguardtest` pins the combined predicate and its dedicated
  waiter. Recipe 071 refuses every split or transitional observation.

### D195 - Huge finite placement values reached undefined integer conversion
- STATUS: FIXED in PR #120 (`8ef41a45d`).
- FOUND: 2026-07-24, fourth independent FP-2 review.
- SYMPTOM: a hand-edited finite offset could invoke undefined behavior while
  solving stable panel placement instead of being refused.
- ROOT: `solvePlacement()` multiplied the available length by floating config
  values and converted the result directly to `int`. Finiteness alone does not
  make an out-of-range floating-to-integer conversion defined, and the later
  output-containment check ran too late.
- FIX: preserve the shipped float truncation order while checking every
  floating product against the complete `int` range before conversion. Add
  the resulting integer start delta in `qint64` and narrow only after the sum
  is proven representable.
- EVIDENCE: sanitizer-backed `floatingpanelgeometrytest` drives both signs of
  the largest finite float offset through Start, Center, and End alignment and
  requires deterministic refusal. It also pins a representable delta whose
  addition would overflow `int` and a maximum available length whose float
  representation rounds beyond `int`.

### D196 - Placement solver trusted enum and QRect arithmetic boundaries
- STATUS: FIXED in PR #120 (`44a5ea89d`).
- FOUND: 2026-07-24, fifth independent FP-2 review.
- SYMPTOM: malformed enum values could return an empty present solution in
  release builds, while endpoint rectangles with valid coordinates could
  abort inside Qt or overflow intermediate edge arithmetic before the solver
  refused them.
- ROOT: the stable geometry boundary treated every non-horizontal edge as
  vertical and relied on `QRect::isValid()`, `width()`, `height()`, and
  `contains()` before proving the closed enum set and inclusive coordinate
  spans. A valid endpoint pair can span more than `int`, and ordinary signed
  arithmetic can overflow even when its final result is representable.
- FIX: validate both enums explicitly, derive rectangle spans from endpoints
  in `qint64`, range-check every edge coordinate before narrowing, construct
  extreme envelopes from checked endpoints, and assert containment through
  the validated endpoint metrics.
- EVIDENCE: sanitizer-backed `floatingpanelgeometrytest` passes 52 cases.
  Invalid edges and alignments fail closed, endpoint spans wider than `int`
  are refused without calling width-based Qt paths, all four extreme output
  origins solve without intermediate overflow, and Center and End each reject
  a branch-local out-of-range expression after a representable offset product.

### D197 - Render callbacks could outlive floating presentation state
- STATUS: FIXED BY PR #122 (`48e1f9b39`).
- FOUND: 2026-07-25, FP-3 (internal presentation, input, effects, and popup
  ownership) render-lifecycle preflight.
- SYMPTOM: a render-thread callback queued during view teardown could
  dereference Effects storage after destruction began.
- ROOT: direct render callbacks captured Effects without one shared lifetime
  boundary covering disconnect, an in-flight post, and teardown.
- FIX: route callbacks through a shared render bridge, close and disconnect
  the bridge first, and wait for any in-flight post before Effects storage can
  disappear.
- EVIDENCE: `floatingmaskhandshaketest` includes a concurrent close/post
  lifecycle case. The canonical gate passed all four nested sanitizer recipes.

### D198 - Floating popup anchors retained their former host window
- STATUS: FIXED BY PR #122 (`228252623`).
- FOUND: 2026-07-25, FP-3 popup-lifecycle preflight.
- SYMPTOM: moving the same visual anchor item to another QQuickWindow could
  leave the dialog listening to revisions from the former window.
- ROOT: the popup event filter followed anchor identity but did not follow
  `QQuickItem::windowChanged`.
- FIX: detach the former host filter, attach the new host, represent the
  no-host state explicitly, and ignore stale former-window revisions.
- EVIDENCE: `floatingpopuppresentationtest` and
  `floatinganchorwindowfiltertest` cover migration, stale revisions, and the
  no-host state.

### D199 - Panel-to-dock conversion could retain a panel input mask
- STATUS: FIXED BY PR #122 (`ca388f82e`).
- FOUND: 2026-07-25, FP-3 geometry-authority removal.
- SYMPTOM: changing a view from Panel to Dock could leave the native window
  carrying the former stable-panel input bridge.
- ROOT: ordinary dock input restoration was behind an animation-state update
  gate even though the view-type change transferred mask ownership
  immediately.
- FIX: restore dock input directly at the ownership handoff while retaining
  the ordinary gate for later dock animation updates.
- EVIDENCE: `sourceguardtest` uses a controlled mutation that fails when the
  direct handoff write is removed.

### D200 - Floating shadow readback reported requested state as applied
- STATUS: FIXED BY PR #122 (`19f3effd7`).
- FOUND: 2026-07-25, independent cold review of PR #122.
- SYMPTOM: schema 6 could report nonzero `shadowPaddingOffsets` even when no
  `KWindowShadow` registry entry existed for the view.
- ROOT: the collector serialized Effects' requested padding, while
  `PanelShadows::setExtraPadding` correctly refuses an unregistered window.
- FIX: query the per-window PanelShadows registry and serialize both borders
  and padding as null when the shadow is absent.
- EVIDENCE: `dbusreportstest`, `sourceguardtest`, and
  `panelshadowstatetest` cover intended/applied disagreement, insertion,
  update, explicit removal, destruction, and absent serialization.

### D201 - Floating presentation cores escaped the pairing ratchet
- STATUS: FIXED BY PR #122 (`19cb727e0`).
- FOUND: 2026-07-25, independent cold review of PR #122.
- SYMPTOM: the new app/view core headers were absent from the pairing
  inventory, so the ratchet could not require one correspondingly named unit
  target for each header.
- ROOT: `app-subtree-units.list` still named only
  `floatingpanelgeometry.h`.
- FIX: register every FP-3 core and add the mechanically paired
  `floatinganchorwindowfiltertest`.
- EVIDENCE: the coverage ratchet reports 116 CTest entries and 45 paired
  headers. The focused host-migration test passes.

### D202 - Floating presentation observability omitted applied details
- STATUS: FIXED BY PR #122 (`15d7dda7e`, corrected by `19f3effd7`).
- FOUND: 2026-07-25, FP-3 transaction review before the first full gate.
- SYMPTOM: the initial schema 6 snapshot exposed masks but omitted live border,
  shadow, popup-hint, and visible-anchor state, so those assertions could only
  be inferred from the controller.
- ROOT: observability stopped at the geometry decision instead of querying the
  production consumers.
- FIX: expose the applied Effects borders, PanelShadows registry state,
  containment display hint, and visible-anchor revision and validate each
  against its authoritative owner.
- EVIDENCE: exact serializer and mutation tests reject every field omission,
  disagreement, invalid order, and inferred popup value.

### D203 - Floating-input history used a bare FP-3 codeword
- STATUS: FIXED BY PR #122 (`44e6d5907`).
- FOUND: 2026-07-25, independent cold review of PR #122.
- SYMPTOM: the input commit's verification paragraph first used `FP-3`
  without its plain-English description.
- ROOT: the focused-run sentence treated the traceability label as
  self-explanatory.
- FIX: rewrite the first use as FP-3 (floating internal presentation, input,
  effects, and popup ownership).
- EVIDENCE: the corrected commit body contains the description at first use.

### D204 - The D-Bus design document retained schema version 5
- STATUS: FIXED BY PR #122 (`19f3effd7`).
- FOUND: 2026-07-25, independent cold review of PR #122.
- SYMPTOM: the design prose called `dockSystemData` schema version 5 while its
  example and implementation used version 6.
- ROOT: the schema label was not advanced with the first schema 6 source
  commit.
- FIX: align the design prose with schema 6 while preserving the exact field
  documentation.
- EVIDENCE: both D-Bus references, adaptor XML, serializer, and exact schema
  tests now name the same version and contract.

### D205 - Panel popup anchors froze during task-removal layout motion
- STATUS: FIXED IN PR #124 (`f8396b5ed`).
- FOUND: 2026-07-25, FP-4A (the direct window-touch runtime) popup-motion
  preflight.
- SYMPTOM: task removal or other applet layout motion could leave a Panel popup
  anchored to the former presentation while the visible paint mask moved.
- ROOT: the `appletsLayoutGeometry` binding required
  `visibilityManager.inNormalState` for Docks and Panels even though Panel
  presentation geometry remains authoritative during unrelated layout
  animation.
- FIX: let Panels publish the current stable-canvas paint mask throughout
  layout motion. Retain the legacy normal-state gate for Docks.
- EVIDENCE: nested recipes 071 and 072 keep the popup anchor's primary span
  stable and require its secondary axis to match the current paint mask through
  transitions and client teardown.

### D206 - Heterogeneous task rows suppressed touching-window state
- STATUS: FIXED IN PR #124 (`36e835fb9`).
- FOUND: 2026-07-25, independent cold review of PR #124.
- SYMPTOM: one legal non-window `TasksModel` row with invalid window-only roles
  could fail the complete evaluation and suppress every real touching window.
- ROOT: candidate collection decoded `IsHidden`, `IsMinimized`, and `Geometry`
  before applying the `IsWindow` discriminator.
- FIX: validate `IsWindow` first, skip false rows immediately, and require exact
  window-only role types only after a row claims window identity.
- EVIDENCE: `windowtouchtrackertest` covers a mixed non-window and touching
  window model, malformed true-window hidden, minimized, and geometry roles,
  and fail-closed recovery. `sourceguardtest` removes the discriminator ordering
  and rejects the mutation. The follow-up role cases are in `fd445ee2f`.

### D207 - D-Bus accepted divergent touching-window authorities
- STATUS: FIXED IN PR #124 (`508dcf630`).
- FOUND: 2026-07-25, independent cold review of PR #124.
- SYMPTOM: `dockSystemData` could serialize a stale
  `FloatingTransition::touchingWindowCount` copy without proving equality with
  the per-view tracker authority.
- ROOT: collection read only the transition copy even though the tracker owns
  the live count and the transition stores a synchronous policy input.
- FIX: compare both counts, log both dock identities and values, fail the whole
  snapshot on disagreement, and serialize only the tracker-owned value.
- EVIDENCE: `dbusreportstest` has a constexpr equal/divergent authority case,
  while `sourceguardtest` replaces the tracker read and rejects the collector
  mutation.

### D208 - Legacy Dock gap readback omitted Windows Go Below
- STATUS: FIXED IN PR #124 (`b552508e3`).
- FOUND: 2026-07-25, independent cold review of PR #124.
- SYMPTOM: a Windows Go Below Dock could consume `hideThickScreenGap` while
  schema 7 reported `dockGapHideRequested` false.
- ROOT: the QML request admitted only Always Visible even though the legacy Dock
  presentation consumes the same setting in both modes.
- FIX: admit exactly Always Visible and Windows Go Below for the legacy Dock
  request while keeping Panel attachment restricted to eligible Always Visible
  views.
- EVIDENCE: unit and source mutation coverage reject unrelated visibility
  modes. Recipe 071 drives maximize and restore in both consuming modes and
  requires a Floated target with no Panel transition geometry.

### D209 - Partial-reservation recipe lacks a reproducible schema-current front door
- STATUS: OPEN.
- FOUND: 2026-07-25, FP-4B (multi-output and separated-span topology
  acceptance) preflight.
- SYMPTOM: `tests/e2e/061-partial-reservation-placement.sh` requires an
  externally prepared three-view fixture and rejects the current
  `dockSystemData` readback because it still requires schema version 6 after
  schema version 7 landed.
- ROOT: the specialized recipe was added without a deterministic fixture
  constructor or registered runner entry, so its embedded schema contract did
  not participate in the schema 7 gate.
- REQUIRED: give recipe 061 a reproducible fixture and explicit runner entry,
  update its schema assertion to the current contract, and prove its
  maximum-depth reservation and non-intersecting side-view assertions remain
  non-vacuous.
- EVIDENCE: the recipe's final state assertion requires
  `schemaVersion == 6`; no script, manifest, or tracking entry invokes recipe
  061 by name.

### D210 - Floating panel attachment changed primary-axis layout clearance
- STATUS: FIXED IN PR #126 (`dc0fda084`).
- FOUND: 2026-07-25, FP-4B (multi-output and separated-span topology
  acceptance) nested-KWin preflight.
- SYMPTOM: attaching a partial Start-aligned floating Panel increased
  `availablePrimaryLength` from 436 to 442 px and changed its popup primary
  span from `[6,436]` to `[0,442]`. The QWindow, configured span, trigger,
  reservation, and output assignment remained stable.
- ROOT: `Effects` correctly removes the painted primary-start border when the
  Panel reaches its attached endpoint. `MultiLayered.qml` also used that live
  painted-border bit as the authority for primary-axis layout padding and
  popup roundness clearance. The six-pixel visual border change therefore
  leaked into two measurements that FP-2 requires to remain stable.
- FIX: separate painted-border presence from layout-clearance ownership with a
  constexpr policy. Configured positive-gap floating Panels retain missing
  primary-axis clearance while thickness edges and non-floating views still
  follow paint. Theme, radius, margin, and indicator inputs remain reactive.
- EVIDENCE: recipe 073 first stopped on
  `availablePrimaryLength: 436 -> 442` and
  `popupAnchorPrimarySpan: [6,436] -> [0,442]`. With the corrected authority,
  all three panels retain their original applet and popup spans while the
  nested client drives attached and floated endpoints across full-touching,
  partial-touching, and disconnected output arrangements. The pure decision
  matrix, QML interaction, compile, lint, and source-mutation checks pass.

### D211 - Operation-storm convergence projected a nonexistent geometry field
- STATUS: FIXED on `main` (`0c5c33fa6`); merged through PR #128.
- FOUND: 2026-07-25, FP-4C (deterministic operation-storm acceptance)
  preflight.
- SYMPTOM: the settled-state comparison could report unchanged visible
  geometry without reading the schema-7 visible geometry at all.
- ROOT: `linked-dock-operation-stress.sh` projected `visibleGeometry` through
  `dict.get()`. `dockSystemData` exposes `currentVisibleGeometry`, so both
  snapshots silently contributed `None` for that field.
- REQUIRED: parse schema 7 through a typed oracle and compare the real current
  visible geometry together with stable canvas, trigger, applet measurement,
  sizing, transition, reservation, and ownership state. Missing required
  fields must be refusals.
- EVIDENCE: `DockSystemViewRecord` serializes `currentVisibleGeometry`; no
  `visibleGeometry` key exists. The stress projection used `.get()` and
  therefore converted the misspelling into equal null values.

### D212 - Operation-storm teardown leaked its mutated dock fixture
- STATUS: FIXED on `main` (`0c5c33fa6`); merged through PR #128.
- FOUND: 2026-07-25, FP-4C (deterministic operation-storm acceptance)
  preflight.
- SYMPTOM: a successful run leaves five created dock records in the nested
  configuration, while an early failure can leave an arbitrary partial graph
  for the next recipe.
- ROOT: the recipe mutates the ambient seed directly and owns no configuration
  transaction or EXIT cleanup. Its only stop and restart belongs to the test
  sequence, not restoration.
- REQUIRED: stage a generated fixture, arm cleanup before the first destructive
  operation, restore the pristine configuration after every exit, preserve a
  failing body status, and convert cleanup failure after success into failure.
- EVIDENCE: the recipe has no cleanup trap, pristine snapshot, or restoration
  call; its final state intentionally contains the five stress views.

### D213 - Operation-storm replay text was not a replayable typed plan
- STATUS: FIXED on `main` (`0c5c33fa6`); merged through PR #128.
- FOUND: 2026-07-25, FP-4C (deterministic operation-storm acceptance)
  preflight.
- SYMPTOM: the artifact names a seed and resolved shell calls, but cannot be
  supplied to the recipe to reproduce the same operation sequence.
- ROOT: two inline `random.Random` loops generate positional integer rows
  directly in shell. The output log has no schema, typed operation variants,
  symbolic identity binding, validation boundary, or replay input path.
- REQUIRED: use one versioned immutable operation document generated by a
  repository-owned fixed algorithm. Validate it before mutation, resolve
  symbolic dock and output identities at execution, retain the resolved log,
  and accept the same document as explicit replay input.
- EVIDENCE: the recipe only reads `LATTE_LINKED_STRESS_SEED`; no artifact path
  is accepted, and its pipe-delimited log cannot reconstruct the generator
  state or validate operation fields.

### D214 - Operation-storm acceptance did not require floating-panel ownership
- STATUS: FIXED on `main` (`0c5c33fa6`); merged through PR #128.
- FOUND: 2026-07-25, FP-4C (deterministic operation-storm acceptance)
  preflight.
- SYMPTOM: the operation storm can pass without exercising the stable
  floating-panel architecture that FP-4C is intended to accept.
- ROOT: the recipe inherits whichever single independent view the ambient seed
  provides and checks neither Panel type nor positive floating gap. It also
  omits schema version, transition endpoint and revisions, reservation groups,
  exact settings-window ownership, transition and touch-controller identities,
  and compositor QWindow uniqueness.
- REQUIRED: stage a deterministic partial floating Panel and require the full
  schema-7 identity, placement, sizing, presentation, transition, reservation,
  edit, lifecycle, convergence, and restart projection after a typed operation
  storm.
- EVIDENCE: the current precondition checks only one independent relationship.
  The final projection omits the listed FP-4 authorities and its fixture setup
  never configures Panel or floating-gap state.

### D215 - Compound relocation exposed split placement authorities
- STATUS: FIXED on `main` (`223ec413a`); merged through PR #128.
- FOUND: 2026-07-25, FP-4C (deterministic operation-storm acceptance)
  checkpoint 33.
- SYMPTOM: an edge and output move could leave the QWindow geometry on one
  output while LayerShellQt and the reservation coordinator still named the
  other output. The applied relocation generation nevertheless advanced.
- ROOT: output policy, QWindow retargeting, edge, alignment, geometry solving,
  layer-shell state, reservation publication, and reveal were separate
  observer-visible transitions. Intermediate resize could also make
  `QWindow::screen()` follow an adjacent output before final margins applied.
- FIX: one Positioner transaction retires old reservation ownership, applies
  output policy, physical output, edge, and alignment under an observer guard,
  solves against the assigned `QScreen`, verifies the LayerShellQt output and
  exact anchors and margins, publishes the new reservation, commits the
  generation, then remaps and reveals. Deferred reservation inputs remain
  dirty until successful publication.
- EVIDENCE: layer-shell, reservation-publication, identity-contract, and source
  tests pin the order and failure semantics. The saved seed 127934575 replay
  completes every output, edge, orientation, and alignment mutation with
  equal applied authorities.

### D216 - Reversible removal retained compositor-owned surfaces
- STATUS: FIXED on `main` (`cef08bd1f`); merged through PR #128.
- FOUND: 2026-07-25, FP-4C (deterministic operation-storm acceptance)
  removal checkpoint.
- SYMPTOM: a removed containment left its mapped dock surface, screen-edge
  helper, floating-gap helper, or reservation contribution alive while Plasma
  retained the QObject for its 60-second Undo window.
- ROOT: `GenericLayout` moved the runtime View out of its active map but did
  not transfer compositor ownership. Plasma's reversible object lifetime was
  incorrectly treated as continued visual and reservation lifetime.
- FIX: reversible removal stops visibility timers, retires reservation
  membership, destroys helper surfaces, and unmaps the canvas before parking
  the View. Undo republishes reservation ownership before recreating helpers
  and remapping the same runtime View.
- EVIDENCE: the ownership contract pins retirement and restoration order. The
  operation storm requires the exact compositor window multiset and no
  reservation contributor for every removed handle.

### D217 - Removal tombstone committed before child transient writes
- STATUS: FIXED on `main` (`cef08bd1f`); merged through PR #128.
- FOUND: 2026-07-25, real notification Undo acceptance.
- SYMPTOM: a root containment could be deleted from persistence and then
  partially recreated by later applet or subcontainment transient writes from
  the same removal action.
- ROOT: libplasma emits root `destroyedChanged(true)` before recursively
  marking its child objects transient. Committing the tombstone in that signal
  handler was therefore not the complete transaction boundary.
- FIX: prepare the full subtree snapshot before removal, trigger libplasma's
  synchronous action, and commit the tombstone only after the action returns.
  Direct permanent removal uses the same post-destruction commit boundary.
- EVIDENCE: the source contract rejects a tombstone from
  `destroyedChanged()`. The real notification recipe removes and restores a
  linked dock with its exact root, applet, and subcontainment state.

### D218 - Single-layout removal used a stale KConfig repository
- STATUS: FIXED on `main` (`cef08bd1f`); merged through PR #128.
- FOUND: 2026-07-25, real notification Undo acceptance.
- SYMPTOM: a tombstone call reported success while the removed containment
  remained on disk, or Undo restored a subtree that a later live sync could
  overwrite.
- ROOT: Corona opened the active layout as `KConfig::SimpleConfig`, while
  `AbstractLayout` and Storage reopened the same pathname with default
  `FullConfig` flags. `KSharedConfig` caches those as distinct repositories,
  so group deletion operated on a stale entry map and became a no-op.
- FIX: active single-layout removal and Undo accept Corona's live
  `KSharedConfigPtr`, validate that its canonical path is the layout file, use
  `SimpleConfig` consistently, synchronize once, and independently verify the
  exact on-disk groups.
- EVIDENCE: the stale-FullConfig observer test proves that deleting through the
  live SimpleConfig authority persists even when the old observer later
  writes. The real Undo and restart recipe preserves the complete subtree.

### D219 - Hidden edit chrome initialization rewrote Maximum length
- STATUS: FIXED on `main` (`eab2e1f59`,
  `1c8d9bf2d`); merged through PR #128.
- FOUND: 2026-07-25, FP-4C (deterministic operation-storm acceptance)
  checkpoint 43.
- SYMPTOM: eight seconds after linked-dock creation, hidden settings chrome
  changed the unrelated Justify root's `maxLength` from 45 percent to 1
  percent. The new linked member remained at 45 percent.
- ROOT: Maximum and Minimum slider handlers were connected before their
  config-to-handle initialization completed. Minimum initialization compared
  persistent state with the sibling Maximum handle's temporary zero and
  invoked that sibling's config writer. Justify correctly ignores stored
  minimum length, so the temporary zero clamped to the one-percent floor and
  became persistent.
- FIX: both controls distinguish config synchronization from interaction,
  initialize before enabling handlers, and treat persistent configuration
  rather than a sibling visual handle as the coupling authority. Maximum owns
  the single clamp-and-store operation; Minimum delegates to it instead of
  duplicating configuration and offset writes.
- EVIDENCE: the focused QML lifecycle test observes zero writes during
  construction and config resynchronization, then exactly one write for an
  interaction. All 131 shipped QML files compile, and the exact saved operation
  replay keeps both dock values stable through delayed chrome warmup. The
  focused settings and QML gates pass while the AppearanceConfig.qml curated
  warning baseline shrinks from 243 to 242.

### D220 - Stress oracle treated containment IDs as globally unique history
- STATUS: FIXED on `main` (`0c5c33fa6`); merged through PR #128.
- FOUND: 2026-07-25, FP-4C (deterministic operation-storm acceptance)
  step 42.
- SYMPTOM: creating a dock after permanent removal failed replay validation
  because Plasma reused the removed containment's numeric ID.
- ROOT: the model retained symbolic-to-numeric bindings after the represented
  persistent record ceased to exist. Plasma allocates the lowest available
  containment ID, so uniqueness applies to live persistent records, not all
  records that ever existed.
- FIX: removal first proves the exact before-and-after persistent delta, then
  retires that handle's binding. Later creation may bind the freed number to a
  new symbolic record while live identities, lineage, reservation
  contributors, and replay transitions remain exact.
- EVIDENCE: the pure model covers removal followed by numeric ID reuse and all
  21 adversarial cases pass. The same immutable plan and resolved replay pass
  after multiple destruction and creation cycles.

### D221 - Rapid return-to-origin placement retained stale pending fields
- STATUS: FIXED on `main` (`3967011eb`); merged through PR #128.
- FOUND: 2026-07-25, independent FP-4C (deterministic operation-storm
  acceptance) review.
- SYMPTOM: an applied Bottom/Center view can receive Top/Left and then
  Bottom/Center again before the first relocation settles, yet still apply the
  stale Top/Left request.
- ROOT: `Positioner::setNextLocation()` represented pending placement as sparse
  field deltas compared with currently applied state. The second request
  matched applied state, so it neither replaced the stale pending fields nor
  advanced the relocation generation. Layout, screen-group-derived output,
  follow-primary policy, and settings-window restoration had the same split
  ownership.
- FIX: `PlacementRequestState` owns one complete generation-tagged target.
  Sparse UI boundaries overlay the newest target, Positioner projects it only
  into Qt acknowledgement fields, and callbacks can complete only the current
  token. Hide and settings ownership remain latched through supersession.
- EVIDENCE: sanitizer-backed pure tests cover A-to-B-to-A, mixed patches,
  follow-primary and screen-group reversal, stale completion, and identical
  target coalescing. The immutable seed now includes a real two-request
  return-to-origin burst that requires an exact two-generation advance and a
  settled final origin. Focused CTest, the production build, coverage ratchet,
  and QML lint gate passed.
- SEVERITY: release blocker.

### D222 - Failed removal Undo left split runtime and persistent ownership
- STATUS: FIXED on `main` (`c675458c6`); merged through PR #128.
- FOUND: 2026-07-25, independent FP-4C (deterministic operation-storm
  acceptance) review.
- SYMPTOM: if persistence restoration or runtime resume fails during Undo, the
  containment remains non-destroyed and the View remains suspended in
  `m_waitingLatteViews`, while the persistent subtree is tombstoned.
- ROOT: the failed-Undo path retains the tombstone but does not restore
  Plasma's destroyed state or otherwise finish removal. The commit timer
  destroys only containments whose destroyed flag is still true, so the split
  state can survive indefinitely.
- FIX: one generation-tagged transaction owns suspension, next-turn Undo
  resolution, runtime retirement, containment destruction, and the final
  persistence tombstone. Viewless children defer to their root transaction,
  expiry uses the same checked finalizer, and Storage propagates actual KConfig
  failure while excluding complete runtime-derived ownership subtrees.
- EVIDENCE: the constexpr pure core injects restore, resume, runtime
  retirement, containment destruction, and persistence failures. StorageTest
  drives a real unwritable KConfig authority, the integration contract pins
  next-turn ordering and retirement proof, and all three focused tests pass.
  The focused independent rereview returned MERGE.
- SEVERITY: release blocker.

### D223 - Reservation oracle trusted mutually wrong runtime geometry
- STATUS: FIXED on `main` (`0f214f012`); merged through PR #128.
- FOUND: 2026-07-25, independent FP-4C (deterministic operation-storm
  acceptance) review.
- SYMPTOM: a reservation publisher with wrong geometry can pass when schema
  readback and the compositor expose the same wrong rectangle.
- ROOT: the oracle checks positivity, member mirrors, and runtime agreement
  against the snapshot's own `windowGeometry`. It does not independently
  derive the exact one-pixel publisher rectangle, anchors, or margins from
  output geometry, edge, and contributors.
- FIX: capture immutable output identity, connector, and geometry records
  before mutation, then derive exact full-edge maximum-depth bands, one-pixel
  publisher surfaces, LayerShell placement, per-view struts, and compositor
  frames from those external records and live view placement.
- EVIDENCE: 25 adversarial model cases reject coherent wrong runtime and
  compositor state across all four edges, including output, connector,
  geometry, membership, depth, anchors, margins, per-view struts, and
  publisher frames. The source contracts pass, and the exact seed 127934575
  replay completes in the private two-output nested compositor with pristine
  cleanup.
- SEVERITY: release blocker for FP-4C acceptance.

### D224 - Applied placement waited circularly on its own relocation commit
- STATUS: FIXED on `main` (`e712cbf63`); merged through PR #128.
- FOUND: 2026-07-25, FP-4C exact nested replay after the D223 reservation
  oracle correction.
- SYMPTOM: the first placement checkpoint remained in relocation forever.
  Geometry and LayerShell placement were applied, but reservation publication
  retried every 100 milliseconds and the applied generation never advanced.
- ROOT: Positioner publishes reservation ownership before committing the
  relocation generation. The explicit applied-placement publication boundary
  rejected every active relocation, so publication waited for the commit that
  itself waited for publication.
- FIX: the explicit AppliedPlacement path accepts an active relocation after
  its candidate surface and LayerShell placement are verified. Ordinary timer
  and observer updates still defer on active or unapplied placement, and
  off-screen publication remains refused.
- EVIDENCE: the identity contract pins the non-circular boundary. The rebuilt
  production binary completes all 76 operations in seed 127934575 and restores
  the exact pristine nested projection.
- SEVERITY: release blocker.

### D225 - Placement handler rewrite left stale settings-inventory selectors
- STATUS: FIXED on `main` (`22c6c17ef`); merged through PR #128.
- FOUND: 2026-07-25, replacement FP-4C canonical gate.
- SYMPTOM: SettingsInventoryTest rejected the screen-count connection and all
  four alignment buttons after D221 changed their structural source hashes.
- ROOT: the placement handler rewrite updated QML and behavior coverage but did
  not update the bidirectional per-view settings source ledger.
- FIX: replace only the five stale structural selectors with their current
  scanner identities while retaining the same screen and alignment audit
  ownership.
- EVIDENCE: SettingsInventoryTest validates all 278 affordances and 25
  exemptions with every source site resolving exactly once.
- SEVERITY: beta blocker.

### D226 - LayerShell output migration bypassed reservation-gated remapping
- STATUS: FIXED on `main` (`01d364d95`); merged through PR #128.
- FOUND: 2026-07-25, final cold independent review of FP-4C (the deterministic
  operation-storm acceptance).
- SYMPTOM: a primary-output change or spontaneous Qt screen reassignment can
  expose a dock on the destination output while its assigned output,
  LayerShell output, and reservation ownership still name the source.
- ROOT: pending-output projection, reservation retirement, and
  `View::moveToScreen()` use `QWindow::screen()` as the migration authority.
  When Qt already reports the destination, the transaction skips the hide and
  retirement path. LayerShell then retargets and remaps immediately before
  destination reservation publication.
- FIX: derive migration from Latte's assigned output and LayerShell output.
  An explicit output-move type distinguishes ownership transfer from QWindow
  observation reconciliation. Any assigned or LayerShell authority mismatch
  retires reservation ownership and keeps the view hidden. LayerShell output
  application no longer remaps a previously visible surface; the
  post-publication relocation commit owns that remap.
- EVIDENCE: the production binary, LayerShell mapping tests, and dock identity
  contracts pass. The focused negative control restored the immediate visible
  remap and failed at the reservation-gated ownership contract, then passed
  after restoring the fix.
- SEVERITY: release blocker.

### D227 - Layout mutation preceded destination-output preflight
- STATUS: FIXED on `main` (`bd744dddc`); merged through PR #128.
- FOUND: 2026-07-25, final cold independent review of FP-4C (the deterministic
  operation-storm acceptance).
- SYMPTOM: if the destination output disappears during the relocation hide
  interval, compound placement can move the dock to the target layout but
  retain the old output, edge, and alignment.
- ROOT: `Positioner::hidingForRelocationFinished()` calls the layout mutation
  before checking whether the generation's `QPointer<QScreen>` survived.
  Failure cancellation then snapshots the already-mutated layout as the new
  committed intent.
- FIX: capture the complete prior intent and build one immutable application
  plan. Resolve and validate output lifetime, layout endpoints, runtime view
  ownership, linked-view requirements, and request generation before
  reservation retirement, then revalidate immediately before synchronous
  mutation. Layout moves share the same validation routine and have no
  recoverable failure after unassignment. Cancellation restores the captured
  prior intent by token instead of resampling runtime state.
- EVIDENCE: the production binary and eight focused placement, identity,
  layout-manager, LayerShell, and reservation tests pass. The pure state test
  restores every prior placement field after simulated partial live-state
  drift. A focused negative control restored the late live-state snapshot and
  failed at the captured-prior contract, then passed after restoring the fix.
- SEVERITY: release blocker.

### D245 - Partial Panels lost endpoint borders at live attachment
- STATUS: FIXED on `main` by `18bc78ed4`, `65f0d5c1a`, `21bb65bae`,
  `27704ff82`, `5abc94338`, and `d8a1b093a`.
- FOUND: 2026-07-31, second independent review of PR #134.
- SYMPTOM: a partial floating Panel loses both primary-axis endpoint borders
  when a dragged window reaches its live attached endpoint. Its rounded ends
  become square even though the Panel does not reach either output endpoint.
- ROOT: the enabled-border decision compared the local effects rectangle with
  the QWindow canvas. A partial Panel's QWindow is itself partial, so paint
  filling that local canvas was incorrectly classified as presentation filling
  the output. The next cold review found that the first correction still used
  `QWindow::screen()` geometry at its call site. That observation can lag the
  synchronously applied Positioner and LayerShell output during relocation.
  The helper also silently classified invalid view or output geometry as a
  partial presentation. The following cold review found that relocation tokens
  still did not cover direct output reassignment or `QScreen::geometryChanged`.
  Positioner also requested a border update before replacing its solver
  scratch, so valid old surface and new output rectangles could still be
  combined. The final review found that FloatingTransition and QWindow state
  still changed before LayerShell accepted the staged surface. It also found
  that an invalid effects rectangle after the first surface publication was
  still silently classified as a partial presentation.
- FIX: translate local effects geometry through the view's global geometry and
  compare the result with the assigned output's primary-axis endpoints. Keep
  the decision independent of view type, output origin, orientation, and
  topology. Positioner now publishes the applied surface, the output geometry
  used to solve it, the placement generation, and forced endpoint borders
  together only after LayerShell accepts the placement. Effects retains the
  previous publication until the new one arrives. Treat absent paint as the
  documented startup or explicit Panel-to-Dock handoff state, but report every
  other missing presentation, output, or surface authority as a critical
  refusal. Geometry solving now produces local staged values. The applied
  surface, controller backing state, QWindow, occupancy, triggers, and observer
  signals publish only after LayerShell accepts that stage.
- EVIDENCE: the pure border test covers all four edges on a negative-origin
  output. It accepts complete output coverage, rejects a partial Panel that
  fills only its QWindow, rejects a one-pixel short presentation, and rejects
  the lagging source output. The source contract requires the immutable
  Positioner geometry pair, placement-generation match, publication callback,
  and loud invalid-geometry refusal. Nested recipe 074
  holds a real titlebar drag at attachment and observes a partial top Panel
  retain `bottom,left,right` borders before button release. The same recipe
  still observes full-output Justify Dock attachment and the 60% to 54%
  Maximum Length lifecycle. Two-output recipe 073 passes full-touching,
  partial-touching, and disconnected output arrangements, exact separated-span
  activation, restart persistence, and its controlled negative oracles. The
  corrected exact source head
  `2049878ae462a4a4efd324a2bd9b5f8922f3f6ea` passes the full canonical gate.
- SEVERITY: beta blocker.

### D256 - Held titlebar endpoint expired before observer sampling
- STATUS: FIXED on `main` by `1bd392e1f`.
- FOUND: 2026-07-31, repeated nested recipe 074 after the required PR #134
  follow-up review.
- SYMPTOM: the live-titlebar recipe can report an expanding Justify Dock at
  its later floated endpoint even though an unchanged rerun observes the
  complete attached endpoint correctly.
- ROOT: fakepointer held each drag waypoint for 900 ms. Repeated D-Bus samples
  on a loaded nested host could consume that interval, letting the scripted
  outward reversal begin before the endpoint assertion observed the frame.
- FIX: allow bounded fakepointer holds up to five seconds and retain each
  recipe endpoint for two seconds. The pointer button remains held throughout
  both transition proofs.
- EVIDENCE: the unchanged production path passed on immediate replay. The
  rebuilt helper rejects 5001 ms and accepts the new bounded range. Recipe 074
  passes its Panel, Center Dock, expanding Justify Dock, and attached ruler
  cases with the longer held endpoint.
- SEVERITY: beta blocker.

### D257 - Real-desktop launcher omitted KWin authority for worktree binaries
- STATUS: FIXED on `main` by `0d30286cb` and `d97fe9d90` (PR #136).
- FOUND: 2026-08-01, final real-layout acceptance of merged PR #134.
- SYMPTOM: a dock launched from a clean validation worktree starts and renders,
  but open-application indicators, direct window-touch attachment, and other
  task-window behavior remain inert. D-Bus reports an empty
  `windowTouchGeometryRoleType`, zero touching windows, and launcher-only task
  rows even after a new client opens.
- ROOT: KWin gates `org_kde_plasma_window_management` by an exact executable
  match in the KService desktop-entry cache. `start-dock.sh` can run a binary
  from any checkout, but it did not register that path. Existing entries named
  the installed package, primary checkout, or older validation worktrees, so
  KWin correctly withheld the window feed without failing the process.
- FIX: at the shared real-config restart boundary, resolve the same `BUILD`
  executable that `run-staged.sh` will launch and register it in one
  current-development desktop entry. Copy the privileged interface list from
  the shipped desktop template as its single source and refresh KService on
  every start. This covers normal, sanitized, and post-e2e restoration paths.
  Refuse a missing or unrepresentable executable and malformed or insufficient
  authority metadata before stopping the existing dock.
- EVIDENCE: the isolated launcher test requires the exact canonical executable,
  preserves spaces, rejects percent paths that KService cannot match exactly,
  refreshes an unchanged cache, replaces stale worktree identity, exercises a
  `BUILD` override through the shared restart boundary, and rejects missing
  binaries or an interface list without window-management authority without
  changing the last valid entry. Final real-session acceptance on the exact
  merged build reported `QRect` window-touch roles on all five views, running
  window rows from `viewTasksData`, and one touching window on the partial top
  Panel. That Panel requested attachment, settled at a zero presented gap, and
  rendered flush and full-width over the touching Firefox window.
- SEVERITY: beta blocker.

### D258 - Unavailable hard network mount can block the real-config process
- STATUS: CONFIRMED EXTERNAL; upstream report pending.
- FOUND: 2026-08-01, controlled restart during D257 diagnosis.
- SYMPTOM: a real-config dock can stop answering D-Bus after startup while its
  render threads remain alive.
- ROOT: the real layout contains an `org.kde.plasma.folder` applet. Its
  `KFilePlacesModelPrivate::initDeviceList()` startup path asks KF6 Solid for
  matching devices. Solid constructs an fstab storage-access interface and
  calls `QDir::isReadable()` for `/home/bree/nas` on the GUI thread. The final
  `access("/home/bree/nas", R_OK)` enters `rpc_wait_bit_killable` because the
  NFS4 mount is hard and its server is unreachable. This call is outside
  Latte's window, placement, task, and transition code.
- FIX: no Latte-side masking is appropriate. Report the synchronous device
  accessibility probe to the KF6 Solid/KIO owner and restore normal launches
  when the mount server is reachable or the mount policy no longer permits an
  unbounded GUI-thread wait. The diagnostic run may intercept only this exact
  path to prove unrelated Latte behavior, but that interception is not a
  product fix.
- EVIDENCE: launch-time file syscall tracing stopped at the incomplete
  `access("/home/bree/nas", R_OK)` call. A temporary exact-path access probe
  returned `ENOENT` and printed the caller stack:
  `KFilePlacesModelPrivate::initDeviceList()` -> `Solid::Device::listFromQuery`
  -> `Solid::Backends::Fstab::FstabStorageAccess` ->
  `isNetworkDeviceAccessibleToUser()` -> `QDir::isReadable()` -> `access()`.
  With only that access isolated, the exact merged Latte build remained
  responsive, exposed running task rows, and completed the top-Panel attached
  presentation. No layout or configuration mutation was involved.
- SEVERITY: known issue.

### D255 - Axis-change acceptance could adopt a duplicate publication
- STATUS: FIXED on `main` by `8284accfb`.
- FOUND: 2026-07-31, required independent follow-up review of PR #134.
- SYMPTOM: recipe 073 could miss an identical extra placement publication if
  that publication landed before the first complete sample. The duplicate
  became the test baseline and the later stability check passed.
- ROOT: the recipe captured its comparison revision after mutation instead of
  retaining the accepted revision that preceded the operation.
- FIX: capture the pre-mutation revision and require exactly one increment
  after the old 150 ms coalescer and 500 ms validator deadlines. Preserve the
  prior revision and complete D-Bus state on failure.
- EVIDENCE: the strengthened recipe failed deterministically with revision 4
  advancing to 6, then passed after the duplicate reveal solve was removed.
  Its source guard rejects removal of the exact revision delta.
- SEVERITY: beta blocker.

### D254 - D-Bus substituted target placement after startup
- STATUS: FIXED on `main` by `d9bd9811c` and `8284accfb`.
- FOUND: 2026-07-31, required independent follow-up review of PR #134.
- SYMPTOM: both D-Bus snapshots could report target output, edge, alignment,
  or orientation as accepted state after startup when no accepted placement
  snapshot existed.
- ROOT: both collectors treated every absent accepted snapshot as startup and
  fell back to mutable View and Positioner target fields. Setting
  `inStartup=false` precedes the first accepted solve, so a real post-startup
  interval reached that fallback.
- FIX: retain fallback reporting only during startup. A missing accepted
  placement after startup logs the violated invariant and refuses the complete
  snapshot.
- EVIDENCE: focused D-Bus and source-contract tests pass. Controlled source
  mutations that remove either collector's refusal fail the contract.
- SEVERITY: release blocker.

### D253 - Accepted placement preceded the final QWindow rectangle
- STATUS: FIXED on `main` by `508801032` and `8284accfb`.
- FOUND: 2026-07-31, required independent follow-up review of PR #134.
- SYMPTOM: synchronous geometry observers could consume a newly accepted
  output, edge, and orientation while QWindow still exposed its previous
  rectangle during placement.
- ROOT: `installAppliedGeometry()` replaced the accepted snapshot before
  `applySolvedWindowGeometry()` performed the final resize and position.
- FIX: retain the complete previous accepted snapshot through QWindow resize
  and positioning, then install and publish the new accepted placement.
- EVIDENCE: the source contract requires signal release, final QWindow apply,
  accepted snapshot install, and publication in that order. Its controlled
  premature-install mutation fails. Focused placement tests and two-output
  recipe 073 pass.
- SEVERITY: release blocker.

### D252 - Placement consumers mixed accepted and target dimensions
- STATUS: FIXED on `main` by `68e6cd32f`.
- FOUND: 2026-07-31, required independent follow-up review of PR #134.
- SYMPTOM: during a pending output or axis change, window touch could classify
  the previous applied rectangle with the target edge, orientation, or screen
  id. D-Bus could likewise report retained applied geometry with target edge,
  alignment, or primary-output policy.
- ROOT: Positioner's accepted snapshot retained only output identity and
  geometry. Placement consumers continued to read every other dimension from
  mutable View target state, so the acceptance boundary was atomic only for
  part of a placement.
- FIX: publish output identity, output geometry, edge, orientation, alignment,
  primary-output policy, and the optional live `QScreen` as one durable value
  snapshot. Window tracking and both D-Bus collectors consume those accepted
  dimensions together and refuse a missing post-startup snapshot loudly.
- EVIDENCE: the applied-placement unit pins every durable value with no live
  screen. Source contracts require Positioner install ordering and every
  window-tracking and D-Bus consumer. Nested recipe 073 changes a live view
  across axes, preserves one accepted publication through the old delayed
  deadlines, then passes full-touching, partial-touching, disconnected, and
  restart cases. Recipe 074 passes live titlebar attachment before button
  release for Panel, Center Dock, and expanding Justify Dock. Exact source head
  `dbc9bdd03434ceb0d27e4b48a1a73d0c48b5f94f` passes the canonical gate,
  including all 125 CTest entries and the sanitized nested drive.
- SEVERITY: release blocker.

### D251 - Hot-unplug erased applied output identity before geometry
- STATUS: FIXED on `main` by `2bedd1174`, `46da3e898`, `cabde65ce`, and
  `68e6cd32f`.
- FOUND: 2026-07-31, fresh cold independent review of PR #134.
- SYMPTOM: destroying the applied `QScreen` could make D-Bus report the target
  connector and screen id with geometry retained from the previous applied
  output. Placement consumers could observe the same mixed generation.
- ROOT: Positioner retained output identity only through a `QPointer<QScreen>`.
  Qt correctly cleared that handle on destruction, but the applied surface and
  output rectangle remained. Collectors then fell back to mutable target
  identity while continuing to publish the retained applied geometry.
- FIX: publish connector, stable ScreenPool id, output rectangle, and the
  remaining accepted placement dimensions as one durable value snapshot. Keep
  the `QPointer<QScreen>` only as the optional process-owned live handle
  required for a new placement application.
- EVIDENCE: the applied-placement unit test pins value identity with no live
  screen. D-Bus and source-contract tests require both collectors and
  `View::screenGeometry()` to consume the durable snapshot. Both the coverage
  inventory and its independent count header include the new 125th unit.
  Two-output recipe 073 passes full-touching, partial-touching, disconnected,
  and restart cases.
- SEVERITY: release blocker.

### D250 - Failed LayerShell rollback traffic disappeared from observability
- STATUS: FIXED on `main` by `2bedd1174`.
- FOUND: 2026-07-31, fresh cold independent review of PR #134.
- SYMPTOM: a LayerShell postcondition failure could retarget and then restore
  output, anchors, margins, and exclusion state while the configure-request
  revision reported no compositor-facing traffic.
- ROOT: `applyViewPlacement()` returned `std::nullopt` for every refusal. That
  discarded the setter count accumulated before the failed postcondition and
  during rollback, conflating a mutation-free refusal with a restored apply.
- FIX: return an explicit application result containing independent `applied`
  and `configureRequests` fields. Count guarded rollback setters and publish
  their revision before reporting the failed placement.
- EVIDENCE: the LayerShell sabotage test corrupts the final anchors, verifies
  exact rollback, and requires nonzero configure traffic on the refused result.
  Focused LayerShell, D-Bus, and source-contract tests pass.
- SEVERITY: beta blocker.

### D249 - Completed geometry armed a delayed duplicate publication
- STATUS: FIXED on `main` by `2bedd1174`, `68e6cd32f`, `bdd6cb3a9`, and
  `8284accfb`.
- FOUND: 2026-07-31, fresh cold independent review of PR #134.
- SYMPTOM: an axis-changing placement could publish its correct rectangle and
  then publish again about 650 ms later, adding avoidable geometry traffic and
  visible jitter under repeated resizing or relocation.
- ROOT: Positioner installed the new backing state and cleared its pending bit
  before applying the final QWindow rectangle. The intermediate resize at the
  old origin armed the 500 ms geometry validator. The final position satisfied
  the geometry but did not stop that timer, whose sync coalescer published
  again 150 ms later. The first correction still left a second duplicate path:
  relocation reveal changed `isHidden`, whose delayed-signal hook
  unconditionally solved the already committed final QWindow rectangle again.
- FIX: retain the pending state through the complete QWindow rectangle apply.
  Stop the consumed geometry coalescer, clear the pending state, advance the
  publication revision, and validate only after the final position is
  installed. Startup geometry consumes the same coalesced request. During
  reveal, reuse a completed placement only when both the accepted generation
  and live QWindow rectangle match. An ordinary displaced reveal retains its
  recovery solve.
- EVIDENCE: two-output recipe 073 changes a live view from vertical to
  horizontal, compares against the pre-mutation revision beyond both old timer
  deadlines, and requires exactly one publication with a matching QWindow
  rectangle before separately waiting for full convergence. The strengthened
  recipe first exposed revision 4 advancing to 6, then passed after the reveal
  hook reused the exact accepted rectangle. Focused source and identity
  contracts pass.
- SEVERITY: beta blocker.

### D248 - Output relocation waited for its own QWindow retarget
- STATUS: FIXED on `main` by `2bedd1174`.
- FOUND: 2026-07-31, manual two-output replay after the fresh cold review of
  PR #134.
- SYMPTOM: a cross-output placement could remain hidden in relocation with its
  output component pending indefinitely. The three-panel topology fixture
  stayed assigned to the source output.
- ROOT: transaction completion waited for `QWindow::screenChanged` to clear
  `m_nextScreenName`. The atomic placement boundary intentionally delays the
  QWindow retarget until final geometry application, while final application
  refuses to run until every pending placement component clears. Each side
  therefore waited for the other.
- FIX: acknowledge the staged output immediately after Positioner accepts the
  destination ownership. The later single applied-geometry boundary still
  retargets QWindow and LayerShell together before publishing observers.
- EVIDENCE: the identity contract requires output acknowledgement before edge
  and alignment staging. Two-output recipe 073 then passes initial cross-output
  placement, an axis change, all three output topologies, and persistence
  reload without stranding a view.
- SEVERITY: release blocker.

### D247 - Placement controllers published before LayerShell acceptance
- STATUS: FIXED on `main` by `27704ff82`, `d8a1b093a`, `2bedd1174`, and
  `68e6cd32f`.
- FOUND: 2026-07-31, final cold independent review of PR #134.
- SYMPTOM: a failed placement or direct resize could expose new Panel
  occupancy, window-touch triggers, presentation geometry, or enabled borders
  while the live surface still used its previous placement.
- ROOT: `solveAndApplyGeometry()` synchronously installed or cleared
  FloatingTransition geometry and mutated the QWindow before calling the
  LayerShell placement boundary. Those mutations emitted controller and window
  signals immediately. A refused LayerShell call returned without restoring
  the old controller state, and a successful call still exposed mixed old and
  new state before the later Positioner snapshot update.
  The first correction staged controller geometry but left three paths outside
  that boundary. `setScreenToFollow()` still changed QWindow output and emitted
  screen, containment, absolute-geometry, reservation, and touch observers
  before the delayed solve. `applyViewPlacement()` mutated QWindow and
  LayerShell output, anchors, margins, and exclusion zone without restoring
  them when its postcondition failed. Both D-Bus collectors also paired target
  screen identity with applied Positioner geometry. The applied
  `screenGeometryChanged` signal fed back into `syncGeometry()`, scheduling a
  redundant second application after every real output publication.
- FIX: solve Panel or Dock surface, canvas, presentation, and border state into
  local values. Pass only the staged surface to LayerShell. On success, install
  every backing value before QWindow or controller notifications; on failure,
  discard the stage without mutating runtime state. Dock offset and edit-canvas
  changes use the same solve-and-publish path. Keep configured LayerShell
  QWindows on the previous output until the lower placement boundary runs,
  suppress their placement signals during retarget, install one applied output
  and geometry snapshot, then replay the screen notification. Restore the
  complete prior QWindow and LayerShell state before refusing a failed
  postcondition. Placement-dependent geometry, touch, borders, neighbor
  availability, and D-Bus retain the old applied snapshot while a replacement
  is pending. Output geometry observation now enters the solver directly;
  applied output publication no longer loops back into another solve. The
  completed boundary also owns output-component acknowledgement, durable
  output identity, rollback configure traffic, the final QWindow rectangle,
  validator and coalescer disarm, the accepted edge, orientation, alignment,
  primary-output policy, and the publication revision.
- EVIDENCE: the production binary and focused LayerShell, source-contract,
  D-Bus, panel-border, window-touch, and reservation-publication tests pass.
  The LayerShell test deliberately corrupts the final anchors after all
  production setters run and verifies exact rollback of QWindow output,
  LayerShell output, anchors, exclusive edge, margins, exclusion zone, and
  visibility. The source contract pins signal suppression, install-before-
  notify ordering, applied-output reporting, pending-consumer refusal, and the
  absence of the publication feedback loop. Nested recipe 074 observes stable
  physical state throughout held live attachment. Two-output recipe 073 passes
  one-publication axis change, output topology changes, and restart
  persistence. The pre-review canonical gate passed at exact source head
  `b80afdd79dad724b1841925bdd8da88cf6bd5e93`, including all 125 CTest entries,
  QML and coverage ratchets, scene probes, the ASan/UBSan nested drive, package
  provenance checks, and matrix refusals. The corrected focused tests and both
  nested placement recipes pass at `68e6cd32f`. Exact source head
  `dbc9bdd03434ceb0d27e4b48a1a73d0c48b5f94f` passes the replacement canonical
  gate with all 125 CTest entries, the ASan/UBSan nested drive, package
  provenance checks, and matrix refusals.
- SEVERITY: release blocker.

### D246 - Hidden partial Docks collapsed edge activation to one pixel
- STATUS: FIXED on `main` by `4397109f8`, `e72574be4`, and `dd3f041cf`.
- FOUND: 2026-07-31, final cold independent review of PR #134.
- SYMPTOM: an auto-hidden partial Dock can retain only one pixel of its
  primary-axis reveal strip, making ordinary pointer reveal effectively
  unreachable.
- ROOT: the live-presentation input path always rebuilt its length from
  `effects.rect`. Hidden composited Docks intentionally replace that rectangle
  with the valid `(-1,-1,1,1)` hide sentinel. The mask core then used the
  sentinel's one-pixel span instead of the last stable occupied geometry.
- FIX: hidden and sidebar input consumes retained `localGeometry`; visible Dock
  input alone consumes the animated presentation rectangle.
- EVIDENCE: the QML boundary test combines the 1x1 hide sentinel with an
  800-pixel retained partial span and produces an 800x2 edge reveal strip. The
  source mutation guard rejects both an always-presented input source and an
  always-stable source. All 247 QML interaction checks pass. The qualified
  runtime-view ownership path and its updated source guard keep the QML lint
  ratchet unchanged. Corrected exact source head
  `2049878ae462a4a4efd324a2bd9b5f8922f3f6ea` passes the full canonical gate.
- SEVERITY: release blocker.

### D244 - Live attached Dock presentation leaked into stable geometry
- STATUS: FIXED on `main` by `51eb53c69`, `0322214c1`, and `b8dd08b68`.
- FOUND: 2026-07-31, live-presentation ownership audit and expanded nested
  acceptance after D241 (floating Docks bypassed fractional presentation).
- SYMPTOM: a floating Dock can animate its gap while a partial Justify
  background remains short, keeps rounded end borders, or publishes animated
  paint as occupied geometry. Automatic sizing and another edge view can then
  react to a temporary titlebar drag.
- ROOT: D241 unified the transition scalar but only the screen-edge gap consumed
  it. Maximum length still followed the committed-maximize route, end borders
  were derived from persistent configuration, and QML local geometry fed the
  animated effects rectangle into `absoluteGeometry`, struts, reservation, and
  sizing readback. Reconciliation could also combine a new Dock request with a
  stale touching count because related QML properties notified in sequence.
  The first independent review also found that a Maximum Length change at the
  fully attached endpoint had no paint event to republish stable occupancy or
  recompute the independent touch trigger. Both could retain the old ratio
  until an unrelated geometry event.
- FIX: derive the presented maximum length and rendered endpoint borders from
  the per-view qreal while retaining configured geometry for touch placement,
  automatic sizing, layout clearance, local occupancy, struts, and reservation.
  Input alone follows the animated effects rectangle. Read the touching count
  once and derive the Dock request from that same policy snapshot. Route
  configured-length changes directly to both stable occupancy publication and
  trigger recomputation instead of depending on animated paint signals.
- EVIDENCE: the background, border, mask, D-Bus, identity, source-contract, QML
  interaction, and QML compile suites pass. Nested recipe 074 drives a Panel, a
  partial Center Dock, and an expanding Justify Dock through fractional attach
  and reversal before button release. The Justify presentation reaches the
  output endpoints and drops its rounded end borders while QWindow geometry,
  local and absolute occupancy, struts, reservation, trigger, icon sizes, and
  available resting length remain stable. With automatic sizing disabled, the
  same recipe attaches a 60% Justify Dock, changes Maximum Length to 54%
  through the real edit ruler, and observes occupancy and trigger converge to
  54% while the attached presentation stays full-width and surface, icon, and
  reservation ownership remain unchanged. The replacement canonical gate
  passes at exact source head
  `b8dd08b68e7c02ed1078629f7e19df0dca954618`,
  including all 124 CTest entries, the reduced qmllint ratchet, rendered scene
  probes, ASan and UBSan nested execution, package provenance controls, and
  matrix refusals.
- SEVERITY: beta blocker.

### D243 - Schema 9 refused pre-Metrics startup snapshots
- STATUS: FIXED on `main` by `b9136b0b4`; merged through PR #132.
- FOUND: 2026-07-27, independent review of PR #132.
- SYMPTOM: `dockSystemData()` returns an empty string while a Dock's live QML
  Metrics object is not yet constructed. The startup and teardown states that
  most need atomic observability therefore become unobservable.
- ROOT: schema 9 made `presentedScreenEdgeGap` an unconditional integer and
  treated absent Metrics as a collection failure. Existing live QML sizing
  fields already represent that expected lifecycle interval as JSON null.
- FIX: make the presented gap optional while keeping the wire key required.
  Startup and teardown may report null. A settled ready view requires the
  numeric value, and a constructed Metrics object missing its declared property
  still logs a critical defect.
- EVIDENCE: all 164 D-Bus tests pass numeric and null serialization, accept the
  explicit startup form, and reject the same missing value after readiness.
  The deterministic schema-9 operation model accepts the nullable lifecycle
  form and passes all 31 adversarial tests. The complete canonical gate passes
  at tree-equivalent reviewed branch head `fb9e527a1`.
- SEVERITY: beta blocker.

### D242 - Dock visibility callbacks erased QML-owned effects geometry
- STATUS: FIXED on `main` by `2ac15ad22`; merged through PR #132.
- FOUND: 2026-07-27, independent review of PR #132.
- SYMPTOM: an ordinary hidden or sidebar transition can briefly clear a Dock's
  effects rectangle and mask before QML republishes them.
- ROOT: both visibility callbacks invoked the Panel presentation handoff
  directly. Its Dock branch deliberately clears C++ geometry when ownership
  changes from Panel to Dock, but visibility state is not an ownership change.
- FIX: route ordinary visibility callbacks through the presentation dispatcher.
  Panels retain C++ geometry publication, Docks retain QML paint and input
  ownership, and the destructive handoff remains limited to actual Dock/Panel
  type changes.
- EVIDENCE: the production binary rebuilds. The source contract passes and
  controlled mutations that restore either destructive callback fail. The
  complete canonical gate passes at tree-equivalent reviewed branch head
  `fb9e527a1`.
- SEVERITY: beta blocker.

### D241 - Floating Docks bypassed fractional presentation
- STATUS: FIXED on `main` by `193cf9514`; merged through PR #132.
- FOUND: 2026-07-27, real-session comparison with Plasma after PR #130.
- SYMPTOM: a dragged window reaches a floating Dock in real time, but the Dock
  gap either changes as a separate Boolean animation or waits for a committed
  maximize. Radius and physical-edge border state can disagree with the
  presented gap.
- ROOT: PR #130 supplied the correct live trigger but left presentation
  authority split. `FloatingTransition` stored `dockGapHideRequested` while
  deliberately retaining the `floated` target. QML separately consumed
  `hideThickScreenGap` through its own `NumberAnimation`. Panels and Docks
  therefore observed the same interaction through different state machines.
- FIX: route eligible Docks through the existing per-view qreal transition.
  Dock QML consumes that scalar for the gap while retaining ownership of its
  paint and input geometry. C++ owns target selection and the exact attached
  border endpoint. The Dock QWindow, primary span, output assignment, and
  reservation do not move. Outward reversal retains transition ownership when
  visibility or the attachment setting changes.
- EVIDENCE: 13 transition tests prove current-value reversal, pointer
  deferral, and invalid-request refusal. Five border tests cover every edge and
  alignment plus full-span and partial-span attached endpoints. The schema-9
  D-Bus tests require `presentedScreenEdgeGap` to equal the configured gap
  multiplied by progress and reject impossible target, request, and border
  combinations. Nested recipe 071 passes committed maximize and restore.
  Recipe 074 observes fractional Panel and Dock frames during one button-held
  titlebar crossing and reversal with no QWindow, reservation, layer-shell, or
  tracker-authority drift. The corrected source contract requires direct
  eligibility, running-transition retention, and displaced-progress retention
  independently; removing any arm fails.
- SEVERITY: beta blocker.

### D240 - Operation model omitted the schema-8 screen-edge margin
- STATUS: FIXED on `main` by `5a2948bb5`; merged through PR #130.
- FOUND: 2026-07-26, independent review of PR #130.
- SYMPTOM: the deterministic operation-storm model accepts a schema-8 view
  without `screenEdgeMargin`, accepts the field with an invalid type, and drops
  it from the restart projection.
- ROOT: the D-Bus schema version was raised when the field was added, but the
  required, numeric, and durable model inventories were not extended with it.
- FIX: require a numeric `screenEdgeMargin`, populate it in the canonical
  fixture, and retain it in the durable projection.
- EVIDENCE: `linkedoperationstormmodeltest` passes the canonical replay plus
  controlled missing-field and wrong-type negatives.
- SEVERITY: beta blocker.

### D239 - Schema 8 accepted impossible floating trigger states
- STATUS: FIXED on `main` by `909889a23`; merged through PR #130.
- FOUND: 2026-07-26, independent review of PR #130.
- SYMPTOM: an active floating gap with a zero screen-edge margin passes the
  atomic D-Bus invariant check. A Panel can also retain a stale trigger after
  all transition geometry is absent.
- ROOT: the validator did not encode the runtime view's positive-margin
  implication. Removing the trigger from the generic geometry bundle for
  Dock-specific triggers also removed exact trigger-presence validation for
  Panels.
- FIX: enforce the positive-margin implication directly and require Panel
  trigger presence to match Panel transition-geometry presence exactly.
- EVIDENCE: `dbusreportstest` passes controlled negatives for both impossible
  states and preserves a legal Dock-owned trigger without Panel transition
  geometry.
- SEVERITY: beta blocker.

### D238 - Topology acceptance retained a pre-validation snapshot
- STATUS: FIXED on `main` by `b97ae60ca`; merged through PR #130.
- FOUND: 2026-07-26, corrected-trigger multi-output acceptance.
- SYMPTOM: the first parked-client case can report stable surface drift where
  `geometrySettled` changes from false to true and the surface publication
  revision advances once.
- ROOT: the recipe observed two equal projections, then validated structure
  through a newer atomic snapshot, but retained the older projection as its
  baseline. A generation that settled between those calls made validation
  succeed against one state and comparison use another.
- FIX: recapture the stable projection after structural validation and accept
  the baseline only when it still equals the candidate.
- EVIDENCE: the two-output recipe passes exact separated-span activation,
  spanning-window fanout, maximum-depth reservations, restart persistence, and
  full-touching, partial-touching, and disconnected output arrangements.
- SEVERITY: known issue.

### D237 - Floating Docks waited for committed maximize
- STATUS: FIXED on `main` by `cf976eccd`; merged through PR #130.
- FOUND: 2026-07-26, real-session comparison with Plasma's panel dodge
  animation.
- SYMPTOM: dragging a window toward a floating Dock does not hide the floating
  gap in real time. The gap changes only after KWin commits maximize.
- ROOT: the direct 10 ms `WindowTouchTracker` was constructed around
  `FloatingTransition`, so only Panel transition geometry could feed it.
  Floating Docks still bound `hideThickScreenGap` to the legacy
  `existsWindowMaximized` summary.
- FIX: make the tracker own an explicit per-view trigger. `View` supplies
  stable Panel transition geometry or a Dock envelope solved from that Dock's
  output, edge, exact resting span, attached depth, and configured gap.
  Eligible Docks consume the live count and retain one attached-depth
  reservation while their internal presentation moves.
- EVIDENCE: tracker and schema tests prove per-view isolation and exact
  reconstruction. Recipe 074 observes Dock attachment and reversal during one
  button-held titlebar drag before release, with no surface, reservation, or
  layer-shell publication drift.
- SEVERITY: beta blocker.

### D236 - Floating Panel touch trigger omitted the gap
- STATUS: FIXED on `main` by `da89c1262`; merged through PR #130.
- FOUND: 2026-07-26, source comparison after live Panel attachment still
  lagged Plasma's animation.
- SYMPTOM: a true floating Panel has the direct KWin geometry feed but does not
  begin attaching when a dragged frame enters the visible floating envelope.
  It reacts only after the frame travels through the gap toward the attached
  background, which can resemble a committed-maximize trigger.
- ROOT: Latte expanded the attached background one logical pixel inward.
  Plasma translates the complete stable floating envelope one logical pixel
  toward the workspace and clips it to the output.
- FIX: centralize the translated full-envelope rule in the pure geometry
  solver and use it for placement, tracker readback, and invariant validation.
- EVIDENCE: all four edges, offset outputs, full-depth clipping, overflow
  refusal, and exact rectangles pass under the pure geometry test. Recipes 072
  and 074 pass Panel reversal and button-held live attachment with stable
  physical state.
- SEVERITY: beta blocker.

### D235 - Unanimated layout moves retained a delayed relocation completion
- STATUS: FIXED on `main` by `7b4cc6e98`; the final fresh
  independent review returned `MERGE`; merged through PR #128.
- FOUND: 2026-07-26, fresh critical rereview of the complete corrected FP-4C
  diff.
- SYMPTOM: a same-activity layout move can apply geometry and reservation
  twice, then emit a false critical error that its already committed placement
  generation is stale.
- ROOT: the synchronous `hidingForRelocationFinished` signal changes layout
  and schedules the ordinary delayed last-reposition callback. The unanimated
  branch then commits the same generation immediately without invalidating that
  scheduled completion. Its later callback reapplies the solved state before
  `completeIfCurrent()` rejects the already cleared request.
- FIX: unanimated completion captures the exact committed token and invalidates
  only a matching scheduled completion before publishing reentrant completion
  signals. Generation fields now use the placement token type consistently.
- EVIDENCE: the constexpr state test cancels the completed token and preserves
  a newer scheduled token. The production-wiring contract requires exact-token
  invalidation before completion publication and retains the animated delayed
  path. The production target builds, `placementrequeststatetest` passes 14/14,
  and `dockidentitycontracttest` passes 27/27. The replacement canonical gate
  passes at exact branch head
  `15baaf03426c39e752e814de937681809c4c7e0c`.
- SEVERITY: beta blocker.

### D234 - First transaction-root publication was not durable
- STATUS: FIXED on `main` by `f4594042e`; the final fresh
  independent review returned `MERGE`; merged through PR #128.
- FOUND: 2026-07-26, fresh critical rereview of the complete corrected FP-4C
  diff.
- SYMPTOM: the first cross-layout move after creating
  `.view-move-transactions` can leave duplicate persistent dock state after a
  host crash.
- ROOT: journal promotion flushes the transaction root itself, but creation of
  that root never flushes the parent layout directory that owns its directory
  entry. A crash after destination publication can therefore preserve the
  staged destination while losing the complete journal root needed for startup
  rollback.
- FIX: after ownership, type, and private-permission validation, flush the
  transaction directory and then its containing layout directory before lock
  acquisition, journal preparation, or endpoint mutation.
- EVIDENCE: exact failure injection leaves the transaction root empty, reports
  no pending journal, preserves all origin, destination, and active-owner bytes,
  and leaves all lifecycle generations unchanged. `storagetest` passes 1/1.
  The replacement canonical gate passes at exact branch head
  `15baaf03426c39e752e814de937681809c4c7e0c`.
- SEVERITY: critical release blocker.

### D233 - Nested seed cleanup waited forever on a crash-stopped dock
- STATUS: FIXED on `main` by `c2ef221ca`; merged through PR #128.
- FOUND: 2026-07-26, replacement canonical gate after D230 through D232.
- SYMPTOM: a dock that enters the stopped process state during nested seed
  startup leaves `asan-e2e-gate.sh` waiting forever instead of returning its
  startup failure.
- ROOT: `lib-e2e-seed.sh` sends SIGTERM to the `setsid` leader and immediately
  performs an unbounded `wait`. A stopped process cannot handle SIGTERM until
  it resumes. No bounded poll or SIGKILL escalation exists on this path.
- FIX: seed teardown uses the existing live-member and zombie-aware bounded
  process-group transaction. It polls after SIGTERM and escalates the complete
  setsid group to SIGKILL before any final wait.
- EVIDENCE: `e2eseedcleanupselftest` creates an exact stopped, TERM-ignoring
  setsid leader. The helper reaches bounded SIGKILL cleanup and the test passes
  directly under a 10-second outer timeout and as a CTest in 1.12 seconds. The
  coverage ratchet records the new entry. The replacement canonical gate
  passed all 124 CTest entries, the complete sanitizer build and nested
  recipes, QML and coverage ratchets, visual probes, package provenance
  controls, and matrix refusals at exact source head
  `103c9e4a9f7bd7d87f7ba523a71ff735b30fddc1`.
- SEVERITY: release blocker for acceptance infrastructure.

### D232 - Operation-storm journal assertion never moved a dock across layouts
- STATUS: FIXED on `main` by `c68f4a974` and
  `1c3b86a85`; merged through PR #128.
- FOUND: 2026-07-26, final cold independent review of FP-4C (the
  deterministic operation-storm acceptance).
- SYMPTOM: every settled checkpoint reports an empty durable move transaction
  set even if cross-layout transaction creation or retirement is broken.
- ROOT: the immutable operation plan calls `setViewPlacement` for every move.
  Those operations change output, edge, and alignment but never layout
  ownership, so no durable cross-layout transaction can exist. The source
  mutation proves only that the readback token remains in the script.
- FIX: D-Bus schema 2 exposes process-local decimal-string generations for
  journal creation, durable commit decision, and retirement without changing
  the on-disk schema. The immutable plan activates two layouts and moves the
  independent root to the destination and back through `moveViewToLayout`.
  Each move must increment all three generations by exactly one. Ordinary
  operations must not change them, restart must reset them, and every boundary
  must expose an empty transaction set.
- EVIDENCE: the pure FP-4C model passes all 31 cases and `sourceguardtest`
  rejects removal of the real move or lifecycle assertion. Exact seed
  127934575 completes all 78 operations in nested KWin. Operations 2 and 3
  advance creation, commit, and retirement from 0 to 1 to 2, reload converges,
  and exact cleanup passes in
  `linked-dock-operation-stress.seed-127934575.run-GFqh3X`.
- SEVERITY: release blocker for D229 and FP-4C acceptance.

### D231 - Queued active-view moves could be recorded as committed
- STATUS: FIXED on `main` by `0e2ec0810`; merged through PR #128.
- FOUND: 2026-07-26, final cold independent review of FP-4C (the
  deterministic operation-storm acceptance).
- SYMPTOM: layout settings can mark an active dock move as saved before the
  Positioner reaches the durable cross-layout move. A later persistence or
  placement refusal returns the dock to its prior placement but leaves the
  settings model recorded as successful.
- ROOT: `GenericLayout::updateView()` returns true immediately after calling
  the void `Positioner::setNextLocation()`. The durable `Manager::moveView()`
  decision occurs later inside the Positioner and has no completion result
  path back to `Views::save()`. The source contract test incorrectly pins the
  unconditional true return.
- FIX: every Positioner request now has one generation-tagged terminal outcome:
  committed, refused, superseded, or abandoned. `GenericLayout` forwards that
  exact result. The settings transaction remains dirty until every submitted
  placement commits and retains its persistent warning on refusal. Stale
  callbacks cannot finalize a newer save.
- EVIDENCE: `placementrequeststatetest` passes all 13 cases for immediate and
  delayed refusal, success, supersession, destruction, and stale callbacks.
  `dockidentitycontracttest` passes all 27 cases, and the focused production
  build passes.
- SEVERITY: release blocker.

### D230 - Layout directory entries were not durable before journal retirement
- STATUS: FIXED on `main` by `aa2744787`; merged through PR #128.
- FOUND: 2026-07-26, final cold independent review of FP-4C (the
  deterministic operation-storm acceptance).
- SYMPTOM: a host crash after journal retirement can restore an older
  combination of destination, active-owner, and origin directory entries. If
  origin retirement survives while destination publication does not, the
  complete dock subtree is lost without a recovery journal.
- ROOT: every KConfig publication calls `sync()` and performs fresh semantic
  readback, but no publication flushes the containing layout directory. The
  separate transaction directory is flushed before its journal is retired, so
  the recovery record can become durable before the endpoint renames it proves.
- FIX: each destination, active-owner, and origin publication now flushes its
  containing layout directory after fresh semantic convergence and before the
  transaction advances or retires its journal. A directory-flush refusal
  retains the recovery record.
- EVIDENCE: focused `storagetest` coverage injects directory-flush failure and
  verifies that the journal remains recoverable. The normal commit,
  rollback, and roll-forward matrix passes.
- SEVERITY: critical release blocker.

### D229 - Cross-layout placement could report success after persistence failure
- STATUS: FIXED on `main`; the replacement canonical gate
  passes and the final fresh independent review returned `MERGE`; merged through PR #128.
- FOUND: 2026-07-26, required independent follow-up review of the D227
  placement preflight correction.
- SYMPTOM: a move to a read-only destination layout can remove the dock from
  the origin file, fail to persist destination ownership, return success, and
  lose the dock after restart.
- ROOT: `Manager::moveView()` mutates runtime and durable origin ownership
  before every fallible destination write has committed. Filesystem endpoint
  classification reduces predictable failures but cannot make KConfig
  infallible. A valid, process-owned, readable and writable file may still
  refuse a write because of KConfig immutability, a held lock, parse failure,
  quota or device failure. The post-mutation `qFatal()` therefore converts a
  normal persistence refusal into process termination after ownership may
  already be split.
- FIX: one private checksummed journal captures the complete root and
  subcontainment subtree before any target mutation. Destination persistence
  is staging until the active hidden layout file records destination ownership.
  Origin ownership rolls staging back; destination ownership rolls the move
  forward and retires origin. Runtime layout maps and signal ownership change
  only after persistent convergence. Startup recovers before any layout file
  loads. Settings callers receive and display direct refusal instead of
  committing false-success model state.
- INVARIANTS: layout names and canonical endpoints must remain direct children
  of the layout directory; journals and snapshots are privately owned regular
  files; every affected KConfig group and key must be mutable; all three
  endpoint locks must be immediately available; destination identities must
  be absent before staging; the complete subtree must name one persistent
  owner. Semantic fresh readback followed by a containing-directory flush
  proves every publication. Every queued settings request reaches one exact
  generation-tagged terminal outcome before the model records success.
- EVIDENCE: the pure C++20 transaction test pins destination-first ordering,
  observed-owner authority, rollback, roll-forward, and non-reuse. Real
  KConfig coverage exercises normal commit and journal retirement, a
  legitimately stale standalone origin, file and entry immutability, held
  locks on origin, destination, and active-owner endpoints, unpublished
  `.prepare` residue, rollback and roll-forward interruption after each
  repository publication, mixed subtree ownership, repeated recovery,
  traversal-bearing manifests, and checksum corruption. Production and four
  focused test targets pass. The replacement exact seed 127934575 replay
  completes all 78 operations, invokes the durable transaction twice, advances
  all lifecycle generations exactly from 0 to 1 to 2, reloads, and restores
  exact nested state in
  `linked-dock-operation-stress.seed-127934575.run-GFqh3X`. The previous
  canonical gate passed all 123 CTest entries, QML and coverage ratchets,
  visual probes, the sanitizer nested recipes, package provenance controls,
  and matrix refusals at exact source head
  `311589122215a17c4a00ec1f1edf9dd117819eb9`; D230 through D232 invalidate
  that historical stamp. The replacement canonical gate passed all 124 CTest
  entries, QML and coverage ratchets, visual probes, the complete ASan/UBSan
  build and four nested recipes, package provenance controls, and matrix
  refusals at exact corrected source head
  `103c9e4a9f7bd7d87f7ba523a71ff735b30fddc1`. The next critical rereview
  found D234 and D235. After their corrections, the replacement canonical gate
  passed the same complete matrix at exact branch head
  `15baaf03426c39e752e814de937681809c4c7e0c`.
- ACCEPTANCE: the final fresh critical rereview independently verified the
  first-root durability boundary, rollback and roll-forward authority, exact
  placement completion, surviving typed callers, operation-storm lifecycle
  coverage, and bounded seed cleanup. It returned `MERGE` with no findings.
- SEVERITY: release blocker.

### D228 - Placement preflight promoted a hide-time QWindow observation to output ownership
- STATUS: FIXED on `main` at `992f9df1c`; the canonical
  gate passed at `728285b39` before D229 invalidated that source head.
- FOUND: 2026-07-25, exact seed 127934575 replay after the D226 and D227
  corrections.
- SYMPTOM: operation 33 leaves independent dock 14 hidden on the right edge
  with relocation generation 5 unapplied. The requested top/Justify placement
  never commits.
- ROOT: hiding a right-edge dock temporarily makes Qt report the neighboring
  output. `preparePlacementApplication()` recomputes output application through
  `outputPlacementIsNeeded()`, which includes `QWindow::screen()`. The
  hide-time observation therefore appears as a new output requirement even
  though the assigned output and LayerShell output still own the correct
  destination. The preflight rejects its earlier projection and strands the
  relocation.
- FIX: keep assigned-output and LayerShell mismatches authoritative
  for placement and reservation ownership. A QWindow mismatch may request
  observation reconciliation when projected before relocation, but a
  hide-induced observation must not create output ownership during preflight.
- EVIDENCE: the production build and seven focused placement, identity, and
  reservation tests pass. A negative control that restored the QWindow-aware
  observation predicate to transaction preflight failed
  `DockIdentityContractTest`. The corrected exact seed 127934575 nested-KWin
  replay completed all 76 operations and exact cleanup. Its replay and
  snapshots are saved in
  `linked-dock-operation-stress.seed-127934575.run-P26Fo2`.
- SEVERITY: release blocker.

### D172 - Floating panel attachment moves the surface and reservation instead of presentation
- STATUS: FIXED on `main` by `da89c1262` and `cf976eccd`; merged through
  PR #130.
  PR #128 established the stable-surface architecture on `main`; D236 and D237
  found and corrected the remaining trigger and Dock-feed gaps.
  FP-1
  (the output-edge maximum reservation authority) is merged. FP-2 (the stable
  canvas and transition controller) is merged
  through PR #120, including schema 5 and nested recipe 071 acceptance. The
  independent review returned MERGE and the canonical gate passed at branch
  head `902bba7f8`, rebased as `c10e1756c`. FP-3 (internal presentation,
  input, effects, and popup ownership) is merged through PR #122. Its required
  follow-up review returned MERGE and its canonical gate passed at branch head
  `a7c941db1`. FP-4A (the direct window-touch runtime and single-client nested
  acceptance) is merged through PR #124 at `f8396b5ed` through `5636966b5`.
  FP-4B (multi-output and separated-span topology acceptance) is merged through
  PR #126 at `4daa80121` through `6fa3c5703`. FP-4C (deterministic
  operation-storm acceptance) passes its immutable 78-operation nested replay.
  The independent review's latest-intent, failed-Undo convergence, and
  independently derived reservation-geometry blockers are corrected.
  Execution is tracked in `floating-panel-parity-plan.md`.
- FOUND: 2026-07-24, Plasma 6.7.3 parity investigation after live floating
  panel maximize, radius, shadow, and animation regressions.
- SYMPTOM: a floating Always Visible panel physically moves toward the screen
  edge and changes its reserved thickness when a tracked window maximizes.
  The result can resize or reposition clients twice, retain presentation
  corners at the wrong state, and reverse with geometry-dependent jitter.
- ROOT: presentation, layer-surface placement, trigger geometry, and
  reservation share mutable state. `VisibilityManager.qml` animates
  `Positioner.slideOffset`; `PositionerGeometry` subtracts it from the real edge
  margin; the layer-shell path applies that margin; and
  `BindingsExternal.qml` changes `strutsThickness`. The existing window tracker
  intersects a view rectangle that can move during the transition, while the
  generic window-change path is coalesced for 150 ms.
- REQUIRED: keep one per-view QWindow envelope, layer-shell margin, resting
  applet measurements and primary-axis span, trigger, and normal reservation
  depth fixed. Animate one qreal `floatingness` inside the surface. Internal
  content may translate with the visible background, but it must not refit or
  resize. Derive the visible mask, Fitts input bridge, shadow, corners, and
  popup anchor from the internal presentation. Route ordinary reservation
  through one maximum-depth coordinator per Latte output identity and edge.
- EVIDENCE: Plasma 6.7.3 `PanelView` keeps a stable padded surface and fixed
  exclusive zone while its QML background follows `floatingness`. The source
  comparison and exact Lattecotta mismatch are recorded in
  `../reference/plasma-floating-panel-parity.md`. FP-2's pure geometry,
  transition, legacy placement, screen-geometry, layer-shell idempotence, and
  source-contract tests pass. FP-3 adds exact internal paint, input, effects,
  shadow, popup, and applied-state coverage. Recipe 071 observes qreal progress
  in both directions and eight rapid reversals while the QWindow, applet
  measurements, partial span, reservation, controller geometry generation,
  surface publications, and layer-shell configure count remain stable. FP-4A
  adds exact current-desktop and current-activity window tracking, schema 7
  policy ownership, and recipe 072 single-client interaction while retaining
  those physical invariants. Schema 8 makes the per-view tracker trigger
  authoritative for Panels and Docks. Recipe 074 proves both view types attach
  and reverse during one button-held titlebar drag before release without
  changing physical surface or reservation state.

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
