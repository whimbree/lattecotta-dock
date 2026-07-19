# Stabilization execution prompt (post-extraction continuation)

Re-runnable - KEEP INVOKING THIS PROMPT until every item in the
priority list below reads DONE. Written 2026-07-16 after the QML
extraction initiative completed; REVISED the same day after the first
execution session (items renumbered to reflect what landed - read
docs/tracking/session-handoff.md's 2026-07-16 entry for that session's full
record). It assumes CLAUDE.md has been read (the working agreements,
root-cause law, regression discipline, copyright rules, code-clarity
laws and Q_ASSERT truths there are binding and are not repeated here).

Documentation contract for every session driving this prompt: keep
docs/tracking/session-handoff.md current as work lands (not at the end); tick
plan items with commit hashes the moment they merge; every subagent
writes its ledger file in docs/agent-logs/ (one per dispatch,
YYYY-MM-DD-<slug>.md); anything discovered to need my physical hands
goes into docs/reference/manual-flake-removal-testing.md with a recipe and the
plan item it verifies, NOT silently skipped. A session that ends
without updating this file's status list below (when an item's status
changed) has not finished its job.

## Standing context

- Repo: ~/Projects/latte-dock, GitHub whimbree/lattecotta-dock
  (project name Lattecotta Dock; binary/D-Bus names stay latte).
- docs/tracking/PORTING_PLAN.md is the tracker: its "Where we are" section
  carries the ranked high-priority list; every task is a checkbox with
  a Commits: line - tick and fill as you land work.
- docs/tracking/QML_EXTRACTION_PLAN.md is COMPLETE; its ledger's executed notes
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
docs/tracking/PORTING_PLAN.md; read the full checklist item there before
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
   docs/reference/manual-flake-removal-testing.md.
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
   is in docs/reference/manual-flake-removal-testing.md) plus the two filed
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
11. **DONE 2026-07-17 (code half; PR + review owed) - comprehensive
    CaptSilver attribution audit and fix.** Executed on branch
    captsilver-attribution-audit as the three commits below (f2658c6d6
    SPDX, 90b6d52e9 citations, da933136d README). Pass 3's full-tree scan
    found more than the four Pass-1 files the external review named: the
    activitysetalgebra header (structurally his), iconsourceclassifier,
    panelbackgroundscan, windowtrackingpredicates, extraviewhints, and the
    five sceneprobe files were all missing his SPDX line - 14 DERIVED files
    total. backend.{h,cpp} deliberately gets NO line (Eike Hein's code,
    only capt's vendoring decision followed). The verification pass is
    clean: every DERIVED file carries his line; the five that reference him
    without one are divergence/contrast notes (capt bugs we avoided) and
    the colortools data tags, all correctly exempt. Ledger:
    docs/agent-logs/2026-07-17-captsilver-attribution-audit.md. REMAINING:
    open the PR, lean independent review, merge. Original task spec kept
    below for the record.
    David Goree
    (github.com/CaptSilver/latte-dock-qt6, author email
    davidgoree2003@gmail.com) is owed SPDX-FileCopyrightText credit on
    every file that derives from his work. The CLAUDE.md citation
    policy (the "CITE TRANSPLANTED CODE PROPERLY" section) defines the
    rules. A first sweep landed 2026-07-17 (the captsilver-attribution
    PR) but missed files AND left many citation comments too terse -
    and it used the blanket origin/main commit 81384003 where a
    per-file specific commit is the honest reference. This task
    completes both. FOUR passes, then a verification pass; land each
    pass as its own commit (names at the end).

    PASS 1 - KNOWN MISSING SPDX LINES (confirmed by external review):
    these files self-document derivation from his work in their header
    comments but lack his SPDX-FileCopyrightText line. Add it ABOVE
    Bree's line per the policy:
    - app/view/positionergeometry.h - says "Adopted from capt's
      extraction (latte-dock-qt6 4a829185 at 81384003)"
    - app/screengeometrycalculator.h - structurally derived from his
      header (same filename, same ifndef guard, same includes, same
      ViewFootprint concept)
    - app/layouts/storageidremapper.h - says "capt's interface shape
      (latte-dock-qt6 73f64383) as the starting point", uses his struct
      names (IdRemapInput, IdRemap), field names, and function signature
    - tests/units/activitysetalgebratest.cpp - says "Case list ported
      from capt's activitysetalgebratest (latte-dock-qt6 941bb7fb,
      7 slots)"

    PASS 2 - CITATION COMMENT QUALITY: every file that references his
    work, whether or not it has his SPDX line, must have a citation a
    STRANGER can follow back to source with no context. Comments like
    "capt's interface shape" / "capt blueprint" / "capt's 339-line
    header adopted" are shorthand that assumes the reader knows who
    "capt" is - that is not a citation. Scan every .cpp/.h/.qml for
    comments containing "capt" (case-insensitive), "blueprint",
    "transplant", "adopted from", or "ported from" that reference his
    work, and ensure each includes ALL of: (a) full name "David Goree's
    latte-dock-qt6", not "capt"; (b) full URL
    github.com/CaptSilver/latte-dock-qt6; (c) the specific commit hash;
    (d) the specific source file path in that repo; (e) what was taken
    ("struct interface adopted", "test cases ported", "seam decision
    used", "header adopted verbatim"). GOOD example:
      // Interface shape (IdRemapInput, IdRemap structs and remap()
      // signature) adopted from David Goree's latte-dock-qt6
      // (app/layouts/storageidremapper.h at 73f64383,
      // github.com/CaptSilver/latte-dock-qt6). The function bodies are
      // re-derived from our upstream-inherited storage.cpp.
    Do NOT over-attribute: if a file only took an IDEA and the code is
    fresh, say so ("Extraction boundary informed by David Goree's
    latte-dock-qt6 (app/foo.h, github.com/CaptSilver/latte-dock-qt6);
    implementation is independent"). Also fix docs: scan
    docs/tracking/QML_EXTRACTION_PLAN.md for bare "capt" - expand to at least
    "David Goree's latte-dock-qt6 (github.com/CaptSilver/latte-dock-qt6)"
    on first mention per unit section, with the commit hash and source
    file where applicable; later mentions in the same section may use
    "Goree's" or "latte-dock-qt6".

    PASS 3 - FULL TREE SCAN FOR UNDISCOVERED GAPS: search every
    .cpp/.h/.qml for derivation markers without a David Goree SPDX
    line - "capt" (case-insensitive), "latte-dock-qt6", "transplant",
    "adopted from", "ported from", "blueprint" referencing capt, and
    any header citing a CaptSilver commit hash (73f64383, 81384003,
    4a829185, 941bb7fb, 15a317ff, c94676b9, b48903ec, 5fcaa9f1,
    c903921d, 9003f33a, or any other attributed to latte-dock-qt6).
    For each: if it already has his SPDX line, skip the SPDX step but
    still fix the citation per Pass 2; else apply the CLAUDE.md
    distinction - DERIVED (his expression adapted) gets the SPDX line +
    good citation, IDEA-only (his concept, our expression) gets the
    good citation only, no SPDX line.

    PASS 4 - README AND PUBLIC-FACING DOCS: the Credits entry for
    CaptSilver undersells his contribution ("independently-found fixes
    ... credited in commit messages"). Rewrite it to acknowledge
    specifically: the sceneprobe visual-regression harness was adopted
    from his repo; the testing standard (docs/reference/TESTING.md) was modeled
    on his testing commits; seven QML-extraction units are tagged "capt
    blueprint" and used his seam decisions as the starting point;
    multiple test files were transplanted with his test cases forming
    the foundation of our coverage; his repo is
    github.com/CaptSilver/latte-dock-qt6, author David Goree. Factual
    and concise - a short paragraph, not a wall. Do NOT remove or change
    the Michail Vourlakos, Varlesh, or ruizhi-lab credits.

    VERIFICATION PASS: run
      grep -rn "capt\|CaptSilver\|latte-dock-qt6\|transplant\|blueprint"
        --include="*.cpp" --include="*.h" --include="*.qml" | grep -v
        "SPDX" | head -80
    and for each result confirm (a) the file has a David Goree SPDX line
    if derived, and (b) the citation is stranger-readable per Pass 2
    (full name, URL, commit hash, source file, what was taken). List any
    remaining gaps with justification for why they are acceptable.

    COMMITS (separate): (1) "fix(attribution): add missing David Goree
    SPDX lines on derived files"; (2) "fix(attribution): expand terse
    'capt' citations to full stranger-readable references"; (3)
    "docs(readme): credit CaptSilver's architectural contributions
    properly". Lands as its own feature-branch PR with an independent
    lean review, per the workflow.

## Session protocol

Operate autonomously with a long horizon. For each item: read the plan
item and its phase notes, design before code, tests first where a core
is touched, land in small bisectable commits, run the gates, live-verify
against the running dock (throwaway first, real config for the final
check when the change is user-visible), tick the plan checkbox with
commit hashes, push, and keep docs/tracking/session-handoff.md current so an
interruption loses nothing. When a live check finds a defect, stop and
root-cause it before continuing the checklist - found defects outrank
planned work. Flag anything needing the author's physical input and
move on rather than blocking.
