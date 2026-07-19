# Debian leg - multi-distro CI matrix (Phase A/B)

Bringing up the Debian testing build + sceneprobe render leg, replicating
the proven Arch leg (docs/tracking/multi-distro-ci-plan.md). Branch
`multi-distro-ci-phase-a-debian`. B2 (e2e gate wiring) is a parallel agent's
job and is deliberately NOT done here.

## Floor versions (observed 2026-07-17 in debian:testing)

| Component     | Debian testing | Floor        | Verdict |
|---------------|----------------|--------------|---------|
| Qt6           | 6.10.2         | >= 6.6       | PASS    |
| KF6           | 6.26.0         | >= 6.5       | PASS    |
| Plasma/libplasma | 6.6.5       | >= 6.5       | PASS    |
| kwin_wayland  | 6.6.5          | -            | -       |
| Mesa (lavapipe) | 26.1.4 (LLVM 21) | -         | -       |

debian:testing clears every floor, so it is the pinned leg - no need to drop
to debian:sid (unstable). testing tracks the next stable and rolls slowly, so
the base tag is stable enough to pin (A3).

## Reproduce

```
# build the deps-only image (cacheable layer)
podman build -f ci/containers/Containerfile.debian -t latte-debian:proto .

# clean build (565/565)
podman run --rm -v "$PWD":/src:ro -v latte-debian-build:/build -w /src \
    latte-debian:proto bash /src/ci/build-and-gate.sh build

# offscreen ctest
podman run --rm -v "$PWD":/src:ro -v latte-debian-build:/build -w /src \
    latte-debian:proto bash /src/ci/build-and-gate.sh test

# sceneprobe render gate (13 scenes, nested kwin + lavapipe)
podman run --rm -e BUILD=/build -v "$PWD":/src:ro -v latte-debian-build:/build \
    -w /src latte-debian:proto bash /src/scripts/sceneprobe-gate.sh
```

## Build result

Clean: 565/565 targets, only benign `-Wmissing-field-initializers` warnings
in dropclassifiertest (designated-initializer partial fills). No source fixes
needed for the build itself - the four portability fixes already in master
(latte-qmldir.txt emission 18aac31b0, qt-6/qml vs qt6/qml search 00400f16c,
etc.) carried Debian's multiarch libdir (/usr/lib/x86_64-linux-gnu) without a
single hardcoded-path bite in the QML/qmltypes/plugin lookups.

## Sceneprobe (B3)

All 13 real scenes render and PASS, self-test valid (good passes, bad/blank
fail). PASSES AT THE BIT-EXACT lavapipe golden tier (SCENEPROBE_DEVICE=
lavapipe uses CompareTolerance{0,0.0}) against the nix-blessed goldens - no
tolerance tier needed. This is notable: Debian's Mesa is 26.1.4/LLVM21,
NEWER than Arch's llvmpipe 22.1.8 and the nix pin, yet the lavapipe output
is bit-identical for these text-free scenes across all three Mesa versions.
Phase C still adds the per-distro tier axis for rolling drift, but on the
current Debian Mesa the render path is bit-exact.

## ctest characterization (A2)

`build-and-gate.sh test` -> 76/81 pass with the base image. With
`/usr/lib/qt6/bin` on PATH (now baked into the Containerfile), 80/81 pass.
The remaining failures, each traced:

1. **askdestroysignalorderingtest** (contract) - FIXED, own commit. Genuine
   libplasma VERSION-SKEW, not a nix-ism: the
   `containmentTypeAppletRemovedImmediatelyAndAgainAtDeath` case pinned the
   libplasma 6.7 widened-guard behavior (containment-type applets get an
   immediate `appletRemoved` and applets() prune). Debian testing ships
   libplasma 6.6.5, where askDestroy still guards the immediate emit AND the
   list removal with `!isContainment()`, so a CustomEmbedded tray gets NO
   immediate emit, stays in applets() for the undo window, and its only
   `appletRemoved` arrives at object death. The port's own parking is
   idempotent both ways and survives either ordering (the test file's HISTORY
   block already documents both contracts). Fix: gate the containment-type
   assertions on `#if PLASMA_VERSION >= 6.7.0`, pinning whichever contract the
   substrate actually ships. The 6.7 branch is unchanged, so the NixOS
   (6.7.3) merge gate is untouched; the pre-6.7 branch pins the 6.6.5 shape
   (count 0 immediate, tray stays in applets(), single appletRemoved at
   death) and PASSES in-container. Root cause: the contract test was stricter
   than the project's declared PLASMA_MIN_VERSION floor (6.5).

2-4. **qmlcompilegate / qmlinteraction / qmlcontracts** - dep/env trap, FIXED
   in the Containerfile. Debian installs the Qt6 host tools (qmltestrunner,
   qmllint) under /usr/lib/qt6/bin, which is off the default PATH (Arch/Fedora
   use /usr/bin). The gates call qmltestrunner by bare name -> "command not
   found". Adding /usr/lib/qt6/bin to the image PATH closes all three (they
   self-stage via LATTE_QML_MODULE_PATH from the image ENV).

5. **qmllintgate** - correctly REFUSED on the distro, not a defect. The
   qmllint ratchet (tests/coverage/qmllint-gate.sh:36-39) hard-refuses any
   qmllint not resolving to /nix/store/*, because its per-file warning
   baseline is calibrated to the EXACT pinned qmllint version; a foreign
   qmllint would drift the ratchet. It is a NixOS-tier merge gate, and the
   distro gating policy (plan lines 168-178) does not include it. The distro
   gate stage should exclude it (`ctest -E qmllintgate`) - a note for B2's
   gate-stage wiring, not a fix.

## Dep-name resolutions (Debian vs Arch/ng)

Every name resolved against debian:testing. Deltas worth remembering:

- KF6 dev packages: `libkf6<component>-dev` (Config, CoreAddons, GuiAddons,
  DBusAddons, Declarative, ItemModels, XmlGui, IconThemes, KIO, I18n,
  Notifications, NewStuff, Archive, GlobalAccel, Crash, WindowSystem, Package,
  Svg, Service, KCMUtils, Solid, Sonnet, TextWidgets, IdleTime, DocTools).
- **Kirigami**: `libkirigami-dev` (6.26.0). NOT `libkf6kirigami-dev` (does not
  exist); `kirigami2-dev` is the old Qt5 build. This is the one genuinely
  surprising name.
- qt5compat -> `qt6-5compat-dev`; shadertools -> `qt6-shadertools-dev`;
  Qt tools -> `qt6-tools-dev` + `qt6-tools-dev-tools`;
  qtwaylandscanner/qtwaylandclient come from `qt6-wayland-dev`;
  base tools (moc/qmltyperegistrar) from `qt6-base-dev-tools`.
- qqc2-desktop-style -> `qml6-module-org-kde-desktop`.
- plasma5support -> `libplasma5support-dev`; kpipewire -> `libkpipewire-dev`;
  plasma-pa -> `plasma-pa`; libksysguard (KSysGuard) -> `libksysguard-dev`.
- PlasmaQuick, LibTaskManager, LibNotificationManager cmake configs all come
  from `plasma-workspace-dev`; PlasmaActivities/Stats ->
  `libplasmaactivities-dev` / `libplasmaactivitiesstats-dev`; libplasma ->
  `libplasma-dev`; layer-shell-qt -> `liblayershellqtinterface-dev`.
- render gate: kwin_wayland -> `kwin-wayland`; vulkan loader ->
  `libvulkan-dev` + `libvulkan1`; validation -> `vulkan-validationlayers`;
  lavapipe -> `mesa-vulkan-drivers`; fonts -> `fonts-noto-core`; setcap ->
  `libcap2-bin`.

## Traps / env paths (all confirmed with ls in-image)

- lavapipe ICD is **`/usr/share/vulkan/icd.d/lvp_icd.json`** (UNSUFFIXED), not
  the arch-suffixed `lvp_icd.x86_64.json` the task guessed. Confirmed by ls
  before pinning.
- QML framework tree: `/usr/lib/x86_64-linux-gnu/qt6/qml` (Debian multiarch
  libdir) -> LATTE_QML_MODULE_PATH.
- validation layers: `/usr/share/vulkan/explicit_layer.d`.
- Qt6 host tools off-PATH under `/usr/lib/qt6/bin` (see qml-gate fix above).
- kwin_wayland on Debian ships WITHOUT cap_sys_nice set (getcap empty), so the
  `setcap -r` is a harmless no-op here, but it is kept for parity/robustness
  and because a future package could add the cap.
- `DEBIAN_FRONTEND=noninteractive` set so tzdata/debconf never prompt in CI.
- `--no-install-recommends` + `rm -rf /var/lib/apt/lists/*` keep the deps
  layer lean and cacheable.
