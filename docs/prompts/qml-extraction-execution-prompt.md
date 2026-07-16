# Session prompt: capt sync + strong-model extraction shortlist

Paste everything below the rule into a fresh strong-model session.
Written 2026-07-15; premises verified against the tree at afddf1ae.

---

Read CLAUDE.md, then docs/session-handoff.md - the MODEL-TRANSITION
PRIORITY STACK at the top. You are executing its item 4: the
strong-model-window shortlist of docs/QML_EXTRACTION_PLAN.md first,
then AS MUCH OF THE REST OF THE PLAN AS POSSIBLE, with one
prerequisite pass before anything (the capt fork sync below). Verify
the tree is clean and HEAD == origin/master before starting. This
session BUILDS and RUNS code, unlike the planning session.

THIS PROMPT IS RE-RUNNABLE AND THE SESSION IS AUTONOMOUS. Each session
picks up from the plan's completeness ledger and the handoff stack:
skip any step already recorded done there. Nobody is watching in real
time - never block on a question; make the call, record it, keep
moving. Failures get root-caused and fixed per CLAUDE.md, not deferred
around; defects the specs flag (bandaids, guards, drift found while
extracting) get fixed at origin during their unit, each as its own
commit with its own evidence.

STEP 0 - CAPT FORK SYNC (bounded; do this before touching the plan).
Follow the latte-fork-sync skill. The reviewed-through hash for
latte-dock-qt6 is 81384003 (recorded in CLAUDE.md). Note: that
repo's working tree is checked out at 9003f33a, BEHIND the reviewed
tip - read everything via `git show <hash>:<path>` after fetching,
never from its working tree.

  cd ~/Projects/latte-dock-qt6 && git fetch origin
  git log --oneline 81384003..origin/main

If new commits exist, read full bodies (`git log --format="%n=== %h %s
===%n%b" 81384003..origin/main`) before judging. You are looking for
two things specifically, beyond the normal sync judgment:
(a) additions to our planning: new test cases on the seven units our
plan blueprints (EX-07/08/09/22/23/24/25) get folded into those specs
as case-list additions (cite the capt hash; fold case lists, never
code), and any NEW pure-unit extraction capt made that our plan lacks
gets judged - worth a unit here means a new EX entry written to the
full section-C template and slotted into the ledger, rank order, and a
wave; not worth it means a one-line rejection with the reason recorded
in the plan's section B de-prioritized list, so the next sync does not
re-litigate it; (b) behavior fixes - remember the standing lesson: check
what OUR tree already decided before folding a fork's fix for the
fork's own bug (their 81384003 layer fix repaired a mistake we never
made; layershellmappingtest documents it). When done, update the
reviewed-through hash in CLAUDE.md and commit the sync record (skill
has the shape). If the range is empty, update nothing and move on.

STEP 1 - WAVE 0 (plan section E, read it plus the Method section and
section D before starting): tests/units/ scaffolding + CMake wiring,
the units/ directories with a README stating the pure-core rules,
scripts/coverage-ratchet.sh + tests/ratchet-baseline wired into
build-check.sh, then EX-22 ActivitySetAlgebra end to end as the proving
unit (smallest spec; exercises header + test + ctest entry + ratchet +
ledger tick + PORTING_PLAN Commits line).

STEP 2 - THE SHORTLIST, IN ORDER: EX-01 PreviewSwitchEngine, then
EX-03 ParabolicMathCore, then EX-02 ParabolicRouter. Read each spec in
full (section C of the plan) before its first commit; each spec is the
contract. Non-negotiables the specs already encode, repeated because
they are the session's failure modes:
- EX-01 lands as two commits (core+tests, then QML cutover + gate
  migration). The preview-contract-gate rule migration is mandatory
  and simultaneous: no invariant may be unpinned in both places, and
  only EX-01 commits may edit scripts/preview-contract-rules.sh.
- Tests come FIRST, from the spec's tables, then the implementation
  greens them. The honest-coverage standard (docs/TESTING.md) applies;
  reference tables generated from current implementations must name
  their generation method in a comment.
- Cutover commits delete the QML logic body they replace (single-copy
  rule). One unit's cutover per commit; rollback is one revert.
- EX-02 is design-first on the bridge handoff; write the design into
  the spec before coding it.

STEP 3 - CONTINUE INTO THE DELEGATE-SAFE WAVES. The waves 2-4 backlog
was written for a weaker model, which makes it trivially executable by
you - keep going, in the plan's wave order (2: C++-only capt ports,
3: QML math cores starting with EX-17 as the calibration unit, 4: the
live-heavy tail), until context or the strong-model window runs out.
You may fan wave-2/3 units out to parallel agent worktrees per the
established workflow (worktree branches, serial merges to master,
prune; the defect-class initiative's shape) - subagents never touch
the live dock; all live verification happens serially in the main
session after each merge. Per-unit done-criteria are unchanged: tests
green, ratchet advanced, live recipe run, Qt5 fidelity confirmed,
ledger + PORTING_PLAN ticked, pushed.

AUTONOMY RULES FOR LIVE VERIFICATION: every spec's live recipe is
designed to run headlessly (fakepointer glides, dumpwins, spectacle,
config round-trips - latte-live-verification has the traps: first
kglobalaccel invoke races, spectacle's own task icon shifts applets,
stale chrome popups eat clicks, the removeView undo window). Run them
yourself. The feel-bearing shortlist units (EX-01/02/03) MUST have
their glide recipes actually run before their cutovers merge - no
exceptions. Only a check that genuinely needs a real mouse or a human
eye (the specs mark none as mandatory-desk, but you may discover one)
gets recorded as a pending live check in the unit's plan entry and in
the handoff's consolidated live-verification list instead of blocking
the unit - the docs/testing/live-only.md pattern, recorded not
papered over.

VERIFICATION RULES (this is where the session earns its keep):
- The plan's line references were verified at 5e1c2b12. Re-verify each
  against HEAD before editing near it; if the tree moved, fix the spec
  in the same commit.
- Live verification per each spec's recipe and the
  latte-live-verification skill: fakepointer GLIDES (~8px steps),
  never coordinate jumps; dumpwins for geometry; screenshots for
  pixels. The real dock is likely running my real config - use the
  throwaway layout via restart-staged.sh for driving, and STOP the
  dock before any rebuild (a running dock executes a deleted binary).
  Restore my real config state (--user-config) at session end if you
  displaced it.
- Full ctest + qml gates green before every push. Push after each
  landed unit, not at session end only.

BOOKKEEPING per unit: tick the plan's completeness ledger, add the
commit hashes to the unit's spec (a Commits line under its heading),
and keep the Phase 10 extraction-initiative item in PORTING_PLAN
pointing true. Commit messages follow latte-conventions: mechanism,
why-at-root, live evidence; no attribution trailers, no em-dashes.

SESSION CHAINING AND THE TRANSITION RULE: when context nears its end,
stop cleanly at a unit boundary, push, tick the ledger truthfully, and
update the handoff stack with exactly where the cut fell - the next
session runs THIS SAME PROMPT and continues from the ledger. If the
strong-model WINDOW (not just the session) closes mid-shortlist, the
unfinished strong-model-only units get DEFERRED markers
(do-not-delegate) in the plan's ledger; the delegate-safe remainder is
simply the next session's starting point either way. Never thin a
spec, skip its live recipe, or hand a strong-model-only unit to the
delegate backlog to finish faster - getting far is the goal, but a
unit half-done to spec beats two done thin.
