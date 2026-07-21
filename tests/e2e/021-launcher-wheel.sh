#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# SC-W1 (the regression guard for D56, pure-launcher task wheel uses inherited
# asymmetric activation): real fakepointer wheel input reaches TaskMouseArea.
# Positive activation can only continue through TaskItem.activateLauncher() to
# TasksModel.requestActivate; process, KWin, and task-model effects distinguish
# it from the accepted negative, ScrollNone, threshold, and rate-limit no-ops.
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"

fixture="$E2E_REPO/tests/e2e/fixtures/sc-w1"
desktop_id="org.kde.latte.sc-w1.desktop"
launcher_url="applications:$desktop_id"
window_title="latte-sc-w1-launcher"
launcher_span=""
backup="$(mktemp)"
cp "$E2E_LAYOUT" "$backup"

view="$(e2e_tasks_view)" || e2e_fail "no tasks view"
tasks_applet="$(e2e_json viewAppletsData u "$view" | python3 -c '
import json, sys
print(next(a["id"] for a in json.load(sys.stdin) if a["plugin"] == "org.kde.latte.plasmoid"))')"
config_group=(--group Containments --group "$view" --group Applets --group "$tasks_applet" --group Configuration --group General)
launchers_key="$(python3 - "$E2E_LAYOUT" "$view" "$tasks_applet" <<'PY'
import re
import sys

header = f"[Containments][{sys.argv[2]}][Applets][{sys.argv[3]}][Configuration][General]"
inside = False
for line in open(sys.argv[1], encoding="utf-8"):
    line = line.rstrip("\n")
    if line.startswith("["):
        inside = line == header
    elif inside and re.match(r"launchers[0-9]*=", line):
        print(line.split("=", 1)[0])
        break
PY
)"
[[ -n "$launchers_key" ]] || e2e_fail "tasks applet $tasks_applet has no launcher-list key"

export XDG_DATA_HOME="$E2E_RT/sc-w1-data"
export SC_W1_LAUNCH_LOG="$E2E_RT/sc-w1-launches"
export SC_W1_PID_LOG="$E2E_RT/sc-w1-pids"
export SC_W1_QML="$(command -v qml)"
mkdir -p "$XDG_DATA_HOME/applications"
python3 - "$fixture/applications/$desktop_id" "$XDG_DATA_HOME/applications/$desktop_id" \
    "$(command -v bash)" "$fixture/launcher.sh" <<'PY'
import pathlib
import sys

text = pathlib.Path(sys.argv[1]).read_text()
pathlib.Path(sys.argv[2]).write_text(text.replace("@BASH@", sys.argv[3]).replace("@LAUNCHER@", sys.argv[4]))
PY
kbuildsycoca6 --noincremental >/dev/null 2>&1 || e2e_fail "fixture desktop-service cache failed"

launch_count() {
    [[ -f "$SC_W1_LAUNCH_LOG" ]] || { echo 0; return; }
    wc -l < "$SC_W1_LAUNCH_LOG" | tr -d ' '
}

window_count() {
    e2e_dumpwins | grep -c "|$window_title|" || true
}

active_window_title() {
    e2e_kwin_js 'print("@TAG@|" + (workspace.activeWindow ? workspace.activeWindow.caption : "none"));' | tail -1
}

target_state() {
    e2e_json viewTasksData u "$view" | python3 -c "
import json, sys
rows = [r for r in json.load(sys.stdin) if r['launcherUrl'] == '$launcher_url']
if any(not row['isLauncher'] and row['isActive'] for row in rows):
    print('active')
elif any(not row['isLauncher'] for row in rows):
    print('window')
elif rows:
    print('launcher')
else:
    print('missing')
"
}

wait_for_state() {
    local expected="$1" state="" i
    for ((i = 0; i < 40; i++)); do
        state="$(target_state)"
        [[ "$state" == "$expected" ]] && return 0
        sleep 0.25
    done
    echo "last target state: $state" >&2
    e2e_json viewTasksData u "$view" >&2
    e2e_dumpwins >&2
    return 1
}

assert_pure_launcher() {
    local rows
    rows="$(e2e_json viewTasksData u "$view")"
    python3 - "$rows" "$launcher_url" <<'PY' || e2e_fail "target is not the only pure launcher"
import json
import sys

rows = json.loads(sys.argv[1])
target = [row for row in rows if row["launcherUrl"] == sys.argv[2]]
assert len(rows) == 1 and len(target) == 1 and target[0]["isLauncher"]
PY
    [[ "$(window_count)" -eq 0 ]] || e2e_fail "fixture window exists before activation"
}

stop_fixture_app() {
    local pid
    pid="$(tail -1 "$SC_W1_PID_LOG" 2>/dev/null || true)"
    [[ -z "$pid" ]] || kill "$pid" 2>/dev/null || true
    wait_for_state launcher || e2e_fail "launched window did not return to a pure launcher"
    for _ in $(seq 1 20); do [[ "$(window_count)" -eq 0 ]] && return; sleep 0.25; done
    e2e_fail "fixture window survived process termination"
}

cleanup() {
    local pid
    if [[ -f "$SC_W1_PID_LOG" ]]; then
        while read -r pid; do kill "$pid" 2>/dev/null || true; done < "$SC_W1_PID_LOG"
    fi
    e2e_dock_stop >/dev/null 2>&1 || true
    cp "$backup" "$E2E_LAYOUT"
    rm -f "$backup"
}
trap cleanup EXIT

configure_mode() {
    local action="$1" scrolling="$2" manual="$3" config applet_width
    if kill -0 "$(e2e_dock_pid)" 2>/dev/null; then
        e2e_dock_stop || e2e_fail "could not stop dock for task-wheel mode $action/$scrolling/$manual"
    fi
    kwriteconfig6 --file "$E2E_LAYOUT" "${config_group[@]}" --key "$launchers_key" "$launcher_url"
    kwriteconfig6 --file "$E2E_LAYOUT" "${config_group[@]}" --key hoverAction 0
    kwriteconfig6 --file "$E2E_LAYOUT" "${config_group[@]}" --key animationLauncherBouncing false
    kwriteconfig6 --file "$E2E_LAYOUT" "${config_group[@]}" --key taskScrollAction "$action"
    kwriteconfig6 --file "$E2E_LAYOUT" "${config_group[@]}" --key scrollTasksEnabled "$scrolling"
    kwriteconfig6 --file "$E2E_LAYOUT" "${config_group[@]}" --key manualScrollTasksType "$manual"
    kwriteconfig6 --file "$E2E_LAYOUT" --group Containments --group "$view" --group General --key alignment 1
    kwriteconfig6 --file "$E2E_LAYOUT" --group Containments --group "$view" --group General --key alignmentUpgraded true
    e2e_dock_start || e2e_fail "dock did not settle for task-wheel mode $action/$scrolling/$manual"
    config="$(e2e_json appletConfigData uu "$view" "$tasks_applet")"
    if ! python3 - "$config" "$action" "$scrolling" "$manual" <<'PY'; then
import json
import sys

cfg = json.loads(sys.argv[1])["config"]
assert cfg["taskScrollAction"] == int(sys.argv[2])
assert cfg["scrollTasksEnabled"] == (sys.argv[3] == "true")
assert cfg["manualScrollTasksType"] == int(sys.argv[4])
PY
        e2e_fail "running task-wheel config does not match the requested mode"
    fi
    wait_for_state launcher || e2e_fail "fixture launcher never entered the task model"
    assert_pure_launcher
    applet_width="$(e2e_json viewAppletsData u "$view" | python3 -c "
import json, sys
print(next(a['geometry'][2] for a in json.load(sys.stdin) if a['id'] == $tasks_applet))
")"
    [[ -n "$launcher_span" ]] || launcher_span="$applet_width"
    if [[ "$scrolling" == true ]]; then
        (( applet_width >= launcher_span )) \
            || e2e_fail "manual-scroll fixture shrank the one-item tasks viewport into overflow"
    fi
}

settle_pointer() {
    local center winx
    winx="$(e2e_view_window_x "$view")"
    center="$({ e2e_json viewsData; e2e_json viewAppletsData u "$view"; } | python3 -c "
import json, sys
views, applets = (json.loads(line) for line in sys.stdin)
view = next(v for v in views if v['containmentId'] == $view)
applet = next(a for a in applets if a['id'] == $tasks_applet)
ax, ay = view['absoluteGeometry'][:2]
lx, ly = view['localGeometry'][:2]
px, py, pw, ph = applet['geometry']
print(int(${winx:-ax - lx} + px + $launcher_span / 2), int(ay - ly + py + ph / 2))
")" || e2e_fail "could not locate the live launcher row"
    read -r wheel_x wheel_y <<< "$center"
    [[ -n "$wheel_x" && -n "$wheel_y" ]] || e2e_fail "launcher center is empty"
    "$E2E_FAKEPOINTER" move "$wheel_x" 500
    sleep 0.3
    "$E2E_FAKEPOINTER" glide "$wheel_x" 500 "$wheel_x" "$wheel_y"
    sleep 0.8
}

expect_no_launch() {
    local label="$1" before
    shift
    before="$(launch_count)"
    "$@"
    sleep 1
    [[ "$(launch_count)" == "$before" ]] || e2e_fail "$label launched the fixture"
    assert_pure_launcher
    echo "ok: $label was a process/window/model no-op"
}

expect_launch() {
    local label="$1" expected pid count attempt poll
    shift
    expected=$(( $(launch_count) + 1 ))
    for attempt in 1 2 3; do
        "$@"
        for ((poll = 0; poll < 8; poll++)); do
            count="$(launch_count)"
            (( count >= expected )) && break
            sleep 0.25
        done
        (( count > expected )) && e2e_fail "$label produced $count launches, expected exactly $expected"
        (( count == expected )) && break
        settle_pointer
    done
    [[ "$(launch_count)" -eq "$expected" ]] || e2e_fail "$label produced $(launch_count) launches, expected exactly $expected"
    wait_for_state active || e2e_fail "$label did not activate the task-model window"
    pid="$(tail -1 "$SC_W1_PID_LOG")"
    kill -0 "$pid" 2>/dev/null || e2e_fail "$label launch process $pid is not alive"
    [[ "$(window_count)" -eq 1 ]] || e2e_fail "$label did not create exactly one fixture window"
    [[ "$(active_window_title)" == "$window_title" ]] || e2e_fail "$label did not activate the fixture window"
    sleep 0.6
    [[ "$(launch_count)" -eq "$expected" ]] || e2e_fail "$label crossed the rate limit and launched twice"
    echo "ok: $label reached launcher activation (pid $pid, active window, active model row)"
}

configure_mode 1 false 0
settle_pointer
expect_no_launch "below-threshold +90 angleDelta" "$E2E_FAKEPOINTER" wheel "$wheel_x" "$wheel_y" 90
expect_no_launch "ScrollTasks negative wheel" "$E2E_FAKEPOINTER" scroll "$wheel_x" "$wheel_y" -1 0
expect_launch "ScrollTasks positive two-detent burst" "$E2E_FAKEPOINTER" scroll "$wheel_x" "$wheel_y" 2 50
stop_fixture_app

configure_mode 2 false 0
settle_pointer
expect_no_launch "ScrollToggleMinimized negative wheel" "$E2E_FAKEPOINTER" scroll "$wheel_x" "$wheel_y" -1 0
expect_launch "ScrollToggleMinimized positive wheel" "$E2E_FAKEPOINTER" scroll "$wheel_x" "$wheel_y" 1 0
stop_fixture_app

configure_mode 0 false 0
settle_pointer
expect_no_launch "ScrollNone with manual scrolling disabled" "$E2E_FAKEPOINTER" scroll "$wheel_x" "$wheel_y" 1 0

configure_mode 0 true 2
settle_pointer
expect_launch "ScrollNone with manual scrolling enabled on a one-item non-overflow row" \
    "$E2E_FAKEPOINTER" scroll "$wheel_x" "$wheel_y" 1 0

echo "PASS: SC-W1 launcher wheel production path"
