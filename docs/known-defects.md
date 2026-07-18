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

### D1 - Aborted task-reorder does not revert
- STATUS: SUSPECTED (found by adversarial code-reading; the C-A3 abort scenario
  will confirm).
- FOUND: 2026-07-18, adversarial abort design (PR #31).
- SYMPTOM: dragging a task across a neighbour commits the reorder immediately;
  Escape or release-back does NOT revert it. Only a zero-cross drag is a true
  no-op.
- EVIDENCE: plasmoid/package/contents/ui/taskslayout/MouseHandler.qml:184 -
  tasksModel.move() is called live inside onDragMove.
- DISPOSITION PENDING: is this Qt5-faithful (KDE's TasksModel is a live-move
  model with no transactional revert) or a Qt6 regression? Qt5 Latte is the spec
  (CLAUDE.md working agreement) - check its aborted-task-drag behaviour before
  deciding FIX vs ACCEPTED. The C-A3 scenario asserts whatever is decided; if
  revert is intended, it stays RED until fixed.

### D2 - ConfigOverlay applet drops from appletOrder on edit-exit mid-drag
- STATUS: SUSPECTED (adversarial code-reading; C-A2 will confirm).
- FOUND: 2026-07-18, adversarial abort design (PR #31).
- SYMPTOM: exiting edit mode WHILE an applet is mid-drag leaves that applet out
  of appletOrder - onEditModeChanged rescues the dndSpacer but not the
  in-flight ConfigOverlay applet; the ConfigOverlay onReleased path always
  commits and has no abort branch.
- EVIDENCE: the A2 family analysis in docs/e2e-interaction-test-plan.md; the
  ConfigOverlay QML (press z=900, onReleased always-commit).

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
