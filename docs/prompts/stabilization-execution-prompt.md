# Stabilization execution prompt (post-extraction continuation)

Re-runnable - KEEP INVOKING THIS PROMPT until every item in the
priority list below reads DONE. Written 2026-07-16 after the QML
extraction initiative completed; REVISED the same day after the first
execution session (items renumbered to reflect what landed - read
docs/session-handoff.md's 2026-07-16 entry for that session's full
record). It assumes CLAUDE.md has been read (the working agreements,
root-cause law, regression discipline, copyright rules, code-clarity
laws and Q_ASSERT truths there are binding and are not repeated here).

Documentation contract for every session driving this prompt: keep
docs/session-handoff.md current as work lands (not at the end); tick
plan items with commit hashes the moment they merge; every subagent
writes its ledger file in docs/agent-logs/ (one per dispatch,
YYYY-MM-DD-<slug>.md); anything discovered to need my physical hands
goes into docs/manual-flake-removal-testing.md with a recipe and the
plan item it verifies, NOT silently skipped. A session that ends
without updating this file's status list below (when an item's status
changed) has not finished its job.

## Standing context

- Repo: ~/Projects/latte-dock, GitHub whimbree/lattecotta-dock
  (project name Lattecotta Dock; binary/D-Bus names stay latte).
- docs/PORTING_PLAN.md is the tracker: its "Where we are" section
  carries the ranked high-priority list; every task is a checkbox with
  a Commits: line - tick and fill as you land work.
- docs/QML_EXTRACTION_PLAN.md is COMPLETE; its ledger's executed notes
  name the still-owed live-verification recipes. Do not reopen units;
  new cores follow the same step-2.5 law (sanitized unit tests via
  latte_add_unit_test, QT_FORCE_ASSERTS, type discipline, qmllint
  ratchet strict-on-touch, coverage-ratchet pairing).
- Gates before any merge: full ctest green, tests/coverage/coverage-ratchet.sh,
  tests/coverage/qmllint-gate.sh (baseline only shrinks), both WITH_X11
  variants build. Use `nix develop -c` for all build/test commands.
- Live verification on the author's real Wayland session is authorized.
  Tools: scripts/restart-staged.sh (-d for the throwaway profile,
  --user-config for the real one - ALWAYS restore --user-config when
  done), ~/.local/bin/fakepointer (glide in small steps, never
  jump-click; scroll takes detents+gap), scripts/tools/dumpwins.sh,
  spectacle -b -n, KWin scripting via busctl for window manipulation,
  kwriteconfig6 against the throwaway layout for config flips (edit
  while the dock is STOPPED, then restart).
- Parallel work: spawn worktree subagents per unit of work where tasks
  are disjoint; the orchestrator merges serially (rebase onto master,
  ff-merge, full gates, live checks, ledger/plan tick, push, prune).
  LIMITS (my direction 2026-07-16, cap raised the same day): at most
  4 subagents at a time (not counting the orchestrating session), and
  every subagent keeps a running log of what it did and found in
  docs/agent-logs/ (one file per dispatch, named
  YYYY-MM-DD-<short-task-slug>.md) - the ledger strategy that keeps
  models and subagents accountable over long horizons.
  Merge lessons already paid for: tests/units/CMakeLists.txt and
  plugin-registration files are both-append unions;
  tests/coverage/ratchet-baseline count = union size with a sorted
  entry list; tests/coverage/qmllint-baseline is taken from ours then REGENERATED with --write-baseline
  after a full build; plugins.qmltypes is NEVER hand-merged - always
  regenerate with the qmlplugindump recipe in the file's own header
  against the freshly staged tree, and verify every expected singleton
  name appears before committing.
- Feature branches + PRs from 2026-07-17 (my direction, session
  three): work lands on a feature branch, gates green on the branch
  head, PR to master, rebase/ff-merge (never squash). Push the branch
  after each landed, verified chunk. Prefer new commits over
  amending. Conventional commits with root-cause bodies. Worktree
  subagent branches feed the orchestrator's feature branch; the
  orchestrator's branch is what goes to PR.

## Priority order (status revised 2026-07-16 end of session one)

Work top to bottom over the OPEN items. Each names its home in
docs/PORTING_PLAN.md; read the full checklist item there before
starting - this list is the map, not the territory. DONE items stay
listed so a re-run knows not to redo them.

(Icon note: Varlesh's original icon set and logo are the PERMANENT
choice, through and beyond the Lattecotta package rename - see that
plan item for the recorded decision. Never land replacement icons.)

0. DONE 2026-07-17 (session three) - the re-pin. Executed exactly as
   specified; the Phase 11 plan item carries the full record and
   hashes (c147fbbdb + four follow-ups). Notable deltas from the
   expectations written here: the system had moved AGAIN past the
   incident entry's hash (753cc8a, read off the live generation as
   instructed); the sceneprobe re-bless proved UNNECESSARY (13/13
   bit-exact against committed goldens on the new Mesa - one new
   Qt-RHI validation VUID suppressed instead); the libplasma
   6.6.5->6.7.3 bump changed askDestroy's containment-type guard and
   the contract test caught it (repinned, machinery unaffected by
   construction); and the module-autostarted PACKAGED dock exposed a
   restart-sweep gap (wrapper comm ".latte-dock-wra" never matched -
   fixed, verified live twice). Lockstep guard lives in gate-all.sh
   (exit 4 on pin/system mismatch). The 04:00 nixos-upgrade.timer
   drift story stays my open decision (manual list).

1. DONE - Session shutdown/logout teardown (525f556c6, e02d1bcde,
   9d183984e). One real logout/login check remains in
   docs/manual-flake-removal-testing.md.
2. DONE - Startup retry deadlock closed not-applicable with evidence
   (9df0732f9); latency record stands; autostart decision is mine
   (manual list).
3. PARTIAL - Lock/unlock: short cycle verified clean; the DPMS/
   output-off arm is in the manual list; the ~20s mystery exit and
   the STARTUP-STRANDING defect (new Phase 8 item, instrumented
   538abc8ec) stay armed - when either fires naturally, root-cause
   from the logged trail. The stranding item is the top OPEN
   code-side thread here.
4. DONE - Cloned-view sync (e3fdcae78, f7561df37).
5. DONE - Phase 7 cluster (87417a0c7 detection contract, reorder
   verdict no-change-needed, 480ae30e3 stuck-icons fix, c97c6bb38).
   Real-mouse checks in the manual list; the boundary-insertion UX
   deviation is my call, parked in its plan item.
6. DONE - Phase 4 layer-shell (538abc8ec struts trigger fix; both
   items ticked).
7. DONE (automatable half) - Settings audit: named sub-checks clean,
   isScreenUiReady shim (b8a489c84); the desk walk is in the manual
   list.
7a. DONE 2026-07-17 - X11 removal and cleanup, MERGED as PR #1
   (rebase-merge; post-rebase hashes re-resolved in the Phase 4
   checklist, 20a3c2506..3f857fbff). Independent cold-context review
   verdict was MERGE with two non-blocking leftovers, both filed in
   the Phase 4 follow-up item: subconfigview trackedWindowId's
   !isPlatformWayland fallback and subwindow's visible-hack block -
   isPlatformWayland-SHAPED branches outside the removal's stated
   scope. build-check is single-tree now (~2x faster gate runs).
   Still mine: the byPassWM decision (Phase 4 item).
8. **ORCA-ONLY - the accessibility/automation quartet** (Phase 10
   requirements subsection). The code half is DONE, all merged session
   three via the PR flow: item b's D-Bus interface (session two,
   a3d2afc7c shortcuts-host fix; Meta+N badge desk re-verify still on
   the manual list); the dock-window KEYBOARD FOCUS MODE, the P0 gate
   (view.cpp AcceptingInputStatus + layer-shell OnDemand keyboard
   interactivity, Meta+Alt+D global shortcut, Indexer-driven traversal,
   Escape/focus-loss exit, D-Bus setViewKeyboardNavigation trigger +
   keyboardNavigation readback, shortcutshosttest pin); the
   ACCESSIBLE.* rollout across task items, applet containers, the
   custom controls and edit-mode chrome, with qmltest pins; and item c
   (deterministic e2e conversion) folded scripts/run-e2e.sh + the
   nested-vehicle scripts into a maintained tests/e2e suite with the
   EX recipes and a state-vs-render geometry guard. REMAINING: only the
   ORCA screen-reader acceptance pass (needs my ears - the desk script
   is in docs/manual-flake-removal-testing.md) plus the two filed
   keyboard-focus follow-ups (denied-activation flag staleness,
   cross-view exclusivity) in the Phase 10 keyboard item.
9. **CROSS-MACHINE-ONLY - CaptSilver adoption** (P1+P2, follow-up
   scenes and P3 were done session two; P3b and P4 landed session
   three via the PR flow). P3b: 14 test suites transplanted (raised to
   this tree's bar) with four upstream-inherited origin fixes
   (updateView maxLength, nameOfConfigFile Qt6 chop, stringToRect
   out-of-range, delegate icon -1 sentinel); properly attributed since
   (exact fork commit 81384003 + David Goree SPDX). P4: e2e pixel
   assertions live (latte-imgdiff + KWin ScreenShot2, the
   preview-tooltip golden) in the maintained nested-vehicle suite.
   REMAINING: only cross-machine golden verification, which needs a
   second machine to exist.
10. **DESK-ONLY tail**: DONE session two: WindowId newtype hardening
    and the LATTE_LAYERSHELL_HAS_SET_SCREEN guard. DONE session three
    via the PR flow: the Phase 9 COLOR-GROUP AUDIT (all ~160 theme
    reads across 44 files classified, three items ticked, two fixes -
    the configure-mode popup-collapse TypeError and the unguarded
    defaultTheme deref); the extraction-ledger EX recipes (EX-14
    drags, EX-15 wheels, EX-17 hover) run in the nested vehicle as e2e
    recipes; and the PHASE 8 SURFACE-DRIFT fix (masked docks anchored
    both length edges, 060-geometry-agreement.sh promoted to a
    permanent guard). STILL OPEN: known-bug-list sweep and EX-12/EX-19
    (mostly desk work, see the manual list), Phases 2/3 mechanical
    tail, and the remaining Phase 8 code threads (shutdown/latency/
    lock-unlock/stranding) which are armed and fire naturally.

## Session protocol

Operate autonomously with a long horizon. For each item: read the plan
item and its phase notes, design before code, tests first where a core
is touched, land in small bisectable commits, run the gates, live-verify
against the running dock (throwaway first, real config for the final
check when the change is user-visible), tick the plan checkbox with
commit hashes, push, and keep docs/session-handoff.md current so an
interruption loses nothing. When a live check finds a defect, stop and
root-cause it before continuing the checklist - found defects outrank
planned work. Flag anything needing the author's physical input and
move on rather than blocking.
