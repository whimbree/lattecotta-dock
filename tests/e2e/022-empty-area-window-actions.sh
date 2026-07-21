#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# SC-WT1 (the D58 tracker-enablement root fix and regression): drive the real
# EnvironmentActions empty-area MouseArea with fakepointer. trackerData proves
# whether the QML binding enabled tracking; KWin's own window state independently
# proves close and minimize effects. The neutral mode is the negative control for
# both disabled settings, and each enabled mode starts with a no-target action.
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"

view="$(e2e_tasks_view)" || e2e_fail "no horizontal tasks view"
group_args=(--file "$E2E_LAYOUT" --group Containments --group "$view" --group General)
backup="$(mktemp)"
cp "$E2E_LAYOUT" "$backup"
fixture_pid=0
fixture_id=""
fixture_title=""

cleanup() {
    local status=$?
    trap - EXIT
    if (( fixture_pid != 0 )); then
        kill "$fixture_pid" 2>/dev/null || true
        wait "$fixture_pid" 2>/dev/null || true
    fi
    if kill -0 "$(e2e_dock_pid)" 2>/dev/null; then
        e2e_dock_stop >/dev/null 2>&1 || status=1
    fi
    cp "$backup" "$E2E_LAYOUT"
    rm -f "$backup"
    exit "$status"
}
trap cleanup EXIT

tracker_state() {
    e2e_json trackerData u "$view" | python3 -c '
import json, sys
t = json.load(sys.stdin)
print(str(t["enabled"]).lower(), str(t["lastActiveWindowPresent"]).lower())
'
}

wait_tracker_state() {
    local expected_enabled="$1" expected_present="$2" label="$3" enabled="" present="" i
    for ((i = 0; i < 40; i++)); do
        read -r enabled present <<< "$(tracker_state)"
        if [[ "$enabled" == "$expected_enabled" && "$present" == "$expected_present" ]]; then
            return 0
        fi
        sleep 0.25
    done
    e2e_fail "$label tracker state was enabled=$enabled target=$present; expected enabled=$expected_enabled target=$expected_present (payload: $(e2e_json trackerData u "$view"))"
}

wait_visibility_mode() {
    local expected="$1" actual="" i
    for ((i = 0; i < 40; i++)); do
        actual="$(e2e_view_field "$view" 'v["visibilityMode"]')"
        [[ "$actual" == "$expected" ]] && return 0
        sleep 0.25
    done
    e2e_fail "view $view stayed in visibility mode $actual; expected $expected"
}

write_config_key() {
    local key="$1" value="$2" label="$3"
    kwriteconfig6 "${group_args[@]}" --key "$key" -- "$value" \
        || e2e_fail "$label: could not write $key=$value"
}

configure_mode() {
    local close_enabled="$1" scroll_action="$2" expected_tracker="$3" label="$4" config
    if kill -0 "$(e2e_dock_pid)" 2>/dev/null; then
        e2e_dock_stop || e2e_fail "$label: could not stop the dock for configuration"
    fi

    write_config_key dragActiveWindowEnabled false "$label"
    write_config_key closeActiveWindowEnabled "$close_enabled" "$label"
    write_config_key scrollAction "$scroll_action" "$label"
    write_config_key backgroundOnlyOnMaximized false "$label"
    write_config_key solidBackgroundForMaximized false "$label"
    write_config_key disablePanelShadowForMaximized false "$label"
    write_config_key windowColors 0 "$label"
    write_config_key screenEdgeMargin -1 "$label"
    write_config_key hideFloatingGapForMaximized false "$label"

    e2e_dock_start 90 || e2e_fail "$label: dock did not restart"
    e2e_call setViewVisibilityMode us "$view" alwaysVisible >/dev/null \
        || e2e_fail "$label: could not select alwaysVisible"
    wait_visibility_mode alwaysVisible

    config="$(e2e_json viewConfigData u "$view")"
    if ! python3 - "$config" "$close_enabled" "$scroll_action" <<'PY'; then
import json
import sys

cfg = json.loads(sys.argv[1])["config"]
assert cfg["dragActiveWindowEnabled"] is False
assert cfg["closeActiveWindowEnabled"] == (sys.argv[2] == "true")
assert cfg["scrollAction"] == int(sys.argv[3])
assert cfg["backgroundOnlyOnMaximized"] is False
assert cfg["solidBackgroundForMaximized"] is False
assert cfg["disablePanelShadowForMaximized"] is False
assert cfg["windowColors"] == 0
assert cfg["screenEdgeMargin"] == -1
assert cfg["hideFloatingGapForMaximized"] is False
PY
        e2e_fail "$label: in-process config does not match the neutral requester fixture"
    fi
    wait_tracker_state "$expected_tracker" false "$label without a target"
}

empty_area_point() {
    local winx
    winx="$(e2e_view_window_x "$view")"
    { e2e_json viewsData; e2e_json viewAppletsData u "$view"; } | python3 -c "
import json, sys
views, applets = (json.loads(line) for line in sys.stdin)
view = next(v for v in views if v['containmentId'] == $view)
ax, ay, aw, ah = view['absoluteGeometry']
lx = view['localGeometry'][0]
ox = ${winx:-ax - lx}
drift = ox - (ax - lx)
ax += drift
spans = sorted((ox + g[0], ox + g[0] + g[2]) for g in (a['geometry'] for a in applets))
gaps, cursor = [], ax
for start, end in spans:
    if start > cursor:
        gaps.append((cursor, start))
    cursor = max(cursor, end)
if ax + aw > cursor:
    gaps.append((cursor, ax + aw))
best = max(gaps, key=lambda gap: gap[1] - gap[0], default=(0, 0))
if best[1] - best[0] < 8:
    sys.exit('widest empty-area gap is under 8px: %s' % (gaps,))
print(int((best[0] + best[1]) / 2), int(ay + ah / 2))
"
}

settle_empty_pointer() {
    local point
    point="$(empty_area_point)" || e2e_fail "could not locate an empty view area"
    read -r pointer_x pointer_y <<< "$point"
    [[ -n "$pointer_x" && -n "$pointer_y" ]] || e2e_fail "empty-area point is incomplete"
    "$E2E_FAKEPOINTER" move "$pointer_x" 500
    sleep 0.3
    "$E2E_FAKEPOINTER" move "$pointer_x" "$pointer_y"
    sleep 0.8
}

fixture_state() {
    e2e_kwin_js "for (const w of workspace.windowList()) {
        if (w.resourceClass === 'org.kde.konsole' && w.caption.includes('$fixture_title')) {
            print('@TAG@|' + w.internalId + '|' + w.minimized + '|' + (workspace.activeWindow === w));
        }
    }" | tail -1
}

fixture_count() {
    e2e_dumpwins | grep -F -c "|org.kde.konsole|$fixture_title" || true
}

spawn_fixture() {
    local title="$1" state="" minimized="" active="" i
    fixture_title="$title"
    fixture_pid=0
    fixture_id=""
    [[ "$(fixture_count)" -eq 0 ]] || e2e_fail "$title: a stale fixture window is already mapped"
    setsid konsole -p "LocalTabTitleFormat=$fixture_title" >/dev/null 2>&1 &
    fixture_pid=$!
    for ((i = 0; i < 40; i++)); do
        [[ "$(fixture_count)" -eq 1 ]] && break
        sleep 0.25
    done
    [[ "$(fixture_count)" -eq 1 ]] || e2e_fail "$title: fixture window did not map"

    fixture_id="$(e2e_kwin_js "for (const w of workspace.windowList()) {
        if (w.resourceClass === 'org.kde.konsole' && w.caption.includes('$fixture_title')) {
            w.minimized = false;
            w.setMaximize(false, false);
            workspace.activeWindow = w;
            print('@TAG@|' + w.internalId);
        }
    }" | tail -1)"
    [[ -n "$fixture_id" ]] || e2e_fail "$title: KWin could not activate the fixture"

    for ((i = 0; i < 40; i++)); do
        state="$(fixture_state)"
        read -r current_id minimized active <<< "${state//|/ }"
        [[ "$current_id" == "$fixture_id" && "$minimized" == false && "$active" == true ]] && return 0
        sleep 0.25
    done
    e2e_fail "$title: fixture did not settle active and restored (state: $state)"
}

assert_fixture_normal() {
    local label="$1" state current_id minimized active
    state="$(fixture_state)"
    read -r current_id minimized active <<< "${state//|/ }"
    [[ "$current_id" == "$fixture_id" && "$minimized" == false ]] \
        || e2e_fail "$label changed the fixture unexpectedly (state: ${state:-absent})"
}

wait_fixture_absent() {
    local label="$1" i
    for ((i = 0; i < 40; i++)); do
        [[ "$(fixture_count)" -eq 0 ]] && return 0
        sleep 0.25
    done
    e2e_fail "$label left the fixture window mapped (state: $(fixture_state))"
}

drive_close_until_absent() {
    local attempt poll
    for attempt in 1 2 3 4; do
        settle_empty_pointer
        "$E2E_FAKEPOINTER" middleclick "$pointer_x" "$pointer_y"
        for ((poll = 0; poll < 8; poll++)); do
            [[ "$(fixture_count)" -eq 0 ]] && return 0
            sleep 0.25
        done
        echo "  (middle click not delivered on attempt $attempt, retrying)"
    done
    e2e_fail "close-only middle click left the fixture window mapped (state: $(fixture_state))"
}

drive_minimize_until_observed() {
    local attempt poll state current_id minimized active
    for attempt in 1 2 3 4; do
        settle_empty_pointer
        "$E2E_FAKEPOINTER" scroll "$pointer_x" "$pointer_y" -1 0
        for ((poll = 0; poll < 8; poll++)); do
            state="$(fixture_state)"
            read -r current_id minimized active <<< "${state//|/ }"
            [[ "$current_id" == "$fixture_id" && "$minimized" == true ]] && return 0
            sleep 0.25
        done
        echo "  (negative wheel not delivered on attempt $attempt, retrying)"
    done
    e2e_fail "minimize-toggle negative wheel did not minimize the fixture (state: ${state:-absent})"
}

retire_fixture() {
    local title="$fixture_title"
    if (( fixture_pid != 0 )); then
        kill "$fixture_pid" 2>/dev/null || true
        wait "$fixture_pid" 2>/dev/null || true
    fi
    wait_fixture_absent "$title cleanup"
    fixture_pid=0
    fixture_id=""
}

# Disabled controls: neither action may turn on the tracker or affect the same
# active normal window.
configure_mode false 0 false "disabled close/minimize"
spawn_fixture "LATTE SC-WT1 DISABLED"
wait_tracker_state false false "disabled close/minimize with active window"
settle_empty_pointer
"$E2E_FAKEPOINTER" middleclick "$pointer_x" "$pointer_y"
sleep 0.8
assert_fixture_normal "disabled close"
"$E2E_FAKEPOINTER" scroll "$pointer_x" "$pointer_y" -1 0
sleep 0.8
assert_fixture_normal "disabled minimize-toggle"
wait_tracker_state false false "disabled controls after input"
echo "ok: disabled close and minimize-toggle kept tracker off and left the window normal"
retire_fixture

# Close-only: enabling the setting alone must enable tracking. The first click
# proves the no-target contract is a no-op; the second closes a tracked window.
configure_mode true 0 true "close-only"
settle_empty_pointer
"$E2E_FAKEPOINTER" middleclick "$pointer_x" "$pointer_y"
sleep 0.8
e2e_wait_running 5 || e2e_fail "close-only no-target click stopped the dock"
wait_tracker_state true false "close-only after no-target click"
echo "ok: close-only no-target click was a no-op with tracking enabled"

spawn_fixture "LATTE SC-WT1 CLOSE"
wait_tracker_state true true "close-only with active window"
drive_close_until_absent
e2e_wait_running 5 || e2e_fail "close-only target click stopped the dock"
echo "ok: close-only enabled tracking and removed the KWin window"
kill "$fixture_pid" 2>/dev/null || true
wait "$fixture_pid" 2>/dev/null || true
fixture_pid=0
fixture_id=""

# Minimize-toggle: the setting alone must enable tracking. A negative wheel with
# no target is harmless; the same real input minimizes a tracked normal window.
configure_mode false 4 true "minimize-toggle"
settle_empty_pointer
"$E2E_FAKEPOINTER" scroll "$pointer_x" "$pointer_y" -1 0
sleep 0.8
e2e_wait_running 5 || e2e_fail "minimize-toggle no-target wheel stopped the dock"
wait_tracker_state true false "minimize-toggle after no-target wheel"
echo "ok: minimize-toggle no-target wheel was a no-op with tracking enabled"

spawn_fixture "LATTE SC-WT1 MINIMIZE"
wait_tracker_state true true "minimize-toggle with active window"
drive_minimize_until_observed
read -r tracker_enabled _ <<< "$(tracker_state)"
[[ "$tracker_enabled" == true ]] || e2e_fail "minimize effect disabled the requester-owned tracker"
echo "ok: minimize-toggle enabled tracking and KWin reports the window minimized"

echo "PASS: SC-WT1 empty-area tracker requester production paths"
