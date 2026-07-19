#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Front door for the DUAL-OUTPUT e2e leg (C-I2 / P1,
# docs/tracking/e2e-interaction-test-plan.md). It is the single-output run-matrix sibling
# for scenarios that need two outputs (the multi-output vehicle self-test today;
# the F5/A4 cross-screen scenarios later): it seeds a clean hermetic config and
# hands the recipes to scripts/run-e2e.sh with E2E_OUTPUT_COUNT=2 set, so the
# nested kwin_wayland comes up with two virtual outputs. The regular
# scripts/run-e2e.sh / scripts/run-matrix.sh stay single-output (default 1), so
# no existing recipe is affected.
#
#   scripts/run-multi-output-e2e.sh                       # the vehicle self-test
#   scripts/run-multi-output-e2e.sh <recipe> [recipe...]  # named 2out recipes
#
# BUILD=<dir> overrides the build (default ./build).
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"

# same pinned-toolchain guard as the other gates: re-exec into the devShell
# unless the pinned kwin_wayland / QML env is already present
if ! command -v kwin_wayland >/dev/null 2>&1 || [[ -z "${LATTE_QML_MODULE_PATH:-}" ]]; then
    exec nix develop "$repo" -c "$0" "$@"
fi

build="${BUILD:-$repo/build}"
if [[ ! -x "$build/bin/latte-dock" ]]; then
    echo "run-multi-output-e2e: FAIL no built binary at $build/bin/latte-dock (build first)"; exit 2
fi

# a hermetic default-layout seed for the vehicle to copy (same seeder the
# single-output matrix uses; the default layout's one view is onPrimary and so
# comes up on the primary output of the dual vehicle, giving the discover step a
# running ScreenPool to read).
seed="$build/_matrix-seedconfig"
echo "run-multi-output-e2e: seeding a default-layout config for the vehicle ($seed)"
source "$repo/scripts/lib-e2e-seed.sh"
e2e_seed_default_config "$repo" "$build" "$seed"

recipes=("$@")
[[ "${#recipes[@]}" -gt 0 ]] || recipes=(multi-output-selftest)

echo "run-multi-output-e2e: driving through a TWO-output nested vehicle: ${recipes[*]}"
BUILD="$build" E2E_CONFIG_BASE="$seed" E2E_OUTPUT_COUNT=2 \
    exec "$repo/scripts/run-e2e.sh" "${recipes[@]}"
