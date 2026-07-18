#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# The DRIVEN sanitized-dock gate (docs/ub-catching-plan.md, Prong A3). Builds
# the build-asan tree (dock + plugins under ASan+UBSan with QT_FORCE_ASSERTS,
# via -DLATTE_SANITIZE=ON) and drives the nested e2e recipes against it, so
# OUR-code UB and tripped invariants in the LIVE integration paths ABORT the run
# with a symbolized stack and a non-zero exit instead of running silently. The
# motivating bug was an insert(-1) from a stale splitter position that vanished a
# justify splitter in the field with no diagnostic; a sanitized unit test could
# not reach it because the placement path only runs in a live dock.
#
# THE EXIT CODE IS THE VERDICT (CLAUDE.md gate discipline): run it, read $?, done.
# Never wrap it in `... || { ... }` - that masks the code.
#
# Distinct from scripts/run-asan-dock.sh, which is the ad-hoc front door for a
# single recipe against an already-built tree. This is the self-contained CI
# gate: it ensures the tree, seeds a hermetic config, runs the reliable recipe
# core, and returns one verdict. Wired into scripts/gate-all.sh as its final
# stage so a merge that introduces integration-path UB fails CI.
#
# The recipe core is deliberately the NO-INPUT set:
#   000-smoke              view creation, layout restore (-> layoutmanager
#                          splitter placement), mask geometry, clean SIGTERM
#   060-geometry-agreement state-vs-render surface agreement
#   070-asan-binary-shadow the /proc/<dock>/maps shadow assertion: the dock and
#                          containment plugin under test resolve under build-asan
#                          AND libasan is mapped (proof the run is really
#                          sanitized, not an uninstrumented shadow that would
#                          catch no UB)
# These exercise the containment-plugin integration paths where the UB class
# lives, without the fakepointer input-timing flake the wheel/edit recipes carry
# under ASan slowdown. A deeper sweep with the full recipe set is a manual
# scripts/run-asan-dock.sh run, not the always-on merge gate.
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"

# re-exec into the devshell unless the pinned toolchain is already in PATH (same
# guard as the other gates: a bare system cmake/kwin poisons the build/vehicle)
if ! command -v cmake >/dev/null 2>&1 || [[ "$(command -v cmake)" != /nix/store/* ]]; then
    exec nix develop "$repo" -c "$0" "$@"
fi

asan_build="$repo/build-asan"
seed="$asan_build/_asan-seedconfig"
recipes=(000-smoke 060-geometry-agreement 070-asan-binary-shadow)

echo "asan-e2e-gate: configuring + building the sanitized tree ($asan_build)"
cmake -S "$repo" -B "$asan_build" -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DLATTE_SANITIZE=ON -DBUILD_TESTING=OFF
cmake --build "$asan_build"

# Seed a hermetic default-layout config with the NORMAL dock (lib-e2e-seed.sh
# header: the sanitized dock's shutdown flush is too variable to seed from). In
# gate-all, build/ is already built by build-check; standalone, build it here.
# The seed is cheap config data and must never fall back to a real _runconfig
# (non-hermetic; a view targeting an absent output would not settle in the
# vehicle).
seed_build="$repo/build"
if [[ ! -x "$seed_build/bin/latte-dock" ]]; then
    echo "asan-e2e-gate: normal build/ absent; building it once for seeding"
    cmake -S "$repo" -B "$seed_build" -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
    cmake --build "$seed_build"
fi

echo "asan-e2e-gate: seeding a default-layout config for the vehicle"
source "$repo/scripts/lib-e2e-seed.sh"
e2e_seed_default_config "$repo" "$seed_build" "$seed"

echo "asan-e2e-gate: driving the sanitized dock through the nested e2e core: ${recipes[*]}"
ASAN_BUILD="$asan_build" E2E_CONFIG_BASE="$seed" \
    "$repo/scripts/run-asan-dock.sh" "${recipes[@]}"

echo "asan-e2e-gate: PASS (no OUR-code UB in the driven integration paths)"
