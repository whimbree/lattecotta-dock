#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# Open the imported Global Menu through its real QMenu path, activate a real
# com.canonical.dbusmenu action, and require focus to return to the exact
# application that owned it before the panel's transient menu took focus. The
# stock QMenu path remains outside Latte's containment focus session and lets
# Qt restore its transient-parent focus directly.
set -uo pipefail

source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"

fixture="$E2E_REPO/tests/e2e/fixtures/d21"
client="$E2E_BUILD/bin/latte-test-panel-focus-menu-client"
platform_theme="${LATTE_PLASMA_PLATFORM_THEME_PLUGIN:?run inside the pinned development shell}"
scratch="$(mktemp -d /tmp/latte-global-menu-focus.XXXXXX)"
backup="$(mktemp -d /tmp/latte-global-menu-backup.XXXXXX)"
app_pid=0
kded_pid=0
monitor_pid=0
backup_ready=false
recipe_finalized=false

cleanup() {
    local body_status=$? cleanup_failed=0
    trap - EXIT
    if (( monitor_pid != 0 )); then
        kill "$monitor_pid" 2>/dev/null || true
        wait "$monitor_pid" 2>/dev/null || true
    fi
    if (( app_pid != 0 )); then
        kill "$app_pid" 2>/dev/null || true
        wait "$app_pid" 2>/dev/null || true
    fi
    if (( kded_pid != 0 )); then
        kill "$kded_pid" 2>/dev/null || true
        wait "$kded_pid" 2>/dev/null || true
    fi
    if (( body_status != 0 )); then
        cp "$scratch/client.log" "$E2E_ARTIFACTS/114-global-menu-client.log" 2>/dev/null || true
        cp "$scratch/menu-bus.log" "$E2E_ARTIFACTS/114-global-menu-bus.log" 2>/dev/null || true
        cp "$scratch/kded.log" "$E2E_ARTIFACTS/114-global-menu-kded.log" 2>/dev/null || true
    fi
    if [[ "$recipe_finalized" != true && "$backup_ready" == true ]]; then
        e2e_dock_stop >/dev/null 2>&1 || cleanup_failed=1
        rm -rf "${E2E_CONFIG_HOME:?}" || cleanup_failed=1
        mkdir -p "$E2E_CONFIG_HOME" || cleanup_failed=1
        cp -a "$backup/config/." "$E2E_CONFIG_HOME/" || cleanup_failed=1
        diff -qr "$backup/config" "$E2E_CONFIG_HOME" >/dev/null \
            || cleanup_failed=1
    fi
    rm -rf "$scratch" "$backup" || cleanup_failed=1
    if (( cleanup_failed != 0 )); then
        echo "FAIL: Global Menu focus fixture cleanup left residue" >&2
        body_status=1
    fi
    exit "$body_status"
}
trap cleanup EXIT

[[ -x "$client" ]] || e2e_fail "built Global Menu focus client is missing"
[[ -f "$platform_theme" ]] || e2e_fail "pinned Plasma platform-theme plugin is missing"
[[ -f "$fixture/D21.layout.latte" && -f "$fixture/kdeglobals" ]] \
    || e2e_fail "D21 Global Menu fixture is incomplete"

mkdir -p "$backup/config" || e2e_fail "could not create the config backup"
cp -a "$E2E_CONFIG_HOME/." "$backup/config/" \
    || e2e_fail "could not back up the nested config"
backup_ready=true

e2e_dock_stop || e2e_fail "could not stop the dock before staging Global Menu"
rm -f "$E2E_CONFIG_HOME"/latte/*.layout.latte
cp "$fixture/D21.layout.latte" "$E2E_CONFIG_HOME/latte/D21.layout.latte" \
    || e2e_fail "could not stage the Global Menu layout"
cp "$fixture/kdeglobals" "$E2E_CONFIG_HOME/kdeglobals" \
    || e2e_fail "could not stage the Global Menu color scheme"

python3 - "$E2E_CONFIG_HOME/lattedockrc" <<'PY' \
    || e2e_fail "could not select the Global Menu fixture layout"
import configparser
import sys

path = sys.argv[1]
config = configparser.RawConfigParser()
config.optionxform = str
config.read(path)
if not config.has_section("UniversalSettings"):
    config.add_section("UniversalSettings")
config.set("UniversalSettings", "singleModeLayoutName", "D21")
config.set("UniversalSettings", "memoryUsage", "0")
with open(path, "w", encoding="utf-8") as output:
    config.write(output, space_around_delimiters=False)
PY

python3 - "$E2E_CONFIG_HOME/latte/D21.layout.latte" <<'PY' \
    || e2e_fail "could not reduce the fixture to one Global Menu applet"
import configparser
import sys

path = sys.argv[1]
config = configparser.RawConfigParser()
config.optionxform = str
config.read(path)
applet = "Containments][1][Applets][5"
if not config.has_section(applet):
    config.add_section(applet)
config.set(applet, "immutability", "1")
config.set(applet, "plugin", "org.kde.plasma.appmenu")
for section in list(config.sections()):
    if section.startswith("Containments][1][Applets][") and section != applet:
        config.remove_section(section)
config.set("Containments][1][General", "appletOrder", "5")
with open(path, "w", encoding="utf-8") as output:
    config.write(output, space_around_delimiters=False)
PY

e2e_dock_start 90 || e2e_fail "dock did not settle with the Global Menu fixture"

kded6 >"$scratch/kded.log" 2>&1 &
kded_pid=$!
for _ in $(seq 1 80); do
    busctl --user list --no-pager \
        | grep -q '^com\.canonical\.AppMenu\.Registrar' && break
    sleep 0.05
done
busctl --user list --no-pager \
    | grep -q '^com\.canonical\.AppMenu\.Registrar' \
    || e2e_fail "nested KDED did not register the Global Menu registrar"

mkdir -p "$scratch/qt-plugins/platformthemes" \
    || e2e_fail "could not create the private platform-theme plugin tree"
ln -s "$platform_theme" \
    "$scratch/qt-plugins/platformthemes/KDEPlasmaPlatformTheme6.so" \
    || e2e_fail "could not stage the pinned Plasma platform-theme plugin"

KDE_FULL_SESSION=true XDG_CURRENT_DESKTOP=KDE QT_QPA_PLATFORMTHEME=kde \
    QT_PLUGIN_PATH="$scratch/qt-plugins" WAYLAND_DEBUG=client \
    "$client" >"$scratch/client.log" 2>&1 &
app_pid=$!

active_caption() {
    e2e_kwin_js 'const active = workspace.activeWindow;
        print("@TAG@|" + (active ? active.caption : ""));' 0.05 | tail -1
}

active_window_id() {
    e2e_kwin_js 'const active = workspace.activeWindow;
        print("@TAG@|" + (active ? active.internalId : ""));' 0.05 | tail -1
}

wayland_keyboard_event_count() {
    local event="$1"
    grep -cE "wl_keyboard#[0-9]+\\.$event" "$scratch/client.log" \
        || true
}

activate_client() {
    e2e_kwin_js 'for (const window of workspace.windowList()) {
        if (window.caption === "LATTE PANEL FOCUS MENU CLIENT") {
            workspace.activeWindow = window;
            print("@TAG@|" + window.internalId);
        }
    }' 0.05 | tail -1
}

for _ in $(seq 1 80); do
    [[ "$(active_caption)" == "LATTE PANEL FOCUS MENU CLIENT" ]] && break
    activate_client >/dev/null
    sleep 0.05
done
[[ "$(active_caption)" == "LATTE PANEL FOCUS MENU CLIENT" ]] \
    || e2e_fail "controlled Global Menu application never became active"
client_window_id="$(active_window_id)"
[[ -n "$client_window_id" ]] \
    || e2e_fail "controlled Global Menu application has no KWin identity"

published_line=""
for _ in $(seq 1 80); do
    published_line="$(grep -m1 -E 'org_kde_kwin_appmenu#[0-9]+\.set_address' \
        "$scratch/client.log" || true)"
    [[ -n "$published_line" ]] && break
    sleep 0.05
done
[[ -n "$published_line" ]] \
    || e2e_fail "Qt did not publish the controlled menu on its Wayland surface"

cid="$(e2e_json viewsData | python3 -c 'import json,sys
views = [view for view in json.load(sys.stdin)
         if view["edge"] in ("top", "bottom")]
print(views[0]["containmentId"] if views else "")')"
[[ -n "$cid" ]] || e2e_fail "Global Menu fixture has no horizontal panel"

panel_focus_state() {
    e2e_json viewsData | python3 -c 'import json,sys
cid = int(sys.argv[1])
view = next(view for view in json.load(sys.stdin)
            if view["containmentId"] == cid)
print("%s %s" % (
    str(view["containmentAcceptsInput"]).lower(),
    str(view["ownsPanelFocusSession"]).lower()))' "$cid"
}

read -r click_x click_y <<<"$({
    e2e_json viewsData
    e2e_json viewAppletsData u "$cid"
} | python3 -c '
import json
import sys

views = json.loads(sys.stdin.readline())
applets = json.loads(sys.stdin.readline())
view = next(view for view in views if view["containmentId"] == int(sys.argv[1]))
applet = next(applet for applet in applets
              if applet["plugin"] == "org.kde.plasma.appmenu")
absolute_x, absolute_y, _, _ = view["absoluteGeometry"]
local_x, local_y, _, _ = view["localGeometry"]
x, y, width, height = applet["geometry"]
assert width > 0 and height > 0
print(absolute_x - local_x + x + min(20, width // 2),
      absolute_y - local_y + y + height // 2)
' "$cid")" || e2e_fail "could not resolve the Global Menu click point"

dbus-monitor --session "interface='com.canonical.dbusmenu'" \
    >"$scratch/menu-bus.log" 2>&1 &
monitor_pid=$!
sleep 0.1
keyboard_leaves_before="$(wayland_keyboard_event_count leave)"
keyboard_enters_before="$(wayland_keyboard_event_count enter)"
"$E2E_FAKEPOINTER" click "$click_x" "$click_y" \
    || e2e_fail "could not open the real Global Menu QMenu"
for _ in $(seq 1 80); do
    (( $(wayland_keyboard_event_count leave) > keyboard_leaves_before )) && break
    sleep 0.05
done
(( $(wayland_keyboard_event_count leave) > keyboard_leaves_before )) \
    || e2e_fail "Global Menu QMenu never took keyboard focus from the application"
[[ "$(panel_focus_state)" == "false false" ]] \
    || e2e_fail "stock Global Menu QMenu unexpectedly entered Latte's panel focus session"

# The top-panel fixture opens the imported QMenu directly below its File
# delegate. Click the only child action through that real popup instead of
# relying on virtual-keyboard navigation inside a transient surface.
action_x=$((click_x + 35))
action_y=$((click_y + 40))
"$E2E_FAKEPOINTER" click "$action_x" "$action_y" \
    || e2e_fail "could not activate the imported Global Menu action"
for _ in $(seq 1 80); do
    grep -q 'PANEL_FOCUS_ACTION_TRIGGERED' "$scratch/client.log" && break
    sleep 0.05
done
grep -q 'PANEL_FOCUS_ACTION_TRIGGERED' "$scratch/client.log" \
    || e2e_fail "the real Global Menu action did not fire in its application"

sleep 0.2
kill "$monitor_pid" 2>/dev/null || true
wait "$monitor_pid" 2>/dev/null || true
monitor_pid=0
grep -q 'member=Event' "$scratch/menu-bus.log" \
    || e2e_fail "the action did not cross com.canonical.dbusmenu.Event"

for _ in $(seq 1 80); do
    (( $(wayland_keyboard_event_count enter) > keyboard_enters_before )) && break
    sleep 0.05
done
(( $(wayland_keyboard_event_count enter) > keyboard_enters_before )) \
    || e2e_fail "Global Menu close did not return keyboard focus to the application"
[[ "$(active_window_id)" == "$client_window_id" ]] \
    || e2e_fail "Global Menu close did not restore the exact application"
[[ "$(panel_focus_state)" == "false false" ]] \
    || e2e_fail "Global Menu close left Latte's panel focus session active"

"$E2E_FAKEPOINTER" key F12 \
    || e2e_fail "could not inject the post-menu focus probe"
for _ in $(seq 1 80); do
    grep -q 'PANEL_FOCUS_KEY_DELIVERED' "$scratch/client.log" && break
    sleep 0.05
done
grep -q 'PANEL_FOCUS_KEY_DELIVERED' "$scratch/client.log" \
    || e2e_fail "the restored application did not receive keyboard input"

e2e_dock_stop || e2e_fail "Global Menu fixture dock did not stop cleanly"
rm -rf "$E2E_CONFIG_HOME" || e2e_fail "could not clear the fixture config"
mkdir -p "$E2E_CONFIG_HOME" || e2e_fail "could not recreate the config directory"
cp -a "$backup/config/." "$E2E_CONFIG_HOME/" \
    || e2e_fail "could not restore the nested config"
diff -qr "$backup/config" "$E2E_CONFIG_HOME" >/dev/null \
    || e2e_fail "restored nested config differs from its backup"
recipe_finalized=true

echo "PASS: Global Menu action restored exact application focus"
