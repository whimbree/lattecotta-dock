# Gentoo leg of the multi-distro CI matrix (Phase A/B)

Branch `multi-distro-ci-phase-a-gentoo`. Gentoo is the source/minimalist
nerd leg: `ci/containers/Containerfile.gentoo`, a clean in-container cmake
build of the port, and the headless sceneprobe render proven in-container.
B2 (the e2e gate-stage wiring) is already productionized on master and out
of scope here.

The Gentoo challenge is that building Qt6 + KF6 + Plasma + kwin + Mesa from
source in a throwaway container takes many hours. This leg pulls PREBUILT
BINARY PACKAGES from Gentoo's upstream binhost instead, so only a small tail
of packages (and the port) compile. The recipe and the traps that got it
there are below.

## The decisive resolution: match the binhost's build profile

The binhost `Packages` index self-reports its build configuration. Two
fields drove every decision:

    PROFILE: default/linux/amd64/17.0/hardened
    REPO_REVISIONS: {"gentoo": "36508f40390a65ca5d92409213664fc28ebd7a81"}

and per-package the recorded USE. The single most important discovery: the
KDE binpkgs carry `USE="... systemd ..."` (plasma-workspace, kwin, ...). The
DEFAULT `gentoo/stage3:latest` is the openrc/elogind stage, so its computed
USE never matches the systemd binpkgs and `--binpkg-respect-use=y` rejects
them, falling back to a full source rebuild of the entire Plasma stack.

Fix: base the image on **`gentoo/stage3:amd64-systemd`** (profile
`default/linux/amd64/23.0/systemd`). With the systemd base, plasma-workspace,
kwin, libplasma and the rest resolve to `[binary]`. This is the crux of the
leg - not a package name, a base-image choice.

Deliberately did NOT copy latte-dock-ng's gentoo Dockerfile CN/tuna mirror
lines (GENTOO_*_CN, USE_MIRRORS) or its `LLVM_SLOT="21"` /
`media-libs/mesa llvm_slot_21` pins; the upstream distfiles.gentoo.org URIs
are used directly and LLVM is left at the binhost default (22).

## Binhost + base

- Base: `docker.io/gentoo/stage3:amd64-systemd`.
- Binhost: `https://distfiles.gentoo.org/releases/amd64/binpackages/23.0/x86-64`
  wired via `/etc/portage/binrepos.conf` (priority 1) + `FEATURES="getbinpkg
  binpkg-request-signature"` + `EMERGE_DEFAULT_OPTS="--getbinpkg --usepkg
  --binpkg-respect-use=y --newuse --autounmask-write=y --autounmask-continue=y"`.
- Ebuild tree synced with `emerge-webrsync`.
- Sandbox FEATURES disabled (`-usersandbox -network-sandbox -pid-sandbox
  -ipc-sandbox`) because Portage's own sandboxes fail under podman's default
  namespace/seccomp setup; a throwaway CI image is the right place to drop them.

## Floor check (all above the CMakeLists floor: Qt >= 6.6, KF6 >= 6.5, libplasma >= 6.5)

Observed on the binhost snapshot in-container 2026-07-17:

    dev-qt/qtbase               6.11.1   (>= 6.6.0 floor: PASS)
    dev-qt/qtdeclarative        6.11.1-r1
    kde-frameworks/kcoreaddons  6.27.0   (>= 6.5.0 floor: PASS)
    kde-plasma/libplasma        6.6.6    (>= 6.5.0 floor: PASS)
    kde-plasma/plasma-workspace 6.6.6    (LibTaskManager / NotificationManager / KWayland)
    kde-plasma/kwin             6.6.6
    media-libs/mesa             26.0.8   (llvm_slot_22)

libplasma 6.6.6 is BELOW 6.7, so the askDestroy containment-type assertions
in askdestroysignalorderingtest stay gated off by the same
`#if PLASMA_VERSION >= 6.7.0` guard the Debian leg added (already on master);
the port build itself needs NO source change here.

## USE-flag resolution (why each package.use entry exists)

`--binpkg-respect-use=y` rebuilds a package from source whenever the computed
USE differs from the binpkg's recorded USE. Each entry below was derived by
diffing the computed USE against the binhost `Packages` index USE for that
atom, so the binary is ACCEPTED instead of rebuilt. Some are autounmask
requirements (a binpkg dep needs a flag); some are either/or conflicts
autounmask cannot pick on its own (qtbase[icu] vs qt5compat[-icu]) and MUST
be pre-seeded.

    dev-qt/qtbase cups gtk icu libproxy        # binpkg qtbase has all four
    dev-qt/qt5compat icu qml                    # match qtbase[icu] (either/or)
    dev-qt/qtdeclarative svg                    # ONE flag: -svg vs binpkg svg
    dev-qt/qtmultimedia alsa pipewire pulseaudio
    kde-plasma/plasma-workspace networkmanager screencast  # binpkg has both
    media-libs/freetype harfbuzz                # freetype<->harfbuzz cycle
    media-libs/libcanberra alsa                 # REQUIRED_USE udev? ( alsa )
    media-plugins/alsa-plugins pulseaudio
    sys-apps/systemd policykit                  # polkit needs systemd[policykit]
    sys-libs/minizip-ng compat                  # from ng's set
    sys-libs/zlib minizip                       # from ng's set
    app-text/xmlto text                         # from ng's set
    x11-base/xwayland libei                     # kwin dep, from ng's set

Also required in make.conf:
- `--newuse` in EMERGE_DEFAULT_OPTS: the stage3's pre-installed systemd has
  `-policykit`; polkit's `systemd[policykit]` dep needs a USE change on the
  already-installed systemd, which only `--newuse` will rebuild (emerge says
  so explicitly: "Enabling --newuse and --update might solve this conflict").
- `VIDEO_CARDS="amdgpu intel nouveau radeon radeonsi lavapipe"`: see below.

## The Mesa/lavapipe trap (the one mandatory source rebuild)

lavapipe is the software-Vulkan driver the headless render gate renders
through, and on Gentoo it is a SEPARATE `video_cards_lavapipe` USE flag. The
binhost Mesa binpkg was built WITHOUT it (its USE lists intel/nouveau/radeon/
radeonsi but not lavapipe), so `/usr/share/vulkan/icd.d/lvp_icd.x86_64.json`
would be absent and the sceneprobe would have no ICD.

So Mesa is the one KDE-stack package that MUST rebuild from source here, with
`video_cards_lavapipe` added. That pulls the LLVM 22 toolchain (clang, mesa_clc
- all binaries) and recompiles Mesa. The rest of the VIDEO_CARDS set matches
the binpkg's build set so libdrm/etc. still resolve as binaries; `amdgpu` is
radeonsi's libdrm dependency. LLVM is NOT pinned (binhost is llvm_slot_22).

## Source-build tail (binhost snapshot lag)

368 packages in the dependency set: ~342 install as binaries, ~26 compile
from source. The source tail is NOT a USE problem - it is the synced ebuild
tree being slightly AHEAD of the binhost snapshot, so fast-moving packages
have no matching binpkg version:

    imagemagick 7.1.2.18 (binhost .10), cups 2.4.19 (binhost 2.4.16),
    networkmanager 1.56.0 (binhost 1.52.1), pipewire 1.6.7 (binhost 1.4.10),
    plus freetype/gtk+3/ffmpeg/exiv2/libpulse/pulseaudio/polkit/modemmanager/
    speech-dispatcher/webrtc-audio-processing/jq/libcanberra/libpcap/w3m/
    libqalculate/libXfont2/xdg-utils and Mesa (lavapipe, above).

One AVOIDABLE source build: `kde-plasma/kwin-x11` is pulled as a
plasma-workspace session dep but is not needed for the headless WAYLAND gate
(~8 min compile). Left in for now; excluding it (`--exclude
kde-plasma/kwin-x11`, verified nothing hard-requires it) is a future image
lean-down. Future full alignment would pin the synced tree to the binhost's
REPO_REVISIONS snapshot so even the fast-movers resolve as binaries.

## Two runtime traps beyond the build (both kwin, both needed for the render gate)

1. **kwin `-lock` (source rebuild #2).** The binhost kwin is built `-lock`
   (no screenlocker), so `kwin_wayland` does not know the `--no-lockscreen`
   option the nested-kwin harness (`scripts/lib-nested-kwin.sh:62`) passes
   unconditionally - it exits "Unknown option 'no-lockscreen'" and the
   compositor never comes up. Fix: `kde-plasma/kwin lock` in package.use,
   which forces a kwin source rebuild (kscreenlocker is already installed as
   a binpkg from the plasma-workspace chain). This is the ONLY package.use
   entry that deliberately forces a rebuild rather than matching a binpkg.

2. **cap_sys_nice on BOTH kwin paths.** Gentoo is split-usr and installs
   `kwin_wayland` at BOTH `/usr/bin` and `/usr/sbin`, each carrying
   `cap_sys_nice=ep`. The harness resolves `/usr/sbin` first via PATH, so
   stripping only `/usr/bin` (the Arch/Fedora one-path fix) still leaves the
   EPERM-on-execve. The Containerfile strips both. (The post-strip
   "Failed to gain real time thread priority" line kwin prints is a harmless
   warning, not the fatal EPERM.)

## Build result

Clean cmake build in the Gentoo container, RelWithDebInfo + BUILD_TESTING=ON:
**565/565 targets** (identical target count to the Arch/Debian/Fedora/
Tumbleweed legs) against Qt 6.11.1 / KF6 6.27.0 / libplasma 6.6.6. NO source
change to the port was needed - master's portability fixes (the qt-6/qml vs
qt6/qml search 00400f16c, the KDE_INSTALL_QMLDIR emit 18aac31b0, and the
libplasma `#if PLASMA_VERSION >= 6.7.0` test guard from the Debian leg) carry
the leg on the split-usr lib64 layout cleanly. `build/latte-qmldir.txt` came
out `lib64/qt6/qml` and the staged org.kde.latte.* modules resolved.

ctest (offscreen suite, the 2 NixOS-tier entries qmllintgate + schemesmodeltest
excluded as the driver does): **79/79 pass (100%)** with the image ENV
(LATTE_QML_MODULE_PATH=/usr/lib64/qt6/qml) and /usr/lib64/qt6/bin +
/usr/libexec/qt6 on PATH. This BEATS Arch (74/82) and Fedora (78/82): the env
lets the QML-loading unit tests (layoutmanagerparking, shortcutshost,
representationswitch) resolve their framework modules, and the qt6/bin PATH
lets the QML script-gates (qmlcompilegate/qmlinteraction/qmlcontracts) find
qmltestrunner/qmllint. Without LATTE_QML_MODULE_PATH those 6 fail - the same
class Arch/Fedora/Tumbleweed filed, resolved here by the image ENV+PATH (the
Containerfile encodes both). Gentoo installs the Qt6 host tools off the
default PATH (under /usr/lib64/qt6/bin, /usr/libexec/qt6), the same trap the
Fedora/Debian legs hit.

## Sceneprobe render result (B3-gentoo)

All 13 scenes render and **PASS bit-exact** against the nix-blessed lavapipe
goldens (sceneprobe-gate.sh exit 0), self-test valid. Render device:
**llvmpipe (LLVM 22.1.8, 256 bits)** - IDENTICAL to the Arch leg, so the
lavapipe output matches nix Mesa bit-for-bit for these text-free scenes and
NO tolerance tier is needed at this Mesa version. This is the same outcome as
Arch/Debian/Tumbleweed, NOT the Fedora tolerance tier - the difference is the
LLVM version: the prompt predicted a tolerance tier assuming LLVM 21 (ng's
make.conf pin), but this leg deliberately did NOT pin LLVM and the binhost
Mesa (26.0.8) is built on LLVM 22, which is the bit-exact-matching major.
Recorded, not relied on: Phase C still owns the rolling-drift axis (a future
Gentoo Mesa/LLVM bump could shift this to tolerance, exactly as Fedora shows).
The Mesa-with-lavapipe source rebuild (video_cards_lavapipe) is what produced
the ICD the probe renders through.

## Reproduce (podman)

Build the deps image (heavy: full Gentoo Qt/KF6/Plasma binpkg pull + the
source tail + a Mesa-with-lavapipe compile):

    podman build -t latte-ci-gentoo -f ci/containers/Containerfile.gentoo .

Clean build of the port (source read-only at /src, out-of-source /build):

    podman run --rm -e JOBS=6 -v "$PWD":/src:ro latte-ci-gentoo \
        bash /src/ci/build-and-gate.sh build

Build + offscreen ctest:

    podman run --rm -e JOBS=6 -v "$PWD":/src:ro latte-ci-gentoo \
        bash /src/ci/build-and-gate.sh test

Build + sceneprobe render gate (needs the nested compositor; use a persistent
build volume, and point BUILD at the writable /build since sceneprobe-gate.sh
defaults to the read-only $repo/build):

    podman volume create latte-gentoo-build
    podman run --rm -e JOBS=6 -v "$PWD":/src:ro -v latte-gentoo-build:/build \
        latte-ci-gentoo bash /src/ci/build-and-gate.sh build
    podman run --rm -v "$PWD":/src:ro -v latte-gentoo-build:/build \
        latte-ci-gentoo bash -c 'BUILD=/build bash /src/scripts/sceneprobe-gate.sh'

## Traps / notes for the next leg

- Base MUST be the systemd stage3, not the default openrc one - otherwise the
  KDE binpkgs (built with USE=systemd) all rebuild from source.
- The binhost `Packages` index (curl the `/Packages` file) is the ground
  truth for per-package USE; diff computed-vs-binpkg USE to eliminate source
  rebuilds instead of guessing.
- Mesa needs a source rebuild for `video_cards_lavapipe`; the binhost mesa
  omits it.
- kwin needs a source rebuild for `lock` (screenlocker), or `--no-lockscreen`
  is an unknown option and the nested compositor never starts.
- Gentoo is split-usr lib64: framework QML at /usr/lib64/qt6/qml, lavapipe ICD
  is arch-suffixed lvp_icd.x86_64.json, Qt6 host tools off-PATH under
  /usr/lib64/qt6/bin + /usr/libexec/qt6 (both added to the image PATH), and
  kwin_wayland exists at BOTH /usr/bin and /usr/sbin (strip the cap on both).
- The same BUILD=/build read-only-mount trap as the Fedora leg applies to
  sceneprobe-gate.sh.
- Two mandatory source rebuilds (Mesa+lavapipe, kwin+lock) plus the
  binhost-snapshot-lag tail make this the heaviest image build in the matrix;
  everything else installs as a binpkg.
