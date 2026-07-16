# Session prompt: capt sync + strong-model extraction shortlist

Paste everything below the rule into a fresh strong-model session.
Written 2026-07-15; premises verified against the tree at afddf1ae.

---

Read CLAUDE.md, then docs/session-handoff.md - the MODEL-TRANSITION
PRIORITY STACK at the top. You are executing its item 4: the
strong-model-window shortlist of docs/QML_EXTRACTION_PLAN.md, with one
prerequisite pass first (the capt fork sync below). Tree is clean at
afddf1ae, HEAD == origin/master. This session BUILDS and RUNS code,
unlike the planning session.

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

THE TRANSITION RULE: if the session (or the strong-model window) ends
mid-shortlist, stop cleanly at a unit boundary, push, mark the
unfinished units DEFERRED with their do-not-delegate markers in the
plan's ledger, and update the handoff stack so the next session knows
exactly where the cut fell. Never thin a spec, skip its live recipe,
or hand a strong-model-only unit to the delegate backlog to finish
faster.
