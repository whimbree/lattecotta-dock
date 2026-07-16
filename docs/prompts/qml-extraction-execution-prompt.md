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

STEP 2.5 - SANITIZER + TYPE-DISCIPLINE PASS (one-time infrastructure;
gates EVERY remaining wave - no wave 2-4 unit starts until this is
recorded done in the ledger; if the ledger already records it, skip):

1. Stand up an ASan+UBSan build path for the pure-core unit tests
   ONLY (-fsanitize=address,undefined on the tests/units targets and
   their ctest runs; NOT the live dock, NOT the packaged runtime).
   LINKING TRAP, do not fall in it: the sanitized test binaries must
   COMPILE the core sources into themselves (header-only cores get
   this for free; .cpp cores are added as sources to the sanitized
   test target) - linking an unsanitized core object library would
   leave the code under test uninstrumented and make the pass
   theater. Wire it so every existing and future tests/units entry
   runs sanitized by default (they are offscreen and small; no
   unsanitized twin entries, so the ratchet count is unaffected).
   Confirm every core landed so far (whatever the ledger shows -
   expected: EX-22, EX-01, EX-03, EX-02) passes green under the
   sanitizers. A trip is a REAL BUG: root-cause it, never suppress
   it; fix it at origin as its own commit, then generalize to the
   class and sweep sibling sites per CLAUDE.md. Flag any such find
   prominently in the session report and handoff; actually pause and
   surface (rather than fix-and-continue) only if the root cause
   implicates already-shipped cutover BEHAVIOR, not just the core.
2. Language standard, verified not assumed: check what the tree's
   CMake/KDECompilerSettings actually sets (KF6 normally mandates
   C++20). Cores are headers consumed by app plugin targets, so
   their standard is the consuming targets' standard - never fork it
   per-target. If the tree is already C++20, nothing to do; if not,
   raising it is a build-system change with blast radius: verify
   with a full build-check on both WITH_X11 variants before
   anything else rides on it. Note: std::expected is C++23 - the
   invalid-states rule below is stated in terms of what the standard
   in force provides (optional, enum class, strong typedefs,
   bounds-checked access), not a wishlist.
3. Amend docs/QML_EXTRACTION_PLAN.md ONCE (Method section + section
   D), binding all not-yet-landed units: every unit's done-criteria
   become (a) logic extracted to a pure core in the project's C++
   standard, (b) invalid states made unrepresentable via the type
   system - optional/expected-where-available, enum class, strong
   typedefs, bounds-checked access, no lifetime-escaping lambdas -
   with the eliminated invalid states NAMED in the unit's ledger
   entry at landing time, and (c) a behavioral test pinning the
   class invariant, green under ASan+UBSan. Do not rewrite the 22
   individual specs; the Method amendment governs them.
4. Hold the fences: the sanitizers and type discipline apply to the
   NEW pure cores and their tests only - no repo-wide modernization
   of inherited Latte code, no sanitizing the live dock, and live
   verification stays mandatory for feel-bearing logic exactly as
   the specs say.
5. Record it as standing law: docs/TESTING.md gets the rule ("pure
   cores are written to the project C++ standard with invalid states
   designed out; their tests run under ASan+UBSan"), CLAUDE.md gets
   one pointer line, and the ledger gets a STEP-2.5 entry so every
   later session knows this landed. Commit and push at this boundary
   before starting any wave unit.
6. QML typing, ratcheted not big-banged. "Strongly typed QML
   project-wide, no exceptions" is rejected as stated, for recorded
   structural reasons: winId properties are var BY DESIGN (8e8cdf31 -
   wayland string ids vs X11 decimal-string ids; an int annotation IS
   the bug), task-model roles in delegates are dynamically typed by
   construction, the indicator API hands third-party packages an
   untyped `indicator` context property (public API; strict mode
   cannot resolve context properties and changing the API breaks
   user-installed indicators - this is also the family-2 surface),
   and the AppletAlternatives mirror stays minimal-diff against
   plasma-desktop. Enforce the enforceable version instead:
   (a) stand up a qmllint gate using the PINNED Qt's qmllint (verify
   it exists in the devshell; never the host's) with a curated rule
   set - unqualified access, untyped function signatures and
   parameters, unresolved types, deprecated syntax - run over the
   package QML the way qml-compile-gate enumerates it;
   (b) baseline the current per-file warning counts into a committed
   file; the gate FAILS on any increase, and the baseline only ever
   shrinks - same ratchet law as tests/ratchet-baseline;
   (c) STRICT-ON-TOUCH: every QML file a cutover commit touches
   leaves at ZERO warnings - typed function signatures on the thin
   shells, qualified access, required properties where a shell takes
   injected objects, no new var except the documented winId class -
   so the extraction's thinned shells are born strict and the
   baseline shrinks wave by wave instead of by a rewrite campaign;
   (d) files that can never reach zero for a structural reason get
   the reason recorded next to their baseline entry - the exception
   is named, never silent (the live-only.md pattern). Fold this in
   as done-criterion (d) of the point-3 plan amendment, and note in
   TESTING.md that full-strict QML is the asymptotic state the
   extraction converges to, not a mandate on inherited files.

STANDING LAW FOR ALL CODE WRITTEN UNDER THIS PROMPT (CLAUDE.md carries
the same rules under "Code clarity"; this restates them where the
executing session reads its orders, because they bind every wave):

- Write for the reader who arrives with zero context. Reading is the
  dominant cost of this codebase and misread code is where bugs
  breed: clarity is a correctness tool. Every name that must be
  decoded, every function that must be opened to learn its job, is
  cognitive budget stolen from the actual problem.
- Names say what the thing DOES, so call sites read like English.
  _rowKinds() is this repo's own counterexample (it built the
  router's row model from the task items; the name said neither).
  A thing that is hard to name is usually two things - split it.
- Small single-purpose functions. A block that needs a WHAT comment
  is a function that wants extracting; comments are for WHY.
- New C++ is idiomatic C++20: RAII, managed memory (unique_ptr/
  shared_ptr, or Qt parent ownership inside the Qt object tree -
  never shared_ptr around QObjects against the parent system), no
  naked new/delete/malloc/free, enum class, optional/variant per the
  step-2.5 type law, const correctness.
- Where code stays rough after real refactoring effort (irreducible
  domain complexity, protocol mirrors, hot paths), the comment
  carries the data patterns and invariants the code cannot show.
  Non-obvious optimizations are explained in place: what they buy,
  what regresses if "simplified". Unexplained cleverness gets undone
  by the next cleanup pass and unexplained complexity gets guessed
  at - both are how understanding rots.
- Assert invariants often and early: a precondition checked at entry
  fails at the defect, not three subsystems away. Preference order:
  unrepresentable via types (the step-2.5 law), static_assert at
  compile time, Q_ASSERT preconditions inside cores, and
  qCritical-and-refuse at boundaries where bad input arrives from
  outside. An assert is documentation that cannot go stale.
- These rules extend, never replace, the existing working agreements:
  match surrounding idiom in inherited files, Qt5-faithful behavior,
  no silent guards, stub discipline.

STEP 3 - CONTINUE INTO THE DELEGATE-SAFE WAVES (only after STEP 2.5
is recorded done). The waves 2-4 backlog
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
