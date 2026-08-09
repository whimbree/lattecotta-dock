#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Front door for the e2e interaction MATRIX (P0 / C-I1). The reusable driver,
# the abort backbone and the fixture generator now live in the typed harness
# (latte_harness.matrix and latte_harness.matrix_fixture, harness/, run via uv);
# scenarios are typed e2e recipes (tests/e2e/*.py) that call the matrix
# scenario_commit / scenario_abort API. This script only guarantees a CLEAN
# hermetic seed and hands the recipes to scripts/run-e2e.sh, which owns the one
# nested-vehicle bring-up - there is no second compositor implementation here.
#
#   scripts/run-matrix.sh                       # run the harness self-test
#   scripts/run-matrix.sh <recipe> [recipe...]  # run named matrix recipes
#
# BUILD=<dir> overrides the build (default ./build). The seed is produced with
# the NORMAL dock (lib-e2e-seed.sh header: the sanitized dock's shutdown flush
# is too variable to seed from).
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"

# same pinned-toolchain guard as the other gates: re-exec into the devShell
# unless the pinned kwin_wayland / uv / QML env is already present (uv joined
# the recipe toolchain with the harness matrix fixture module; run-e2e.sh
# carries the full rationale)
if ! command -v kwin_wayland >/dev/null 2>&1 || ! command -v uv >/dev/null 2>&1 \
   || [[ -z "${LATTE_QML_MODULE_PATH:-}" ]]; then
    exec nix develop "$repo" -c "$0" "$@"
fi

build="${BUILD:-$repo/build}"
if [[ ! -x "$build/bin/latte-dock" ]]; then
    echo "run-matrix: FAIL no built binary at $build/bin/latte-dock (build first)"; exit 2
fi

# a hermetic default-layout seed for the vehicle to copy. Never fall back to a
# real _runconfig: a view targeting an absent output would not settle in the
# single-output vehicle (the same rule asan-e2e-gate follows).
seed="$build/_matrix-seedconfig"
echo "run-matrix: seeding a default-layout config for the vehicle ($seed)"
source "$repo/scripts/lib-e2e-seed.sh"
e2e_seed_default_config "$repo" "$build" "$seed"

recipes=("$@")
[[ "${#recipes[@]}" -gt 0 ]] || recipes=(matrix-harness-selftest)

echo "run-matrix: driving the matrix recipes through the nested vehicle: ${recipes[*]}"
BUILD="$build" E2E_CONFIG_BASE="$seed" exec "$repo/scripts/run-e2e.sh" "${recipes[@]}"
