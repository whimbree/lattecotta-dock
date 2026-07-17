#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# EX-15 live check 3 (docs/agent-logs/EX-15.md): wheel over a task icon
# with taskScrollAction enabled cycles the task's windows - one activation
# per detent-sized event past the threshold (the TaskMouseArea cutover
# through LatteCore.WheelStepper, DominantAxis pick, threshold 96). Two
# konsole windows group under one icon; each detent must hand activation
# to the OTHER window, wrapping A -> B -> A, asserted on KWin's
# activeWindow. No config flip: taskScrollAction defaults to ScrollTasks.
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"

view="$(e2e_tasks_view)" || e2e_fail "no tasks view"

#! the default-config premise, checked instead of assumed: an explicit
#! ScrollNone in the base config would make every wheel a silent no-op
tasks_applet="$(e2e_json viewAppletsData u "$view" | python3 -c '
import json, sys
print(next(a["id"] for a in json.load(sys.stdin) if a["plugin"] == "org.kde.latte.plasmoid"))')"
tsa="$(kreadconfig6 --file "$E2E_LAYOUT" --group Containments --group "$view" \
    --group Applets --group "$tasks_applet" --group Configuration --group General \
    --key taskScrollAction 2>/dev/null || true)"
[[ -z "$tsa" || "$tsa" == 1 ]] || e2e_fail "base config sets taskScrollAction=$tsa; this recipe expects the ScrollTasks default"

#! hover previews are OFF for this recipe: the icon dwell before each detent
#! exceeds previewsDelay, and the preview dialog mapping right at the wheel
#! moment disturbs the nested compositor's pointer focus the same way the
#! desktop switch does in 010 (dropped events). Previews have their own
#! recipes (parabolic-hover-preview, 040); this one is about the wheel.
orig_hover="$(kreadconfig6 --file "$E2E_LAYOUT" --group Containments --group "$view" \
    --group Applets --group "$tasks_applet" --group Configuration --group General \
    --key hoverAction 2>/dev/null || true)"
e2e_dock_stop || e2e_fail "could not stop the dock for the hover flip"
kwriteconfig6 --file "$E2E_LAYOUT" --group Containments --group "$view" \
    --group Applets --group "$tasks_applet" --group Configuration --group General \
    --key hoverAction 0
restore_config() {
    e2e_dock_stop >/dev/null 2>&1 || true
    if [[ -n "$orig_hover" ]]; then
        kwriteconfig6 --file "$E2E_LAYOUT" --group Containments --group "$view" \
            --group Applets --group "$tasks_applet" --group Configuration --group General \
            --key hoverAction "$orig_hover"
    else
        kwriteconfig6 --file "$E2E_LAYOUT" --group Containments --group "$view" \
            --group Applets --group "$tasks_applet" --group Configuration --group General \
            --key hoverAction --delete
    fi
}
e2e_dock_start || e2e_fail "dock did not come back after the hover flip"

active_window() {
    e2e_kwin_js 'print("@TAG@|" + (workspace.activeWindow ? workspace.activeWindow.internalId : "none"));' | tail -1
}

pids=()
cleanup() {
    local p
    for p in "${pids[@]}"; do kill "$p" 2>/dev/null; done
    restore_config
}
trap cleanup EXIT

konsole_windows() { e2e_dumpwins | grep -c '|org.kde.konsole|' || true; }

[[ "$(konsole_windows)" -eq 0 ]] || e2e_fail "konsole windows already present; this recipe owns its clients"
for _ in 1 2; do
    setsid konsole >/dev/null 2>&1 &
    pids+=($!)
    sleep 2
done
for _ in $(seq 1 20); do
    [[ "$(konsole_windows)" -ge 2 ]] && break
    sleep 1
done
[[ "$(konsole_windows)" -ge 2 ]] || e2e_fail "two konsole windows never mapped"
sleep 2   #! let the grouped task settle into the bar

grouped="$(e2e_json viewTasksData u "$view" | python3 -c '
import json, sys
t = [x for x in json.load(sys.stdin) if x["appId"] == "org.kde.konsole.desktop"]
print(t[0]["isGrouped"] and t[0]["childCount"] == 2 if t else False)')"
[[ "$grouped" == "True" ]] || e2e_fail "konsole task did not group two windows"

read -r kx ky <<< "$(e2e_task_center "$view" org.kde.konsole.desktop)"
[[ -n "$kx" ]] || e2e_fail "could not locate the konsole task icon"

#! settle the pointer on the icon from outside (see 010's header: an axis
#! event racing its own enter is dropped by the nested compositor)
"$E2E_FAKEPOINTER" move "$kx" 500; sleep 0.3
"$E2E_FAKEPOINTER" move "$kx" "$ky"; sleep 0.8

a="$(active_window)"
[[ -n "$a" && "$a" != "none" ]] || e2e_fail "no active window to cycle from"

#! detents spaced past TaskMouseArea's 200ms scrollDelayer block, so each
#! event is entitled to exactly one cycle step
"$E2E_FAKEPOINTER" scroll "$kx" "$ky" -1 100; sleep 0.8
b="$(active_window)"
[[ -n "$b" && "$b" != "$a" ]] || e2e_fail "first detent did not activate the other window (still $a)"

"$E2E_FAKEPOINTER" scroll "$kx" "$ky" -1 100; sleep 0.8
c="$(active_window)"
[[ "$c" == "$a" ]] || e2e_fail "second detent did not wrap back (got $c, expected $a)"

echo "task wheel cycled A -> B -> A over the grouped konsole icon ($a / $b)"
