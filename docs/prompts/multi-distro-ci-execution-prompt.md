# Multi-distro CI + packaging execution prompt (drive to v0.20.0)

Re-runnable - KEEP INVOKING THIS PROMPT until the agreed CI matrix is
green on a release commit, the native packages build+install+gate on
every target, and v0.20.0 is tagged. Written 2026-07-17.

## Read first, in order

1. `CLAUDE.md` - the binding rigor: root-cause law, regression
   discipline (esp. the import-path/plugin-shadowing section - it bites
   in containers), code-clarity and Q_ASSERT rules, commit shape and
   definition of done, gate discipline, the PR workflow + independent-
   review contract (reviews are a lean OPUS subagent), copyright/
   attribution rules, author voice, no AI attribution.
2. `docs/tracking/multi-distro-ci-plan.md` - the plan: motivation (this is the
   v0.20.0 release gate), architecture, the golden-per-distro strategy,
   the distro set, the phased A-G checklist, the packaging workstream,
   and the open DECISIONs.
3. Skim the harness you are containerizing: `scripts/sceneprobe-gate.sh`,
   `scripts/lib-nested-kwin.sh`, `tests/sceneprobe/run_in_kwin.sh`,
   `tests/sceneprobe/imagecompare.h` + `main.cpp` (the three compare
   tiers), `tests/e2e/` (numbered recipes + `lib.sh`).
   `docs/tracking/session-handoff.md` carries the latest state.

## Mission

Execute `docs/tracking/multi-distro-ci-plan.md` top to bottom: take the existing
HEADLESS harness (nested kwin_wayland + lavapipe sceneprobe + the
tests/e2e nested-vehicle recipes) to the KDE-enthusiast distro set in
container CI with per-distro golden validation (Phases A-E), then produce
native packages for every format (Phases F-G), then tag v0.20.0. Tick the
plan's `- [ ]` items with commit hashes as work lands.

## Standing context (do not re-derive)

- The whole gate stack already runs HEADLESS on CPU lavapipe (no GPU, no
  /dev/dri) - exactly what a container gives. You are REUSING it.
- `podman` is the project's container runtime (NOT docker) and is on the
  host PATH - prototype every container step locally before wiring any CI.
- TWO toolchains, never conflate: the NixOS pinned stack stays nix
  (`nix develop -c ...`, `scripts/gate-all.sh`, bit-exact goldens) and
  is the canonical per-commit MERGE gate; the container work uses
  podman + DISTRO Qt6/KF6/Plasma/Mesa (not nix) and is the
  RELEASE/periodic gate.
- Hard floors (CMakeLists.txt): Plasma >= 6.5, Qt >= 6.6, KF6 >= 6.5.
  Excludes stock Ubuntu LTS 24.04 (Plasma 5.27). Verify each base image
  meets the floor.
- CI TEST LEGS (build+render environments): NixOS (bit-exact home), Arch
  (rolling; covers Manjaro/EndeavourOS/Garuda), openSUSE Tumbleweed
  (rolling; rpm-but-not-Fedora), Fedora (bless-frozen), KDE neon
  (Ubuntu-family/deb, covers Kubuntu/Pop/Mint-KDE), Debian sid, Gentoo,
  Void.
- PACKAGE FORMATS (five, blanket the ecosystem): deb, rpm (Fedora AND
  openSUSE from one spec), PKGBUILD, ebuild, xbps.
- Golden strategy = GRADUATED RIGOR (cross-environment rendering is NOT
  bit-exact): bit-exact on the NixOS pin, bless-frozen per frozen
  base-image tag on stable distros, invariant+tolerance on rolling. The
  probe already supports all three (imagecompare.h `checkInvariants` +
  `CompareTolerance{0,0}` vs `{2,0.005}`; per-scene `probeTolerance`) -
  extend the device/tier axis, do not invent machinery.
- Behavioral tests/e2e recipes assert STATE not pixels, so they are a
  HARD pass on every distro regardless of golden tier.
- No CI workflows exist yet (only the inherited `.kde-ci.yml`). Version
  is CMakeLists VERSION 0.10.77; tag target v0.20.0 (upstream stopped at
  v0.10.8).
- PARKED (do not chase): cross-ARCH aarch64 goldens. Never set
  `LP_NATIVE_VECTOR_WIDTH` - forcing it crashes lavapipe on this x86
  build. A self-hosted ARM Forgejo runner is the future home for an
  aarch64 tier; findings are in the plan doc.

## Decisions

RESOLVED (in the plan): 1 (graduated golden rigor), 3 (KDE neon is the
deb leg), 4 (openSUSE Tumbleweed in; full KDE-nerd set).

OPEN, collect from Bree WITH AskUserQuestion AT THE PHASE THAT NEEDS
THEM, not up front - none block Phase A/B:
- 2 (CI cadence) - needed at Phase D.
- 5 (release-gate scope: all legs vs NixOS + >=1) - needed at Phase E.
- 6 (runner: self-hosted Forgejo vs GitHub Actions) - needed at Phase D;
  keep workflows runner-portable until then.
- 7 (packaging phasing: all five recipes now vs mainstream-first) -
  needed at Phase F.
- 8 (swarm batching) - needed at Phase F.

## Execution order

CI matrix first (A-E), then packaging (F-G). Phase A needs no decision -
start it immediately.

- **A** containerize + build the port against each distro's Qt/KF6
  (local podman; resolve the per-distro dep-name matrix; pin base tags at
  the floor).
- **B** get nested kwin_wayland + lavapipe + the tests/e2e recipes
  running in each container (per-distro kwin env quirks and Mesa-version
  validation-layer suppressions; the harness already parameterizes the
  ICD and no-permission-checks).
- **C** per-distro golden tiers (extend the device/tier axis; bless the
  frozen tiers; set tolerances for rolling).
- **D** CI workflow, runner-agnostic (DECISION 6): matrix, `container:`
  per leg, layer cache, PNG-triple artifacts on failure (DECISION 2).
- **E** bump VERSION -> 0.20.0, changelog, release process; matrix green;
  tag (DECISION 5).
- **F** Tier-1 packaging recipes (deb/rpm/PKGBUILD/ebuild/xbps),
  CI-built, installed, and gated against the INSTALLED package. The SPDX
  attribution feeds debian/copyright (DEP-5) and rpm %license.
- **G** Tier-2 upstreaming (later wave; social/process, needs Bree's
  hands - AUR/Void/Gentoo overlay first, gatekept archives tracked only).

### Packaging swarm (Phase F, Bree's model)

Orchestrator + one WORKTREE subagent per package FORMAT, the repo's
established orchestrator-merges-serially pattern. The orchestrator owns
the SHARED scaffold (CI skeleton, version/changelog, common copyright
metadata, base images) so it is not duplicated five times; each subagent
owns one format in its own worktree. CLAUDE.md caps subagents at 4, so
batch: wave A = deb, rpm, arch; wave B = gentoo, void (DECISION 8). A
format subagent needs a PROVEN Phase-A/B build env for its distro first.
Every subagent keeps a ledger in docs/agent-logs/YYYY-MM-DD-<slug>.md.

## Risky unknowns - checkpoint cheaply BEFORE scaling

- Does nested kwin_wayland + lavapipe actually run inside each distro's
  container (no display, no GPU)? Prove ONE distro end to end before
  writing all legs. Per-distro kwin may need different env
  (dbus-run-session, private XDG_RUNTIME_DIR,
  KWIN_WAYLAND_NO_PERMISSION_CHECKS, the lavapipe ICD path).
- Per-distro Qt6/KF6/libplasma/kwin/mesa-lavapipe PACKAGE NAMES differ -
  resolve the dep matrix in Phase A1.
- lavapipe ICD + validation layers must come from the DISTRO packages,
  not a stray copy - same shadowing discipline as CLAUDE.md's regression
  section.
- One rpm .spec must build on both Fedora AND openSUSE (macros/names/OBS
  differ) - the F2 portability sub-check.

## Workflow discipline (from CLAUDE.md, non-negotiable)

- Every landing is a feature branch -> real GitHub PR
  (`nix run nixpkgs#gh --`) -> LEAN independent OPUS review (cold
  context, diff-read only, no rebuild/ctest - the branch gate stamp
  proves the gates) -> local `git merge --ff-only` -> master push. Never
  squash, never GitHub rebase-merge. Merge only on a MERGE verdict.
- Containerfiles, CI workflow files, packaging recipes and any
  sceneprobe-gate changes are CODE (not docs/*.md), so the pre-push hook
  needs a fresh `scripts/gate-all.sh` stamp before pushing them. The exit
  code is the verdict; never mask it; never read the verdict and push in
  one tool call.
- Conventional commits with root-cause bodies; author voice ("caught
  live", not "user-reported"); NO Co-Authored-By and NO "Generated with
  Claude Code" footer anywhere.
- Update docs/tracking/session-handoff.md as work lands, not at the end. Tick plan
  checkboxes with post-merge hashes. README updated when a landing is
  major (a new gate surface, the matrix going green, the release).

## Definition of done (the initiative)

The agreed matrix (DECISION 5) is green on a release commit; the five
package formats build+install+gate on their targets; CMakeLists VERSION
bumped 0.10.77 -> 0.20.0 with a changelog and a documented release
process; v0.20.0 tagged; the plan's A-G checklist ticked with hashes;
README states the multi-distro matrix, the packages, and the v0.20.0
release.

## Session protocol

Operate autonomously with a long horizon. Start Phase A now (local podman
build per distro). For each phase: read the plan item, checkpoint the
risky unknown on ONE distro before scaling, land small bisectable
commits, run the gates, PR + lean Opus review + ff-merge, tick the
checkbox, keep session-handoff current. When a build or render fails in a
container, root-cause it (version skew, dep name, kwin env) before moving
on - found defects outrank planned work. Collect an OPEN decision only
when its phase arrives. Flag anything needing Bree's hands (a decision, a
release tag, runner hosting, Tier-2 upstreaming) and move on rather than
blocking.
