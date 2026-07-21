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
backup="$(mktemp)" || e2e_fail "could not allocate the layout backup"
cp "$E2E_LAYOUT" "$backup" || { rm -f "$backup"; e2e_fail "could not back up $E2E_LAYOUT"; }
fixture_pid=0
fixture_id=""
fixture_title=""
fixture_count_value=""
recipe_finalized=false
cleanup() {
    local body_status=$? cleanup_failed=0 pid="" wait_status=0 count="" query_status=0
    trap - EXIT
    if [[ "$recipe_finalized" != true ]]; then
        if (( fixture_pid != 0 )); then
            if kill -0 "$fixture_pid" 2>/dev/null && ! kill -TERM "$fixture_pid" 2>/dev/null; then
                echo "FAIL: cleanup could not terminate fixture pid $fixture_pid" >&2
                cleanup_failed=1
            fi
            wait "$fixture_pid" 2>/dev/null
            wait_status=$?
            if (( wait_status != 0 && wait_status != 143 )) || kill -0 "$fixture_pid" 2>/dev/null; then
                echo "FAIL: cleanup fixture pid $fixture_pid did not terminate cleanly (wait=$wait_status)" >&2
                cleanup_failed=1
            fi
        fi
        if [[ -n "$fixture_title" ]]; then
            count="$(fixture_count)"; query_status=$?
            if (( query_status != 0 )) || [[ ! "$count" =~ ^[0-9]+$ ]] || (( count != 0 )); then
                echo "FAIL: cleanup could not prove fixture '$fixture_title' absent (count='${count:-query-failed}')" >&2
                cleanup_failed=1
            fi
        fi
        pid="$(e2e_dock_pid 2>/dev/null)"
        if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null && ! e2e_dock_stop >/dev/null 2>&1; then
            echo "FAIL: cleanup could not stop dock pid $pid" >&2
            cleanup_failed=1
        fi
        if ! cp "$backup" "$E2E_LAYOUT" || ! cmp -s "$backup" "$E2E_LAYOUT"; then
            echo "FAIL: cleanup could not restore layout $E2E_LAYOUT from $backup" >&2
            cleanup_failed=1
        fi
    fi
    rm -f "$backup" || { echo "FAIL: cleanup could not remove $backup" >&2; cleanup_failed=1; }
    (( cleanup_failed == 0 )) || echo "FAIL: SC-WT1 recipe cleanup left residue" >&2
    (( body_status != 0 || cleanup_failed == 0 )) || body_status=1
    exit "$body_status"
}
trap cleanup EXIT
read_tracker_state() {
    local label="$1" payload state status
    payload="$(e2e_json trackerData u "$view")"; status=$?
    (( status == 0 )) || e2e_fail "$label: trackerData query failed with status $status"
    state="$(python3 -c '
import json, sys
t = json.load(sys.stdin)
print(str(t["enabled"]).lower(), str(t["lastActiveWindowPresent"]).lower())
' <<< "$payload")"; status=$?
    (( status == 0 )) || e2e_fail "$label: invalid trackerData payload: $payload"
    read -r tracker_enabled tracker_present <<< "$state"
}
wait_tracker_state() {
    local expected_enabled="$1" expected_present="$2" label="$3" enabled="" present="" i
    for ((i = 0; i < 40; i++)); do
        read_tracker_state "$label"
        enabled="$tracker_enabled"; present="$tracker_present"
        if [[ "$enabled" == "$expected_enabled" && "$present" == "$expected_present" ]]; then
            return 0
        fi
        sleep 0.25
    done
    e2e_fail "$label tracker state was enabled=$enabled target=$present; expected enabled=$expected_enabled target=$expected_present"
}
wait_visibility_mode() {
    local expected="$1" actual="" i
    for ((i = 0; i < 40; i++)); do
        actual="$(e2e_view_field "$view" 'v["visibilityMode"]')" \
            || e2e_fail "visibility-mode readback failed for view $view"
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
inject() {
    # Injector failures are fatal. Retries are only for status-0 input whose
    # independent KWin effect has not appeared yet.
    local label="$1" status
    shift
    "$E2E_FAKEPOINTER" "$@"; status=$?
    (( status == 0 )) || e2e_fail "$label: fakepointer '$*' failed with status $status"
}
configure_mode() {
    local close_enabled="$1" scroll_action="$2" expected_tracker="$3" label="$4" config status
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
    config="$(e2e_json viewConfigData u "$view")"; status=$?
    (( status == 0 )) || e2e_fail "$label: viewConfigData query failed with status $status"
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
    local winx status
    winx="$(e2e_view_window_x "$view")"; status=$?
    if (( status != 0 )) || [[ -z "$winx" ]]; then
        echo "empty_area_point: could not resolve the rendered x origin for view $view" >&2
        return 1
    fi
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
    inject "settling pointer outside the view" move "$pointer_x" 500
    sleep 0.3
    inject "settling pointer on the empty view area" move "$pointer_x" "$pointer_y"
    sleep 0.8
}
fixture_state() {
    e2e_kwin_js "for (const w of workspace.windowList()) {
        if (w.resourceClass === 'org.kde.konsole' && w.caption.includes('$fixture_title')) {
            print('@TAG@|' + w.internalId + '|' + w.minimized + '|' + (workspace.activeWindow === w));
        }
    }"
}
fixture_count() {
    local dump status
    dump="$(e2e_dumpwins)"; status=$?
    if (( status != 0 )); then
        echo "fixture_count: e2e_dumpwins failed for '$fixture_title' with status $status" >&2
        return "$status"
    fi
    python3 -c 'import sys
title = sys.argv[1]
print(sum(f"|org.kde.konsole|{title}" in line for line in sys.stdin))' "$fixture_title" <<< "$dump"
}
read_fixture_count() {
    local label="$1" status
    fixture_count_value="$(fixture_count)"; status=$?
    (( status == 0 )) || e2e_fail "$label: KWin fixture-count query failed with status $status"
    [[ "$fixture_count_value" =~ ^[0-9]+$ ]] \
        || e2e_fail "$label: KWin fixture count is not numeric: '$fixture_count_value'"
}
read_fixture_state() {
    local label="$1" state status
    state="$(fixture_state)"; status=$?
    (( status == 0 )) || e2e_fail "$label: KWin fixture-state query failed with status $status"
    [[ -n "$state" && "$state" != *$'\n'* ]] \
        || e2e_fail "$label: expected exactly one fixture state, got '${state:-none}'"
    read -r current_id minimized active <<< "${state//|/ }"
}
spawn_fixture() {
    local title="$1" status i
    fixture_title="$title"
    fixture_pid=0
    fixture_id=""
    read_fixture_count "$title stale-fixture check"
    (( fixture_count_value == 0 )) || e2e_fail "$title: $fixture_count_value stale fixture window(s) already mapped"
    setsid konsole -p "LocalTabTitleFormat=$fixture_title" >/dev/null 2>&1 &
    fixture_pid=$!
    for ((i = 0; i < 40; i++)); do
        read_fixture_count "$title map wait"
        (( fixture_count_value == 1 )) && break
        sleep 0.25
    done
    (( fixture_count_value == 1 )) || e2e_fail "$title: fixture window count reached $fixture_count_value instead of 1"

    fixture_id="$(e2e_kwin_js "for (const w of workspace.windowList()) {
        if (w.resourceClass === 'org.kde.konsole' && w.caption.includes('$fixture_title')) {
            w.minimized = false;
            w.setMaximize(false, false);
            workspace.activeWindow = w;
            print('@TAG@|' + w.internalId);
        }
    }")"; status=$?
    (( status == 0 )) || e2e_fail "$title: KWin activation query failed with status $status"
    [[ -n "$fixture_id" && "$fixture_id" != *$'\n'* ]] || e2e_fail "$title: KWin did not identify exactly one fixture"

    for ((i = 0; i < 40; i++)); do
        read_fixture_state "$title activation wait"
        [[ "$current_id" == "$fixture_id" && "$minimized" == false && "$active" == true ]] && return 0
        sleep 0.25
    done
    e2e_fail "$title: fixture did not settle active and restored (id=$current_id minimized=$minimized active=$active)"
}
assert_fixture_normal() {
    local label="$1"
    read_fixture_state "$label"
    [[ "$current_id" == "$fixture_id" && "$minimized" == false ]] \
        || e2e_fail "$label changed the fixture unexpectedly (id=$current_id minimized=$minimized active=$active)"
}
wait_fixture_absent() {
    local label="$1" i
    for ((i = 0; i < 40; i++)); do
        read_fixture_count "$label"
        (( fixture_count_value == 0 )) && return 0
        sleep 0.25
    done
    e2e_fail "$label left $fixture_count_value fixture window(s) mapped"
}
drive_close_until_absent() {
    local attempt poll
    for attempt in 1 2 3 4; do
        settle_empty_pointer
        inject "close-only middle click attempt $attempt" middleclick "$pointer_x" "$pointer_y"
        for ((poll = 0; poll < 8; poll++)); do
            read_fixture_count "close-only effect wait"
            (( fixture_count_value == 0 )) && return 0
            sleep 0.25
        done
        echo "  (middle click not delivered on attempt $attempt, retrying)"
    done
    e2e_fail "close-only middle click left $fixture_count_value fixture window(s) mapped"
}
drive_minimize_until_observed() {
    local attempt poll
    for attempt in 1 2 3 4; do
        settle_empty_pointer
        inject "minimize-toggle wheel attempt $attempt" scroll "$pointer_x" "$pointer_y" -1 0
        for ((poll = 0; poll < 8; poll++)); do
            read_fixture_state "minimize-toggle effect wait"
            [[ "$current_id" == "$fixture_id" && "$minimized" == true ]] && return 0
            sleep 0.25
        done
        echo "  (negative wheel not delivered on attempt $attempt, retrying)"
    done
    e2e_fail "minimize-toggle negative wheel did not minimize the fixture (id=$current_id minimized=$minimized active=$active)"
}
terminate_fixture() {
    local label="$1" wait_status=0
    if (( fixture_pid != 0 )); then
        if kill -0 "$fixture_pid" 2>/dev/null; then
            kill -TERM "$fixture_pid" || e2e_fail "$label: could not terminate fixture pid $fixture_pid"
        fi
        wait "$fixture_pid" 2>/dev/null
        wait_status=$?
        (( wait_status == 0 || wait_status == 143 )) \
            || e2e_fail "$label: fixture pid $fixture_pid exited unexpectedly with status $wait_status"
        ! kill -0 "$fixture_pid" 2>/dev/null \
            || e2e_fail "$label: fixture pid $fixture_pid survived termination"
    fi
    wait_fixture_absent "$label absence check"
    fixture_pid=0
    fixture_id=""
}
finalize_recipe() {
    local pid
    terminate_fixture "final minimize fixture"
    read_fixture_count "final residue check"
    (( fixture_count_value == 0 )) || e2e_fail "finalization left $fixture_count_value fixture window(s)"
    pid="$(e2e_dock_pid)" || e2e_fail "finalization could not read the dock pid"
    [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null \
        || e2e_fail "finalization found no running dock to stop"
    e2e_dock_stop || e2e_fail "finalization could not stop dock pid $pid"
    cp "$backup" "$E2E_LAYOUT" || e2e_fail "finalization could not restore $E2E_LAYOUT"
    cmp -s "$backup" "$E2E_LAYOUT" || e2e_fail "finalization restored different layout bytes"
    recipe_finalized=true
}

# Disabled controls: neither action may turn on the tracker or affect the same
# active normal window.
configure_mode false 0 false "disabled close/minimize"
spawn_fixture "LATTE SC-WT1 DISABLED"
wait_tracker_state false false "disabled close/minimize with active window"
settle_empty_pointer
inject "disabled close control" middleclick "$pointer_x" "$pointer_y"
sleep 0.8
assert_fixture_normal "disabled close"
inject "disabled minimize-toggle control" scroll "$pointer_x" "$pointer_y" -1 0
sleep 0.8
assert_fixture_normal "disabled minimize-toggle"
wait_tracker_state false false "disabled controls after input"
echo "ok: disabled close and minimize-toggle kept tracker off and left the window normal"
terminate_fixture "disabled fixture"

# Close-only: enabling the setting alone must enable tracking. The first click
# proves the no-target contract is a no-op; the second closes a tracked window.
configure_mode true 0 true "close-only"
settle_empty_pointer
inject "close-only no-target control" middleclick "$pointer_x" "$pointer_y"
sleep 0.8
e2e_wait_running 5 || e2e_fail "close-only no-target click stopped the dock"
wait_tracker_state true false "close-only after no-target click"
echo "ok: close-only no-target click was a no-op with tracking enabled"

spawn_fixture "LATTE SC-WT1 CLOSE"
wait_tracker_state true true "close-only with active window"
drive_close_until_absent
e2e_wait_running 5 || e2e_fail "close-only target click stopped the dock"
echo "ok: close-only enabled tracking and removed the KWin window"
terminate_fixture "closed fixture process"

# Minimize-toggle: the setting alone must enable tracking. A negative wheel with
# no target is harmless; the same real input minimizes a tracked normal window.
configure_mode false 4 true "minimize-toggle"
settle_empty_pointer
inject "minimize-toggle no-target control" scroll "$pointer_x" "$pointer_y" -1 0
sleep 0.8
e2e_wait_running 5 || e2e_fail "minimize-toggle no-target wheel stopped the dock"
wait_tracker_state true false "minimize-toggle after no-target wheel"
echo "ok: minimize-toggle no-target wheel was a no-op with tracking enabled"

spawn_fixture "LATTE SC-WT1 MINIMIZE"
wait_tracker_state true true "minimize-toggle with active window"
drive_minimize_until_observed
read_tracker_state "tracker state after minimize"
[[ "$tracker_enabled" == true ]] || e2e_fail "minimize effect disabled the requester-owned tracker"
echo "ok: minimize-toggle enabled tracking and KWin reports the window minimized"

finalize_recipe
echo "PASS: SC-WT1 empty-area tracker requester production paths"
