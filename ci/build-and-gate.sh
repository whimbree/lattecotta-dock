#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Distro-agnostic build+gate driver for the multi-distro CI matrix
# (docs/tracking/multi-distro-ci-plan.md). Runs INSIDE a per-distro container (see
# ci/containers/Containerfile.<distro>) against the source tree bind-mounted
# read-only at /src, building out-of-source in a writable /build.
#
# This is the container counterpart to scripts/gate-all.sh: gate-all.sh is
# the canonical bit-exact NixOS MERGE gate; this driver is the per-distro
# RELEASE gate that proves the port builds and renders against the DISTRO's
# Qt6/KF6/Plasma/Mesa. The two never share a toolchain by design.
#
# Stages, gated by the first argument (default: build):
#   build  - cmake configure + compile only (Phase A2 checkpoint)
#   test   - build, then ctest (the offscreen unit/contract suite)
#   gate   - build, ctest, then the headless render/behavioral gates
#            (nested kwin + lavapipe sceneprobe, tests/e2e) [Phase B]
#
# The container image exports LATTE_VULKAN_LAVAPIPE_ICD and
# LATTE_VK_LAYER_PATH pointing at the DISTRO's lavapipe ICD and validation
# layers (not the nix devShell's) so the reused harness finds them.
set -euo pipefail

STAGE="${1:-build}"
SRC="${LATTE_SRC:-/src}"
BUILD="${LATTE_BUILD:-/build}"
JOBS="${JOBS:-$(nproc)}"

echo "==> latte multi-distro driver: stage=$STAGE src=$SRC build=$BUILD jobs=$JOBS"
echo "==> toolchain: $(cmake --version | head -1), $(cc --version 2>/dev/null | head -1)"

mkdir -p "$BUILD"

# RelWithDebInfo + BUILD_TESTING=ON mirrors the dev-tree gate semantics so
# the ctest/e2e/sceneprobe results are comparable across the matrix; the
# packaged artifacts (Phase F) build Release separately.
echo "==> configure"
cmake -S "$SRC" -B "$BUILD" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_TESTING=ON

echo "==> build"
cmake --build "$BUILD" --parallel "$JOBS"

# ---- gate helpers (only used by the gate stage) ----------------------------

# build_fakepointer <out>: generate the fake-input protocol glue and compile
# scripts/tools/fakepointer.c, the org_kde_kwin_fake_input injector the e2e
# recipes drive. The protocol XML location is resolved distro-agnostically:
# pkg-config wins on distros that ship plasma-wayland-protocols.pc, else the
# container-provided LATTE_FAKEINPUT_PROTOCOL (Arch ships no .pc). $SRC is
# read-only, so the generated header/source and the binary land under $BUILD.
build_fakepointer() {
    local out="$1" gendir xml="" pkgdir
    if command -v pkg-config >/dev/null 2>&1; then
        pkgdir="$(pkg-config --variable=pkgdatadir plasma-wayland-protocols 2>/dev/null || true)"
        [[ -n "$pkgdir" && -f "$pkgdir/fake-input.xml" ]] && xml="$pkgdir/fake-input.xml"
    fi
    [[ -n "$xml" ]] || xml="${LATTE_FAKEINPUT_PROTOCOL:-}"
    if [[ -z "$xml" || ! -f "$xml" ]]; then
        echo "gate: FAIL cannot locate fake-input.xml (pkg-config plasma-wayland-protocols failed and LATTE_FAKEINPUT_PROTOCOL='${LATTE_FAKEINPUT_PROTOCOL:-unset}' is not a file); the container image must provide the Wayland fake-input protocol" >&2
        return 1
    fi
    gendir="$(dirname "$out")"
    mkdir -p "$gendir"
    wayland-scanner client-header "$xml" "$gendir/fake-input-client-protocol.h"
    wayland-scanner private-code  "$xml" "$gendir/fake-input-protocol.c"
    cc -O2 -o "$out" "$SRC/scripts/tools/fakepointer.c" "$gendir/fake-input-protocol.c" \
        -I"$gendir" $(pkg-config --cflags --libs wayland-client)
}

# The default-layout config seeder lives in scripts/lib-e2e-seed.sh, shared with
# the NixOS sanitized gate (scripts/asan-e2e-gate.sh) so the two gates can never
# drift on how a hermetic seed is produced. e2e_seed_default_config <repo>
# <build> <seeddir> brings up a throwaway nested compositor, runs the staged dock
# against an empty config until it self-inits its default layout, tears the
# compositor down, and leaves a seed tree run-e2e.sh can copy.
source "$SRC/scripts/lib-e2e-seed.sh"

# Two ctest entries cannot pass off the NixOS pin, by construction, and are
# excluded from the distro matrix (named here, never silently dropped):
#  - qmllintgate: the qmllint ratchet pins per-file warning counts against the
#    NixOS-pinned qmllint and STRUCTURALLY refuses any qmllint outside
#    /nix/store. Warning counts are Qt-version-specific, so a distro's qmllint
#    makes the ratchet meaningless; it is a NixOS-tier merge gate (gate-all.sh
#    runs it on the pin), not a portability check.
#  - schemesmodeltest: non-hermetic - the KF6 color-scheme lookup still pulls
#    the distro's /usr/share/color-schemes over the test's temp fixtures even
#    though the test pins XDG_DATA_DIRS, so real Breeze schemes shift the
#    asserted rows. Known-open, tracked in docs/tracking/multi-distro-ci-plan.md B2
#    (needs a hermetic scheme-path injection); passes on the nix devShell whose
#    allow-listed XDG_DATA_DIRS does not carry the conflicting schemes.
CTEST_MATRIX_EXCLUDE='^(qmllintgate|schemesmodeltest)$'

case "$STAGE" in
    build)
        echo "==> build stage complete"
        ;;
    test)
        echo "==> ctest (excluding NixOS-tier: $CTEST_MATRIX_EXCLUDE)"
        ctest --test-dir "$BUILD" --output-on-failure -E "$CTEST_MATRIX_EXCLUDE"
        ;;
    gate)
        echo "==> ctest (excluding NixOS-tier: $CTEST_MATRIX_EXCLUDE)"
        ctest --test-dir "$BUILD" --output-on-failure -E "$CTEST_MATRIX_EXCLUDE"

        # The container ENV must provide the distro framework QML tree. The
        # reused QML harnesses (run-e2e.sh, lib-qml-env.sh) re-exec into
        # `nix develop` when it is unset; there is no nix in-container, so
        # refuse loudly rather than let a downstream harness detonate mid-run.
        # This is a container contract, not something to default here.
        : "${LATTE_QML_MODULE_PATH:?the container image must export LATTE_QML_MODULE_PATH (the distro framework qml tree); see ci/containers/Containerfile.<distro>}"

        fakepointer="$BUILD/_e2e-tools/fakepointer"
        echo "==> building fakepointer"
        build_fakepointer "$fakepointer"

        echo "==> sceneprobe render gate"
        BUILD="$BUILD" "$SRC/scripts/sceneprobe-gate.sh"

        seedbase="$BUILD/_seedconfig"
        echo "==> seeding a default layout config"
        e2e_seed_default_config "$SRC" "$BUILD" "$seedbase"

        # The e2e suite splits by environment dependency in a bare container.
        # This gate runs the ENVIRONMENT-AGNOSTIC recipes (dock lifecycle,
        # wheel-over-empty-area, ruler, geometry agreement, keyboard-nav,
        # settings-window) - they assert pure dock state and pass on any distro
        # that builds. Five recipes need integration a minimal container does
        # not provide; they are NOT silently dropped, they are enumerated here
        # and tracked in docs/tracking/multi-distro-ci-plan.md B2 with the root cause:
        #   020/040/parabolic-hover-preview: a konsole window's app_id resolves
        #     to the bare "konsole" in-container, not the "org.kde.konsole.
        #     desktop" the recipes match - the desktop-app-database resolution
        #     my plasma-integrated env provides is absent (konsole
        #     windows DO map and the dock DOES track them, so this is app_id
        #     resolution, not konsole availability - proven in the ledger).
        #   050-drag-reorder-launchers: the default-template seed carries
        #     launchers with empty app_ids (same resolution gap) and the recipe
        #     needs >=3 real pinned launchers, i.e. a purpose-built fixture.
        #   duplicate-view-idremap: removeView's containment removal never
        #     reaches the layout file even after the full 120s undo window - a
        #     libplasma removal-flush divergence still to root-cause on Arch.
        # konsole/imagemagick stay in the image so these recipes drop straight
        # in once the app_id/fixture/flush items land.
        e2e_agnostic=(
            000-smoke 010-wheel-desktops 030-wheel-ruler-maxlength
            060-geometry-agreement keyboard-navigation-mode settings-window-onscreen
        )
        echo "==> e2e behavioral recipes (environment-agnostic subset)"
        BUILD="$BUILD" E2E_CONFIG_BASE="$seedbase" E2E_FAKEPOINTER="$fakepointer" \
            "$SRC/scripts/run-e2e.sh" "${e2e_agnostic[@]}"

        echo "==> gate stage complete"
        ;;
    *)
        echo "unknown stage '$STAGE' (build|test|gate)" >&2
        exit 2
        ;;
esac
