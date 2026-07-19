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
- STATUS: OPEN (fix - this is the real culprit behind the D15 confusion).
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
- STATUS: OPEN (fix).
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
  directly. Filed as proposal D2 in docs/x11-cleanup-audit.md; not fixed this
  pass (the survivor sweep executes removals only, surfaces behaviour changes).

## Recorded elsewhere - indexed here so the flat scan is complete

These predate the registry and are detailed in their source docs; indexed here
so "what is known broken" is one scan. Full detail migrates on next touch.

### D10 - Tasks config page renders but does not apply its settings
- STATUS: OPEN (inherited upstream half-finished feature; decide wire-up vs hide).
- LatteDockConfiguration.qml still exposes the Tasks config tab; latte-dock-ng
  hid theirs (9faccabda) after judging it non-applying. Detail:
  docs/ng-upstream-audit.md:322, docs/REVIEW_NOTES.md, and CLAUDE.md's
  stub-tracking cautionary tale (this is that exact case).

### D11 - Dev-dock env leak into child Qt apps
- STATUS: OPEN (re-evaluate at Phase 11 packaging).
- QML2_IMPORT_PATH and the stage-first XDG_DATA_DIRS leak into Qt apps LAUNCHED
  FROM the dev dock, so a child app can lose its platform plugin. Distinct from
  the #23 shadow fix (that is about what the dock ITSELF loads; this is about
  child processes the dev dock spawns). Detail: docs/PORTING_PLAN.md ~1724.

### D12 - Plasma lookup-by-id can silently fail on an id mismatch
- STATUS: OPEN/CHECK. An applet whose metadata embedded id mismatches makes
  Plasma's lookup-by-id silently fail. Detail: docs/PORTING_PLAN.md ~2362.

### D13 - Dock blank after display churn
- STATUS: SUSPECTED/UNCONFIRMED (could NOT reproduce as a monitor-sleep bug).
  Detail: docs/REVIEW_NOTES.md "## Open".

NOTE: deferred/STUBBED features are NOT defects and are tracked separately by the
stub discipline (`grep -rn 'STUB:'`): app/infoview.cpp:165 +
app/wm/waylandinterface.cpp:299 (Phase 4 WId), app/layouts/synchronizer.cpp:507
(Phase 8 activity-stop). REVIEW_NOTES.md stays the human review-session log
(Open/Resolved); this registry is the flat defect index that points into it.

## Fixed (kept for the record)

### D14 - invalid-color qCriticals at every startup
- FIXED: this PR. Startup logged a burst of `Tools.colorBrightness: invalid
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
