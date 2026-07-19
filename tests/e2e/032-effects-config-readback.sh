#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# CL-4 live P3 leg (docs/tracking/edit-mode-settings-audit-plan.md, controls 75-90): the
# Effects page controls REFLECT the dock's live state on open. The wiring (which
# key/property each control writes) is pinned deterministically in
# tests/effectshandleraudittest.cpp; this recipe proves the other direction on a
# REAL running dock - that the CL-0 readback surfaces the live effects state the
# settings window would show:
#
#   AU-4a Shadows  - config.appletShadowsEnabled / shadowSize / shadowOpacity /
#                    shadowColorType reflect the seeded General values.
#   AU-4b Animations - config.animationsEnabled / durationTime reflect. durationTime
#                    is seeded to 1 (the x3 "faster" button's stored value, the S-c
#                    pairing) and read back as the raw integer, the sole contract
#                    (nothing resolves it by the schema choice name).
#   AU-4c Indicators - view.indicatorEnabled / indicatorType / indicatorPresent
#                    reflect the seeded [Indicator] sub-group. These controls write
#                    a C++ Indicator PROPERTY (class `ind`), persisted to the
#                    containment [Indicator] group and read back live here - the
#                    CL-0 indicator readback this cluster was told to CONSUME.
#
# Method (the 110-colorizer seed+restart shape): stop the dock, write known
# NON-DEFAULT effects values into the containment config (General + Indicator),
# restart, and assert every value via the CL-0 snapshot helpers. Non-default on
# purpose - a readback that only ever shows the schema default would pass on a
# dock that reads nothing, so each seeded value differs from its default and the
# assertion is a real signal. A full backup of the layout file is restored on
# exit so the vehicle config is left as found.
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"
source "$E2E_REPO/tests/e2e/matrix/matrix-lib.sh"
source "$E2E_REPO/tests/e2e/audit/audit-lib.sh"

view="$(e2e_tasks_view)" || e2e_fail "no tasks view"
echo "CL-4: effects readback view is containment $view"

# full-file backup/restore: simplest correct restore for a multi-key,
# multi-group seed (General + Indicator), and it cannot leave a stray key behind
backup="$(mktemp --suffix=.latte)"
cp "$E2E_LAYOUT" "$backup"
restore() {
    e2e_dock_stop >/dev/null 2>&1 || true
    cp "$backup" "$E2E_LAYOUT"
    rm -f "$backup"
}
trap restore EXIT

gen() { kwriteconfig6 --file "$E2E_LAYOUT" --group Containments --group "$view" --group General --key "$1" "$2"; }
ind() { kwriteconfig6 --file "$E2E_LAYOUT" --group Containments --group "$view" --group Indicator --key "$1" "$2"; }

# ---- seed non-default effects state while the dock is stopped ----------------
# stop first: a clean SIGTERM flushes the CURRENT (old) config, so the write must
# land after the flush, exactly as 030/110 order it.
e2e_dock_stop >/dev/null 2>&1 || true

gen appletShadowsEnabled false      # default true
gen shadowSize 55                   # default 30
gen shadowOpacity 40                # default 70
gen shadowColorType 2               # default 0 (Default); 2 = User
gen animationsEnabled false         # default true
gen durationTime 1                  # default 2; 1 = the x3 "faster" stored value
ind enabled false                   # default true
ind type org.kde.latte.plasma       # default org.kde.latte.default

e2e_dock_start 90 || e2e_fail "dock never settled after seeding the effects config"

# ---- assert the CL-0 readback reflects every seeded value --------------------
cfg="$(mktemp)"; vw="$(mktemp)"
audit_config_snapshot "$view" > "$cfg" || e2e_fail "viewConfigData config snapshot failed"
audit_view_snapshot  "$view" > "$vw"  || e2e_fail "viewConfigData view snapshot failed"
echo "--- config snapshot (effects keys) ---"; grep -E '^(appletShadowsEnabled|shadowSize|shadowOpacity|shadowColorType|animationsEnabled|durationTime)\b' "$cfg" || true
echo "--- view snapshot (indicator) ---";      grep -E '^indicator' "$vw" || true

rc=0
# AU-4a shadows (config half)
audit_assert_reflects "$cfg" appletShadowsEnabled false || rc=1
audit_assert_reflects "$cfg" shadowSize 55              || rc=1
audit_assert_reflects "$cfg" shadowOpacity 40           || rc=1
audit_assert_reflects "$cfg" shadowColorType 2          || rc=1
# AU-4b animations (config half)
audit_assert_reflects "$cfg" animationsEnabled false    || rc=1
audit_assert_reflects "$cfg" durationTime 1             || rc=1
# AU-4c indicators (view half - the CL-0 indicator readback)
audit_assert_reflects "$vw" indicatorPresent true                    || rc=1
audit_assert_reflects "$vw" indicatorEnabled false                   || rc=1
audit_assert_reflects "$vw" indicatorType '"org.kde.latte.plasma"'   || rc=1

rm -f "$cfg" "$vw"
(( rc == 0 )) || e2e_fail "an effects value did not reflect through the CL-0 readback (see the snapshots above)"

echo "PASS: CL-4 effects config/indicator readback reflects live state (AU-4a/b/c P3)"
