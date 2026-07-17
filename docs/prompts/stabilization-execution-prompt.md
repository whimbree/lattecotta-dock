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
- Gates before any merge: full ctest green, scripts/coverage-ratchet.sh,
  scripts/qmllint-gate.sh (baseline only shrinks), both WITH_X11
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
  plugin-registration files are both-append unions; tests/ratchet-
  baseline count = union size with a sorted entry list; tests/qmllint-
  baseline is taken from ours then REGENERATED with --write-baseline
  after a full build; plugins.qmltypes is NEVER hand-merged - always
  regenerate with the qmlplugindump recipe in the file's own header
  against the freshly staged tree, and verify every expected singleton
  name appears before committing.
- Push after each landed, verified chunk. Prefer new commits over
  amending. Conventional commits with root-cause bodies.

## Priority order (status revised 2026-07-16 end of session one)

Work top to bottom over the OPEN items. Each names its home in
docs/PORTING_PLAN.md; read the full checklist item there before
starting - this list is the map, not the territory. DONE items stay
listed so a re-run knows not to redo them.

(Icon note: Varlesh's original icon set and logo are the PERMANENT
choice, through and beyond the Lattecotta package rename - see that
plan item for the recorded decision. Never land replacement icons.)

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
8. **OPEN - the accessibility/automation quartet** (Phase 10
   requirements subsection; requirements, not polish). Standing:
   b's interface is DESIGNED (docs/dbus-observability-interface.md)
   and step 1 is LIVE (viewsData + setViewEditMode, merged and
   live-verified); a+d have their baseline inventory recorded in the
   plan items (docs/agent-logs/2026-07-16-a11y-surface-inventory.md,
   P0 gate = the dock window's keyboard-focus mode). Remaining, in
   order: interface steps 2-4 per the design doc (viewAppletsData +
   activateTaskAt; trackerData + setViewVisibilityMode; viewTasksData
   + colorizerData + layoutsData - each step lands with a busctl
   smoke and converts e2e checks as it goes, which IS item c); then
   the keyboard focus mode for the dock window; then Accessible.*
   rollout per the inventory's gap list with qmltest pins; the Orca
   pass is acceptance and needs my hands at the end.
9. **OPEN - CaptSilver adoption, remaining waves** (the analysis and
   P1+P2 are DONE and merged: sceneprobe runs headless on lavapipe
   with bit-exact goldens, six contract pins landed including the
   alternatives createApplet live-bug fix). Remaining per
   docs/captsilver-testability-adoption.md: P3 behavioral tests over
   lattedock-core (screenpool and visibility-reveal first - they
   serve Phase 8), P4 e2e pixel assertions (latte-imgdiff + KWin
   ScreenShot2, composes with the new D-Bus surface), follow-up
   scenes (parabolic_zoom with a PINNED font, colorizer stack,
   monochromatic icons, indicator glow), and cross-machine golden
   verification when a second machine exists.
10. **OPEN - the tail**: Phase 9 color-group audit (surveyed: ~12
    files read Kirigami.Theme colors; audit each site's theme-object
    scope), WindowId newtype hardening (its own pass, wm/-wide blast
    radius), known-bug-list sweep (mostly desk work, see manual
    list), extraction-ledger live-verification recipes not yet run,
    Phases 2/3 mechanical tail. Also two small hardening finds from
    the agent runs: build-check's toolchain guard landed (151a8c829
    lineage) but the unguarded LATTE_LAYERSHELL_HAS_SET_SCREEN
    try_compile still re-probes every configure and can silently
    flip the define after one broken-env run - guard it or make it
    fail loudly.

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
