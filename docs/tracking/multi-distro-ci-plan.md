# Multi-distro CI matrix plan (v0.20.0 release pre-req)

Planning artifact, written 2026-07-17. The Plasma 6 port has a mature
HEADLESS gate stack (nested kwin_wayland + lavapipe sceneprobe, the
tests/e2e nested-vehicle suite) that today runs only on the pinned NixOS
dev environment. This plan takes that same harness to the distros real
users run - Arch, Fedora, an Ubuntu-family Plasma 6 image - in a fully
automated container CI matrix with per-distro golden validation. Green
across the matrix is the release gate for **v0.20.0**, the first tagged
continuation release (upstream Latte stopped at v0.10.8; this tree is at
an interim VERSION 0.10.77 in CMakeLists.txt). A linked NATIVE PACKAGING
workstream (Phases F-G) rides on the same per-distro build environments
to produce installable .deb/.rpm/PKGBUILD/ebuild/xbps artifacts.

This is a CHECKLIST, not prose to read once - same discipline as
docs/tracking/PORTING_PLAN.md. Every task is a `- [ ]` with a Commits: line;
tick and fill as work lands.

## Why this is the v0.20.0 gate

The NixOS pinned sceneprobe gate proves BIT-EXACT determinism under a
frozen Qt+Mesa+fontconfig, which is the precise per-commit regression
tool. It proves nothing about whether the port BUILDS and RENDERS
correctly against the distro-shipped Qt6/KF6/Plasma/Mesa that actual
users have. A continuation release that jumps upstream's dormant v0.10.8
to v0.20.0 needs that portability evidence, automated, before the tag.
The matrix is additive: the NixOS pinned tier stays the canonical merge
gate; the distro matrix is the release/periodic gate.

## Execution model (2026-07-17): orchestrator + Opus swarm

The procedural entry point remains `docs/prompts/multi-distro-ci-orchestrator-
prompt.md`; the execution prompt retains the standing mission and context.
Phases A-C (local distro containers, headless gates, and golden tiers) and F1-F5
(the five local native package recipes) are complete. F6 (package-artifact CI),
Phase D (the hosted CI matrix), Phase E (the release), and Phase G (package
upstreaming) remain pending and are outside the completed local-recipe scope.

The driving session remains an ORCHESTRATOR for the parallel work: it owns
shared packaging metadata and serial landing, and farms one self-contained
format to each capable Opus worktree subagent under the four-subagent cap.
Returned branches receive their applicable gate evidence and an independent
lean Opus diff review before landing through the current GitHub PR rebase-merge
workflow; authoring and approval never share one context. Podman and CPU are
shared, so a gate or ctest timeout under concurrent agent load is load, not a
defect; verify it solo before changing code.

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
  images, runner-agnostic (DECISION 6, the hosted-CI runner choice). Runners are
  x86_64 - fine, lavapipe is CPU. Build the dep layer once and cache it to keep
  legs fast. Upload actual/expected/diff PNG triples as artifacts on failure.

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
- v0.20.0 blocks on the agreed matrix being green (DECISION 5: all
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
      qt6-qtbase-private-devel, kwin6 vs kwin, %cmake macro set). FEDORA DONE
      (ci/containers/Containerfile.fedora, same deps-only pattern). Fedora
      name deltas resolved by repoquery on fedora:43: kf6-plasma-devel ->
      libplasma-devel; qqc2-desktop-style -> kf6-qqc2-desktop-style;
      kpipewire-devel / plasma5support-devel (no kf6- prefix); kwin_wayland
      lives in the `kwin` package; lavapipe is mesa-vulkan-drivers; fonts are
      google-noto-sans-fonts. Two Fedora packaging SPLITS added as build deps
      (Arch/nix bundle them): qt6-qtbase-private-devel (Qt6::GuiPrivate for
      the sceneprobe) and libasan/libubsan (the sanitized unit tests link
      -fsanitize). Full dep-name resolution in
      docs/agent-logs/2026-07-17-fedora-leg.md. KDE NEON DONE
      (ci/containers/Containerfile.neon, deps-only cacheable layer, deb/
      multiarch like Debian). Base is the KDE registry image
      invent-registry.kde.org/neon/docker-images/plasma:user (the stale
      Docker Hub kdeneon/* tags are Ubuntu 22.04 / Plasma 5.27, below floor).
      neon builds its own KF6/Plasma packages, so FIVE dev names diverge from
      BOTH Arch and Debian, resolved by in-image apt dry-run: KSvg is
      kf6-ksvg-dev (libkf6svg-dev does NOT exist on neon); qqc2-desktop-style
      is kf6-qqc2-desktop-style (not qml6-module-org-kde-desktop);
      plasma5support-dev (the kf6- name is a transitional dummy);
      plasma-activities-dev / plasma-activities-stats-dev; qt6-svg-dev
      (libqt6svg6-dev is a transitional dummy). Everything else matches
      Debian. neon-specific: the base defaults to an unprivileged USER neon
      (uid 1001), so the image sets USER root for apt + the driver (the other
      legs already run as root). ENV/traps identical to Debian (unsuffixed
      lvp_icd.json, multiarch qt6/qml tree, Qt6 host tools off-PATH under
      /usr/lib/qt6/bin). Full resolution:
      docs/agent-logs/2026-07-17-neon-leg.md. GENTOO DONE
      (ci/containers/Containerfile.gentoo): NOT a package-name distro - a
      BINHOST distro. Deps come as prebuilt binpkgs from
      distfiles.gentoo.org/releases/amd64/binpackages/23.0/x86-64 wired via
      binrepos.conf + FEATURES=getbinpkg, not a per-package name install. The
      decisive resolution was the BASE IMAGE, not names: the KDE binpkgs carry
      USE=systemd, so gentoo/stage3:amd64-systemd (not the default openrc
      stage3) is what lets --binpkg-respect-use accept them instead of
      source-rebuilding the whole Plasma stack. package.use entries align
      computed USE to the binhost's recorded USE (diffed from the Packages
      index); the render gate needs two mandatory SOURCE rebuilds the binhost
      omits (media-libs/mesa +video_cards_lavapipe, kde-plasma/kwin +lock).
      Full atom/USE resolution + binhost recipe in
      docs/agent-logs/2026-07-17-gentoo-leg.md. All 8 legs done.
      Commits: d68ad64c0, 3eaa21261, a14c6efef, 2b2469b5e, 0d052f3b7, 8838dcf31
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
      the gate scripts' bare-name lookup misses. Flagged for B2. FEDORA DONE:
      builds clean (565/565, identical target count) against Fedora's Qt
      6.10.3 / KF6 6.28.0 / libplasma 6.7.2; 78/82 offscreen ctest pass
      (beats Arch's 74/82 because the image ENV sets LATTE_QML_MODULE_PATH,
      so the QML-loading unit tests resolve their modules). The 4 failures
      are all the QML SCRIPT-GATE ctests (qmlcompile/qmlinteraction/qmllint/
      qmlcontracts) - harness-env, same class as Arch: on Fedora
      qmltestrunner/qmllint live under /usr/lib64/qt6/bin (installed but off
      the default PATH) -> a B2 PATH note. No SOURCE fix was needed to build;
      the two Fedora blockers were missing build DEPS (qt6-qtbase-private-
      devel, libasan/libubsan), resolved in the Containerfile, not tree
      defects. KDE NEON DONE: builds clean (565/565, same benign
      dropclassifiertest -Wmissing-field-initializers as Debian/Fedora)
      against neon's Qt 6.11.1 / KF6 6.28.0 / Plasma 6.7.3 on noble; NO
      source fix needed (master's portability fixes carry the multiarch
      libdir). ci/build-and-gate.sh test -> 80/80: the driver's test stage
      now runs ctest -E '^(qmllintgate|schemesmodeltest)$', so the two
      NixOS-tier entries are excluded by construction and every other test
      passes (the image PATH carries /usr/lib/qt6/bin for the QML
      script-gates, and LATTE_QML_MODULE_PATH resolves the framework modules).
      The raw ctest still shows those two failing for the documented
      NixOS-tier reasons only (qmllint ratchet refuses a foreign qmllint;
      schemesmodeltest non-hermetic against the neon base's real Breeze
      schemes) - the same pair every non-nix leg excludes, no per-leg action.
      neon is on the Plasma 6.7 branch so the askDestroy contract needs no
      #if gate (unlike Debian 6.6.5). VOID DONE
      (ci/containers/Containerfile.void): builds clean (565/565, same target
      count) against Void's Qt 6.11.1 / KF6 6.25.0 / libplasma 6.6.3. NO ng
      docker reference exists for Void, so every name was resolved by
      xbps-query -Rs prototyping (full table in
      docs/agent-logs/2026-07-17-void-leg.md). Void name deltas: KF6 is
      kf6-<name>-devel (like Tumbleweed) but ECM is the BARE
      extra-cmake-modules (no kf6- prefix, unlike Tumbleweed); Qt private
      headers split into qt6-base-private-devel (like Tumbleweed); libplasma
      is libplasma-devel and plasma-workspace/-activities/plasma5support keep
      bare names + -devel (NOT version-prefixed like Tumbleweed's plasma6-*);
      kwayland is kf6-kwayland-devel; layer-shell is layer-shell-qt-devel;
      kwin (bare) ships kwin_wayland; lavapipe is mesa-vulkan-lavapipe;
      Vulkan headers/validation use Khronos CamelCase (Vulkan-Headers,
      Vulkan-ValidationLayers); setcap is in libcap-progs; the glibc-full
      base image ships NO bash (a hard build dep). One PORTABILITY GAP fixed
      in the image (a distro dep-split, NOT a source defect): Void splits the
      gcc ASan/UBSan runtime + static preinit into libsanitizer-devel, which
      the step-2.5 law's -fsanitize test targets need to link. test stage:
      80/82 ctest (schemesmodeltest non-hermetic XDG + qmllintgate ratchet -
      the known classes; better than Arch/Tumbleweed because the image
      exports LATTE_QML_MODULE_PATH + Qt6 QML host-tool PATH). GENTOO DONE:
      builds clean (565/565, identical target count) against the binhost's Qt
      6.11.1 / KF6 6.27.0 / libplasma 6.6.6; NO port source change needed
      (master's portability fixes carry the split-usr lib64 layout -
      build/latte-qmldir.txt came out lib64/qt6/qml). Offscreen ctest = 79/79
      (100%, the 2 NixOS-tier excluded), BEATING Arch (74/82) and Fedora
      (78/82): the image ENV (LATTE_QML_MODULE_PATH=/usr/lib64/qt6/qml) lets
      the QML-loading unit tests resolve, and /usr/lib64/qt6/bin +
      /usr/libexec/qt6 on the image PATH lets the QML script-gates find
      qmltestrunner/qmllint (same off-PATH trap as Fedora/Tumbleweed, resolved
      in the image). No build DEPS were missing - Gentoo bundles the Qt
      private headers and sanitizer runtimes. libplasma 6.6.6 < 6.7, so the
      askdestroysignalorderingtest containment assertions stay gated off by the
      existing PLASMA_VERSION >= 6.7.0 guard (Debian leg), untouched.
      Commits: 00400f16c, d68ad64c0, 3fb8f899d, 3eaa21261, a14c6efef, 2b2469b5e, 0d052f3b7, 72dc44dab, 8838dcf31
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
      same open item as Arch). FEDORA PINNED:
      registry.fedoraproject.org/fedora:43. Floor check (in-container dnf +
      rpm -q, 2026-07-17): libplasma 6.7.2 / plasma-workspace 6.7.2 /
      plasma-activities 6.7.2 / layer-shell-qt 6.7.2 / kf6-kcoreaddons
      6.28.0 / qt6-qtbase 6.10.3 - all meet Qt>=6.6, KF6>=6.5,
      libplasma>=6.5. fedora:42 ships Plasma 6.3.x (below floor), so 43 is
      the earliest qualifying Fedora stable; did NOT use ng's fedora:44 /
      USTC-mirror lines. KDE NEON PINNED:
      invent-registry.kde.org/neon/docker-images/plasma:user (the User
      Edition, neon's stable release channel). Floor checked in-container
      2026-07-17 and recorded in the Containerfile header: Qt 6.11.1 / KF6
      6.28.0 / Plasma+kwin 6.7.3 on Ubuntu 24.04 noble, all past floor. The
      stale Docker Hub kdeneon/* tags (Ubuntu 22.04 / Plasma 5.27, 2023) are
      below floor and were rejected. neon User rolls with each Plasma
      release, so a registry digest pin is the open reproducibility item
      (same as Arch/Tumbleweed). VOID floor CHECKED in-container 2026-07-17 and
      recorded in the Containerfile comment: Qt 6.11.1 / KF6 6.25.0 /
      libplasma+kwin 6.6.3 / Mesa lavapipe 26.1.5 (LLVM 21.1.7), well past
      the >=6.5/>=6.6 floor. Void is rolling and has no dated archive
      (ghcr.io/void-linux/void-glibc-full:latest for the prototype; the
      -full image's frozen packages are stale until the leg's first
      xbps-install -Suy sync, which is the initial RUN step), so a
      reproducible pin would be an image digest, same open item as
      Arch/Tumbleweed. GLIBC image deliberately (musl is a separate axis).
      GENTOO PINNED: base image gentoo/stage3:amd64-systemd (the systemd
      stage3, required so the binhost's USE=systemd KDE binpkgs resolve as
      binaries) + binhost snapshot
      distfiles.gentoo.org/releases/amd64/binpackages/23.0/x86-64. Floor check
      (in-container, 2026-07-17): Qt 6.11.1 / KF6 6.27.0 / libplasma 6.6.6 /
      plasma-workspace 6.6.6 / kwin 6.6.6 / mesa 26.0.8 - all meet Qt>=6.6,
      KF6>=6.5, libplasma>=6.5. Both the stage3 tag and the binhost path roll
      (Gentoo is a rolling source distro); a binhost REPO_REVISIONS/snapshot
      pin is TBD, the same open item as Arch/Tumbleweed. All 8 base tags
      resolved.
      Commits: 3eaa21261, a14c6efef, 2b2469b5e, 0d052f3b7, 72dc44dab, 8838dcf31

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
      provides setcap). FEDORA DONE: nested kwin_wayland comes up headless in
      podman and drives all 13 sceneprobe scenes. Same cap_sys_nice=ep quirk
      (Fedora's kwin package) - same setcap -r fix. No Fedora-specific
      validation-layer suppressions needed: the existing vk-suppressions.txt
      covers Fedora's Mesa (LLVM 21.1.8) warnings. KDE NEON DONE: nested
      kwin_wayland comes up headless in podman and drives all 13 sceneprobe
      scenes. neon's kwin_wayland ships WITHOUT cap_sys_nice (getcap empty),
      so the setcap -r is a harmless no-op - kept for parity/robustness. No
      neon-specific suppressions needed: the only validation message is the
      known VK_KHR_create_renderpass2/multiview/maintenance2 lavapipe
      emulation warning, already covered by vk-suppressions.txt. VOID DONE:
      nested kwin_wayland (from the bare kwin package) comes up headless in
      podman and drives the full sceneprobe suite (13/13). SAME cap_sys_nice=ep
      trap as Arch/Tumbleweed (getcap showed cap_sys_nice=ep before strip,
      empty after); the image strips it (libcap-progs provides setcap on Void
      too). GENTOO DONE: nested kwin_wayland comes up headless and drives all
      13 scenes, but Gentoo's binhost kwin needed TWO in-image fixes the other
      legs did not. (1) The binpkg kwin is built USE=-lock (no screenlocker),
      so it does not know the --no-lockscreen option lib-nested-kwin.sh passes
      unconditionally and exits "Unknown option no-lockscreen" - fixed by
      rebuilding kwin from source with USE=lock (kscreenlocker already a
      binpkg). (2) Gentoo is split-usr and installs kwin_wayland at BOTH
      /usr/bin and /usr/sbin, each cap_sys_nice=ep; the harness resolves
      /usr/sbin first, so the setcap -r must strip BOTH paths (the one-path
      Arch fix is insufficient). The existing vk-suppressions.txt covers
      Gentoo's Mesa (LLVM 22.1.8) warnings.
      Commits: 79a8008f0, 3eaa21261, a14c6efef, 2b2469b5e, 0d052f3b7, 72dc44dab, 8838dcf31
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
      modules resolved. FEDORA DONE (invariant+tolerance tier, as
      predicted): all 13 scenes RENDER (non-blank, correct regions/colors,
      checkInvariants passes) through nested kwin on lavapipe in the
      fedora:43 container. NOT bit-exact against the nix goldens - Fedora
      ships Mesa on LLVM 21.1.8 (vs nix/Arch's LLVM 22.x), so 5 of 13 scenes
      differ by max Δ=2 (a single LSB per channel): 8 bit-exact PASS, and
      applet_colorizer/indicator_glow/multieffect_degenerate/shadoweditem
      (0.01-1.27% px, Δ=1) + multieffect_blur (93.3% px but Δ=2, the
      rounding spread across a gradient) differ. A CompareTolerance{2,
      >=0.95} passes all 13; the pinned tag could alternatively carry a
      bless-frozen fedora bit-exact golden - both are Phase C (C1/C2), not
      wired here. This is the first concrete proof of the graduated-rigor
      model's premise: bit-exactness is a per-Mesa/LLVM accident, so Fedora
      needs the tolerance tier. Detail:
      docs/agent-logs/2026-07-17-fedora-leg.md. KDE NEON DONE (invariant+
      tolerance tier): all 13 scenes RENDER (non-blank, correct
      regions/colors, self-test valid) through nested kwin on lavapipe in the
      neon:user container. NOT bit-exact - neon ships Mesa 25.2.8 on LLVM
      20.1.2, so the SAME 5 scenes as Fedora differ, by the SAME pixel counts
      and the SAME max Δ=2 (8 bit-exact PASS; applet_colorizer/indicator_glow/
      multieffect_degenerate/shadoweditem at Δ=1, multieffect_blur 93.3% px
      but Δ=2 from the blur spreading the LSB rounding). neon's LLVM20 and
      Fedora's LLVM21 land bit-identically on the same side of the nix pin's
      rounding. CompareTolerance{2, >=0.95} passes all 13; the tier/bless is
      Phase C (C1/C2), not this leg, and sceneprobe-gate.sh exiting 1 at the
      bit-exact tier is correct (neon is not a bit-exact distro). Second
      concrete proof of the graduated-rigor premise after Fedora. Detail:
      docs/agent-logs/2026-07-17-neon-leg.md. VOID DONE: all 13 scenes render
      and PASS in the Void container, self-test valid - and bit-exact against
      the nix-blessed lavapipe goldens too. Void's Mesa 26.1.5 / LLVM 21.1.7
      lavapipe matches nix Mesa for these text-free scenes (delta 0 at the
      {0,0} tier), same LLVM 21 major as Debian/Tumbleweed, so no tolerance
      tier was needed at this Mesa version (recorded, not relied on - Phase C
      owns the rolling-drift axis). The image ENV (LATTE_QML_MODULE_PATH=/usr/
      lib/qt6/qml, arch-suffixed lvp_icd.x86_64.json) resolves the framework
      qml tree and ICD; note /usr/lib64 is a symlink to lib on Void, so the
      canonical lib path is used. GENTOO DONE (bit-exact tier, NOT tolerance):
      all 13 scenes render and PASS bit-exact against the nix-blessed lavapipe
      goldens (sceneprobe-gate.sh exit 0) in the Gentoo container. Render
      device llvmpipe (LLVM 22.1.8, 256 bits) - IDENTICAL to Arch, so the
      output matches nix Mesa bit-for-bit and no tolerance tier is needed at
      this Mesa version. This deliberately did NOT reproduce ng's LLVM 21 pin
      (which the prompt predicted would land Gentoo in Fedora's tolerance
      tier): leaving LLVM unpinned took the binhost's LLVM 22 Mesa, the
      bit-exact-matching major. Enablers: the Mesa-with-lavapipe source rebuild
      (video_cards_lavapipe) produced the ICD, and the QMLDIR emit (18aac31b0)
      resolved the staged org.kde.latte.* modules on lib64/qt6/qml. Recorded,
      not relied on - Phase C still owns the rolling-drift axis (a future
      Gentoo Mesa/LLVM bump could shift this to tolerance, as Fedora shows).
      Detail: docs/agent-logs/2026-07-17-gentoo-leg.md.
      Commits: 18aac31b0, 79a8008f0, 3eaa21261, a14c6efef, 2b2469b5e, 0d052f3b7, 72dc44dab, 8838dcf31

### Phase C - per-distro golden tiers
- [x] C1 Extend the sceneprobe device/tier axis to per-distro naming
      (`*.expected.<tier>.png`) and wire the graduated-rigor selection
      (bit-exact NixOS / bless-frozen stable / invariant+tolerance
      rolling). DONE by DECOUPLING rigor from the device: the compare
      keyed both the golden filename and the tolerance off
      SCENEPROBE_DEVICE, which denied Fedora/neon (rendering the lavapipe
      device, comparing `.expected.lavapipe.png`) the tolerance they
      render at. Added an orthogonal SCENEPROBE_TIER axis, an enum-class
      GoldenTier {BitExact, Tolerance} in imagecompare.{h,cpp}
      (parseGoldenTier: unset->BitExact so the merge gate is unchanged,
      unknown->nullopt refused LOUDLY at the boundary; toleranceForTier:
      exhaustive switch to the CompareTolerance). The three graduated tiers
      are all expressible: bit-exact NixOS (default), invariant+tolerance
      rolling (SCENEPROBE_TIER=tolerance vs the SAME nix goldens with a
      delta), and bless-frozen stable (the pre-existing device-keyed naming
      already gives `.expected.<distro>.png` via SCENEPROBE_DEVICE=<distro>
      - no new naming machinery needed, and no unused scaffold built). The
      device still keys the golden filename + the missing-golden hard-fail;
      only the tolerance selection moved to the tier. Detail:
      docs/agent-logs/2026-07-17-phase-c-tiers.md.
      Commits: 67f24ad44, 139b123fe
- [x] C2 Bless the pinned-tag tiers for the stable distros; set
      tolerances for the rolling tier. TOLERANCE SET: the tolerance tier
      gates at CompareTolerance{2, 0.005} - Fedora (Mesa LLVM 21.1.8) and
      neon (LLVM 20) show max per-channel Δ=2 with NO pixel exceeding Δ2,
      so perChannelDelta=2 filters every differing pixel (measured 0%
      exceed on all 13 scenes on both distros) and the 0.5% budget is
      margin; it is the value the compare already used for non-lavapipe
      devices, now owned by the tier. Each Containerfile declares its tier
      (Fedora/neon=tolerance; Arch/Debian/openSUSE/Void/Gentoo=bitexact,
      explicit + commented as a rolling-Mesa accident with the tier as the
      flip knob). NO per-distro bit-exact golden was blessed: the matrix
      put Fedora/neon in the tolerance tier (compare nix goldens with a
      delta), not the bless-frozen tier, so no `.expected.<distro>.png`
      set exists - that path stays an available extension (device-keyed
      naming supports it) for a future pinned-tag decision, deliberately
      not built now. Verified in-container: nix bit-exact 13/13 UNCHANGED
      (unset->bitexact), Fedora+neon 13/13 at tolerance (and Fedora FAILS
      5 scenes at bitexact, proving the tier is load-bearing), Arch 13/13
      still bit-exact. Commits: 67f24ad44, 139b123fe

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

### Phase E - v0.20.0 release
- [ ] E1 Bump CMakeLists VERSION 0.10.77 -> 0.20.0; changelog; release
      process doc. Commits:
- [ ] E2 Matrix green on the release commit; tag v0.20.0. Commits:

## Native packaging workstream (distinct from, built on, the CI matrix)

The CI matrix above is a GATE ("does it build+render on distro X"); native
packaging produces installable ARTIFACTS (.deb/.rpm/PKGBUILD/ebuild/xbps).
They share the per-distro build ENVIRONMENTS - once Phase A can build in a
distro's container, producing that distro's package is one recipe away. So
packaging is an EXTENSION of the matrix, sequenced AFTER Phase A/B (a
packaging task needs a proven build env for its distro first).

Two tiers - the honest cut is recipes vs upstreaming, not
mainstream vs obscure:

- **Tier 1 - in-repo recipes plus package-artifact CI.** All five local recipes
  now live under `packaging/<fmt>/`. F6 (the package-artifact CI task) is the
  pending automation half; no package-artifact CI exists yet. The SPDX
  attribution audit (David Goree, Michail, the KDE authors) feeds directly into
  `debian/copyright` (DEP-5) and the RPM `%license` block.
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
- [x] F0 (the shared installed-package provenance and nested-runtime gate):
      require package ownership for live roots; reject source, build, Nix,
      loader and ambient-path fallback; require the installed ELF executable;
      validate all five plugin identities with a verified Qt 6 inspector under
      bounded loading; prove startup mappings, isolated environment and clean
      process-group shutdown under nested KWin.
      Commits: e50a7ad21, 728fdf675, dabaf058b, 9a24b538d, 0032e17f2,
      f08fbe2c4, 035d38da8, 3074c6adf, 1895c6c30, 14543f43f,
      cfe736213, 484052179, f8bd05d60, ce8950b11, 1d091efe8,
      c394c43cf, 02153ed63, 9fe8ddd1d, 3b025df03, 40ad5a245,
      ebcda72fa, 29a5e4ce6, c329eb138, 98f4ff797, 009c406dc,
      3fb92a05a.
- [x] F1 (the Debian-family native recipe): `packaging/debian/` supplies the
      debhelper/CMake recipe, DEP-5 metadata, manpage, quilt patch, and an
      exact-HEAD source-build helper. A minimal Ubuntu noble root with only the
      signed KDE neon User repository resolved all declared build dependencies;
      the checked build passed 92/92 package tests and lintian reported no tags.
      A fresh root installed the local `.deb`, passed `dpkg --audit` and
      `apt-get check`, and produced a 768-entry `dpkg-query` manifest with
      SHA-256 `43e4a2c3c13443545febb61f03fc495c7deb15f6ee9732640c3ecaefc035ff7e`.
      The installed-package gate exited 0 after exact `/usr` executable/plugin
      mappings, settled nested-KWin startup, and status-0 shutdown. The package
      SHA-256 is `c1e9f343bc4bfb22c862f14d8475652d40587501b28044ce199081018141c779`.
      The fast repository gate passed 94/94 at pre-rebase `4c937adde`,
      patch-equivalent to post-rebase `a04c47614`; the final follow-up changed
      only the manpage and passed `groff`, lintian, and `diff --check`.
      Commits: 43131b00f, 43ea0f17f, 48bb23fb6, b2889ffbb, b344808a8,
      02770a57c, 7cf2be7aa, efbefc500, 99ed25c46, 82fcc84b1,
      aeb47d001, 07d94fe64, 3d9d58342, d497d2f78, a04c47614,
      71fa379c2.
- [x] F2 (the shared Fedora/openSUSE RPM recipe): one spec maps native package
      names and CMake macros for Fedora 43 and openSUSE Tumbleweed, preserves
      each distro's hardening, and creates commit-bound reproducible source
      packages. Both builds produced source, runtime, debuginfo, and populated
      debugsource RPMs and passed `%check`. Fresh installs passed `rpm -V` and
      the installed-package gate through exact mappings, nested-KWin startup,
      and clean shutdown. Fedora's 659-entry manifest has SHA-256
      `828111dd72e23787f9ad82133fd274fd98a660a96397e82956ab18a30a122459`;
      Tumbleweed's 642-entry manifest has SHA-256
      `806453d3bb3d30ad862d23d6203f4f688e35b7b6fd144ac1a5bb741853796bd0`.
      The runtime RPM SHA-256 values are
      `dbbb55482915ccd622cda4c01ed9697f0a9eec77355e79fad97823cf2ebec1f1`
      for Fedora and
      `532e84569ad1c3de215f63d975f233031bebe55d94ba9a2cb739aeba2762ac3f`
      for Tumbleweed. The fast repository gate passed 94/94 at pre-rebase
      `b9f9ed439`, patch-equivalent to post-rebase `f1184c3d3`.
      Commits: bb5e45f1f, 17e1b10fd, 3567e3c9e, 4a10ccc0a, f1184c3d3.
- [x] F3 (the Arch native PKGBUILD and generated `.SRCINFO`): `makepkg
      --cleanbuild` produced the main and debug packages with Arch's `-O2`
      policy authoritative and explicit `NDEBUG`. A fresh Arch root installed
      the local package and produced a 766-entry `pacman -Qlq` manifest with
      SHA-256 `9c8f4c0598cbecb3632a8a4ff64124a24f0b6b9bebfdc9d1a8119abca8e6d4d6`.
      The installed-package gate exited 0 after exact executable/plugin
      mappings, settled nested-KWin startup, and status-0 shutdown. The package
      SHA-256 is `2180c8f6a0630161d74cf8968627f3d216a4fc32ae6bca3d5a7e81d0afdbeebc`;
      namcap reported zero errors and `.SRCINFO` exactly matched
      `makepkg --printsrcinfo`. The fast repository gate passed 94/94 at
      pre-rebase `90f610c59`, patch-equivalent to post-rebase `85ed78b2c`.
      Commits: 59e9367ff, bfc62b3e0, 85ed78b2c.
- [x] F4 (the Gentoo native package recipe): its initial acceptance used the
      EAPI 8 local overlay at exact source commit `f1184c3d3`, with
      explicit Qt 6, KF6, Plasma 6, KWin, Wayland, QML, PipeWire, and audio
      dependencies. The test-enabled Portage depgraph matched
      `qtbase-6.11.1[vulkan]` and `qtdeclarative-6.11.1-r1[vulkan]` and exited 0;
      a temporary test-disabled depgraph resolved `qtbase`, `qtdeclarative`,
      `qtmultimedia`, and `qtquick3d` with `-vulkan` and exited 0. The forced
      `USE=test` source rebuild passed all 90 selected tests under Qt 6.11.1,
      KF6 6.27.0, and Plasma 6.6.6. Its rebuilt GPKG metadata records concrete
      `qtbase:6/6.11.1=[vulkan]` and `qtdeclarative:6/6.11.1=[vulkan]`
      dependencies. `pkgcheck scan -v /var/db/repos/lattecotta` exited 0 with no
      findings, and `ebuild ... clean manifest` exited 0. Portage produced local
      unsigned artifact `latte-dock-0.10.77_p20260720-5.gpkg.tar` with SHA-256
      `577ca280a6dc338030323066dffa2579109a6f371af7d80d625a0a40d366aaa4`.
      Its Portage-owned `/usr` manifest contains 552 objects; the installed-
      package gate with `--root / --prefix /usr` exited 0 through nested-KWin
      startup, exact executable and plugin mappings, and clean shutdown.
      `LATTE_GATE_FAST=1 scripts/gate-all.sh` exited 0 at pre-rebase head
      `d38e12ed26a198b56b76773adae8ae8c5534713a`, with 94 CTest entries,
      qmllint, all 13 scene probes, and matrix fixtures green.
      Commits: 22cb06836.
- [x] F5 (the Void native package recipe): `packaging/void/` supplies a directly
      stageable `void-packages/srcpkgs/latte-dock` tree and a clean-source
      staging helper. Void `xlint` and ShellCheck passed; argument, dirty-source,
      modified-destination, patch-application, and exact-provenance helper paths
      were exercised. A fresh `void-packages` checkout at
      `af364dd90499741882e1012b0bfd5727b17af7db` built exact source head
      `fbdc3a3150b5fe4dad788e189101e9989129fbb8` through the helper. The source-
      content SHA-256 is
      `f34981e2ba7e5d4e1e086792fc9ec15e27d2b826f5a5daa789d4243d64cd7484`; the
      resulting `latte-dock-0.10.77_1.x86_64.xbps` SHA-256 is
      `f7edf5709af736d75ddcaaf47e3b7202accc2794f95b0f949e5ed670317931f6`.
      A separate container from the untouched Void glibc-full base installed
      the package from the local XBPS repository. `xbps-pkgdb -a`,
      `desktop-file-validate`, and `appstreamcli validate --no-net` passed; the
      installed metadata contains `org.kde.latte-dock` exactly once and no stale
      library provider. The 552-entry `xbps-query` package-owned `/usr` manifest
      has SHA-256 `1e3b5a320728dcde5877c83d3506ed6e0fba1a2f8db382442600ff884ae11e0e`.
      The installed-package gate with `--root / --prefix /usr` exited 0 through
      nested-KWin startup, five mapped-artifact provenance checks, and clean
      shutdown. `LATTE_GATE_FAST=1 scripts/gate-all.sh` exited 0 at pre-rebase
      head `fbdc3a3150b5fe4dad788e189101e9989129fbb8`, with all 77 installed-
      package refusal controls, 94/94 tests, qmllint, all 13 scene probes, and
      matrix fixtures green. That acceptance used the original helper while its
      then-current source still needed the AppStream patch. At that point the
      tracked template was pinned to older source and retained the patch. The
      helper later changed to rewrite its staged copy to exact current HEAD
      without a patch; the package-gate self-test verifies that patch-free recipe
      and corrected metadata inside the exact source archive.
      Commits: ea1bc5acb, ced7a48a5.
- [ ] Repin the F3-F5 native recipes after D59 (invalid standalone AppStream
      identity and stale library provider) was corrected. Arch, Gentoo, and Void
      must consume merged PR #91 head `804519254`; Gentoo and Void must delete
      their obsolete patches, while Arch keeps its patch-free design. Debian and
      RPM already consume corrected current source without patches. The final
      tracked tree must contain no per-distribution AppStream patch file or live
      recipe reference. Package versions and revisions stay unchanged because no
      continuation artifact has been released. The implementation is complete on
      branch `build/appstream-source-repin` at provisional commit `c3ad16a23`.
      Five independent external lanes passed at exact head `45c0d27cb`. Arch
      verified 766 files and runtime SHA-256 prefix `766e5499`; Debian/KDE neon
      passed 94/94 selected tests, lintian with no errors or warnings, and 769
      entries with runtime prefix `9d02e711`; Fedora and openSUSE passed full RPM
      build/install gates with 659/642 entries and runtime prefixes
      `6e79aa2b`/`fafe9e46`; Gentoo passed Manifest, pkgcheck, 92/92 tests,
      `qcheck` 761/761 and 552 entries with GPKG prefix `80a9a21d`; Void passed
      xlint, build, index, pkgdb and 552 files with XBPS prefix `1fbf7988`.
      Every artifact passed exact AppStream structure, fresh-install integrity,
      the full nested-Wayland package gate and status-0 shutdown with no live
      per-distribution patch.
      Keep this item unchecked until GitHub supplies the final post-rebase hash.
      Commits: c3ad16a23 (provisional branch hash; final post-rebase hash required)
- [ ] F6 (the package-artifact CI task): extend the matrix to build each native
      package artifact in its distro environment, install that artifact, and
      run the installed-package gate against it (registry/runner per DECISION
      6). Package CI will add this automation; it does not exist yet. This task
      remains pending and is explicitly outside the completed local-recipe
      scope. Commits:

### Phase G - Tier 2 upstreaming (later wave; needs Bree's hands)
- [ ] G1 Low-friction channels we control: AUR push, Void PR, Gentoo
      overlay, personal Copr/OBS. Commits:
- [ ] G2 Gatekept archives (Debian/Fedora/openSUSE Factory) - process,
      sponsorship, review; tracked, not agent-automatable. Commits:

## Decisions

- DECISION 1 (the per-distro golden-rigor model) - RESOLVED (2026-07-17):
  use the graduated model implemented above: bit-exact NixOS, optional
  bless-frozen stable tiers, and invariant+tolerance rolling tiers.
- DECISION 2 (the CI cadence) - build+e2e on every PR vs nightly + pre-release
  only. Recommendation: build+e2e on PR if leg time is acceptable, full
  sceneprobe matrix nightly + on release tags.
- DECISION 3 (the Ubuntu-family matrix leg) - RESOLVED (2026-07-17): KDE neon
  is the Ubuntu-family/deb leg (always-current Plasma, the KDE reference env);
  covers Kubuntu/Pop/Mint-KDE. Stock Ubuntu LTS 24.04 excluded (below floor).
- DECISION 4 (the openSUSE Tumbleweed matrix scope) - RESOLVED (2026-07-17):
  openSUSE Tumbleweed IN. Widened the CI test-leg set to the full
  KDE-enthusiast list (Arch, Tumbleweed, Fedora, neon, Debian sid, Gentoo,
  Void); package formats stay five.
- DECISION 5 (the release-gate scope) - ALL matrix distros green vs NixOS +
  >=1 mainstream distro green.
- DECISION 6 (the hosted-CI runner) - self-hosted Forgejo runners vs GitHub
  Actions (undecided; keep the workflow runner-portable either way). A
  self-hosted ARM Forgejo runner would later host the parked aarch64 golden tier.
- DECISION 7 (the Tier-1 recipe scope) - RESOLVED (2026-07-20): complete all
  five Tier-1 recipes in this initiative.
- DECISION 8 (the packaging swarm batch split) - RESOLVED (2026-07-20): wave A
  is deb/rpm/arch and wave B is Gentoo/Void under the four-subagent cap; the
  orchestrator owns shared packaging metadata and serial landing.
