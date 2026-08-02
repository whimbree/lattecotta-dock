#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# Prove that Dodge Active follows current KWin admission state instead of a
# cached tracker row. One persistent Wayland toplevel overlaps a left dock,
# becomes taskbar-and-switcher-skipped without being unmapped, becomes
# eligible again under the same KWin identity, then disappears while a
# coalesced eligibility refresh is pending.
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"
source "$E2E_REPO/tests/e2e/matrix/matrix-lib.sh"

view=""
kpid=0
configured=0

dock_field() {
    local expression="$1"
    e2e_json dockSystemData | python3 -c "
import json, sys
snapshot = json.load(sys.stdin)
if snapshot['schemaVersion'] != 11:
    sys.exit('expected dockSystemData schema 11')
matches = [record for record in snapshot['views']
           if record['persistentDockId'] == $view]
if len(matches) != 1:
    sys.exit('expected exactly one dockSystemData record for view $view')
v = matches[0]
print($expression)
"
}

tracker_probe() {
    e2e_json trackerData u "$view" | python3 -c '
import json, sys
tracker = json.load(sys.stdin)
print(
    str(tracker["activeWindowTouching"]).lower(),
    str(tracker["activeWindowTouchingEdge"]).lower(),
    str(tracker["existsWindowTouching"]).lower(),
    str(tracker["existsWindowTouchingEdge"]).lower(),
    str(tracker["existsWindowActive"]).lower(),
)
'
}

wait_for_dodge_state() {
    local expected_touching="$1" expected_hidden="$2" phase="$3"
    local active_touching=unread active_edge=unread
    local exists_touching=unread exists_edge=unread exists_active=unread
    local hidden=unread

    for _ in $(seq 1 160); do
        read -r active_touching active_edge exists_touching exists_edge \
            exists_active <<< "$(tracker_probe)"
        hidden="$(dock_field 'str(v["isHidden"]).lower()')"

        if [[ "$active_touching" == "$expected_touching"
              && "$exists_touching" == "$expected_touching"
              && "$exists_active" == "$expected_touching"
              && "$hidden" == "$expected_hidden" ]]; then
            return 0
        fi
        sleep 0.05
    done

    e2e_fail "$phase did not settle (activeTouching=$active_touching activeEdge=$active_edge existsTouching=$exists_touching existsEdge=$exists_edge existsActive=$exists_active hidden=$hidden)"
}

konsole_count() {
    e2e_dumpwins \
        | grep -c '|org.kde.konsole|LATTE D264 WINDOW ADMISSION' \
        || true
}

set_konsole_geometry() {
    local x="$1" y="$2" width="$3" height="$4"
    e2e_kwin_js "for (const window of workspace.windowList()) {
        if (window.resourceClass === 'org.kde.konsole'
                && window.caption.includes('LATTE D264 WINDOW ADMISSION')) {
            const geometry = Object.assign({}, window.frameGeometry);
            geometry.x = $x;
            geometry.y = $y;
            geometry.width = $width;
            geometry.height = $height;
            window.frameGeometry = geometry;
            workspace.activeWindow = window;
            print('@TAG@|' + window.internalId);
        }
    }" | tail -1
}

set_konsole_admission() {
    local accepted="$1" skipped=false
    [[ "$accepted" == false ]] && skipped=true

    e2e_kwin_js "for (const window of workspace.windowList()) {
        if (window.resourceClass === 'org.kde.konsole'
                && window.caption.includes('LATTE D264 WINDOW ADMISSION')) {
            window.skipTaskbar = $skipped;
            window.skipSwitcher = $skipped;
            workspace.activeWindow = window;
            print('@TAG@|' + window.internalId + '|'
                + window.skipTaskbar + '|' + window.skipSwitcher);
        }
    }" 0.05 | tail -1
}

konsole_identity() {
    e2e_kwin_js "for (const window of workspace.windowList()) {
        if (window.resourceClass === 'org.kde.konsole'
                && window.caption.includes('LATTE D264 WINDOW ADMISSION')) {
            print('@TAG@|' + window.internalId + '|'
                + window.skipTaskbar + '|' + window.skipSwitcher);
        }
    }" 0.01 | tail -1
}

cleanup() {
    local body_status=$? cleanup_failed=0 dock_pid
    trap - EXIT

    if (( kpid != 0 )); then
        kill "$kpid" 2>/dev/null || true
        wait "$kpid" 2>/dev/null || true
    fi

    if (( configured == 1 )); then
        if ! e2e_dock_stop >/dev/null 2>&1; then
            cleanup_failed=1
        fi
        rm -rf "${E2E_CONFIG_HOME:?}"
        cp -r "$MATRIX_PRISTINE" "$E2E_CONFIG_HOME" \
            || cleanup_failed=1
        dock_pid="$(e2e_dock_pid)"
        if [[ -n "$dock_pid" ]] && kill -0 "$dock_pid" 2>/dev/null; then
            cleanup_failed=1
        elif ! e2e_dock_start 90 >/dev/null 2>&1; then
            cleanup_failed=1
        fi
    fi

    if (( cleanup_failed != 0 )); then
        echo "FAIL: D264 window-admission cleanup did not restore the dock configuration" >&2
        (( body_status == 0 )) && body_status=1
    fi
    exit "$body_status"
}
trap cleanup EXIT

matrix_init \
    || e2e_fail "could not capture the pristine nested configuration"
configured=1
matrix_stage dock-left-center-1out \
    || e2e_fail "could not realize the D264 left-dock fixture"
view="$(matrix_view_id)" \
    || e2e_fail "could not resolve the D264 left-dock fixture"

e2e_call setViewVisibilityMode us "$view" dodgeActive >/dev/null \
    || e2e_fail "could not set the left dock to Dodge Active"
for _ in $(seq 1 40); do
    [[ "$(dock_field 'v["visibilityMode"]')" == dodgeActive ]] && break
    sleep 0.05
done
[[ "$(dock_field 'v["visibilityMode"]')" == dodgeActive ]] \
    || e2e_fail "left dock did not enter Dodge Active"

read -r screen_x screen_y screen_width screen_height \
    dock_x dock_y dock_width dock_height \
    <<< "$(dock_field '"%d %d %d %d %d %d %d %d" % (
        *v["screenGeometry"], *v["absoluteGeometry"]
    )')"

"$E2E_FAKEPOINTER" move \
    $((screen_x + screen_width - 20)) \
    $((screen_y + screen_height / 2)) \
    || e2e_fail "could not park the nested pointer away from the left dock"
wait_for_dodge_state false false "initial no-window control"

[[ "$(konsole_count)" -eq 0 ]] \
    || e2e_fail "a tagged Konsole already exists; this recipe owns one client"
setsid konsole -p 'LocalTabTitleFormat=LATTE D264 WINDOW ADMISSION' \
    >/dev/null 2>&1 &
kpid=$!
for _ in $(seq 1 40); do
    [[ "$(konsole_count)" -eq 1 ]] && break
    sleep 0.25
done
[[ "$(konsole_count)" -eq 1 ]] \
    || e2e_fail "the D264 Wayland client never mapped"

client_width=$((screen_width * 2 / 3))
client_height=$((screen_height * 2 / 3))
client_x="$screen_x"
client_y=$((dock_y + dock_height / 2 - client_height / 2))
(( client_y < screen_y )) && client_y=$screen_y
(( client_y + client_height > screen_y + screen_height )) \
    && client_y=$((screen_y + screen_height - client_height))

fixture_id="$(set_konsole_geometry \
    "$client_x" "$client_y" "$client_width" "$client_height")" \
    || e2e_fail "KWin could not place the D264 client over the left dock"
[[ -n "$fixture_id" && "$fixture_id" != *$'\n'* ]] \
    || e2e_fail "KWin returned an invalid D264 client identity"
wait_for_dodge_state true true "accepted overlapping window"

rejected="$(set_konsole_admission false)" \
    || e2e_fail "KWin could not make the D264 client ineligible"
[[ "$rejected" == "$fixture_id|true|true" ]] \
    || e2e_fail "ineligible transition changed identity or flags (expected=$fixture_id|true|true actual=$rejected)"
wait_for_dodge_state false false "same-window rejection"
[[ "$(konsole_identity)" == "$fixture_id|true|true" ]] \
    || e2e_fail "the rejected client was unmapped or replaced"

accepted="$(set_konsole_admission true)" \
    || e2e_fail "KWin could not make the D264 client eligible again"
[[ "$accepted" == "$fixture_id|false|false" ]] \
    || e2e_fail "re-admission changed identity or flags (expected=$fixture_id|false|false actual=$accepted)"
wait_for_dodge_state true true "same-window re-admission"

#! Queue a coalesced invalidation and immediately destroy the client. A stale
#! timer must not recreate either tracker touch state or hidden presentation.
set_konsole_admission false >/dev/null \
    || e2e_fail "KWin could not queue the final D264 invalidation"
kill "$kpid" 2>/dev/null \
    || e2e_fail "could not destroy the D264 client"
wait "$kpid" 2>/dev/null || true
kpid=0
for _ in $(seq 1 80); do
    [[ "$(konsole_count)" -eq 0 ]] && break
    sleep 0.05
done
[[ "$(konsole_count)" -eq 0 ]] \
    || e2e_fail "the D264 client survived destruction"
wait_for_dodge_state false false "destruction during pending refresh"
sleep 0.25
wait_for_dodge_state false false "post-debounce convergence"

echo "PASS: Dodge Active rejected and re-admitted one persistent KWin window, then discarded its pending update on destruction"
