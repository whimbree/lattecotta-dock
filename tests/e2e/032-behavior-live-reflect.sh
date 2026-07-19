#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# CL-3 behavior audit, the LIVE half (docs/tracking/edit-mode-settings-audit-plan.md).
# The Behavior page mixes four writer classes; the cfg controls (checkboxes,
# the two action combos) and the S-a TypeSelection dead-key finding are pinned
# deterministically in tests/behaviorwiringaudittest.cpp. The pos/vis/view
# controls write NO config key - Location/Alignment/Screen call
# positioner.setNextLocation(...), Visibility/Delay write visibility.* and the
# Environment checkboxes write latteView.byPassWM / isPreferredForShortcuts - so
# their audit is a READBACK question: does the live view expose edge/alignment/
# screen/visibilityMode and the visibility-timer / byPassWM / shortcut state, and
# do those readbacks REFLECT the running view (P3, "two views of one value never
# disagree")?
#
# This recipe CONSUMES the CL-0 readbacks (it adds none):
#   - viewsData        -> edge / alignment / screen / visibilityMode (AU-3a/3b)
#   - viewConfigData   -> config.alignment (int) and the "view" half:
#                         byPassWM, isPreferredForShortcuts, visibilityTimerShow,
#                         visibilityTimerHide, visibilityEnableKWinEdges,
#                         visibilityRaiseOnDesktop, visibilityRaiseOnActivity,
#                         indicatorType (AU-3b/3c)
# and proves: the readbacks ANSWER for a real view; the alignment reported two
# ways AGREES (viewsData string vs config int - the P3/P4 cross-view identity);
# and the drive -> readback loop closes (edit mode flips viewsData.editMode and
# the view half still answers in edit mode). The pixel-drive of an individual
# pos/vis button is the plan's audit_settings_click scaffolding; its per-control
# fractions are not calibrated here because the readback identity already pins
# what the plan asks for these controls, without a fragile per-button click.
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"
source "$E2E_REPO/tests/e2e/matrix/matrix-lib.sh"
source "$E2E_REPO/tests/e2e/audit/audit-lib.sh"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

view="$(e2e_tasks_view)" || e2e_fail "no tasks view to audit"
echo "auditing behavior readbacks for view $view"

# --- viewsData readback: the Location/Alignment/Screen/Visibility surface -----
# (AU-3a/3b: the pos/vis controls' live effect is read here, not in a config diff)
IFS=$'\t' read -r vd_edge vd_alignment vd_vismode vd_screen < <(
    e2e_json viewsData | python3 -c '
import json, sys
view = "'"$view"'"
views = {str(v["containmentId"]): v for v in json.load(sys.stdin)}
v = views.get(view)
if v is None:
    sys.exit("view %s absent from viewsData" % view)
# tab-delimited, screen (may hold a connector name) last so a stray space
# cannot shift the visibilityMode token
print("\t".join([str(v.get("edge","")), str(v.get("alignment","")),
                 str(v.get("visibilityMode","")), str(v.get("screen",""))]))
') || e2e_fail "could not read viewsData for view $view"

[[ -n "$vd_edge" && "$vd_edge" != "None" ]]        || e2e_fail "viewsData carries no edge for view $view"
[[ -n "$vd_alignment" ]]                           || e2e_fail "viewsData carries no alignment for view $view"
[[ -n "$vd_vismode" ]]                             || e2e_fail "viewsData carries no visibilityMode for view $view"
echo "  viewsData: edge=$vd_edge alignment=$vd_alignment screen='$vd_screen' visibilityMode=$vd_vismode"

# --- viewConfigData readback: the config int + the C++ "view" half ------------
audit_config_snapshot "$view" > "$work/config" || e2e_fail "viewConfigData config snapshot failed"
audit_view_snapshot   "$view" > "$work/view"   || e2e_fail "viewConfigData view snapshot failed"

cfg_get()  { awk -F'\t' -v k="$1" '$1==k{print $2}' "$work/config"; }
view_get() { awk -F'\t' -v k="$1" '$1==k{print $2}' "$work/view"; }

# --- AU-3a P3/P4: the alignment reported two ways must AGREE -------------------
# viewsData reports the alignment as a name; viewConfigData.config.alignment is
# the raw enum int (LatteCore.Types Alignment: -1 none, 0 center, 1 left,
# 2 right, 3 top, 4 bottom, 10 justify - declarativeimports/coretypes.h.in).
# Two surfaces of one value: they must never disagree.
cfg_alignment="$(cfg_get alignment)"
[[ -n "$cfg_alignment" ]] || e2e_fail "viewConfigData config carries no alignment key"
expected_alignment_name="$(python3 -c '
names = {-1:"none", 0:"center", 1:"left", 2:"right", 3:"top", 4:"bottom", 10:"justify"}
import sys
print(names.get(int(sys.argv[1]), "?"))
' "$cfg_alignment")"
if [[ "$expected_alignment_name" == "$vd_alignment" ]]; then
    echo "  P4 agree: alignment reads '$vd_alignment' (viewsData) == config int $cfg_alignment"
else
    e2e_fail "alignment disagrees across surfaces: viewsData='$vd_alignment' but config int $cfg_alignment maps to '$expected_alignment_name'"
fi

# --- AU-3b/3c: the "view" half answers every C++-property field, sanely --------
# These are the readbacks CL-0 added for the pos/vis/view controls that write no
# config key; the plan (decision 4, section 8) mandates they exist so a control
# reflecting live state can be verified. Assert each ANSWERS and holds a
# well-typed value (never missing, never a placeholder).
for boolfield in byPassWM isPreferredForShortcuts visibilityEnableKWinEdges \
                 visibilityRaiseOnDesktop visibilityRaiseOnActivity; do
    v="$(view_get "$boolfield")"
    [[ "$v" == "true" || "$v" == "false" ]] \
        || e2e_fail "view-half '$boolfield' is not a boolean readback (got '${v:-<absent>}')"
done
echo "  view half booleans answer: byPassWM=$(view_get byPassWM) isPreferredForShortcuts=$(view_get isPreferredForShortcuts) enableKWinEdges=$(view_get visibilityEnableKWinEdges) raiseOnDesktop=$(view_get visibilityRaiseOnDesktop) raiseOnActivity=$(view_get visibilityRaiseOnActivity)"

# AU-3b: the show/hide DELAY timers are the readback that pins those controls
for timer in visibilityTimerShow visibilityTimerHide; do
    v="$(view_get "$timer")"
    [[ "$v" =~ ^[0-9]+$ ]] \
        || e2e_fail "view-half '$timer' is not a non-negative integer readback (got '${v:-<absent>}')"
done
echo "  view half timers answer: show=$(view_get visibilityTimerShow)ms hide=$(view_get visibilityTimerHide)ms"

# indicatorType is a plugin id string (AU-3c/CL-4 boundary; asserted present here)
[[ -n "$(view_get indicatorType)" ]] || e2e_fail "view-half indicatorType absent"

# --- drive -> readback loop closes: edit mode flips a readback, view half still answers
audit_enter_editmode "$view" || e2e_fail "edit mode never turned on"
edit_now="$(e2e_json viewsData | python3 -c '
import json, sys
view = "'"$view"'"
v = {str(x["containmentId"]): x for x in json.load(sys.stdin)}.get(view, {})
print(str(v.get("editMode","")).lower())
')"
[[ "$edit_now" == "true" ]] || e2e_fail "viewsData.editMode did not reflect the edit-mode drive (got '$edit_now')"

# the view-half readback must still answer while the settings window is up (this
# is the state a real audit reads a control's reflected value in)
audit_view_snapshot "$view" > "$work/view_edit" || e2e_fail "view snapshot failed in edit mode"
grep -q $'^byPassWM\t' "$work/view_edit" || e2e_fail "view-half stopped answering in edit mode"
echo "  drive->readback: edit mode flipped viewsData.editMode=true; view half still answers"

audit_exit_editmode "$view" || true

echo "behavior live reflect: viewsData + viewConfigData readbacks answer and agree for the pos/vis/view controls"
