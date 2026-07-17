#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Distro-agnostic build+gate driver for the multi-distro CI matrix
# (docs/multi-distro-ci-plan.md). Runs INSIDE a per-distro container (see
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

case "$STAGE" in
    build)
        echo "==> build stage complete"
        ;;
    test)
        echo "==> ctest"
        ctest --test-dir "$BUILD" --output-on-failure
        ;;
    gate)
        echo "==> ctest"
        ctest --test-dir "$BUILD" --output-on-failure
# STUB: Phase B wires the headless render/behavioral gates here (nested
# kwin_wayland + lavapipe sceneprobe in invariant+tolerance mode, plus the
# tests/e2e recipes). "gate" refuses loudly until then rather than passing
# a build-only run off as a full gate. Done = both harnesses green in-container.
        echo "==> gate stage not wired yet (Phase B); use 'build' or 'test'" >&2
        exit 1
        ;;
    *)
        echo "unknown stage '$STAGE' (build|test|gate)" >&2
        exit 2
        ;;
esac
