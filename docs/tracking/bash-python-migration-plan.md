# Bash-to-Python migration plan (BP)

Migrate the port's ~16,650 lines of bash harness, gate, and test scripting to
strictly typed Python managed by uv, keeping bash only where bash is genuinely
the better tool. Executes the 2026-07-18 language decision (recorded in the
harness-scripting-typed-python memory): typed Python with pydantic validation
at every D-Bus/config boundary, strict type checking and ruff enforced as a
gate leg, uv-pinned interpreter and dependencies so the harness runs identically
on NixOS and off-nix. Approved for execution 2026-08-04, together with the
retained-bash set below, the deletion of the legacy installer trio, and
basedpyright as the checker.

Chunk ids use the BP prefix (bash-to-python). Every chunk is a checklist item
with a Commits: line, same traceability contract as the porting plan.

## Decision context

- The 2026-07-18 decision chose typed Python over bash-heredocs, TypeScript,
  Bun, and Nushell, and deferred execution until after the audit and e2e
  epics. Execution now begins (direction, 2026-08-04).
- The 2026-07-18 carve-out kept the process/signal skeleton and gate exit-code
  plumbing in bash. This plan refines that line per script (the disposition
  table below): sequencing-and-exit-code scripts stay bash, but the nested
  vehicle lifecycle moves to Python because the harness that owns it becomes
  Python and a two-language seam inside one process tree is worse than a
  careful port.
- Review contract for this workstream (direction, 2026-08-04; review model
  revised to Opus the same day): implementation farms to Opus worktree
  subagents; every PR gets a quick context-aware read by the orchestrator (a
  garbage-or-good sanity pass) plus an independent cold-context lean-Opus
  review, per the standing CLAUDE.md default.

## Target architecture

- One uv project at `harness/`, package `latte_harness`, Python 3.14 (matches
  the pinned nixpkgs python3 3.14.6; `.python-version` pins 3.14 for off-nix
  uv-managed interpreters).
- Runtime dependency: pydantic only. D-Bus readbacks keep going through
  `busctl` subprocess calls (the documented observability contract in
  docs/reference/dbus-interface-reference.md); pydantic models validate the
  JSON at the boundary. No Python D-Bus binding dependency.
- Dev dependencies: ruff (lint + format), basedpyright at strict typechecking
  mode (decided 2026-08-04 over pyright: same checks at strict, but installs
  hermetically via uv off-nix where pyright's PyPI package fetches node at run
  time), pytest (unit tests of the harness itself; e2e recipes keep their own
  runner, below). All pinned in `uv.lock`.
- devShell additions from the existing nixpkgs pin (no re-pin): python3, uv,
  ruff, basedpyright. Fixes the latent gap where harness scripts assumed a
  devShell python3 that was never declared. uv is configured to prefer the
  system (nix) interpreter inside the devShell; off-nix it provisions its own
  3.14.
- Distro CI containers (ci/containers/Containerfile.*) install uv and bake
  `uv sync --locked` at image build so the gate stage needs no network at run
  time. ci/build-and-gate.sh stays the bash container-side sequencer.
- Entry-point policy: human-facing entry points keep their current
  `scripts/*.sh` paths as thin exec shims into `uv run` (stable interface for
  skills, docs, muscle memory, and the pre-push/gate contract); machine-facing
  call sites (ctest COMMAND entries, gate-leg invocations) switch to direct
  `uv run` invocations. E2e recipes migrate to `tests/e2e/*.py` with no shims;
  the runner discovers both `.sh` and `.py` during the transition.
- Recipe markers (`# e2e-mode:`, `# e2e-expect:`) keep identical syntax and
  semantics in `.py` recipes; the runner's classification matrix
  (PASS / FAIL / XFAIL / XPASS / SKIP) is ported bit-identically and its
  classifier self-test comes with it.
- A retained-bash allowlist ratchet lands with the foundation: a gate leg that
  fails when a `.sh` file exists outside the committed allowlist. The
  allowlist starts as the full current inventory and only shrinks as chunks
  delete their bash, so progress is monotonic and bash cannot quietly
  re-accrete. End state: the allowlist is exactly the retained set below.

## Disposition table

Python (the migration proper, ~16,000 lines of bash plus the untyped
fixture.py):

| Area | Files | Why Python wins |
| --- | --- | --- |
| QML/coverage analyzers | qml-effect-rules, qml-tooltip-rules, preview-contract-rules, qmllint-gate, coverage-ratchet, matrix-fixture-check, qml-compile-gate, qml-interaction-tests | Text/JSON analysis and ratchet arithmetic in bash today; typed parsing, exact-count ratchets, and refusal paths are Python's home turf |
| Fixture generator | tests/e2e/matrix/fixture.py | Already Python, untyped; promote into the package with pydantic models and unit-tested refusals |
| QML env assembly | scripts/lib-qml-env.sh | Path allow-list construction and stage/restore manifests are data logic; exports a shell-eval bridge for the remaining bash consumers |
| Vehicle lifecycle | scripts/lib-nested-kwin.sh, scripts/lib-e2e-seed.sh | Process groups, bounded waits, and cleanup become context managers with exact exit-code parity (130/143); owned by the Python runner's process tree |
| E2e runner + front doors | run-e2e, run-matrix, run-multi-output-e2e, run-colorizer-e2e, run-asan-dock | Discovery, marker parsing, result classification, artifact handling; front doors become subcommands behind thin shims |
| Recipe API + libs | tests/e2e/lib.sh, matrix-lib, audit-lib, golden-bridge, dnd-lib, task-reorder-lib, multi-output-lib, applet-reorder-driver | The harness brain: D-Bus readbacks, geometry assertions, config snapshot/diff, drivers; pydantic at every readback |
| E2e recipes | ~47 tests/e2e/*.sh | Assertion logic over the typed API; the bulk of the line count |
| Sceneprobe gate | sceneprobe-gate.sh, tests/sceneprobe/run_in_kwin.sh | Nested-kwin choreography plus image comparison; rides the Python vehicle module |
| Package gate subsystem | installed-package-gate.sh, lib-installed-package-gate.sh, installed-package-gate-selftest.sh, installed-package-gate-runtime-test.sh | ~2,970 lines of ELF/proc-maps auditing and process-group lifecycle written in bash; the least readable bash in the tree and the biggest single win |
| Staged-run env core | scripts/run-staged.sh | Consumes the QML env assembly, which is Python after BP-1a; assembles env and execs the dock |
| Dev tools | scripts/tools/dumpwins.sh, scripts/tools/watch-dock-presentation.sh | JSON/KWin-script processing; watch-dock-presentation sources the e2e lib, which is Python after BP-2c |

Bash retained (sequencing and exit-code plumbing, external contracts,
fixtures; 797 lines across 17 files, approved 2026-08-04 as the 15-file
613-line enumeration and corrected 2026-08-04 when the shebang inventory
surfaced the two extensionless packaging helpers):

| Script | Why bash stays |
| --- | --- |
| scripts/gate-all.sh | The canonical gate spine: run legs in order, propagate exit codes, write the stamp. 81 comment-rich battle-tested lines; a Python port adds an interpreter dependency to the push guard for zero readability gain |
| scripts/build-check.sh | Same shape: cmake configure, build, ctest, one ratchet call |
| scripts/git-hooks/pre-push | Git hook, sha compare; must work on a fresh clone before uv exists |
| ci/build-and-gate.sh | Container-side sequencer across 7 distros; bash is the one universally present tool there |
| scripts/restart-staged.sh, start-dock.sh, start-dock-sanitized.sh | The daily-driver kill/setsid/detach dance; high blast radius, stable, no logic |
| Messages.sh (x5) | KDE translation-infrastructure contract (scripty invokes by name and convention); an external interface, not this repo's harness |
| tests/e2e/fixtures/sc-w1/launcher.sh, rate-launcher.sh | Test fixtures simulating launched desktop apps; being tiny shell executables is the point |
| packaging/rpm/make-snapshot-source.sh | Packaging-side tarball helper invoked from the RPM spec context |
| packaging/debian/build-package, packaging/void/build-package | Same packaging-context family (extensionless; run where the harness venv does not exist); surfaced by the shebang inventory |

Upstream-inherited, decided 2026-08-04: install.sh, uninstall.sh, and
formatter.sh are DELETED (BP-5b). The CMake/Nix flow and the native packages
superseded them; git history preserves them.

## Equivalence contract (definition of done per chunk)

- Exit-code contract preserved exactly, including distinguished codes
  (fixture refusal 2, lockstep guard 4, INT 130, TERM 143) and the
  no-scraped-verdicts rule.
- Every ported gate proves itself both ways: same verdict as the bash version
  on the current tree, and a driven negative control (an introduced violation
  the gate must catch), so no port lands vacuous.
- Ported recipes run green in the nested vehicle; recipes carrying
  `# e2e-expect:` markers preserve their exact expected status.
- The ratchets survive intact: ctest entry count, coverage ratchet, qmllint
  exact-count baseline, and the new retained-bash allowlist only shrinks.
- Each chunk deletes the bash it replaces in the same PR and updates every
  call site (the call-graph inventory in this plan's research is the map).
- Gate green per the standing contract: LATTE_GATE_FAST branch gates, full
  gate at merge for anything touching gate legs themselves.

## Chunks

Phase BP-0, foundation (serial, lands first):

- [x] BP-0a (devShell toolchain): add python3, uv, ruff, basedpyright to the
  devShell from the existing pin; wire uv interpreter preference. Commits:
  a9166e140 (PR #153)
- [ ] BP-0b (harness package skeleton): `harness/` uv project, pyproject,
  uv.lock, ruff + basedpyright strict configs, `latte_harness` core modules
  (process control, repo paths, logging), pytest wiring; the harness-check
  gate leg (ruff check + format check, typecheck, pytest) added to
  build-check and ctest; the retained-bash allowlist ratchet seeded with the
  full current inventory. Commits: ce10b759b, 976b74810, ab813a23f, b7d4fb54f
  (PR #153; the ctest wiring moved to BP-0c with the container uv
  provisioning, so a uv-requiring ctest entry never lands before containers
  can satisfy it; the gate-all leg covers the merge gate meanwhile)
- [x] BP-0c (container uv provisioning): uv + baked `uv sync --locked` in all
  7 Containerfiles (distro package on arch/fedora/opensuse/void; pinned
  sha256-verified standalone 0.11.26 on debian/neon/gentoo, none packaged);
  ci/build-and-gate.sh runs the offline harness-check leg first and mirrors
  the D271 (ambient QML import path) env-strip onto its raw ctest calls (PR
  #152 review nit). The harness-check ctest entry stays deferred (the BP-0b
  tick records why). Commits: c061634d3, 0a90bda65 (PR #157)

Phase BP-1, analyzers (file-disjoint, parallel after BP-0):

- [x] BP-1a (QML env module): port lib-qml-env.sh to `latte_harness.qmlenv`
  with a shell-eval export bridge for bash consumers. Carried the two filed
  check.py nits from the PR #153 second review (TimeoutExpired on the checker
  probe counts as cannot-run; per-tool resolution cached). Byte-for-byte
  import-list equivalence proven against the bash on the real tree. Commits:
  967560f3e, 77351643f, 334988de6 (PR #156)
- [x] BP-1b (fixture promotion): promote fixture.py into the package, typed,
  pydantic KConfig models, unit-tested refusals; port matrix-fixture-check.
  Golden byte-identity proven against the pre-delete original; the vehicle
  front doors gained the uv guard. Commits: ed577b31e, 101141890, cab6b99f9
  (PR #162)
- [x] BP-1c (coverage ratchet): port coverage-ratchet.sh; both refusal modes
  driven as negative controls. The shim made build-check transitively require
  uv, so its devShell re-exec guard now tests cmake and uv (the gate-all
  stale-proxy lesson). Full gate (asan included) ran at merge per the
  gate-leg contract. Commits: a5745eca7, f8520fd3b (PR #155)
- [x] BP-1d (qmllint ratchet): port qmllint-gate.sh, exact-count semantics,
  plus the D269 per-warning fingerprint diagnostics and the D270 codepoint
  serialization. Root-caused D269 (locale-collated input order). Commits:
  c113fc10e, e33a521a7 (PR #161); the D269 closure itself is 03c239ae2 with
  corrections 28414b87b (PRs #163, #164)
- [x] BP-1e (rule scanners): port qml-effect-rules, qml-tooltip-rules,
  preview-contract-rules; 42 mutation-tested rules, byte-identical failure
  messages. Commits: 415900ec9, 57beafa6b (PR #159)
- [x] BP-1f (QML compile/interaction gates): port qml-compile-gate,
  qml-interaction-tests behind shims (ctest COMMAND entries unchanged per the
  shim policy; the direct-uv switch batches with the shim removal). The
  seed-var list went public in qmlenv rather than mirrored (the PR #160
  review's addition-blindness finding). Commits: 138d6108a, 89fbb01b7,
  a19d0a7ef (PR #160)

Phase BP-2, vehicle spine (serial):

- [x] BP-2a (vehicle + seed): port lib-nested-kwin.sh and lib-e2e-seed.sh to
  `latte_harness.vehicle` / `.seed` with exit-code and cleanup parity.
  State-file-driven subcommands; the reparent-to-init design gained a
  leader-identity teardown gate (starttime-checked killpg; the review's
  zombie-hold finding) and the package gate's teardown reroutes through it.
  Full suite A/B against the bash bridge (33/52 both sides, deltas
  vehicle-independent); full gate at merge. Commits: d97842980, f78816f19,
  f4ea48984, 64394d8a6, 477bc19de (PR #167)
- [x] BP-2b (e2e runner): port run-e2e.sh; mixed .sh/.py recipe discovery,
  bit-identical classification matrix plus self-test (byte-identical verdict
  messages); the front doors stayed unchanged behind the run-e2e shim
  instead of becoming subcommands (less churn, same contract). Recorded
  deviation: discovery takes top-level recipes only - the bash find ran nine
  subdir libs and fixtures as guaranteed-failure recipes. The four
  measurement flips against the BP-2a baseline were named and re-driven 4/4
  PASS (three missing-build-artifact, one front-door-seed). Full asan gate
  at merge through the ported driver. Commits: 559a9af37, 234309b42,
  6d1b5e285 (PR #169)
- [ ] BP-2c (recipe API): port tests/e2e/lib.sh to the typed recipe API with
  pydantic readback models. Commits:
- [ ] BP-2d (sceneprobe): port sceneprobe-gate.sh and run_in_kwin.sh.
  Commits:
- [ ] BP-2e (staged run): port run-staged.sh; restart-staged.sh execs the
  Python entry. Commits:

Phase BP-3, recipe libs then recipes (libs serialize before their batches;
batches are file-disjoint and parallel):

- [ ] BP-3a (matrix lib + golden bridge): port matrix-lib.sh,
  golden-bridge.sh. Commits:
- [ ] BP-3b (audit lib): port audit-lib.sh. Commits:
- [ ] BP-3c (drivers): port dnd-lib, task-reorder-lib, multi-output-lib,
  applet-reorder-driver. Commits:
- [ ] BP-3d..3i (recipe batches): ~47 recipes in ~6 file-disjoint batches,
  grouped by lib dependency (plain-lib recipes start after BP-2c; matrix and
  audit recipes after BP-3a/3b; driver recipes after BP-3c); bash libs are
  deleted by the batch that ports their last consumer. Commits:

Phase BP-4, package gate (serial pair, independent of BP-3; needs BP-2a):

- [ ] BP-4a (gate engine): port lib-installed-package-gate.sh,
  installed-package-gate.sh, installed-package-gate-runtime-test.sh.
  Commits:
- [ ] BP-4b (gate selftest): port installed-package-gate-selftest.sh with its
  signal-handling and refusal controls intact. Commits:

Phase BP-5, tail:

- [ ] BP-5a (dev tools): port dumpwins.sh and watch-dock-presentation.sh.
  Commits:
- [ ] BP-5b (upstream-inherited disposition): delete install.sh, uninstall.sh,
  formatter.sh per the 2026-08-04 decision; record Messages.sh retention.
  Commits:
- [ ] BP-5c (docs sweep + closeout): update TESTING.md, CLAUDE.md script
  references, the orchestrator prompt, skills, README, ROADMAP; shrink the
  retained-bash allowlist to the final set; update the
  harness-scripting-typed-python memory. Commits:

## Filed follow-ups (wave 1)

- Uniform shim self-heal: the eight BP-1 shims assume uv on PATH and die
  with command-not-found from a bare shell; add the nix-develop re-exec
  guard line to each (PR #162 review finding 1). Small standalone PR.
- docs/tracking/e2e-interaction-test-plan.md still names the deleted
  tests/e2e/matrix/fixture.py path at two sites (PR #162 review finding 2).
- Backgrounded nohup gate runs break the installed-package selftest's
  SIGINT control (bash SIG_IGN inheritance for async commands); the gate
  runs foreground or under a harness that restores the disposition
  (BP-1e agent finding, not a code defect).

- Matrix-runner chunk: pre-create the harness/.venv mountpoint on the host
  (or a tracked .gitkeep carve-out) so the documented tmpfs overlay works on
  a clean checkout (PR #157 review finding 3).
- Widen harness/pyproject.toml's uv_build upper bound past 0.12 with a
  recorded reason (stale bound flagged by the PR #157 review; benign today).
- Run the coverage-ratchet driven refusal controls after the build so a cold
  canonical gate exercises them (PR #155 review durability finding).
- Rolling-distro uv versions drift ahead of the 0.11.26 lock writer; a lock
  revision bump would fail --locked loudly on those legs (latent, PR #157
  review finding 1).
- Process note: PR #153 merged on a fast stamp although it touched
  gate-all.sh; the PR #155 merge ran the full gate over the combined state,
  covering it retroactively. Gate-leg PRs take the full merge gate.
- D272 (storagetest fails as root in containers) blocks every distro gate
  stage; fix direction is a euid-0 QSKIP guard or unprivileged container
  ctest. Small standalone chunk, outside BP.

## Execution shape

Per the orchestrator prompt's core loop: decompose (this plan), farm each
chunk to an Opus worktree subagent (Template A, adapted to name this plan and
the equivalence contract), review every PR through the orchestrator's
quick context-aware read plus an independent cold-context lean-Opus subagent
(Template B; revised 2026-08-04), merge
serially through `gh pr merge --rebase` with re-rebase and re-gate as main
moves, orchestrator owns plan ticks, defect filing, and session-handoff.
Concurrency around 4; BP-2 chunks serialize; BP-4 runs alongside BP-3.
Estimated 20-24 PRs.

## Risks

- Vehicle lifecycle parity (BP-2a): the trap/killpg/unmount discipline is
  hard-won; the port carries the semantics over exactly and the e2e suite
  plus sceneprobe are the regression net. Highest-care chunk, smallest line
  count.
- Runner cutover (BP-2b): every recipe rides it. Mixed discovery keeps the
  bash suite green during BP-3; the classification self-test ports first.
- Ratchet fidelity (BP-1c/1d): exact-count semantics; negative controls
  required.
- Python 3.14 + pydantic compatibility: locked versions, verified in BP-0b
  before anything rides on them.
- Container drift (BP-0c): baked uv sync must keep the gate stage
  network-free at run time; verified per-distro in the CI matrix.
