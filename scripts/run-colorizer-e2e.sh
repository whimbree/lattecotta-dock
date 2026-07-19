#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Front door for the D21 colorizer-contrast e2e guard
# (tests/e2e/110-colorizer-applet-contrast.sh, docs/known-defects.md). It seeds
# a hermetic config from tests/e2e/fixtures/d21 - a Light-colors panel (digital
# clock + systray + show-desktop) on a bundled dark Plasma color scheme, the
# condition that engages the colorizer - and hands it to scripts/run-e2e.sh. The
# recipe then asserts stock-applet contrast at both the state and rendered-pixel
# level. Needs no pre-existing staged config, so it runs standalone in CI.
#
#   scripts/run-colorizer-e2e.sh
#
# BUILD=<dir> overrides the build (default ./build).
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"

# same pinned-toolchain guard as the other e2e front doors
if ! command -v kwin_wayland >/dev/null 2>&1 || [[ -z "${LATTE_QML_MODULE_PATH:-}" ]]; then
    exec nix develop "$repo" -c "$0" "$@"
fi

build="${BUILD:-$repo/build}"
if [[ ! -x "$build/bin/latte-dock" ]]; then
    echo "run-colorizer-e2e: FAIL no built binary at $build/bin/latte-dock (build first)"; exit 2
fi

fixture="$repo/tests/e2e/fixtures/d21"
seed="$build/_colorizer-seedconfig"
echo "run-colorizer-e2e: seeding the D21 Light-colors config for the vehicle ($seed)"
rm -rf "$seed"
mkdir -p "$seed/latte"
cp "$fixture/D21.layout.latte" "$seed/latte/D21.layout.latte"
cp "$fixture/kdeglobals" "$seed/kdeglobals"
cat > "$seed/lattedockrc" <<'RC'
[UniversalSettings]
memoryUsage=0
singleModeLayoutName=D21
version=2
RC

echo "run-colorizer-e2e: driving the D21 contrast guard through the nested vehicle"
BUILD="$build" E2E_CONFIG_BASE="$seed" \
    exec "$repo/scripts/run-e2e.sh" 110-colorizer-applet-contrast
