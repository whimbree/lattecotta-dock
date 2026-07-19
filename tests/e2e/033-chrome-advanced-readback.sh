#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# AU-6b (docs/tracking/edit-mode-settings-audit-plan.md, cluster CL-6): the settings-window
# chrome audit's live leg. Two things it proves against a real dock:
#
#   1. The universalSettings readback CL-6 added to viewConfigData's "view"
#      object answers live and with sane values: inAdvancedModeForEditSettings
#      (control 7, the advanced switch) and settingsWindowScaleWidth/Height
#      (control 8, the drag-corner per-screen window scales). These live on
#      UniversalSettings, not the containment config, so before this readback the
#      audit's P3 leg for controls 7 and 8 had nothing to read.
#
#   2. The Advanced switch (control 7) DRIVEN through the real settings window
#      flips inAdvancedModeForEditSettings, and the readback re-tracks it (P1
#      applies + P3 reflects, end to end). The advanced label carries a MouseArea
#      that toggles the switch; it sits right-aligned in the window header, so the
#      click sweeps a small right-of-header grid and the readback confirms which
#      hit landed.
#
# Non-destructive: it toggles advanced mode and restores it, never adds or
# removes a view. The add/duplicate/remove chrome (controls 10-11) are `act`
# one-shots proven at the view level by duplicate-view-idremap.sh; their
# control-level drive is a CL-6 follow-up, not this recipe.
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"
source "$E2E_REPO/tests/e2e/matrix/matrix-lib.sh"
source "$E2E_REPO/tests/e2e/audit/audit-lib.sh"

view="$(e2e_tasks_view)" || e2e_fail "no tasks view"

view_field() { audit_view_snapshot "$view" | awk -F'\t' -v k="$1" '$1==k{print $2}'; }

audit_enter_editmode "$view" || e2e_fail "edit mode never turned on"

#! leg 1: the new readback answers live with well-formed values
adv0="$(view_field inAdvancedModeForEditSettings)"
sw="$(view_field settingsWindowScaleWidth)"
sh="$(view_field settingsWindowScaleHeight)"
[[ "$adv0" == "true" || "$adv0" == "false" ]] \
    || e2e_fail "view.inAdvancedModeForEditSettings is not a bool: '$adv0'"
python3 -c "w=float('$sw'); h=float('$sh'); assert 0.3 <= w <= 3.0 and 0.3 <= h <= 3.0, ('scales out of range', w, h)" \
    || e2e_fail "view.settingsWindowScaleWidth/Height out of the sane 0.3..3.0 range ($sw / $sh)"
#! control 6's persistent state is queryable through the config object
stick="$(audit_config_snapshot "$view" | awk -F'\t' '$1=="configurationSticker"{print $2}')"
[[ "$stick" == "true" || "$stick" == "false" ]] \
    || e2e_fail "config.configurationSticker (pin state) is not a bool: '$stick'"
echo "readback answers live: inAdvancedModeForEditSettings=$adv0 scales=$sw/$sh configurationSticker=$stick"

#! leg 2: drive the Advanced switch and confirm the readback flips. The switch is
#! right-aligned in the header; sweep a small grid until inAdvancedModeForEditSettings
#! changes from its baseline.
want="$([[ "$adv0" == "true" ]] && echo false || echo true)"
flipped=""
for yf in 0.05 0.07 0.09 0.11; do
    for xf in 0.86 0.91 0.80 0.95; do
        audit_settings_click "$xf" "$yf"
        for i in 1 2 3 4; do
            now="$(view_field inAdvancedModeForEditSettings)"
            if [[ "$now" == "$want" ]]; then flipped="$now"; break; fi
            sleep 0.4
        done
        [[ -n "$flipped" ]] && break
    done
    [[ -n "$flipped" ]] && break
done
[[ -n "$flipped" ]] || e2e_fail "the Advanced switch drive never flipped inAdvancedModeForEditSettings from $adv0"
echo "Advanced switch driven: inAdvancedModeForEditSettings $adv0 -> $flipped (control 7 applies; readback re-tracks)"

#! restore advanced mode to its baseline through the same control, then leave edit mode
if [[ "$flipped" != "$adv0" ]]; then
    for yf in 0.05 0.07 0.09 0.11; do
        for xf in 0.86 0.91 0.80 0.95; do
            audit_settings_click "$xf" "$yf"
            [[ "$(view_field inAdvancedModeForEditSettings)" == "$adv0" ]] && break 2
            sleep 0.3
        done
    done
fi

audit_exit_editmode "$view"
echo "AU-6b: the CL-6 universalSettings readback answers live and the Advanced switch drives it"
