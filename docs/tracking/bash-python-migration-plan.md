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
fixtures). The 2026-08-04 approval projected a 15-file / 613-line
enumeration, corrected the same day to 17 files / 797 lines when the
shebang inventory surfaced the two extensionless packaging helpers. The
migration completed 2026-08-06 at a wider floor - 46 files - because the
thin entry-point shims fronting the typed harness (the run-*, qml-*, gate
and vehicle front doors dispositioned in the Python table above) and the
defect-blocked e2e recipes stayed bash rather than being deleted (the
"allowlist floor is deliberate, not final-set" note in the BP-5c tick).
The table below enumerates the retained spine, external contracts,
fixtures, and the two launcher-authorization helpers; the shims carry
their disposition in the Python-migration table above:

| Script | Why bash stays |
| --- | --- |
| scripts/gate-all.sh | The canonical gate spine: run legs in order, propagate exit codes, write the stamp. 81 comment-rich battle-tested lines; a Python port adds an interpreter dependency to the push guard for zero readability gain |
| scripts/build-check.sh | Same shape: cmake configure, build, ctest, one ratchet call |
| scripts/git-hooks/pre-push | Git hook, sha compare; must work on a fresh clone before uv exists |
| ci/build-and-gate.sh | Container-side sequencer across 7 distros; bash is the one universally present tool there |
| scripts/restart-staged.sh, start-dock.sh, start-dock-sanitized.sh | The daily-driver kill/setsid/detach dance; high blast radius, stable, no logic |
| scripts/ensure-dev-wayland-interfaces.sh | The Wayland-authorization step of that same launcher path: restart-staged.sh calls it to write the KService desktop entry naming the exact dev binary and refresh kbuildsycoca6, so KWin grants the dev dock the privileged window-management protocols. Session-cache mutation on the real desktop, high blast radius, no logic worth typing in a second language |
| tests/startdockauthoritytest.sh | The co-located ctest selftest (add_test startdockauthority) for that launcher-authorization bash: drives restart-staged.sh and ensure-dev-wayland-interfaces.sh as subprocesses under a faked PATH (kbuildsycoca6/pgrep/setsid) and asserts the desktop-entry contents, the unchanged-entry cache refresh, the exec-path and missing-binary refusals, and the insufficient-interface refusal. Testing retained bash through a foreign harness adds an interpreter for zero equivalence gain; the test stays in the language of what it guards |
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
- [x] BP-2c (recipe API): the typed recipe API with pydantic readback
  models (latte_harness.recipe), delivered as a fresh module rather than a
  bridge: per-call subprocess bridging would add interpreter startup to
  every helper call, so lib.sh stays for the bash recipes until the BP-3
  batches delete them. 15/15 helper parity proven live (byte-identical
  screenshots and dumpwins); the pilot 000-smoke.py replaced 000-smoke.sh
  (the first true retained-bash shrink). Follow-up filed: reconcile the
  runner's _dock_pid (returns None on an unset E2E_DOCK_PIDFILE) with
  lib.sh's refuse-loudly contract, which recipe.py matches. Commits:
  28ca27bf0, 9ec9f3015, 49fe5176b (PR #171)
- [x] BP-2d (sceneprobe): port sceneprobe-gate.sh and run_in_kwin.sh onto
  the vehicle library; all 14 scenes green through the shims with a driven
  golden-corruption control. The review's lifecycle finding (the compositor
  log path read after teardown was dead diagnostic code) fixed at the
  origin: CompositorStartError captures the log text at raise time for
  every caller. Commits: 8a4c04b03, 2009cf1df, 06c30a6e2 (PR #173)
- [x] BP-2e (staged run): port run-staged.sh; restart-staged.sh execs the
  Python entry. Empty env diff across 212 variables on both argv paths; the
  shim execs the venv interpreter directly because uv run forks a wrapper
  child that would break the launcher-pid-is-dock-pid contract; delivered
  live with a verified single-instance pid==sid restart. Commits:
  abc7aa1ad, 6d1ed9696 (PR #174)

Phase BP-3, recipe libs then recipes (libs serialize before their batches;
batches are file-disjoint and parallel):

- [x] BP-3a (matrix lib + golden bridge): typed twins
  latte_harness.matrix + matrix_golden per the BP-2c fresh-module design;
  byte-identical parity captures; the selftest pilot ported with all ten
  controls. Found viewsData.screen is a connector-name string (caught by
  pydantic live) and the D275 zombie independently. Commits: ac4562eac,
  f58f29d9b, 8adb843b2 (PR #178)
- [x] BP-3b (audit lib): port audit-lib.sh to latte_harness.audit, the
  typed edit-mode settings-audit driver, composing over the matrix and
  recipe modules (the BP-2c fresh-module design). Byte-identical
  snapshot/assert formulas with the bash 0/1/2 status contract; the
  selftest cutover to .py ran all 13 crafted controls plus the live leg
  green in the nested vehicle (exec bit kept - the D273 lesson);
  audit-lib.sh itself stayed for its six bash consumers until the R5
  batch ported them and retired it.
  Recorded deviations: bool-returning drive helpers, a loud refusal on
  unset E2E_FAKEPOINTER, the code-point sort unification (the D269
  locale lesson). Resumed from a crashed agent's uncommitted worktree
  WIP, adopted after a critical read. Commits: 0752856d2, dea56987b,
  bbbcb89ac (PR #182)
- [x] BP-3c (drivers): port dnd-lib, task-reorder-lib, multi-output-lib,
  applet-reorder-driver to typed twins (one commit per lib; the bash libs
  stayed for their then-unported recipe consumers - the R7 batch later
  ported 092/093/100/022 and deleted dnd-lib.sh, so only
  create-linked-dock.sh and the multi-output selftest, both dual-output
  blocked, keep task-reorder-lib, applet-reorder-driver and
  multi-output-lib alive). Parity driven in the nested vehicle:
  byte-identical readbacks on every comparable surface, a real
  explorer-to-containment Wayland DnD, a real task reorder, the
  rearrange lifecycle, and the topology-mutation gate's single-output
  refusal as the negative control. One recorded delta: applet_reorder_z
  returns float 0.0 where bash echoed JSON-collapsed 0 (z is a C++
  double; every consumer compares numerically). The review added
  env-only negative controls for the mutation safety gate and the
  bash-faithful minus-only int guard. Resumed from a crashed agent's
  worktree: dnd and task_reorder adopted from its uncommitted WIP,
  applet_reorder and multi_output written fresh. Commits: b6461ecdd,
  5343501a8, ef7df4d68, e72360208, 5d3c74e33 (PR #185)
- [ ] BP-3d..3i (recipe batches): ~47 recipes in ~6 file-disjoint batches,
  grouped by lib dependency (plain-lib recipes start after BP-2c; matrix and
  audit recipes after BP-3a/3b; driver recipes after BP-3c); bash libs are
  deleted by the batch that ports their last consumer. Landed so far:
  R2 (110-context-menu-normal-mode, settings-window-onscreen,
  080-key-escape-cancels-move; PR #175) and R1 (060-geometry-agreement,
  030-wheel-ruler-maxlength, 020-wheel-task-cycle; PR #176) - both batches
  honestly reduced: four R2 recipes were matrix-dependent (mis-slotted,
  they join the matrix batch), three R1 recipes were blocked on D275 (a
  recipe-started dock stays a zombie; fixed), 070 is blocked on D274 (the
  maximize-length input-region defect), 040 on D276 (the stale golden),
  061 on the dual-output vehicle. Commits: 8e8675a01, 804502cc4,
  1fd16143e (PR #175); d2e550ad2, 734c04ee1, db34f5809 (PR #176).
  R4 (090-remove-applet, 080-add-applet, dock-edit-retarget-cancel;
  completed after the environment crash interrupted the batch): the
  retarget port's first drive surfaced an equivalence the bash hid -
  dbusreports refuses the whole viewsData reply while a view lacks an
  accepted placement (transient during an edit-mode enter), the bash
  polled through the empty payload, a bare json.loads crashed - mapped
  to the pollable RecipeError at the two bash swallow sites. Commits:
  68e96bda4, f191ae33c, ac9a83cdf (PR #183).
  R5 (031-ruler-slider-crossview, 032-behavior-live-reflect,
  032-effects-config-readback, 032-wheel-editbackground-opacity,
  033-chrome-advanced-readback, 034-tasks-config-apply - the audit
  batch, retiring audit-lib.sh with its last consumers): two recipes
  arrived BROKEN and were repaired on the bash first, proven PASS, then
  ported - 031's committed golden was stale (the D276 host-palette
  class; re-blessed through the recipe's own E2E_BLESS flow with by-eye
  verification) and 032-wheel's aim premise was stale (the edit canvas
  layout moved the grid band to the upper quarter; the aim now derives
  from canvas geometry). Commits: c1cb4ead4, bbea96959, 3935e1f81,
  21330b676, 35e770c64, 4520ebb8f, 22e90c968, 5f9d852f6 (PR #188).
  R7 (092-task-reorder, 093-widget-explorer-dnd, 100-applet-reorder,
  022-configoverlay-wheel-threshold - the driver batch, deleting
  dnd-lib.sh with 093, its last consumer): 022's status-57 XFAIL
  contract observed in the driven run; the review's lifecycle finding
  (cleanup outside a finally stranded the vehicle dock on the fixture
  config on unexpected exits) fixed with the finally plus conventional
  signal exits before merge; create-linked-dock stays bash on the
  dual-output block. Commits: a4458ab1f, 752c2ccee, 1e5065ce5,
  486d051c1 (PR #187).
  R6 (075-wayland-window-admission, 072-window-touch-transition,
  071-maximized-window-length, 074-live-titlebar-window-touch - the
  window-touch batch): the 072 port drive surfaced D278 (072's
  fractional captures race the D259 200 ms transition, latent since
  cd74a9244) - root-caused with an unmodified-bash control run, fixed
  on the bash recipe first (background departure glide,
  trigger-adjacent maximize captures), proven green both ways, then
  ported; the 074 port drive surfaced D279 (fixed-count poll loops lose
  their wall-clock horizon in ported recipes - ~3 ms busctl probes
  shrink a bash 100-iteration loop from ~10 s to ~1.3 s; 074's five
  drag-choreography samplers now poll to a 10 s monotonic deadline).
  The sourceguard contract net that reads these recipes' text
  retargeted per landing commit, so sourceguardtest stayed green at
  every commit. 073-window-touch-topology stays bash with its allowlist
  line (dual-output, the same block as 061 and create-linked-dock).
  Commits: 69432f316, 87ba9800e, 2dbfeaa86, 02e6e62f5, a296feacf
  (PR #190).
  R10 (091-drop-marker, duplicate-view-idremap, 033-canvas-remap-placement,
  linked-dock-removal-undo - the lifecycle batch; the removal-undo drive
  was the first successful dual-output vehicle exercise in this
  environment): duplicate-dock-independent stayed bash after its drive
  exposed D283 (the legacy AllScreensGroup clone path no longer reuses
  its persisted replica on reload - a dock regression, not test
  staleness; approach decision owed), and linked-dock-operation-stress
  stayed bash pending the sourceguard mock-harness redesign (follow-up
  below). Commits: 308ca9a32, 83a208e7f, af7a61a70, dc19fb460, 2594dd96f
  (PR #194).
  R8 (010-wheel-desktops, 050-drag-reorder-launchers, 021-launcher-wheel,
  022-empty-area-window-actions, 023-task-middle-click-runtime - the
  input/wheel batch): all five arrived green with byte-identical control
  runs; the review caught the recurring cleanup-outside-finally class in
  022 (fixed with the finally plus conventional signal exits, second
  review clean). Commits: 9db39c62c, 8001d1060, e0a795f04, a36ff9d8a,
  dbae48b66, b46a3a090 (PR #195).
  R9 (112/113/114 focus restoration, keyboard-navigation-mode, 110/111
  colorizers): landed WITH the D280 dock fix first (removing a view that
  owned the panel focus session stranded keyboard focus for the whole
  removal-undo window; released at destroyedChanged now), plus the D281
  and D282 recorded port-timing accommodations; 110 proven through the
  colorizer front door; full gate (asan) at merge for the dock C++.
  Commits: 9463c1cee, bc0a1fe0e, 8c8426b0d, 75936b869, 522e74e56,
  c854f5f14, 94ced6365, 1637d2ff1, 6330aeda0 (PR #196).
  R11 (presentation-coverage-selftest, 070-asan-binary-shadow,
  090-golden-bridge-selftest - the gate-adjacent batch): the asan gate
  driven 4/4 over the extension-swapped set as the decisive evidence;
  golden-bridge.sh RETAINED (matrix-lib.sh still sources it - the
  last-consumer premise was wrong and the grep proof corrected it); full
  gate at merge. Commits: d6a4b521f, a6cb5e8a0, d5c1dec76 (PR #197).
  Remaining bash after wave 4: one PORTABLE recipe
  (parabolic-hover-preview - unassigned in the wave composition, queued
  below), the deferred stress recipe, the blocked set (040 on D276,
  070-maximize on D274, duplicate-dock-independent on D283; 061, 073,
  create-linked-dock, multi-output-selftest on the dual-output vehicle),
  lib.sh, and the five retained matrix libs.
  R12 (multi-output-selftest, create-linked-dock - the dual-output wave,
  2 of 4 ported): the selftest's 15/15 controls paid the PR #185
  residual with the first LIVE drives of multi_output's mutating
  transactions (discover, capture/restore, place full/partial/
  disconnected, pin, view-on tripwires); create-linked-dock retired
  task-reorder-lib.sh and applet-reorder-driver.sh (grep-proven last
  consumer). 061 stays bash (D209, the missing three-view fixture -
  the bash refuses identically) and 073 stays bash (the same
  bash-eval sourceguard coupling as the stress recipe; the R13
  redesign documents its mechanical follow-up). The review-response
  commit moved both restores into the converged finally shape.
  Commits: 5dcf8373a, ebe40fb2e, 0be2c357b (PR #200).
  R13 (linked-dock-operation-stress + parabolic-hover-preview, with
  the sourceguard cleanup-net REDESIGN): the bash-eval cleanup test
  became a typed injectable pure core (latte_harness.storm_cleanup)
  driven in-process by pytest mutation controls (status-masking and
  live-dock-replacement mutants caught), with the contract matcher
  retargeted to pin the recipe's wiring incl. the two safety
  predicates (the review's seam finding); the storm drove PASS at
  seed 127934575 in the dual-output vehicle with iteration counts
  byte-identical; parabolic drove PASS with the synthetic-glide race
  recorded as flaky for BOTH languages (~1/3 vehicle runs, a
  follow-up below, not a port defect). The redesign generalizes to
  073's twin test; each recipe extracts its own decision core.
  Commits: 4fa2031cd, c3c3395f8, 69748a733 (PR #202)

Phase BP-4, package gate (serial pair, independent of BP-3; needs BP-2a):

- [x] BP-4a (gate engine): port lib-installed-package-gate.sh and
  installed-package-gate.sh (the runtime-test stays untouched as the
  container acceptance - a deliberate re-scope from this item's original
  three-file wording). The unmodified 91-control selftest is the
  equivalence net; process-group teardown converged onto
  vehicle.stop_process_group with the identity gate; the review's
  byte-transparency finding fixed (surrogateescape at the read borders).
  The lib .sh sheds only engine-only helpers until BP-4b (the selftest
  fault-injects by sourcing it). Full gate at merge. Commits: 5f6ea2554,
  d1e9874af, 77cb743a9, 24a7fc49e (PR #177)
- [x] BP-4b (gate selftest): port installed-package-gate-selftest.sh with its
  signal-handling and refusal controls intact. Landed as a marker-gated
  pytest module run as its own gate-all leg (deselected from the default
  harness-check run, so the offline leg stays toolchain-free); the 91
  bash controls reconcile as 70 ported (every refusal a driven negative
  asserting the exact diagnostic), 19 pinned by named existing unit
  tests, 2 structurally bash-only and recorded. The bash selftest ran
  exit 0 on the same tree before deletion (the reference verdict);
  lib-installed-package-gate.sh shed to its one live helper (the
  e2e-seed-cleanup consumer); SIGINT/SIGTERM 130/143 driven through
  cleanup foreground. Full gate (asan leg) at merge per the gate-leg
  contract. Commits: fac2595f3, 2555e62ac, 6a13e32fd (PR #189)

Phase BP-5, tail:

- [x] BP-5a (dev tools): port dumpwins.sh and watch-dock-presentation.sh
  to latte_harness modules behind thin shims at their existing paths
  (the DUMPWIN output contract byte-identical; the watcher on the typed
  recipe API with two additive shared helpers, try_json_payload and
  is_running). The dumpwins live comparison ran the read-only tool
  against the session kwin because the bash original is journal-bound
  and cannot run nested (recorded honestly; the nested reference is the
  maintained lib twin). Commits: 7a74c370c, 08c098f76, 54c6ef81d
  (PR #199)
- [x] BP-5b (upstream-inherited disposition): delete install.sh, uninstall.sh,
  formatter.sh per the 2026-08-04 decision; record Messages.sh retention.
  INSTALLATION.md now names the cmake commands the wrapper ran plus the
  Nix and packaging/ routes; the review confirmed the dropped l10n and
  ENABLE_MAKE_UNIQUE branches were already dead. Follow-up for BP-5c:
  the orphaned astylerc (formatter.sh was its only consumer) and the
  Qt5-era INSTALLATION.md prerequisite lists. Commits: 16a59e364
  (PR #201)
- [x] BP-5c (docs sweep + closeout): TESTING.md's entry-point references
  now name the typed modules behind the shims/bridges; the README
  carries the timeless typed-harness line; INSTALLATION.md's Qt5-era
  dependency lists replaced with the packaging/ and ci/containers/
  pointers; the orphaned astylerc deleted (formatter.sh was its only
  consumer, the PR #201 review's grep). CLAUDE.md, the orchestrator
  prompt and the skills swept clean already (no stale script
  references). The allowlist floor is deliberate, not final-set: six
  bash files remain in tests/e2e, every recipe among them blocked on a
  tracked defect (040/D276, 061/D209, 070-maximize/D274,
  duplicate-dock-independent/D283) plus lib.sh and multi-output-lib.sh
  for those consumers; the retained-bash spine and external contracts
  hold per the disposition table. The harness-scripting-typed-python
  memory updated with the completion state. Commits: (this docs PR's
  hash at merge)

## Filed follow-ups (wave 1)

- Uniform shim self-heal: the eight BP-1 shims assume uv on PATH and die
  with command-not-found from a bare shell; add the nix-develop re-exec
  guard line to each (PR #162 review finding 1). LANDED: 411ba164f
  (PR #166).
- docs/tracking/e2e-interaction-test-plan.md still names the deleted
  tests/e2e/matrix/fixture.py path at two sites (PR #162 review finding 2).
  FIXED in the R4/BP-3b docs pass, together with its stale
  080/090-recipe .sh and matrix-selftest .sh names (PR #183 review
  finding).
- Widen the typed View model with editMode/isCloned/isClonedFrom so
  dock-edit-retarget-cancel rides the typed boundary instead of raw JSON
  dicts (PR #183 review nit; the View docstring already anticipates the
  editMode widening).
- linked-dock-operation-stress port: LANDED with the sourceguard
  redesign (PR #202; the R13 entry above).
- parabolic-hover-preview port: LANDED (PR #202). Its synthetic-glide
  race stays a follow-up: the vehicle's injected glide misses the
  parabolic hover ~1/3 of runs for the bash and the port alike (A/B
  proven, identical coords and velocity); a vehicle-side stabilization
  or a retry-with-budget is the fix direction, not a recipe change.
- 073-window-touch-topology port: LANDED (PR #204; 76dab2b0d,
  b7068d117, a0b8eafdf) via the R13 methodology
  (latte_harness.topology_cleanup, pytest mutation controls, the
  matcher pinning every load-bearing wire). 073 arrived broken - D284
  (the fixed 800 ms axis-change sample raced the settle; A/B proved the
  bash failed identically), fixed bash-first with no coverage lost.
  matrix-lib.sh and golden-bridge.sh retired grep-proven;
  multi-output-lib.sh stays for tests/multioutputstatecontracttest.sh.
- Committed seed variants for the richer-precondition recipes: 092 needs
  three or more launchers and 100 a vertical view with an ordinary
  applet; the clean default seed cannot satisfy them (the bash refused
  identically), so the R7 drives used scratch E2E_CONFIG_BASE seeds. A
  committed variant would make them drivable from a clean checkout
  (PR #187 finding).
- Periodic full-suite manual drive: no gate drives the wheel/audit
  recipes (the asan core is the no-input set), so 031's stale golden and
  032-wheel's stale aim sat invisible until the R5 batch's manual full
  drive found both. A recurring plan item to drive the whole suite
  catches this class (PR #188 finding).
- The shared fakepointer binary at the canonical ~/.local/bin location
  can go stale silently: the 2026-08-05 disk-recovery revert left a
  Jul 23 build predating the draghold verb and 074 could not drive at
  all until it was rebuilt from current source. Consider a version/verb
  probe in the vehicle preflight (PR #190 finding).
- Infra: worktree build trees live on the snapshotted home dataset, so
  hourly snapshots pin every deleted multi-GB build until retention
  rotates - the 2026-08-05 pool exhaustion was this compounding.
  Options: a non-snapshotted scratch dataset for builds (lake has
  headroom) or shorter hourly retention.
- BP-3c dual-output residual: multi_output's live transactions
  (mo_discover_outputs, capture/restore topology,
  place-secondary-for-topology, pin resolution) need the dual-output
  vehicle (E2E_OUTPUT_COUNT=2 via run-multi-output-e2e.sh); their pure
  cores are unit-covered including all refusal paths, the live
  dual-output drive is owed when that vehicle next runs (PR #185).
- Typed-readback refusal hardening: recipe.views()/view_applets() and the
  raw json_payload consumers crash with JSONDecodeError or a pydantic
  ValidationError when dbusreports refuses a reply (a view without an
  accepted placement, transient during edit-mode transitions);
  dock-edit-retarget-cancel maps the refusal to RecipeError locally, the
  shared API does not (found during the R4 retarget port). Decide the
  central mapping and port the local handling onto it.
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
