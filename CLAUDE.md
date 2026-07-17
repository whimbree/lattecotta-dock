# Lattecotta Dock (Plasma 6 / Qt6 port)

## What this is

Lattecotta Dock ("Art in Coffee, kiln-fired for Plasma6/QT6"): a
from-scratch Plasma 6/Qt6 port of upstream KDE latte-dock
(`invent.kde.org/plasma/latte-dock`), forked to
`whimbree/lattecotta-dock` on GitHub with full original history intact -
not derived from either community fork below. Package, binary and
D-Bus names still say latte; renaming them is a deliberately deferred
task in the porting plan's continuation section.

## End goal

Two things, explicitly, not in tension: a production-grade daily-driver
dock, and a learning vehicle for me, the author (new to Qt6/KF6 and its
moving parts). This is a MAINTAINED CONTINUATION (decided 2026-07-15):
upstream KDE latte-dock is dormant, and "would upstream merge this" is
not a planning constraint - the PORTING_PLAN's continuation-features
section already operated under this framing, and architecture decisions
may diverge from upstream's when the divergence is understood and
recorded. The clean-history discipline stays on its own merits, not as
upstream etiquette: small bisectable commits are a debugging tool I use
constantly (git log -S dated the preview unmap workaround's stale
premise to the hour), and well-shaped commits keep patch exchange with
any other continuation cheap if that ever becomes worth doing.

Explain Qt6/KF6/QML concepts as they come up rather than just applying
fixes silently - this is explicitly wanted, not a nice-to-have.

## Why this repo exists instead of adopting an existing fork

Two community Plasma6 ports already exist:
`~/Projects/latte-dock-ng` (ruizhi-lab, Wayland-only, history reset by
its maintainer) and `~/Projects/latte-dock-qt6` (CaptSilver, also
Wayland-only despite some leftover dead `isPlatformX11()` branches in
the code - its own commit `1cef7fe7` confirms the X11 backend was
actually removed - real original history preserved). Both were
evaluated in depth
(build + run + live crash debugging, not just reading commit messages)
before deciding to start fresh - see
`~/Projects/latte-dock-ng/docs/fork-comparison-journal.md` for the full
writeup. Summary: both forks have real, live, user-facing bugs found
through actual testing (not just static analysis), and latte-dock-ng's
reset history makes it a bad base for something meant to eventually go
back upstream. Both are kept as **reference material** - read their
diffs for a given subsystem, take what's actually correct, discard the
rest, write new commits.

## Plan

`docs/PORTING_PLAN.md` has the real detail - it's the product of
reading every commit body (not just subjects) across both reference
forks' full port-work ranges (406 commits in latte-dock-ng, 194 in
latte-dock-qt6), cross-referenced against live testing of both. 13
phases (0-12): build environment/testing ground rules -> build system
-> mechanical Qt6 conversion -> KF6 migration -> window-system
backends (Wayland-only; X11 removal decided 2026-07-17 - KDE ships
Plasma 6.8 Wayland-exclusive in October 2026, the Phase 4 section
has the full rationale and removal checklist) ->
QML controls/rendering -> task manager -> widget
management/drag-drop/edit-mode -> layout/shutdown/multi-screen ->
theming polish -> stabilization -> Nix packaging -> upstream prep.

Phase 7 (widget management/drag-drop/edit-mode) and Phase 8 (shutdown/
multi-screen) carry the most specific gotchas - both reference forks
needed many repeated fix attempts there (edit-mode detection alone took
8+ tries in one fork). Read those sections before touching either
subsystem, not just the phase list here.

**The plan is a checklist, not prose to read once.** Every task is a
`- [ ]` with a `Commits:` line. When a task lands, tick it and fill in
the commit hash(es) - that's the traceability mechanism for the whole
port: any checklist item names its commit(s), any commit can be traced
back to the task it was for. 127 atomic items across the 13 phases as
of the last plan revision. Keep this
in sync as work happens - don't let it drift into "mostly done, some
stale checkboxes."

## Working agreements

- Behavior is Qt5-faithful: when the port and Qt5 Latte disagree on how
  something behaves (defaults, semantics, what a control adjusts, what
  is drawn where), Qt5 Latte is the spec unless a platform constraint
  genuinely forces a change - and then the deviation gets a comment
  saying what forced it. Both reference forks reinterpreted behaviors
  in ways users notice (e.g. edit-mode grid opacity rewired from
  editBackgroundOpacity to panelTransparency); read the Qt5 source
  before accepting a fork's version of any behavior.
- Never add Co-Authored-By or other AI attribution to commits (global
  preference, not specific to this repo). This extends to PR bodies:
  never append a "Generated with Claude Code" line (or any tool-
  attribution footer) to a pull request description, even if a harness
  default suggests it (my direction, 2026-07-17 - it was stripped from
  PRs #2 and #3 after the fact).
- Everything committed is written in the author's voice: commit
  messages, docs, and code comments never say "the user", "the owner",
  "user-reported" or "user-verified" as if reports arrive from a third
  party - I am the author, reporter and verifier. Write "caught live",
  "reproduced twice", "verified with a real mouse", "my real config".
  Generic end-user prose in UX comments ("shown long enough for the
  user to observe") is fine and matches upstream style; the ban is on
  operator framing, not on the word itself. Already-pushed messages
  get reworded at the pre-PR history cleanup, not amended now.
- No em-dashes, no AI-sounding marketing-style phrasing in docs, commit
  messages, or code comments - write plainly, like a programmer.
- EVERY major feature lands as its own GitHub PR (my direction,
  2026-07-17, reinforced after several features were reviewed but
  ff-merged locally without opening a PR - that shortcut is banned).
  A major feature is any code landing beyond a docs tick or a
  one-line fix: a new subsystem, a harness, a defect fix with its
  test, a transplant wave, a phase's worth of work. The flow is:
  branch off master -> push to origin (whimbree/lattecotta-dock) ->
  open a real PR (`gh pr create`, installed via `nix run nixpkgs#gh
  --`) -> independent review (below) -> merge via the PR. Do not
  local-ff-and-push a feature straight to master; the PR is the
  reviewable unit and the record.
- Merge mechanics: the bisectable small-commit discipline lives INSIDE
  the branch - never squash-merge, it destroys the bisection tool.
  Prefer a local `git merge --ff-only` of the reviewed branch then a
  master push (sha-preserving, the PR auto-closes as merged) over
  GitHub's rebase-merge, which REWRITES every commit sha and forces a
  re-resolution of every plan/handoff hash the branch just filed (the
  2bba6cb8b lesson, re-paid on PR #1). gate-all.sh green on the branch
  head before the PR opens and again at merge if master moved (the
  pre-push hook enforces the stamp on every code push, any branch).
  Push the branch after each landed, verified chunk. Plan checkboxes
  get their final hashes at merge time if a rebase rewrote them.
- Every PR gets an INDEPENDENT review before merge (my direction,
  2026-07-17): a fresh subagent with cold context reviews the full
  diff read-only against this file's standards - commit bodies checked
  against their own claims, deleted "dead" arms verified actually dead,
  removed API grepped tree-wide for surviving callers. Keep the review
  LEAN (my direction, 2026-07-17, to conserve tokens): a Sonnet
  subagent, a concise prompt scoped to the diff's real risks, NO
  independent rebuild or ctest run - the branch's own gate stamp
  already proves the gates, so the review is diff-reading only.
  Verdict is MERGE / MERGE AFTER FIXES / DO NOT MERGE with concrete
  findings; blockers get root-caused and fixed on the branch before
  merge, non-blocking nits get filed as plan items. The author-session
  merges only on a MERGE verdict - authoring and approving inside one
  context is not review.
- Gate verdicts are EXIT CODES, never scraped log text. Run
  scripts/gate-all.sh after the final commit and before any push of code;
  it stamps the validated HEAD and the committed pre-push hook
  (scripts/git-hooks/pre-push, enabled via
  `git config core.hooksPath scripts/git-hooks`) refuses unstamped code
  pushes (docs-only drift is exempt). Never wrap gate runs in
  `... || { ... }` compounds that mask the exit code, and never put the
  verdict read and the push in the same tool invocation - a broken master
  shipped for 20 minutes on 2026-07-16 from exactly that pair of mistakes.
- The README is public-facing state and updates IN THE SAME SESSION any
  major change lands (a new surface, harness, phase completion, or defect
  class retired) - a landed major change without its README line is an
  unfinished landing, same discipline as the plan checkbox (my rule,
  2026-07-16). REGISTER: the README describes what the project IS, in
  timeless terms - never session narration ("the same date's second
  wave", "kept paying"), no date-stamped play-by-play, no war-story
  defect enumerations (those live in commit bodies and the plan). If a
  sentence only makes sense to someone who watched the work happen, it
  does not belong in the README (my correction, 2026-07-17).
- Prefer new commits over amending, except when explicitly asked (e.g.
  cleaning up history before opening a PR).
- This file and `docs/PORTING_PLAN.md` are committed, but both are live
  documents - update them as direction changes or new information comes
  in (e.g. from watching the reference forks' ongoing work) rather than
  treating either as fixed once written.

### Commit shape and definition of done

(Absorbed from the deleted latte-conventions skill, 2026-07-16 - this
file is the single source; skills stay procedural, never duplicate
agreements.)

Types: feat, fix, docs, test, build, ci, chore, stub. Scope names the
subsystem: `fix(wm):`, `build(flake):`. The subject states the
user-visible outcome, not the internals (good: "fix(components): stop
writing ComboBox.pressed so popups open again on Qt6"; bad: "fix:
update ComboBox.qml"). The body carries three things: the mechanism
traced to its origin, why the fix sits at the root (or why a guard is
the deliberate contract), and the verification evidence - what was
driven and what was observed; "it builds" is not evidence. One root
cause per commit: when a symptom turns out to be two stacked causes,
land two commits (1607d022 + c5bdc239 is the model). Credit a
reference-fork parallel in the body when one exists.

Landing mechanics: tick the plan with POST-REBASE hashes at merge
time, never hashes copied from an agent ledger (worktree merges
rewrite them; 2bba6cb8b cleaned up exactly that). New findings get
filed as plan items with evidence even when fixed the same day - a
bug found, fixed and never written down is invisible to the next
session. Update docs/session-handoff.md as work lands, not at the end.

Definition of done, every change: root cause at origin (or the
guard's contract commented); driven with recorded evidence (nested
vehicle or live session); temporary instrumentation removed; plan
ticked and findings filed; commit body per above; QML gate if QML was
touched; stubs marked both ways; new asserted-on state gets its D-Bus
readback recorded in the XML + design doc + usage reference
(docs/dbus-interface-reference.md); scripts/gate-all.sh green AFTER
the final commit before any code push (exit code is the verdict, the
pre-push hook enforces the stamp); README updated when the landing is
major.

### Failures and root cause

- Never silently swallow a failure. A quiet early-return, an empty
  catch, a `?:` that hides a null, a clamp that papers over a bad value -
  all worse than the failure itself, because they turn a loud, findable
  bug into a silent, wrong-behaviour one that surfaces somewhere far away.
  If something cannot proceed, say so: `qWarning()`/`qCritical()` with
  enough context to act on, or fail loudly. Silence is not error handling.
- A degenerate value is a symptom, not a thing to guard away. A zero-size
  window, a null containment, an empty action list, an index of -1 - none
  of these are normal states to clamp back into range. Ask what produced
  it and why, and fix it there. `if (size.isEmpty()) return;` at the point
  of use is a bandaid; the bug is whatever handed you an empty size.
- Trace every failure to its root cause before writing a fix. "It stopped
  crashing" is not "it is fixed" if the fix is a guard downstream of the
  real defect. Prefer the fix at the origin. If a guard is genuinely the
  right layer (e.g. a real optional that is legitimately absent), say why
  in a comment, so it reads as a deliberate contract and not a patch over
  something not understood.
- When you cannot see why from the code, instrument and reproduce - add
  temporary logging that prints the actual values at the actual moment,
  drive the failure, read it, then remove the instrumentation. Guessing
  and clamping is how bandaids accrete.

### Regression discipline

- Know a change's blast radius before making it, especially environment,
  launcher, build and plugin/module-resolution changes. Adding a directory
  to `QML2_IMPORT_PATH`, `QT_PLUGIN_PATH`, `XDG_DATA_DIRS` or the like is
  never "narrow": it can shadow ANY same-named module or plugin the process
  already resolves. A broad append of the system Qt6 QML tree once replaced
  the dock's right-click menu with the stock task menu, because Latte's
  `import org.kde.taskmanager` then resolved from the system copy instead of
  Latte's pinned one. If you must expose extra modules/plugins, allow-list
  the specific leaves (or symlink them into a private tree), never add a
  shared root that also carries components we ship our own copies of.
- Verify causation, do not assume it. When something regresses, isolate the
  one variable and prove it - revert it alone and retest, or read the actual
  loaded state - before "fixing" the suspected cause. Two wrong guesses in a
  row cost more than one measurement. The launcher QML-path change and the
  QT_PLUGIN_PATH gap were distinct faults; only reverting and re-reading the
  running process's env told them apart.
- After a fix, confirm it against the real running artifact (logs, the live
  dock), not just "it builds". A green build says nothing about whether the
  menu came back.

### Copyright and attribution

NEVER remove an existing SPDX-FileCopyrightText line. Adopted code
keeps its authors' lines verbatim (capt's files stay "2026 Latte Dock
contributors"); an extraction that carries an upstream algorithm keeps
the original author's line (Michail's math stays his even when it
moves files); refactoring or extending a file ADDS my line next to
the existing ones, never in place of them. Genuinely new files -
glue, wrappers, tests authored from scratch - carry my line AND
"2026 Latte Dock contributors" for good measure. When in doubt,
over-attribute: an extra line costs nothing, an erased one is wrong.

CITE TRANSPLANTED CODE PROPERLY (my direction, 2026-07-17). Anything
carried over from another fork - most often CaptSilver's Qt6 port,
github.com/CaptSilver/latte-dock-qt6, author David Goree - is cited
two ways, both required:
1. A header comment naming the source file at an EXACT fork commit,
   never a moving ref: "Transplanted from latte-dock-qt6
   (tests/foo.cpp at 81384003, github.com/CaptSilver/latte-dock-qt6)".
   A bare "origin/main" drifts and is not a citation - pin the sha
   (the reviewed-through commit in the reference-fork sync section is
   the right one unless you transplanted from a newer state).
2. An SPDX-FileCopyrightText line for the original author at the top
   of the file (David Goree for capt's ports), ABOVE my line - the
   original author comes first, the adaptation second. This applies
   whenever code is DERIVED (transplanted, adapted, extended from his
   file); a file that only took an IDEA and was written fresh gets
   the citation comment but not the copyright line (claiming his
   copyright over code he did not write is as wrong as erasing it).
Same rule for any other fork or upstream source: name the exact
commit, carry the author's SPDX line when the code is derived.

### Code clarity

Reading is the dominant cost of this codebase: every future session
(often a weaker model, or me months from now) arrives with zero
context and pays a comprehension tax on every line. Misread code is
where bugs breed, so clarity is a correctness tool, not a style
preference. Concretely:

- Name things for what they DO, so call sites read like English and
  the reader never has to open a function to learn its job. The test
  is literally "what does that do?" asked of the bare name. This
  repo's own two-stage lesson: `_rowKinds()` said nothing;
  the first fix `_routerRowOfTasks()` STILL failed the test - it
  names the returned artifact in subsystem jargon, and a noun phrase
  never answers a "does" question. The real fix is the verb phrase:
  `_classifyTasksForRouting()` - classify the tasks for routing;
  the reader knows the job without opening it. Prefer verb-first
  names for anything that does work; reserve noun names for cheap
  pure lookups. If something is hard to name, that is usually the
  design saying it does more than one job - split it.
- Small single-purpose functions: a function does the whole thing its
  name promises and nothing else. When a block inside a function
  needs a comment to say WHAT it does, extract it and let the name
  say it (comments are for WHY; names are for WHAT).
- New C++ is idiomatic C++20: RAII everywhere; managed memory
  (std::unique_ptr/std::shared_ptr, or Qt parent ownership when the
  object lives in the Qt tree - never fight the parent system by
  wrapping QObjects in shared_ptr) instead of naked
  new/delete/malloc/free; enum class, std::optional/std::variant for
  absent and alternative states (the step-2.5 law in
  docs/TESTING.md); const correctness; value types where copies are
  cheap.
- Expressive, concise code that follows ESTABLISHED LANGUAGE
  PATTERNS. "The purpose of abstracting is not to be vague, but to
  create a new semantic level in which one can be absolutely
  precise" (Dijkstra) - established idioms ARE those semantic
  levels: std::visit over a variant is a precise sentence
  ("exhaustively handle every alternative, compiler-enforced"),
  switch is one ("enumerated dispatch"), RAII is one ("ownership
  follows scope"). Use them, including if constexpr,
  constexpr/consteval, concepts, [[likely]]/[[unlikely]] - freely
  and especially where they make things faster, give gcc/clang
  better codegen or diagnostic hints, or turn runtime errors into
  compile errors. What is BANNED is self-built flow control wearing
  a language feature's clothes: an immediately-invoked lambda bent
  into a switch, hand-rolled dispatch contraptions replacing
  if/else - those have no established meaning, so the reader must
  simulate the machinery to learn what it does. Cautionary tale
  from this repo's own history: a std::visit + if-constexpr
  dispatch was once "simplified" to a get_if chain in the name of
  plainness - deleting the compile-time exhaustiveness guarantee
  and replacing it with a runtime bad_variant_access. The idiom was
  the precise form; the "plain" version was the vague one. When an
  idiom's payoff is not obvious at the site, say what it is (the
  explained-optimization rule below).
- Concepts (C++20) are welcome where they make template constraints
  readable. C++23 is allowed WITH A RECORDED REASON: raising the
  standard is a build-system change with blast radius, so it follows
  the same process the C++17->20 bump did (the reasoned comment at
  the top-level CMakeLists, full build-check green before anything
  rides on it).
- Optimization levels, verified in this tree: the packaged build
  (package.nix) is nixpkgs-default Release, i.e. -O3 -DNDEBUG - the
  maximum reasonable level (-Ofast is not reasonable: it breaks IEEE
  semantics the parabolic math relies on). The dev tree deliberately
  stays RelWithDebInfo (-O2 -g) because the gdb-wrapper workflow and
  live debugging depend on it - do not "optimize" the dev loop at
  the cost of debuggability.
- When code stays rough after honest refactoring effort - irreducible
  domain complexity, protocol mirrors, hot paths - the comment
  carries what the code cannot: the data patterns, the invariants,
  the shape of the state machine. The parabolic router's semantics
  block is the model.
- Any non-obvious optimization gets explained where it lives: what it
  buys, what breaks or slows if someone "simplifies" it away.
  Unexplained optimizations get undone by the next cleanup pass.
- Assert invariants often and early. A precondition checked at the
  function's first line fails at the defect; the same violation left
  to flow fails three subsystems away (the whole lesson of the
  failures-and-root-cause section). In order of preference:
  make the invalid state unrepresentable (types - the step-2.5 law),
  static_assert what is knowable at compile time (the engine's
  settle-vs-burst ordering is the model), Q_ASSERT runtime
  preconditions in cores, and qCritical-and-refuse at API boundaries
  where bad input can arrive from outside (the router wrapper's
  malformed-row refusal is the model). An assert is documentation
  that cannot go stale. Q_ASSERT truths, verified in this tree
  (RelWithDebInfo defines QT_NO_DEBUG and NDEBUG): the preprocessor
  DELETES the whole assert expression outside debug builds, so (a)
  the unit-test targets define QT_FORCE_ASSERTS
  (latte_add_unit_test) or the asserts would be dead code exactly
  where they matter - proven with a step=0 probe that sailed through
  stripped and aborted forced; (b) an assert expression must NEVER
  carry side effects, the release build silently removes them; (c)
  an assert is a test/debug-time tripwire, not input handling - a
  boundary that can receive bad input from outside still refuses it
  loudly at runtime.

### Observability first

Everything should be cheaply instrumentable from outside the process,
with D-Bus as the primary surface (decided 2026-07-16; the Phase 10
requirements subsection in docs/PORTING_PLAN.md carries the full
contract). The extraction initiative's live-verification tail proved
the cost of its absence: state had to be read by pixel-peeping
screenshots and pointer acrobatics. Concretely:

- New subsystems ship their observability surface as part of the
  definition of done - a state readback (D-Bus query) and, where a
  test would drive it, a driving surface.
- State a test asserts on is pull-queryable, never log-scraped.
  Categorized qCDebug logging and the debug window are for high-rate
  traces D-Bus fits badly, not a substitute for queries.
- Safety is a constraint, not an afterthought: read surfaces expose
  state, never arbitrary execution; mutating surfaces stay
  coarse-grained user actions or sit behind an explicit debug-mode
  gate; nothing exposes secrets or other applications' content.

### Pure-core discipline

New pure cores (the QML extraction initiative) follow the step-2.5
law in docs/TESTING.md: project C++ standard, invalid states designed
out via types, tests green under ASan+UBSan, qmllint ratchet with
strict-on-touch for cutover-touched QML.

### Stub tracking

Anything stubbed to keep a phase moving - a function returning a
placeholder, a feature deferred to a later phase, a config page that
renders but isn't wired up - gets marked two independent, greppable
ways so it can't quietly get forgotten once a phase "builds fine":

- Commit subject prefixed `stub:` - its own conventional-commit type
  alongside feat/fix/docs/test/build/ci/chore. `git log --oneline |
  grep '^[a-f0-9]* stub:'` (or `git log --grep '^stub:'`) finds every
  one across the whole history.
- A `// STUB:` comment (or `# STUB:` in QML/CMake) at the exact
  location in code, stating what's missing and which phase should
  finish it. `grep -rn 'STUB:'` finds every currently-live stub
  without needing git history at all - this is the one that matters
  if someone's just reading the code, not archaeology-ing the log.
- The commit body says why it's stubbed now instead of done right, and
  what "done" actually looks like - not just "TODO fix this."

Never stub something silently under a `fix:`/`feat:` message. Case in
point, inherited from upstream rather than introduced by either fork:
the original latte-dock's Tasks config page rendered but never actually
applied its settings - a genuinely half-finished upstream feature that
was never marked as a stub anywhere. It went unnoticed across every
fork for a long time until latte-dock-ng's `9faccabda fix: hide
unfinished Tasks configuration tab` found it and hid it rather than
shipping something that silently did nothing (the right call at the
time). Mark it as a stub in the first place and this kind of silent gap
doesn't happen. (Since fixed properly upstream in latte-dock-ng's
`eabf7c89a`/`94f87ba66`/`ed0afd054` - see Phase 5/6 in
`docs/PORTING_PLAN.md` for the actual Plasma 6 config-access pattern
that fix uncovered, which is worth carrying into this port directly.)

## Reference fork sync status

Both reference forks are active projects, not frozen snapshots -
latte-dock-ng in particular is moving fast. Track the last commit
actually reviewed for each, so a future "check for updates" is a diff
from here, not a re-read of the whole history:

- **latte-dock-ng** (`~/Projects/latte-dock-ng`, remote `origin` ->
  `ruizhi-lab/latte-dock-ng`): reviewed through `456154efb` (2026-07-14).
  Check for new work:
  `cd ~/Projects/latte-dock-ng && git fetch origin && git log --oneline 456154efb..origin/main`
  If that shows anything, read full bodies (not just subjects) via
  `git log --format="%n=== %h %s ===%n%b" 456154efb..origin/main`
  before deciding what's worth folding in, then update this hash.
- **latte-dock-qt6** (`~/Projects/latte-dock-qt6`, remote `origin` ->
  `CaptSilver/latte-dock-qt6`): reviewed through `81384003` (2026-07-14;
  before that `9003f33a`, the end of the port-work range named in the
  original comparison request). It woke back up in July 2026 with a
  testability campaign (pure-helper extractions, DI seams, coverage
  gates) - that restructuring is their architecture direction and is
  NOT foldable into our upstream-shaped tree wholesale; read their
  fix commits individually, and remember their 81384003 top-layer fix
  repairs a layerFor(WindowsGoBelow)=Bottom mapping mistake we never
  made (our layershellmappingtest pins the corrected mapping).
  `cd ~/Projects/latte-dock-qt6 && git fetch origin && git log --oneline 81384003..origin/main`
- **plasma-desktop** (KDE upstream, not a fork): the vendored
  task-manager backend in `plasmoid/plugin/` (backend, smartlauncher*)
  derives from `applets/taskmanager/` there, which keeps evolving
  inside KDE's C++ applet. At every sync pass, also diff upstream's
  files against our copies and port their fixes:
  `curl -s "https://invent.kde.org/api/v4/projects/plasma%2Fplasma-desktop/repository/files/applets%2Ftaskmanager%2Fbackend.cpp/raw?ref=master" | diff - plasmoid/plugin/backend.cpp`
  (ours carries deliberate extensions - KWin D-Bus watcher,
  showAudioStreamOsd - so expect a stable base diff; look for NEW
  upstream hunks. See docs/taskmanager-integration-research.md for the
  vendor-vs-integrate analysis and the decision record.)

## Current status

(This section was stale for a long time - keep it honest.) The port is
a daily driver: I run it against my real config. Phases 0-7 are
substantially done; Phase 4 is now WAYLAND-ONLY (X11 backend removed
2026-07-17, main() refuses non-wayland platforms); Phase 8 still has
open items (shutdown/latency/lock-unlock/stranding) though its
bottom-dock surface-drift is fixed. Session three (2026-07-17) landed
via the PR flow: the dev re-pin to the running system, the X11
removal, the accessibility quartet's code half (dock-window keyboard
focus mode as the P0 gate, Accessible.* rollout across the interactive
items, the e2e nested-vehicle suite with EX-14/15/17 recipes and a
state-vs-render geometry guard), the Phase 9 color-group audit, the
CaptSilver P3b transplant wave, and the Phase 8 surface-drift fix -
only the Orca screen-reader acceptance pass and the desk-hands checks
remain from the quartet. The QML extraction initiative is COMPLETE
(2026-07-16): all 25 units executed and merged - the ledger in
docs/QML_EXTRACTION_PLAN.md is the record. The tree now carries 82
ctest entries with 29 sanitized unit-header pairs (the coverage
ratchet enforces the count) and a qmllint baseline that only shrinks.
The step-2.5 law
stays in force for all future cores (C++20, sanitized unit tests with
forced asserts, qmllint ratchet, type discipline, the code-clarity
rules above). Two companion-applet continuation items are planned as
sibling repos consumed by flake input (separator first, then the
applet-window-appmenu port) - see the continuation-features section of
the porting plan. The README carries the public-facing roadmap and
real-dock screenshots (docs/screenshots/). docs/session-handoff.md
carries the running session state.
