# Session prompt: QML extraction planning

Paste everything below the rule into a fresh strong-model session. Written
2026-07-15; premises verified against the tree at b4f5621c.

---

ROLE: You are planning a major architectural refactor for latte-dock (this
repo, the Plasma 6 / Qt6 port). Do NOT write implementation code. This
session's only deliverable is `docs/tracking/QML_EXTRACTION_PLAN.md`, committed and
pushed, plus one cross-reference checklist item added to
`docs/tracking/PORTING_PLAN.md` so the plan stays on the traceability spine.

CONTEXT THAT SHAPES EVERYTHING: this plan will be executed across a model
transition. A strong model (you) is available for only a few more days;
development then continues on a weaker model. The plan's job is to convert
judgment into artifacts: specs a weaker model can execute cold, tests that
catch its mistakes, and an explicit split between work that is safe to
delegate and work that must land before the transition.

GOAL
Move behavioral logic out of QML into strongly-typed, unit-testable C++,
pinned by tests, using CaptSilver/latte-dock-qt6's testability campaign as
the reconnaissance map and our own bug history as the priority signal. End
state: QML thins toward declarative view; math, state machines, ordering,
and geometry live in typed C++ behind clean seams. Target the swamp, not
the ocean: the parts of the codebase where our hardest bugs actually breed.

POSTURE (decided, recorded in CLAUDE.md as of ce94bb1d; do not relitigate)
This repo is a maintained continuation; upstream KDE latte-dock is dormant
and mergeability upstream is not a constraint. What IS non-negotiable:
small bisectable commits (a debugging tool, not etiquette), Qt5 behavioral
fidelity per extraction, and re-implementation with understanding. capt/ng
are blueprints of WHAT to extract and WHICH invariants to pin, never
sources to paste.

NON-NEGOTIABLE CONSTRAINTS (from CLAUDE.md; apply throughout)
- Qt5 Latte behavior is the spec. The concrete fidelity mechanism: the Qt5
  source is in OUR OWN git history. Read it at tag f0ad7b23 (v0.10.8) via
  `git show f0ad7b23:<path>`; each unit's spec cites the Qt5 file/lines it
  must match. Platform-forced deviations get named.
- No bandaids: no polling timers, silent early-returns, value-hiding
  clamps, or stubs. Where QML logic only "works" via such a hack today,
  flag it as a defect to fix during extraction, not carry over.
- Root cause over symptom: where QML logic papers over a Qt6/Wayland
  issue, name the underlying cause and plan the real fix.
- Author's voice; no AI attribution; no em-dashes.
- Live verification for feel-bearing logic, per the latte-live-verification
  skill (fakepointer GLIDES, never coordinate jumps; that lesson is written
  in the skill; dumpwins; screenshots; config round-trips).
- Evidence discipline: every commit hash, file path, and line reference
  must be verified with git/grep during this session before it lands in
  the doc. "Not read deeply, evidence insufficient, needs a follow-up
  read" is a legal and preferred answer over confident filler.

INPUTS TO MINE (read before planning)
1. The repo skills: latte-architecture (subsystem map),
   latte-plasma6-defect-families (the recurring bug classes; these are the
   extraction targets' rap sheets), latte-conventions,
   latte-live-verification.
2. QML under containment/, plasmoid/, shell/, indicators/,
   declarativeimports/. Depth budget below.
3. Bug history, freshest first: docs/tracking/session-handoff.md. The 2026-07-15
   preview saga (c6eeeb20..45c15433) is the densest hot-spot evidence in
   the repo: seven QML-logic defects in one subsystem in one day, each
   with a measured root cause. Then the hardening sweeps (silent-break,
   class-A stranding, loops/degenerate-values), edit-mode, rearrange
   drift, colorizer (38734328, 5c06b497), indicators. Read fix-commit
   BODIES for the hot files, not just subjects.
4. Existing test infrastructure. Inventory it, extend it, do not
   re-invent it: tests/ (C++ ctest targets), tests/qml + tests/contracts
   (drive the REAL shipped QML), scripts/qml-interaction-tests.sh,
   qml-compile-gate.sh, qml-effect-rules.sh,
   preview-contract-rules.sh (b4f5621c: the ten pinned previews
   invariants; your plan must not propose changes that fight this gate
   without saying so), build-check.sh, and the honest-coverage standard
   in docs/reference/TESTING.md. The dialog resize-session work (d12baff2) is a
   recent QML<->C++ seam done in our tree; study its shape.
5. capt (~/Projects/latte-dock-qt6, reviewed through 81384003): the
   extraction campaign. storageidremapper.h, activitysetalgebra.h,
   screengeometrycalculator.h, iconsourceclassifier.h,
   positionergeometry.h, windowtrackingpredicates, extraviewhints. For
   each: what logic it pulled, from which original file, the pure
   interface, what its tests pin. These units do not exist in our tree;
   that gap is the work. Where you infer capt's intent, say so and cite
   the capt commit.
6. ng (~/Projects/latte-dock-ng): note where ng's jank (the edit-mode
   polling timer) reveals logic that belongs in typed C++ with a proper
   signal.

COMPREHENSIVENESS CONTRACT (the plan must be complete; honesty governs HOW)
The finished plan covers EVERYTHING: every QML file classified in the
inventory, and a full spec for every unit that makes the extraction
backlog - not a top-N. Completeness is achieved structurally, never by
compressing quality:
- Delegate BREADTH to read-only Explore subagents: fan out the inventory
  sweep (file classification, logic typing, S/M/L sizing) across
  containment/, plasmoid/, shell/, indicators/, declarativeimports/ while
  the main session keeps its context for judgment (ranking, specs,
  seams). Verify subagent-reported line references yourself before citing.
- Write per-unit specs in RANK ORDER, highest leverage first, so the
  document is maximally valuable at every intermediate state.
- Maintain a COMPLETENESS LEDGER section at the top of the document:
  which directories are fully inventoried, which specs are DONE, which
  are PENDING with a one-line scope note. The ledger is updated as you
  go, not at the end.
- If the session approaches its limits before the ledger is clear, STOP
  writing specs cleanly at a unit boundary, commit, push, and update the
  handoff stack so the NEXT session continues from the ledger - a second
  planning session is an expected and budgeted outcome, silent
  truncation or thinning specs to finish is not. Never pad a spec with
  unverified filler to close the ledger faster.

WHAT TO PRODUCE - docs/tracking/QML_EXTRACTION_PLAN.md:

A. QML LOGIC INVENTORY. Classify per file: geometry math / state machine /
   ordering / model transform / event routing / pure presentation; S/M/L
   extractable size; explicitly separate "genuinely presentational, leave
   in QML" from "behavioral, extraction candidate."

B. HOT-SPOT RANKING. Score on three axes: bug-density (count past fix
   commits touching the logic, verified hashes cited), testability-gain,
   feel-risk (higher = heavier live verification, sequenced later).
   Expected swamp: preview adoption/anchoring, positioner geometry,
   visibility/dodge state machines, parabolic zoom math,
   rearrange/ordering, colorizer, storage id-remap, screen geometry.
   De-prioritize purely-drawing QML.

C. PER-UNIT EXTRACTION SPECS (every backlog unit, written in rank order
   per the comprehensiveness contract). Each contains:
   - Unit name, header location, one-line responsibility
   - Source QML file/lines (verified)
   - EXTRACT-VS-PIN JUSTIFICATION: why extract to C++ instead of pinning
     the logic in place with the existing qmltest/contract harness (which
     drives real shipped QML and has caught real regressions). "Pin in
     place, don't extract" is a fully acceptable verdict that moves the
     unit off the extraction backlog into a test-only task.
   - Interface / DI seam: plain values in, plain values out; no
     QObject/scenegraph/binding deps in the core; how the QML thins to
     feed values and render results
   - capt cross-reference where one exists, with divergence notes for OUR
     architecture (C++ input-region, kpipewire previews, layer-shell, the
     LRU'd preview delegate cache)
   - Test plan: specific invariants and edge cases (mine capt's analogous
     test for the case list); where the test must ride the extraction
     seam vs. can precede it
   - Qt5-fidelity check: the f0ad7b23 file/lines the C++ must match
   - Live-verification recipe for feel-bearing units
   - DELEGATION TAG: `delegate-safe` (pure logic, exhaustive tests, low
     feel risk; executable by a weaker model from this spec alone) or
     `strong-model-only` (feel-bearing, judgment-heavy; must land in the
     next few days or be deferred, never delegated)
   - Risk + bisect/rollback story

D. COVERAGE + TEST INFRASTRUCTURE. How new units plug into the EXISTING
   harness and the honest-coverage standard; a coverage-ratchet baseline
   enforced by scripts/build-check.sh locally (no CI exists yet; design
   the ratchet so CI can adopt it later without rework).

E. SEQUENCING INTO WAVES. Ordered waves sized for parallel agent
   worktrees (the established workflow: worktree branches, merge to
   master, prune). Shared type/enum extractions first. THE TRANSITION
   RULE GOVERNS ORDER: strong-model-only units are scheduled inside the
   remaining strong-model window or explicitly deferred with a "do not
   delegate" marker; delegate-safe units form the post-transition
   backlog, each self-contained. Per wave, done = tests green + ratchet
   advanced + live-verified + Qt5-fidelity confirmed + pushed.

F. RISK REGISTER + NON-GOALS. Binding-entangled logic flagged
   "design-first, extract-second" rather than forced; feel-regression
   risk with live-verify mitigation; non-goals: presentational QML,
   ocean-boiling, bundled feature work.

END with a one-screen executive summary: top 5 extractions, the wave
order, the strong-model-window shortlist (what must land before the
transition), and the single biggest risk.

METHOD: each spec implementable from cold by an agent with no memory of
this session and no ability to re-derive the judgment behind it. Commit
the doc and the PORTING_PLAN cross-reference; push.
