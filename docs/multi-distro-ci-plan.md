# Multi-distro CI matrix plan (v0.12.0 release pre-req)

Planning artifact, written 2026-07-17. The Plasma 6 port has a mature
HEADLESS gate stack (nested kwin_wayland + lavapipe sceneprobe, the
tests/e2e nested-vehicle suite) that today runs only on the pinned NixOS
dev environment. This plan takes that same harness to the distros real
users run - Arch, Fedora, an Ubuntu-family Plasma 6 image - in a fully
automated container CI matrix with per-distro golden validation. Green
across the matrix is the release gate for **v0.12.0**, the first tagged
continuation release (upstream Latte stopped at v0.10.8; this tree is at
an interim VERSION 0.10.77 in CMakeLists.txt). A linked NATIVE PACKAGING
workstream (Phases F-G) rides on the same per-distro build environments
to produce installable .deb/.rpm/PKGBUILD/ebuild/xbps artifacts.

This is a CHECKLIST, not prose to read once - same discipline as
docs/PORTING_PLAN.md. Every task is a `- [ ]` with a Commits: line;
tick and fill as work lands.

## Why this is the v0.12.0 gate

The NixOS pinned sceneprobe gate proves BIT-EXACT determinism under a
frozen Qt+Mesa+fontconfig, which is the precise per-commit regression
tool. It proves nothing about whether the port BUILDS and RENDERS
correctly against the distro-shipped Qt6/KF6/Plasma/Mesa that actual
users have. A continuation release that jumps upstream's dormant v0.10.8
to v0.12.0 needs that portability evidence, automated, before the tag.
The matrix is additive: the NixOS pinned tier stays the canonical merge
gate; the distro matrix is the release/periodic gate.

## Parked prior art: cross-architecture golden determinism (2026-07-17)

Recorded so the next session does not re-derive it. Before choosing the
multi-distro direction, cross-ARCHITECTURE golden verification (item 9's
"needs a second machine") was investigated and PARKED:

- The NixOS goldens reproduce bit-exactly at the host's NATIVE llvmpipe
  vector width (256-bit on this AVX2 Ryzen) - 13/13. Determinism on the
  native path is solid.
- Forcing `LP_NATIVE_VECTOR_WIDTH=128` (to model ARM/NEON or pre-AVX2
  x86) CRASHES lavapipe on this x86 Mesa build - the broken-shader
  self-test and 12/13 real scenes segfault (24 core dumps), zero images
  produced. So the width knob is NOT a usable cross-machine proxy and
  must never be set in the gate env. It also kills the idea of
  "pin the vector width to make determinism structural" - pinning below
  native crashes on this stack.
- Cross-ARCH bit-exactness (aarch64 vs x86) is therefore expected to be
  FALSE anyway (different LLVM backend + SIMD lowering), so any ARM path
  needs its own per-arch golden tier, not a shared golden.
- Producing an aarch64 guest on this x86 host is blocked without a
  system change: a cross NixOS VM dry-run needs 264 aarch64 derivations
  built LOCALLY (only 799 of ~1063 paths are cache-fetchable), and the
  host has no aarch64 in extra-platforms and no qemu-aarch64 binfmt
  registered. Unblocking needs either
  `boot.binfmt.emulatedSystems = [ "aarch64-linux" ]` (root + rebuild)
  or a real/cloud ARM box. Deferred deliberately - the multi-distro
  matrix below is the higher-value portability signal and needs none of
  that.

The one design lesson that carries forward: **cross-environment rendering
is not bit-exact**, so per-environment golden tiers with graduated rigor
(below) are the right model, whether the environment axis is CPU arch or
distro.

## What we reuse (already built, portable by design)

- `scripts/sceneprobe-gate.sh` + `tests/sceneprobe/` - renders 13 scenes
  through nested kwin_wayland on lavapipe (pure-CPU Vulkan, no GPU, no
  /dev/dri), golden compare with a self-test guard. Runs headless, which
  is exactly what a CI container provides.
- `tests/e2e/` nested-vehicle suite - numbered behavioral recipes
  (000-smoke .. 060-geometry-agreement, plus keyboard-nav, drag-reorder,
  preview-tooltip, duplicate-view). These assert STATE (views settle,
  geometry agreement, D-Bus readbacks) not pixels, so they are
  environment-agnostic and should pass on any distro that builds.
- `scripts/lib-nested-kwin.sh`, `tests/sceneprobe/run_in_kwin.sh` - the
  headless compositor harness (dbus-run-session, private XDG_RUNTIME_DIR,
  KWIN_WAYLAND_NO_PERMISSION_CHECKS, ICD selection).
- The probe ALREADY has graduated rigor (tests/sceneprobe/imagecompare.h,
  main.cpp): `checkInvariants(frame, 0.005)` structural check;
  `CompareTolerance{0,0.0}` bit-exact for the lavapipe device vs
  `{2,0.005}` tolerance for others; per-scene `probeTolerance` override.
  Per-distro tiers are a natural extension of the existing device axis,
  not new machinery.

## Architecture

- **One parameterized Containerfile** (base stage + per-distro package
  layer) that, per distro: installs build deps (Qt6 base/declarative/
  shadertools, the KF6 set from .kde-ci.yml, libplasma, kwin_wayland,
  wayland, mesa lavapipe / vulkan-loader + validation layers, cmake,
  ninja, gcc, jq, fonts, the imgdiff deps), builds the port with cmake
  against the DISTRO Qt/KF6 (not nix), and runs the headless gates.
- **Distro matrix** (the Plasma 6.5 / Qt 6.6 / KF6 6.5 floors in
  CMakeLists.txt are a hard selection constraint):
  - **Arch** (rolling) - always-current Plasma 6; rolling means goldens
    rot, so it runs the TOLERANCE/INVARIANT tier, not bit-exact.
  - **Fedora** (latest stable that ships Plasma >= 6.5, e.g. Fedora 42) -
    pinned base tag, can carry a blessed bit-exact tier per frozen tag.
  - **Ubuntu-family Plasma 6** - stock Ubuntu LTS 24.04 ships Plasma 5.27
    (BELOW floor, unusable); options are Ubuntu 25.04+ (Plasma 6.3) or
    KDE neon (Ubuntu-based, always-current Plasma - arguably the best
    Ubuntu-family target for a Plasma dock). DECISION 3.
  - Optional 4th: openSUSE Tumbleweed (rolling, current Plasma).
- **CI**: GitHub Actions matrix, one leg per distro, `container:` on
  official base images. Hosted runners are x86_64 - fine, lavapipe is
  CPU. Build the dep layer once and cache via GHCR to keep legs fast.
  Upload actual/expected/diff PNG triples as artifacts on failure.

## The golden-per-distro strategy (the crux)

Each distro ships different Mesa/lavapipe + Qt, and distro packages MOVE
(Arch rolling especially), so bit-exact per-distro goldens rot fast -
the same variance the nix pin was invented to kill. Resolve with
GRADUATED RIGOR per tier (the probe already supports all three):

1. **NixOS pinned tier** - BIT-EXACT goldens (`*.expected.lavapipe.png`),
   the precise per-commit merge gate. Unchanged from today.
2. **Stable distros pinned to a frozen base-image tag** (Fedora N,
   Ubuntu/neon tag) - optionally BLESS a per-distro bit-exact golden
   (`*.expected.<distro>.png`) valid for that frozen tag, re-blessed when
   the base image bumps. High precision, bounded rot.
3. **Rolling distros (Arch) and default non-NixOS** - INVARIANT +
   TOLERANCE only: `checkInvariants` (renders, non-blank, opaque
   fraction, expected color regions) plus a per-distro
   `CompareTolerance{delta,budget}`. Proves "builds and renders the right
   thing" without golden rot; not a pixel regression gate.

The behavioral e2e recipes assert state, not pixels, so they are a HARD
pass on every distro regardless of tier.

## Gating policy

- **Build** - hard gate, must pass on every matrix distro (release
  blocker).
- **Behavioral e2e recipes** (tests/e2e/0xx + named) - hard gate on every
  distro; environment-agnostic state assertions.
- **Sceneprobe** - bit-exact on NixOS (merge gate); per-distro tier
  (bless-frozen or invariant+tolerance) on the matrix (release gate,
  diff artifacts on failure).
- v0.12.0 blocks on the agreed matrix being green (DECISION 5: all
  distros, or NixOS + at least one mainstream distro).

## Phased checklist

### Phase A - containerization (build the port on each distro)
- [ ] A1 Base Containerfile + per-distro package layers; resolve the
      distro dep-name matrix (Qt6/KF6/libplasma/kwin/mesa-lavapipe names
      differ per distro). Commits:
- [ ] A2 Clean cmake build of the port in each container locally (podman
      is on the host - prototype here, no CI yet). Record per-distro
      build quirks. Commits:
- [ ] A3 Pin the exact base image tags that meet the Plasma 6.5 floor;
      document the floor check per distro. Commits:

### Phase B - headless gates in-container
- [ ] B1 Get nested kwin_wayland + lavapipe running in each container
      (per-distro kwin env quirks, validation-layer suppressions may
      differ by Mesa version; the harness already parameterizes ICD and
      no-permission-checks). Commits:
- [ ] B2 Run the behavioral e2e recipes in-container; make them a hard
      pass on each distro. Commits:
- [ ] B3 Run sceneprobe in-container in invariant+tolerance mode; confirm
      scenes render (not blank, right regions). Commits:

### Phase C - per-distro golden tiers
- [ ] C1 Extend the sceneprobe device/tier axis to per-distro naming
      (`*.expected.<tier>.png`) and wire the graduated-rigor selection
      (bit-exact NixOS / bless-frozen stable / invariant+tolerance
      rolling). Commits:
- [ ] C2 Bless the pinned-tag tiers for the stable distros; set
      tolerances for the rolling tier. Commits:

### Phase D - CI matrix workflow (runner-agnostic)
Runner is UNDECIDED (DECISION 6): self-hosted Forgejo runners or GitHub
Actions. Forgejo Actions is largely GH-Actions-syntax-compatible, so keep
the workflow portable (avoid GH-only actions; parameterize the registry/
cache). A self-hosted Forgejo runner also opens an ARM runner later - the
natural home for the parked aarch64 golden tier.
- [ ] D1 Net-new CI workflow (none exist today; only the inherited
      .kde-ci.yml). Matrix over distros, `container:` per leg, a layer
      cache (registry TBD by runner choice), PNG-triple artifacts on
      failure. Commits:
- [ ] D2 Triggers: build + e2e on PR-to-master if fast enough; full
      sceneprobe matrix nightly + on release tags (DECISION 2). Commits:
- [ ] D3 Required checks / branch protection wired to the matrix.
      Commits:

### Phase E - v0.12.0 release
- [ ] E1 Bump CMakeLists VERSION 0.10.77 -> 0.12.0; changelog; release
      process doc. Commits:
- [ ] E2 Matrix green on the release commit; tag v0.12.0. Commits:

## Native packaging workstream (distinct from, built on, the CI matrix)

The CI matrix above is a GATE ("does it build+render on distro X"); native
packaging produces installable ARTIFACTS (.deb/.rpm/PKGBUILD/ebuild/xbps).
They share the per-distro build ENVIRONMENTS - once Phase A can build in a
distro's container, producing that distro's package is one recipe away. So
packaging is an EXTENSION of the matrix, sequenced AFTER Phase A/B (a
packaging task needs a proven build env for its distro first).

Two tiers - the honest cut is recipes vs upstreaming, not
mainstream vs obscure:

- **Tier 1 - in-repo recipes + CI-validated packages (automatable, do all
  five formats now).** The marginal cost of the "obscure" formats is small
  once the container build works, so there is no reason to split them into
  a later wave. Each format's recipe lives in-repo under packaging/<fmt>/;
  CI builds it in that distro's container, installs the built package, and
  runs the gates against the INSTALLED package (not just the build tree).
  The SPDX attribution audit (David Goree, Michail, the KDE authors) feeds
  directly into debian/copyright (DEP-5) and the rpm %license block.
- **Tier 2 - upstreaming into official channels (the real second wave;
  social/process, not code, so an agent cannot "do it now").** Low-friction
  channels we control first (AUR, Void void-packages PR, a Gentoo overlay/
  GURU, a personal Copr/OBS repo); the heavy gatekept archives (Debian
  proper, Fedora dist-git, openSUSE OBS Factory) are a genuine later wave
  with review queues and maintainer duties.

Execution model (Bree's idea, 2026-07-17): orchestrator + one worktree
subagent per package FORMAT, the repo's established
orchestrator-merges-serially pattern. The orchestrator owns the SHARED
parts (CI skeleton, version/changelog, common copyright metadata, base
images) so they are not duplicated five times; each subagent owns one
format. CLAUDE.md caps subagents at 4, so batch: wave A = deb, rpm, arch
(highest reach); wave B = gentoo, void.

### Phase F - Tier 1 packaging recipes (after A/B; swarm)
- [ ] F1 packaging/debian/ (control, rules via dh cmake, changelog,
      DEP-5 copyright from the SPDX audit); build .deb in the Ubuntu-family
      container, install, gate. Commits:
- [ ] F2 packaging/rpm/latte-dock.spec (%cmake macros, Qt6/KF6/Plasma
      BuildRequires, %license); build .rpm in the Fedora container,
      install, gate. Commits:
- [ ] F3 packaging/arch/PKGBUILD (+ .SRCINFO); makepkg in the Arch
      container, install, gate. Commits:
- [ ] F4 packaging/gentoo/ ebuild (cmake + kde eclasses, USE flags) in an
      overlay layout; build in a Gentoo container, gate. Commits:
- [ ] F5 packaging/void/ template (xbps-src) in a void-packages layout;
      build in a Void container, install, gate. Commits:
- [ ] F6 CI: extend the matrix to build + install + gate each package
      format as an artifact (registry/runner per DECISION 6). Commits:

### Phase G - Tier 2 upstreaming (later wave; needs Bree's hands)
- [ ] G1 Low-friction channels we control: AUR push, Void PR, Gentoo
      overlay, personal Copr/OBS. Commits:
- [ ] G2 Gatekept archives (Debian/Fedora/openSUSE Factory) - process,
      sponsorship, review; tracked, not agent-automatable. Commits:

## Open decisions (need Bree)

- DECISION 1 - golden rigor per distro: the graduated model above
  (bit-exact NixOS / bless-frozen stable / invariant+tolerance rolling)
  vs simpler (invariant-only everywhere except NixOS). Recommendation:
  graduated.
- DECISION 2 - CI cadence: build+e2e on every PR vs nightly + pre-release
  only. Recommendation: build+e2e on PR if leg time is acceptable, full
  sceneprobe matrix nightly + on release tags.
- DECISION 3 - Ubuntu-family target: Ubuntu 25.04+ vs KDE neon vs
  Kubuntu. Recommendation: KDE neon (always-current Plasma, Ubuntu base).
- DECISION 4 - 4th distro (openSUSE Tumbleweed) in or out for v0.12.0.
- DECISION 5 - release gate scope: ALL matrix distros green vs NixOS +
  >=1 mainstream distro green.
- DECISION 6 - runner: self-hosted Forgejo runners vs GitHub Actions
  (undecided; keep the workflow runner-portable either way). A self-hosted
  ARM Forgejo runner would later host the parked aarch64 golden tier.
- DECISION 7 - packaging phasing: all five Tier-1 recipes now (the
  recommendation - the obscure formats are cheap once the build env
  exists) vs mainstream three now + gentoo/void in a later wave.
- DECISION 8 - packaging swarm batching: wave A (deb, rpm, arch) + wave B
  (gentoo, void) under the 4-subagent cap, orchestrator owns the shared
  CI/version/copyright scaffold. Confirm the cut.
