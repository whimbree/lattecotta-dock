#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# Drive the complete maximizeWhenMaximized and hideFloatingGapForMaximized path
# with a real Wayland toplevel. A rapid restore/maximize cycle proves the changed
# strut reaches layer-shell promptly; sourceguardtest separately pins that this
# path bypasses the geometry throttle. The concrete KWin frame geometry then
# verifies that KWin applied the new work area.
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"

view="$(e2e_json viewsData | python3 -c '
import json, sys
views = [v for v in json.load(sys.stdin)
         if v["edge"] in ("top", "bottom") and v["type"] == "dock" and not v["isCloned"]]
views.sort(key=lambda v: v["absoluteGeometry"][2] / v["screenGeometry"][2])
print(views[0]["containmentId"] if views else "")
')"
[[ -n "$view" ]] || e2e_fail "no horizontal non-clone view to exercise"

layout="$E2E_LAYOUT"
group_args=(--file "$layout" --group Containments --group "$view" --group General)
orig_maximize="$(kreadconfig6 "${group_args[@]}" --key maximizeWhenMaximized --default __absent__)" || e2e_fail "could not read maximizeWhenMaximized"
orig_length="$(kreadconfig6 "${group_args[@]}" --key maxLength --default __absent__)" || e2e_fail "could not read maxLength"
orig_hide_gap="$(kreadconfig6 "${group_args[@]}" --key hideFloatingGapForMaximized --default __absent__)" || e2e_fail "could not read hideFloatingGapForMaximized"
orig_edge_margin="$(kreadconfig6 "${group_args[@]}" --key screenEdgeMargin --default __absent__)" || e2e_fail "could not read screenEdgeMargin"
orig_visibility_mode="$(e2e_view_field "$view" 'v["visibilityMode"]')" || e2e_fail "could not read the original visibility mode"
kpid=0
configured=0

restore_key() {
    local key="$1" value="$2"
    if [[ "$value" == __absent__ ]]; then
        kwriteconfig6 "${group_args[@]}" --key "$key" --delete
    else
        kwriteconfig6 "${group_args[@]}" --key "$key" -- "$value"
    fi
}

set_konsole_maximized() {
    local enabled="$1"
    e2e_kwin_js "for (const w of workspace.windowList()) {
        if (w.resourceClass === 'org.kde.konsole' && w.caption.includes('LATTE D27 MAXIMIZE')) {
            workspace.activeWindow = w;
            w.setMaximize($enabled, $enabled);
            print('@TAG@|' + w.internalId);
        }
    }"
}

active_window_id() {
    e2e_kwin_js 'print("@TAG@|" + (workspace.activeWindow ? workspace.activeWindow.internalId : "none"));' | tail -1
}

konsole_frame_geometry() {
    local window geometry x y width height output extra
    window="$(e2e_dumpwins | grep '|org.kde.konsole|LATTE D27 MAXIMIZE' | tail -1)" || return 1
    [[ -n "$window" ]] || return 1
    geometry="$(awk -F'|' '{ split($4, g, " "); split(g[1], p, ","); split(g[2], s, "x"); print p[1], p[2], s[1], s[2] }' <<<"$window")"
    read -r x y width height extra <<< "$geometry"
    output="$(awk -F'|' '{ print $5 }' <<<"$window")"
    [[ -z "$extra" && "$x" =~ ^-?[0-9]+$ && "$y" =~ ^-?[0-9]+$ && "$width" =~ ^[0-9]+$ && "$height" =~ ^[0-9]+$ ]] || return 1
    (( width > 0 && height > 0 )) || return 1
    [[ -n "$output" ]] || return 1
    printf '%s %s %s %s %s\n' "$x" "$y" "$width" "$height" "$output"
}

assert_konsole_work_area() {
    local phase="$1" geometry kx ky kw kh output expected_x expected_y expected_w expected_h
    geometry="$(konsole_frame_geometry)" || e2e_fail "$phase maximize has no valid Konsole frame geometry"
    read -r kx ky kw kh output <<< "$geometry"
    [[ "$output" == "$screen" ]] || e2e_fail "$phase maximize placed the Konsole fixture on output '$output'; expected discovered output '$screen'"
    expected_x=$screen_x
    expected_w=$screen_w
    expected_h=$((screen_h - max_strut))
    if [[ "$edge" == top ]]; then
        expected_y=$((screen_y + max_strut))
    else
        expected_y=$screen_y
    fi
    (( kx == expected_x && ky == expected_y && kw == expected_w && kh == expected_h )) \
        || e2e_fail "$phase maximize has frame $kx,$ky ${kw}x${kh}; expected exact $edge work area $expected_x,$expected_y ${expected_w}x${expected_h} on output '$screen' from the ${max_strut}px strut"
}

cleanup() {
    local body_status=$? cleanup_failed=0 dock_pid restored_visibility=false
    trap - EXIT
    if (( kpid != 0 )); then
        kill "$kpid" 2>/dev/null || true
        wait "$kpid" 2>/dev/null || true
    fi
    if (( configured == 1 )); then
        if ! e2e_dock_stop >/dev/null 2>&1; then
            cleanup_failed=1
        fi
        restore_key maximizeWhenMaximized "$orig_maximize" || cleanup_failed=1
        restore_key maxLength "$orig_length" || cleanup_failed=1
        restore_key hideFloatingGapForMaximized "$orig_hide_gap" || cleanup_failed=1
        restore_key screenEdgeMargin "$orig_edge_margin" || cleanup_failed=1
        dock_pid="$(e2e_dock_pid)"
        if [[ -n "$dock_pid" ]] && kill -0 "$dock_pid" 2>/dev/null; then
            cleanup_failed=1
        elif ! e2e_dock_start 90 >/dev/null 2>&1; then
            cleanup_failed=1
        elif ! e2e_call setViewVisibilityMode us "$view" "$orig_visibility_mode" >/dev/null 2>&1; then
            cleanup_failed=1
        else
            for _ in $(seq 1 40); do
                if [[ "$(e2e_view_field "$view" 'v["visibilityMode"]')" == "$orig_visibility_mode" ]]; then
                    restored_visibility=true
                    break
                fi
                sleep 0.25
            done
            [[ "$restored_visibility" == true ]] || cleanup_failed=1
        fi
    fi
    if (( cleanup_failed != 0 )); then
        echo "FAIL: D27 fixture cleanup did not restore the dock and its original configuration" >&2
        (( body_status == 0 )) && body_status=1
    fi
    exit "$body_status"
}
trap cleanup EXIT

e2e_dock_stop || e2e_fail "dock did not stop before fixture configuration"
configured=1
kwriteconfig6 "${group_args[@]}" --key maximizeWhenMaximized true || e2e_fail "could not configure maximizeWhenMaximized"
kwriteconfig6 "${group_args[@]}" --key maxLength 60 || e2e_fail "could not configure maxLength"
kwriteconfig6 "${group_args[@]}" --key hideFloatingGapForMaximized true || e2e_fail "could not configure hideFloatingGapForMaximized"
kwriteconfig6 "${group_args[@]}" --key screenEdgeMargin 18 || e2e_fail "could not configure screenEdgeMargin"
e2e_dock_start 90 || e2e_fail "dock did not restart with maximizeWhenMaximized enabled"
e2e_call setViewVisibilityMode us "$view" alwaysVisible >/dev/null || e2e_fail "could not set the fixture view to alwaysVisible"
for _ in $(seq 1 40); do
    [[ "$(e2e_view_field "$view" 'v["visibilityMode"]')" == alwaysVisible ]] && break
    sleep 0.25
done
[[ "$(e2e_view_field "$view" 'v["visibilityMode"]')" == alwaysVisible ]] || e2e_fail "view $view did not enter alwaysVisible mode"
tracker_enabled="$(e2e_json trackerData u "$view" | python3 -c 'import json, sys; print(str(json.load(sys.stdin)["enabled"]).lower())')"
[[ "$tracker_enabled" == true ]] || e2e_fail "Always Visible hide-gap fixture did not enable window tracking"

read -r base_w base_strut base_published screen_x screen_y screen_w screen_h edge screen <<< "$(e2e_view_field "$view" '"%d %d %d %d %d %d %d %s %s" % (v["absoluteGeometry"][2], v["strutsThickness"], v["publishedStruts"][3], v["screenGeometry"][0], v["screenGeometry"][1], v["screenGeometry"][2], v["screenGeometry"][3], v["edge"], v["screen"])')"
(( screen_w > 0 && screen_h > 0 )) || e2e_fail "view $view reported invalid output dimensions ${screen_w}x${screen_h}"
[[ -n "$screen" ]] || e2e_fail "view $view did not report its output name"
(( base_w * 100 < screen_w * 90 )) || e2e_fail "fixture view $view did not start at a floating length ($base_w of ${screen_w}px)"
(( base_strut == base_published )) || e2e_fail "base strut was not published (thickness=$base_strut published=$base_published)"

setsid konsole -p 'LocalTabTitleFormat=LATTE D27 MAXIMIZE' >/dev/null 2>&1 &
kpid=$!
for _ in $(seq 1 30); do
    konsole="$(e2e_dumpwins | grep '|org.kde.konsole|LATTE D27 MAXIMIZE' | tail -1)"
    [[ -n "$konsole" ]] && break
    sleep 0.5
done
[[ -n "${konsole:-}" ]] || e2e_fail "Konsole maximize fixture never mapped"

#! Konsole can remember a maximized state from an earlier nested run. Normalize
#! the fixture before generating the first maximize edge.
fixture_id="$(set_konsole_maximized false)" || e2e_fail "KWin did not normalize the Konsole maximize fixture"
[[ -n "$fixture_id" && "$fixture_id" != *$'\n'* ]] || e2e_fail "KWin found multiple tagged Konsole fixtures"
for _ in $(seq 1 40); do
    read -r active_maximized exists_maximized normalized_published <<< "$(e2e_json trackerData u "$view" | python3 -c '
import json, sys
tracker = json.load(sys.stdin)
print(str(tracker["activeWindowMaximized"]).lower(), str(tracker["existsWindowMaximized"]).lower(), end=" ")
'; e2e_view_field "$view" 'v["publishedStruts"][3]')"
    [[ "$active_maximized" == false && "$exists_maximized" == false && "$normalized_published" == "$base_strut" ]] && break
    sleep 0.25
done
[[ "$active_maximized" == false && "$exists_maximized" == false && "$normalized_published" == "$base_strut" ]] || e2e_fail "Konsole fixture did not normalize to restored state"

[[ "$(set_konsole_maximized true)" == "$fixture_id" ]] || e2e_fail "KWin did not maximize the tagged Konsole fixture"

maximized=false
for _ in $(seq 1 40); do
    read -r active_maximized exists_maximized max_w max_strut max_published <<< "$(e2e_json trackerData u "$view" | python3 -c '
import json, sys
t = json.load(sys.stdin)
print(str(t["activeWindowMaximized"]).lower(), str(t["existsWindowMaximized"]).lower(), end=" ")
'; e2e_view_field "$view" '"%d %d %d" % (v["absoluteGeometry"][2], v["strutsThickness"], v["publishedStruts"][3])')"
    if [[ "$active_maximized" == true && "$exists_maximized" == true && "$max_strut" == "$max_published" ]]; then
        maximized=true
        break
    fi
    sleep 0.25
done
if [[ "$maximized" != true ]]; then
    tracker_payload="$(e2e_json trackerData u "$view" 2>/dev/null || true)"
    frame_geometry="$(konsole_frame_geometry 2>/dev/null || true)"
    e2e_fail "active tagged window did not reach tracker and the published strut (active=$active_maximized exists=$exists_maximized thickness=$max_strut published=$max_published frame='${frame_geometry:-unavailable}' tracker='${tracker_payload:-unavailable}')"
fi
(( max_strut < base_strut )) || e2e_fail "maximized floating gap did not shrink the strut ($base_strut -> $max_strut)"
(( max_strut > 0 && max_strut < screen_h )) || e2e_fail "maximized strut $max_strut is invalid for ${screen_w}x${screen_h} output '$screen'"
[[ "$(active_window_id)" == "$fixture_id" ]] || e2e_fail "tagged Konsole was not active when activeWindowMaximized became true"

assert_konsole_work_area first

[[ "$(set_konsole_maximized false)" == "$fixture_id" ]] || e2e_fail "KWin did not restore the tagged Konsole fixture"

restored=false
for _ in $(seq 1 40); do
    read -r active_maximized exists_maximized restored_w restored_published <<< "$(e2e_json trackerData u "$view" | python3 -c '
import json, sys
tracker = json.load(sys.stdin)
print(str(tracker["activeWindowMaximized"]).lower(), str(tracker["existsWindowMaximized"]).lower(), end=" ")
'; e2e_view_field "$view" '"%d %d" % (v["absoluteGeometry"][2], v["publishedStruts"][3])')"
    if [[ "$active_maximized" == false && "$exists_maximized" == false && "$restored_published" == "$base_strut" ]] && (( restored_w * 100 < screen_w * 90 )); then
        restored=true
        break
    fi
    sleep 0.25
done
[[ "$restored" == true ]] || e2e_fail "restored window left active/exists maximize state or full-width geometry behind (active=$active_maximized exists=$exists_maximized width=$restored_w)"

#! Keep the second maximize close to the restore and require the reservation to
#! land well below the old one-second delay.
sleep 0.1
start_ns="$(date +%s%N)"
set_konsole_maximized true >/dev/null &
shortcut_pid=$!
reservation_ms=-1
for _ in $(seq 1 100); do
    read -r active_maximized exists_maximized published <<< "$(e2e_json trackerData u "$view" | python3 -c '
import json, sys
tracker = json.load(sys.stdin)
print(str(tracker["activeWindowMaximized"]).lower(), str(tracker["existsWindowMaximized"]).lower(), end=" ")
'; e2e_view_field "$view" 'v["publishedStruts"][3]')"
    if [[ "$active_maximized" == true && "$exists_maximized" == true && "$published" == "$max_strut" ]]; then
        reservation_ms=$(( ($(date +%s%N) - start_ns) / 1000000 ))
        break
    fi
    sleep 0.01
done
wait "$shortcut_pid" || e2e_fail "KWin did not find the tagged Konsole fixture during the second maximize"
(( reservation_ms >= 0 )) || e2e_fail "second maximize never published the $max_strut px strut"
(( reservation_ms < 750 )) || e2e_fail "second maximize waited ${reservation_ms}ms behind the geometry throttle"
[[ "$(active_window_id)" == "$fixture_id" ]] || e2e_fail "tagged Konsole was not active during the timed maximize transition"

assert_konsole_work_area second

echo "real maximized window drove view $view to ${max_w}px; active-throttle strut published in ${reservation_ms}ms and KWin reapplied the ${max_strut}px work area"
