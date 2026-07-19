# Multi-distro CI ORCHESTRATOR prompt (continue to v0.20.0)

Re-runnable - KEEP INVOKING until the agreed matrix is green on a release
commit, the native packages build+install+gate on every target, and
v0.20.0 is tagged. Written 2026-07-17, after Arch was proven end to end.

**You are the ORCHESTRATOR, not the sole worker.** There is a lot of work
left and most of it is parallelizable. Your job is to DECOMPOSE it into
self-contained chunks and FARM each chunk to a capable Opus worktree
subagent, then merge their work serially through the PR flow. Do the work
yourself only for the shared scaffold and the serial-merge steps that
cannot be parallelized. Reach for a subagent before reaching for your own
hands whenever a chunk is self-contained.

## Read first, in order

1. `CLAUDE.md` - the binding rigor: root-cause law, regression discipline
   (esp. import-path/plugin-shadowing - it bites in containers), commit
   shape + definition of done, gate discipline (exit codes, the pre-push
   stamp), the PR workflow + INDEPENDENT-REVIEW contract (reviews are a
   lean OPUS subagent, cold context, diff-only), copyright/attribution,
   author voice, no AI attribution, the subagent cap (batch at 4).
2. `docs/tracking/multi-distro-ci-plan.md` - the checklist, phases A-G, the
   graduated-golden strategy, the DECISIONs, and the "Execution model"
   section (the swarm shape).
3. `docs/prompts/multi-distro-ci-execution-prompt.md` - the STANDING
   mission/context/reuse/risky-unknowns. Still fully valid; THIS prompt
   changes HOW the work is driven (orchestrated), not WHAT the work is.
4. `docs/tracking/session-handoff.md` - the latest state (the 2026-07-17 Phase A/B/
   B2 entries).

## Where things stand (do not re-derive)

- **Arch is proven end to end in podman**: the port builds clean
  (565/565), the nested kwin_wayland + lavapipe sceneprobe renders 13/13
  (bit-exact against the nix goldens at 256-bit), and the behavioral e2e
  vehicle runs (`000-smoke` PASSES: dock reaches running, kactivitymanagerd
  activates, views settle, clean restart). PRs #8, #9, #10 merged.
- **Four portability defects are already fixed in master** (all nix-only
  spellings baked into portable tooling, each caught by RUNNING in the
  container): qmltypes `qt-6/qml` path, QMLDIR `lib/qml` (now emitted to
  `build/latte-qmldir.txt`), `run-staged.sh` `$USER` under set -u, plus
  two set -e/ordering traps. **Consequence: the tree is now
  distro-portable** - new distro legs should mostly "just build" with
  per-distro dep-NAME changes, not new source fixes. If a new leg needs a
  source fix, it is probably another latent nix-ism worth the same
  root-cause treatment.
- **Shared scaffold already exists**: `ci/containers/Containerfile.arch`
  (deps-only cacheable layer pattern) and `ci/build-and-gate.sh`
  (distro-agnostic build/test/gate driver; the `gate` stage is still a
  marked `# STUB:` pending B2 productionization).
- **The proven-by-hand B2 e2e recipe is captured** in the plan's B2 item:
  compile fakepointer (Arch has no `plasma-wayland-protocols.pc` - resolve
  `fake-input.xml` at `/usr/share/plasma-wayland-protocols/`), export
  `LATTE_QML_MODULE_PATH` = the distro framework qml tree
  (`/usr/lib/qt6/qml` on Arch - belongs in each Containerfile's ENV like
  the ICD path), seed a config via one `run-staged.sh` self-init run in a
  nested kwin, then `run-e2e.sh`.
- **Container runtime gotchas already found**: kwin_wayland's
  `cap_sys_nice=ep` must be stripped (`setcap -r`) or podman's default cap
  set makes execve EPERM; the lavapipe ICD + validation layer paths come
  from the distro packages via the Containerfile ENV (`LATTE_VULKAN_
  LAVAPIPE_ICD`, `LATTE_VK_LAYER_PATH`).

## Your role as orchestrator

- **Own the SHARED parts** so they are never duplicated across chunks:
  `ci/build-and-gate.sh`, the Containerfile pattern, the CI workflow
  skeleton (Phase D), version/changelog (Phase E), common packaging
  metadata + copyright (Phase F). When a wave depends on a shared piece,
  land that piece YOURSELF first, then dispatch the wave against it.
- **Decompose and dispatch**: one Opus worktree subagent per
  self-contained chunk. Batch under the 4-subagent cap (CLAUDE.md).
- **Merge serially**: each returned branch -> `scripts/gate-all.sh` stamp
  -> INDEPENDENT lean Opus review (separate cold-context agent, diff-only,
  no rebuild - the branch stamp proves the gates) -> `git merge --ff-only`
  -> master push -> prune. NEVER author-and-approve in one context; the
  reviewer is always a separate agent. Merge only on a MERGE verdict;
  root-cause blockers on the branch before merge, file non-blocking nits
  as plan items.
- **Keep the record current** as each chunk lands: tick plan checkboxes
  with POST-MERGE hashes, update `docs/tracking/session-handoff.md`, update the
  README when a landing is major (the matrix going green, the release).
- **Collect an OPEN DECISION** from Bree with `AskUserQuestion` only when
  its phase arrives (none block the Phase A/B wave): DECISION 2 (cadence,
  Phase D), 5 (release-gate scope, Phase E), 6 (runner, Phase D), 7/8
  (packaging phasing/batching, Phase F).
- **Flag Bree-hands items** and move on: the release tag, runner hosting,
  Tier-2 upstreaming (Phase G), any DECISION.

## Worker-agent contract (paste the relevant bits into every dispatch)

- **Model Opus, isolation worktree.** One chunk only.
- **Reuse, don't reinvent**: start from `Containerfile.arch` +
  `build-and-gate.sh` + the landed portability fixes; expect the port to
  build with mostly dep-NAME changes. The latte-dock-ng `docker/` pipeline
  (`~/Projects/latte-dock-ng/docker/Dockerfile.<distro>`) is the per-distro
  dep-name reference - but OUR set is a SUPERSET (libksysguard, kpipewire,
  plasma-pa, plasma5support, kcmutils, sonnet, ktextwidgets,
  qqc2-desktop-style, qt5compat, kidletime) PLUS render-gate deps ng omits
  (kwin, vulkan loader+lavapipe+validation, fonts, libcap, rsync, perl,
  python3, imagemagick). Do NOT copy ng's China USTC mirror lines or its
  Mageia leg.
- **Prototype every container step locally with podman** before concluding
  anything. Root-cause each failure to version skew / dep name / kwin env /
  another latent nix-ism - a found defect outranks planned work and gets
  its own fix commit with evidence.
- **Verify the floor**: each base image must meet Plasma >= 6.5 / Qt >= 6.6
  / KF6 >= 6.5 (CMakeLists). Pin the base tag (A3).
- **Standards**: small bisectable commits; conventional-commit bodies with
  mechanism -> root -> verification evidence; author voice ("caught live",
  never "the user"); NO Co-Authored-By / tool-attribution anywhere; SPDX +
  transplant citation rules; mark stubs both ways.
- **Ledger** in `docs/agent-logs/2026-..-<slug>.md`.
- **Handoff mechanic**: commit on the worktree branch, run
  `scripts/gate-all.sh` on it (the stamp is required before any code push;
  the container results are additional evidence, not the merge gate), push
  the branch, and report back the branch name + gate verdict + a summary of
  what landed and any found defects. The ORCHESTRATOR runs the independent
  review and does the ff-merge. Do NOT push master or open the PR yourself.

## Recommended wave plan

- **First, land the shared piece the waves need**: productionize the B2
  gate - wire the fakepointer build + config self-init seed + `run-e2e.sh`
  into `ci/build-and-gate.sh`'s `gate` stage (un-stub it), add imagemagick
  to `Containerfile.arch`, and characterize the FULL e2e suite in the Arch
  container (which recipes pass / skip live-only / need magick / flake).
  This can itself be one Opus agent (Arch, independent of new distros); its
  output is the reusable gate every distro leg then runs. Also give
  `run-staged.sh`'s unguarded `$HOME` the `${HOME:-...}` treatment (filed
  under B2).
- **Wave A (parallel, <=4 Opus worktree agents)**: Phase A/B legs for
  **fedora**, **opensuse Tumbleweed**, **debian sid**. Each agent:
  `Containerfile.<distro>` (adapt ng deps + our superset + render deps +
  the cap-strip + the framework-qml ENV), clean build, sceneprobe +
  `build-and-gate.sh gate` (once B2 is wired) green in-container,
  characterize, pin the base tag, ledger.
- **Wave B**: **KDE neon** (Ubuntu/deb leg), **gentoo**, **void**.
- **Then**: Phase C (per-distro golden tiers - decouple the render DEVICE
  from the golden TIER name in `tests/sceneprobe/main.cpp` so non-nix legs
  run invariant+tolerance instead of bit-exact; more central, one agent or
  do it yourself), Phase D (CI workflow, runner-agnostic - DECISION 6/2),
  Phase E (VERSION 0.20.0 + changelog + tag - DECISION 5), Phase F
  (packaging swarm, one Opus worktree agent per FORMAT: wave = deb, rpm,
  arch; then gentoo, void - orchestrator owns the shared CI/version/
  copyright scaffold).

## Concurrency cautions (the host is shared)

- podman's image store and the CPU are shared across concurrent agents.
  Heavy parallel container builds AND concurrent `gate-all` runs contend -
  the CLAUDE.md load lesson holds: a gate/ctest timeout under concurrent
  load is LOAD, not a defect; verify solo before calling a failure.
  Consider staggering the heaviest container builds.
- Each distro's deps image (`latte-ci-<distro>`) is built once and cached;
  each `podman run` gets a fresh `/build` tmpfs. Worktrees share the host
  nix + podman.

## Definition of done (unchanged)

The agreed matrix (DECISION 5) green on a release commit; the five package
formats build+install+gate on their targets; CMakeLists VERSION 0.10.77 ->
0.20.0 with a changelog + documented release process; v0.20.0 tagged; the
plan's A-G checklist ticked with hashes; README states the multi-distro
matrix, the packages, and the v0.20.0 release.

## Session protocol

Operate autonomously with a long horizon. Each cycle: land any shared
scaffold the next wave needs; dispatch the wave (<=4 Opus worktree agents,
each with the worker contract above); as branches return, merge serially
(gate stamp -> independent lean Opus review -> ff-merge -> prune); tick the
plan + handoff; collect a DECISION only when its phase arrives; flag
Bree-hands items and keep going. Prefer dispatching over doing it yourself
whenever a chunk is self-contained.
