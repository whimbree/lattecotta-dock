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

## Execution model (2026-07-17): orchestrator + Opus swarm

The next-session entry point is `docs/prompts/multi-distro-ci-orchestrator-
prompt.md` (supersedes the execution prompt for HOW the work is driven; the
execution prompt still holds the standing mission/context). Direction: the
remaining work (six more distro legs, golden tiers, CI workflow, release,
five packaging formats) is highly parallelizable, so the driving session
acts as an ORCHESTRATOR - it owns the shared scaffold (build-and-gate.sh,
the Containerfile pattern, the CI skeleton, version/changelog, packaging
metadata) and the serial-merge steps, and FARMS self-contained chunks to
capable Opus WORKTREE subagents (one distro leg / packaging format / the
B2 productionization per agent, batched under the 4-subagent cap). Merges
stay serial: each returned branch gets its gate-all stamp, then an
INDEPENDENT lean Opus review (separate cold-context agent, diff-only), then
ff-merge - never author-and-approve in one context. The heavy lifting that
made Arch work (the four portability fixes) is done, so new legs should
mostly build with dep-name changes only; a leg that needs a source fix is
probably another latent nix-ism to root-cause. Concurrency caution: podman
and the CPU are shared, so a gate/ctest timeout under concurrent agent load
is LOAD, not a defect - verify solo before calling a failure.

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
  CMakeLists.txt are a hard selection constraint). Two axes that are
  easy to conflate: the FIVE PACKAGE FORMATS (deb/rpm/PKGBUILD/ebuild/
  xbps, Phase F) already blanket the whole ecosystem, so "add every
  distro a KDE nerd runs" mostly adds CI TEST LEGS (build+render
  environments), not new formats. The KDE-enthusiast set, resolved
  (Bree, 2026-07-17 - "any distro a Latte nerd would run to"):
  - **NixOS** - the dev/gate home, BIT-EXACT tier (already live).
  - **Arch** (rolling) - the canonical KDE-nerd rolling distro; goldens
    rot, so TOLERANCE/INVARIANT tier. Covers Manjaro/EndeavourOS/Garuda
    (Arch-based, same makepkg/PKGBUILD) - add them as legs only if a
    downstream-specific bug appears.
  - **openSUSE Tumbleweed** (rolling, current Plasma; DECISION 4 = IN) -
    TOLERANCE tier. RPM but NOT Fedora: different macros, package names,
    and OBS as the idiomatic build host, so the .spec must build on both
    Fedora AND Tumbleweed (portability sub-check in F2).
  - **Fedora** (latest stable shipping Plasma >= 6.5, e.g. 42) - pinned
    tag, can carry a bless-frozen bit-exact tier.
  - **KDE neon** (Ubuntu-based, always-current Plasma, made BY KDE - the
    reference KDE-enthusiast environment; resolves DECISION 3 toward
    neon) - TOLERANCE tier; deb format. Covers Kubuntu/Pop!_OS/Mint-KDE
    (deb-based) - stock Ubuntu LTS 24.04 stays EXCLUDED (Plasma 5.27,
    below floor).
  - **Debian testing/sid** - deb, current-ish Plasma, the root of the
    largest derivative tree; a reasonable additional leg.
  - **Gentoo** and **Void** - source/minimalist nerd distros; already
    Phase F4/F5 formats and natural CI legs.
- **CI**: matrix, one leg per distro, `container:` on official base
  images, runner-agnostic (DECISION 6). Runners are x86_64 - fine,
  lavapipe is CPU. Build the dep layer once and cache it to keep legs
  fast. Upload actual/expected/diff PNG triples as artifacts on failure.

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
- [~] A1 Base Containerfile + per-distro package layers; resolve the
      distro dep-name matrix (Qt6/KF6/libplasma/kwin/mesa-lavapipe names
      differ per distro). ARCH DONE (ci/containers/Containerfile.arch,
      deps-only cacheable layer + ci/build-and-gate.sh distro-agnostic
      driver). Per-distro NAME reference: the latte-dock-ng docker/
      pipeline (my PR #26) covers arch/fedora/opensuse/ubuntu/debian/
      gentoo - adapt those, but our set is a SUPERSET (libksysguard,
      kpipewire, plasma-pa, plasma5support, kcmutils, sonnet,
      ktextwidgets, qqc2-desktop-style, qt5compat, kidletime) PLUS the
      render-gate deps ng omits (kwin, vulkan loader+lavapipe+validation,
      fonts). DEBIAN DONE (ci/containers/Containerfile.debian): deps-only
      layer against debian:testing, all names resolved (see the ledger
      docs/agent-logs/2026-07-17-debian-leg.md). Debian-specific name
      deltas: KF6 Kirigami is libkirigami-dev (NOT libkf6kirigami-dev);
      qt5compat -> qt6-5compat-dev; qqc2-desktop-style ->
      qml6-module-org-kde-desktop; PlasmaQuick/LibTaskManager/
      LibNotificationManager all ship in plasma-workspace-dev; layer-shell
      -> liblayershellqtinterface-dev; lavapipe -> mesa-vulkan-drivers.
      Debian ENV traps: lavapipe ICD is UNSUFFIXED lvp_icd.json; QML tree
      at the multiarch /usr/lib/x86_64-linux-gnu/qt6/qml; Qt6 host tools
      (qmltestrunner/qmllint) off-PATH under /usr/lib/qt6/bin (added to
      the image PATH). OPENSUSE TUMBLEWEED DONE
      (ci/containers/Containerfile.opensuse, deps-only cacheable layer +
      cap-strip + ENV block). Full dep-name resolution and the
      Arch->Tumbleweed rename table are in
      docs/agent-logs/2026-07-17-opensuse-leg.md; all KF6 frameworks are
      kf6-<name>-devel, ECM is kf6-extra-cmake-modules, qt6-5compat is
      qt6-qt5compat-devel, and Qt private headers are a SPLIT package
      (qt6-base-private-devel) Arch bundles in qt6-base. F2 feed: the ledger
      records the Tumbleweed-vs-Fedora divergences the single .spec must
      bridge (ECM name, qt6 private-devel name qt6-base-private-devel vs
      qt6-qtbase-private-devel, kwin6 vs kwin, %cmake macro set). Remaining:
      fedora/neon/gentoo/void layers. Commits: d68ad64c0, 3eaa21261, a14c6efef
- [~] A2 Clean cmake build of the port in each container locally (podman
      is on the host - prototype here, no CI yet). Record per-distro
      build quirks. ARCH DONE: builds clean (565/565) against Arch's Qt
      6.11.1 / KF6 6.28 / Plasma 6.7.3; 74/82 offscreen ctest pass. QUIRK
      found + fixed: two contract tests hardcoded the "qt-6/qml" (nixpkgs)
      qmltypes subdir spelling; Arch uses "qt6/qml" so configure aborted
      FATAL off-nix - now searches both (00400f16c). The 8 ctest failures
      are all harness-env (7 need the staged QML tree + LATTE_QML_MODULE_
      PATH; schemesmodeltest is non-hermetic re XDG_DATA_DIRS) -> Phase B.
      DEBIAN DONE: builds clean (565/565) against Debian testing's Qt
      6.10.2 / KF6 6.26.0 / Plasma 6.6.5; NO source fix needed for the
      build (master's four portability fixes carried the multiarch libdir
      cleanly). Offscreen ctest 80/81 with the image PATH fix (76/81
      without). QUIRK found + FIXED (own commit, a genuine libplasma
      VERSION-SKEW, not a nix-ism): askdestroysignalorderingtest pinned
      the libplasma 6.7 widened-guard behavior for containment-type
      applets, but Debian ships 6.6.5 where askDestroy still guards the
      immediate emit/prune with !isContainment() - stricter than the
      project's 6.5 floor. Fixed by gating the containment-type
      assertions on #if PLASMA_VERSION >= 6.7.0 so the test pins whichever
      contract the substrate ships (NixOS 6.7.3 branch unchanged, so the
      merge gate is untouched). QUIRK 2 (env, fixed in the Containerfile):
      Qt6 host tools live off-PATH under /usr/lib/qt6/bin, so the QML
      ctest gates hit "qmltestrunner: command not found" until PATH was
      extended. The one remaining ctest failure (qmllintgate) is CORRECT:
      its ratchet baseline is calibrated to the pinned nix qmllint and it
      refuses a foreign one by design - a NixOS-tier merge gate, excluded
      from the distro gate stage (note for B2). OPENSUSE TUMBLEWEED DONE:
      builds clean (565/565, same target count as Arch) against
      Tumbleweed's Qt 6.11.1 / KF6 6.28.0 / Plasma 6.7.3; the qt-6/qml vs
      qt6/qml search fix (00400f16c) and the KDE_INSTALL_QMLDIR emit
      (18aac31b0) carry the leg with NO source change - build/latte-
      qmldir.txt came out lib64/qt6/qml and resolved. test stage: 77/81
      ctest pass; the 4 fails (qmlcompilegate/qmlinteraction/qmllintgate/
      qmlcontracts) are the same harness-env QML-gate class Arch defers to
      Phase B, plus a Tumbleweed trap under it - the Qt6 QML tooling
      (qmllint/qmlcachegen/qmlimportscanner) is not on PATH here (under
      /usr/lib64/qt6/bin + /usr/libexec/qt6; only qmllint6 in /usr/bin), so
      the gate scripts' bare-name lookup misses. Flagged for B2. Remaining
      distros TBD.
      Commits: 00400f16c, d68ad64c0, 3fb8f899d, 3eaa21261, a14c6efef
- [~] A3 Pin the exact base image tags that meet the Plasma 6.5 floor;
      document the floor check per distro. Arch is rolling (archlinux:
      latest for the prototype; archive-snapshot pin TBD). DEBIAN DONE:
      debian:testing pinned (Qt 6.10.2 / KF6 6.26.0 / Plasma 6.6.5 - all
      above floor; sid not needed). Floor check recorded in the
      Containerfile header comment + the ledger. testing rolls slowly
      (tracks next stable), so the tag is stable enough; a digest pin can
      follow once CI caches the base. OPENSUSE TUMBLEWEED floor CHECKED
      in-container 2026-07-17 and recorded in the Containerfile comment: Qt
      6.11.1 / KF6 6.28.0 / Plasma+kwin 6.7.3, well past the >=6.5/>=6.6
      floor. Tumbleweed is rolling (opensuse/tumbleweed:latest for the
      prototype; an OBS/download.o.o archived-repo snapshot pin is TBD,
      same open item as Arch). Remaining distros TBD.
      Commits: 3eaa21261, a14c6efef

### Phase B - headless gates in-container
- [~] B1 Get nested kwin_wayland + lavapipe running in each container
      (per-distro kwin env quirks, validation-layer suppressions may
      differ by Mesa version; the harness already parameterizes ICD and
      no-permission-checks). ARCH DONE: nested kwin_wayland comes up
      headless in podman. QUIRK: Arch's kwin_wayland carries cap_sys_nice=ep
      and podman's default cap set excludes CAP_SYS_NICE, so execve failed
      EPERM until the image strips the cap (setcap -r); the cap is
      irrelevant to a throwaway CI compositor. DEBIAN DONE: nested
      kwin_wayland comes up headless in podman and drives the full
      sceneprobe suite (13/13) under the default cap set. QUIRK: Debian's
      kwin_wayland ships WITHOUT cap_sys_nice (getcap empty), so the
      setcap -r is a harmless no-op here - kept for parity/robustness in
      case a future package adds the cap. OPENSUSE TUMBLEWEED DONE: nested
      kwin_wayland (from kwin6) comes up headless in podman; the SAME
      cap_sys_nice=ep trap applies and the image strips it (libcap-progs
      provides setcap). Commits: 79a8008f0, 3eaa21261, a14c6efef
- [~] B2 Run the behavioral e2e recipes in-container; make them a hard
      pass on each distro. GATE STAGE PRODUCTIONIZED (branch
      multi-distro-ci-b2-gate): ci/build-and-gate.sh's gate stage is
      un-stubbed and runs the full headless gate in-container end-to-end
      GREEN (exit 0) - build -> ctest (80/80, minus two NixOS-tier entries)
      -> fakepointer -> sceneprobe (13/13) -> seed default layout -> e2e
      agnostic subset (6/6). The wiring is distro-agnostic: fakepointer
      resolves fake-input.xml via pkg-config then the container's
      LATTE_FAKEINPUT_PROTOCOL ENV (Arch ships no .pc); LATTE_QML_MODULE_PATH
      is asserted as a container contract (the reused QML harnesses re-exec
      into `nix develop` when it is unset, fatal in-container); seeding runs
      run-staged.sh once in a throwaway nested kwin to self-init the default
      My Layout, then run-e2e.sh copies its throwaway from that base. Image
      additions (Containerfile.arch): imagemagick, konsole, python (declared,
      was transitive), LATTE_QML_MODULE_PATH=/usr/lib/qt6/qml,
      LATTE_FAKEINPUT_PROTOCOL, and /usr/lib/qt6/bin on PATH (for the
      unversioned `qml` runtime the keyboard-nav recipe uses). Both PR-review
      nits folded in: run-staged.sh:85 $HOME guarded under set -u; the
      qml-interaction staging-reuse guard now uses KDE_INSTALL_QMLDIR
      ($qmldir leaked from qml_env_setup) instead of hardcoded lib/qml.
      Once LATTE_QML_MODULE_PATH is exported the A2 ctest failures dropped
      from 8 to 2: qmllintgate (the ratchet structurally refuses any qmllint
      outside /nix/store - a NixOS-tier merge gate, excluded from the matrix
      ctest) and schemesmodeltest (still non-hermetic, below). Trap found and
      fixed: the gate died EXIT 143 during seeding because
      nested_kwin_cleanup ends with `wait $NESTED_KWIN_PID` (the kwin's
      SIGTERM status, 143) and the seed subshell inherited the driver's
      set -e - the library is errexit-unsafe by design (run-e2e.sh uses
      `set -uo pipefail`), so the seed subshell now runs `set +e`. Full
      characterization in docs/agent-logs/2026-07-17-b2-gate-productionization.md.
      FULL e2e suite = 6 PASS (000-smoke, 010-wheel-desktops,
      030-wheel-ruler-maxlength, 060-geometry-agreement, keyboard-navigation-
      mode, settings-window-onscreen - these are the gate's hard set), 5
      environment-dependent FAIL, each root-caused and filed here as the
      remaining B2 work (NOT silently dropped - enumerated in the driver's
      run-e2e call too):
        - 020-wheel-task-cycle / 040-preview-tooltip-text /
          parabolic-hover-preview: ONE root cause - a konsole window's app_id
          resolves to the bare "konsole" in-container, not the
          "org.kde.konsole.desktop" the recipes match. VERIFIED via a live
          vehicle probe that konsole windows DO map and the dock DOES track
          them as tasks, so this is desktop-app-database resolution (the
          dev's plasma-integrated nix env provides it; a bare container does
          not, even after kbuildsycoca6 into a shared cache), not konsole
          availability. TO DO: investigate the app_id resolution divergence
          (KService menu-id / StartupWMClass / konsole version) and/or make
          the recipes accept the container's app_id form.
        - 050-drag-reorder-launchers: the default-template seed carries a
          tasks applet with two launchers whose app_ids are EMPTY (same
          resolution gap) and the recipe needs >=3 real pinned launchers. TO
          DO: a purpose-built e2e fixture config with resolvable launchers,
          not the bare default template seed.
        - duplicate-view-idremap: separate root cause (not app-db, not a load
          flake - confirmed solo) - removeView's containment removal never
          reaches the layout file even after the full 120s undo window. TO
          DO: root-cause the libplasma removal-flush divergence on Arch.
      konsole+imagemagick stay in the image so the four app-integration
      recipes drop straight in once the above land. STILL OPEN (unchanged):
      schemesmodeltest is non-hermetic - the KF6 color-scheme lookup pulls
      the distro's /usr/share/color-schemes over the test's temp fixtures
      even though the test pins XDG_DATA_DIRS, so real Breeze schemes shift
      the asserted rows (the nix devShell's allow-listed XDG_DATA_DIRS hides
      it); excluded from the matrix ctest, needs a hermetic scheme-path
      injection. Commits:
- [x] B3 Run sceneprobe in-container in invariant+tolerance mode; confirm
      scenes render (not blank, right regions). ARCH DONE: all 13 scenes
      render and PASS in the Arch container - and bit-exact against the
      nix-blessed lavapipe goldens (Arch llvmpipe 22.1.8 at 256-bit
      matches nix Mesa for these text-free scenes), so no tolerance tier
      was even needed on Arch at this Mesa version. The QMLDIR fix
      (18aac31b0) was the enabler; before it, 3 scenes failed "module
      org.kde.latte.components not installed". Phase C still adds the
      per-distro tier axis for rolling drift, but the render path is
      proven. DEBIAN DONE: all 13 scenes render and PASS in the Debian
      container, self-test valid - and bit-exact against the nix-blessed
      lavapipe goldens even though Debian's Mesa is 26.1.4/LLVM21, NEWER
      than Arch's llvmpipe 22.1.8 and the nix pin. So the lavapipe output
      is bit-stable for these text-free scenes across three distinct Mesa
      versions; no tolerance tier needed on the current Debian Mesa. The
      image ENV (LATTE_QML_MODULE_PATH, lavapipe ICD path) is what let the
      reused gate resolve the distro's framework qml tree and ICD.
      OPENSUSE TUMBLEWEED DONE: all 13 scenes render and PASS in the
      Tumbleweed container, and bit-exact against the nix-blessed lavapipe
      goldens too - Tumbleweed's Mesa 26.1.4 lavapipe matches nix Mesa for
      these text-free scenes, same as Arch, so no tolerance tier was needed
      at this Mesa version (recorded, not relied on - Phase C still owns the
      rolling-drift axis). The QMLDIR emit (18aac31b0) carries it: build/
      latte-qmldir.txt came out lib64/qt6/qml and the staged org.kde.latte.*
      modules resolved. Commits: 18aac31b0, 79a8008f0, 3eaa21261, a14c6efef

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
      install, gate. Portability sub-check: the same .spec must also build
      on openSUSE Tumbleweed (different macros/package names/OBS) - fix
      the divergences so one spec serves both. Commits:
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
- DECISION 3 - RESOLVED (2026-07-17): KDE neon is the Ubuntu-family/deb
  leg (always-current Plasma, the KDE reference env); covers Kubuntu/Pop/
  Mint-KDE. Stock Ubuntu LTS 24.04 excluded (below floor).
- DECISION 4 - RESOLVED (2026-07-17): openSUSE Tumbleweed IN. Widened the
  CI test-leg set to the full KDE-enthusiast list (Arch, Tumbleweed,
  Fedora, neon, Debian sid, Gentoo, Void); package formats stay five.
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
