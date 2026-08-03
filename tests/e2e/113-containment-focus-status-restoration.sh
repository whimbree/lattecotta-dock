#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# Plasma PanelView preserves the active application while a containment holds
# AcceptingInputStatus. Drive the same contract through a deterministic test
# applet: external application focus ends without stealing focus back, Passive
# restores, Active keeps the current focus, and the status and keyboard-
# navigation reasons share one session without ending it early.
set -uo pipefail

source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"

fixture="$E2E_REPO/tests/e2e/fixtures/panel-focus"
plugin="org.kde.latte.panel-focus-fixture"
fixture_data="$E2E_RT/panel-focus-data"
backup="$(mktemp -d /tmp/latte-panel-focus-backup.XXXXXX)"
scratch="$(mktemp -d /tmp/latte-panel-focus-client.XXXXXX)"
declare -A client_pids=()
backup_ready=false
recipe_finalized=false
launched_client_id=""

cleanup() {
    local body_status=$? cleanup_failed=0 label pid=""
    trap - EXIT
    for label in "${!client_pids[@]}"; do
        pid="${client_pids[$label]}"
        if [[ -n "$pid" ]]; then
            kill "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
        fi
    done
    if [[ "$recipe_finalized" != true && "$backup_ready" == true ]]; then
        pid="$(e2e_dock_pid 2>/dev/null)"
        if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
            e2e_dock_stop >/dev/null 2>&1 || cleanup_failed=1
        fi
        rm -rf "$E2E_CONFIG_HOME" || cleanup_failed=1
        mkdir -p "$E2E_CONFIG_HOME" || cleanup_failed=1
        cp -a "$backup/config/." "$E2E_CONFIG_HOME/" || cleanup_failed=1
        diff -qr "$backup/config" "$E2E_CONFIG_HOME" >/dev/null \
            || cleanup_failed=1
        rm -rf "$fixture_data" || cleanup_failed=1
    fi
    rm -rf "$backup" "$scratch" || cleanup_failed=1
    if (( cleanup_failed != 0 )); then
        echo "FAIL: panel-focus fixture cleanup left residue" >&2
        body_status=1
    fi
    exit "$body_status"
}
trap cleanup EXIT

[[ -f "$fixture/PanelFocus.layout.latte" \
    && -f "$fixture/plasmoids/$plugin/metadata.json" \
    && -f "$fixture/plasmoids/$plugin/contents/ui/main.qml" ]] \
    || e2e_fail "panel-focus fixture is incomplete"

mkdir -p "$backup/config" || e2e_fail "could not create the config backup"
cp -a "$E2E_CONFIG_HOME/." "$backup/config/" \
    || e2e_fail "could not back up the nested config"
backup_ready=true

e2e_dock_stop || e2e_fail "could not stop the vehicle before staging panel focus"
export XDG_DATA_HOME="$fixture_data"
mkdir -p "$XDG_DATA_HOME/plasma/plasmoids" \
    || e2e_fail "could not create the fixture data tree"
cp -a "$fixture/plasmoids/." "$XDG_DATA_HOME/plasma/plasmoids/" \
    || e2e_fail "could not stage the panel-focus applet"
rm -f "$E2E_CONFIG_HOME"/latte/*.layout.latte
cp "$fixture/PanelFocus.layout.latte" \
    "$E2E_CONFIG_HOME/latte/PanelFocus.layout.latte" \
    || e2e_fail "could not stage the panel-focus layout"

python3 - "$E2E_CONFIG_HOME/lattedockrc" <<'PY' \
    || e2e_fail "could not select the panel-focus fixture layout"
import configparser
import sys

path = sys.argv[1]
config = configparser.RawConfigParser()
config.optionxform = str
config.read(path)
if not config.has_section("UniversalSettings"):
    config.add_section("UniversalSettings")
config.set("UniversalSettings", "singleModeLayoutName", "PanelFocus")
config.set("UniversalSettings", "memoryUsage", "0")
with open(path, "w") as output:
    config.write(output, space_around_delimiters=False)
PY

cat > "$scratch/key-client.qml" <<'EOF'
import QtQuick

Window {
    id: window
    readonly property string label: Qt.application.arguments[Qt.application.arguments.length - 1]
    visible: true
    width: 700
    height: 500
    title: "LATTE PANEL FOCUS " + label + " READY"
    color: "#334455"

    Item {
        anchors.fill: parent
        focus: true
        Keys.onPressed: (event) => {
            window.title = "LATTE PANEL FOCUS " + window.label + " KEY " + event.key
            event.accepted = true
        }
    }
}
EOF

window_id_for_caption() {
    local expected="$1"
    e2e_kwin_js 'for (const window of workspace.windowList()) {
        if (window.caption === "'"$expected"'") {
            print("@TAG@|" + window.internalId);
        }
    }' 0.05 | tail -1
}

window_caption() {
    local id="$1"
    e2e_kwin_js 'for (const window of workspace.windowList()) {
        if (String(window.internalId) === "'"$id"'") {
            print("@TAG@|" + window.caption);
        }
    }' 0.05 | tail -1
}

activate_window() {
    local id="$1"
    e2e_kwin_js 'for (const window of workspace.windowList()) {
        if (String(window.internalId) === "'"$id"'") {
            window.minimized = false;
            workspace.activeWindow = window;
        }
    }' 0.05 >/dev/null
}

active_window_id() {
    e2e_kwin_js 'const window = workspace.activeWindow;
        if (window) {
            print("@TAG@|" + window.internalId);
        }' 0.05 | tail -1
}

wait_for_active_window() {
    local expected="$1"
    for _ in $(seq 1 80); do
        [[ "$(active_window_id)" == "$expected" ]] && return 0
        sleep 0.05
    done
    return 1
}

activate_window_and_require() {
    local id="$1" boundary="$2"
    activate_window "$id"
    wait_for_active_window "$id" \
        || e2e_fail "$boundary application did not become active"
}

set_window_minimized() {
    local id="$1" minimized="$2"
    e2e_kwin_js 'for (const window of workspace.windowList()) {
        if (String(window.internalId) === "'"$id"'") {
            window.minimized = '"$minimized"';
        }
    }' 0.05 >/dev/null
}

window_is_minimized() {
    local id="$1"
    e2e_kwin_js 'for (const window of workspace.windowList()) {
        if (String(window.internalId) === "'"$id"'") {
            print("@TAG@|" + window.minimized);
        }
    }' 0.05 | tail -1
}

launch_client() {
    local label="$1" expected id=""
    expected="LATTE PANEL FOCUS $label READY"
    qml "$scratch/key-client.qml" -- "$label" >"$scratch/client-$label.log" 2>&1 &
    client_pids[$label]=$!
    for _ in $(seq 1 100); do
        id="$(window_id_for_caption "$expected")"
        [[ -n "$id" ]] && break
        sleep 0.05
    done
    [[ -n "$id" ]] || e2e_fail "controlled key client $label never mapped"
    launched_client_id="$id"
}

send_key_and_require_delivery() {
    local id="$1" key="$2" boundary="$3" before after
    before="$(window_caption "$id")"
    "$E2E_FAKEPOINTER" key "$key" || e2e_fail "$boundary could not inject $key"
    for _ in $(seq 1 80); do
        after="$(window_caption "$id")"
        [[ "$after" != "$before" ]] && return 0
        sleep 0.05
    done
    e2e_fail "$boundary did not deliver $key to client $id"
}

send_key_and_require_saved_client_unchanged() {
    local id="$1" key="$2" boundary="$3" before after
    before="$(window_caption "$id")"
    "$E2E_FAKEPOINTER" key "$key" || e2e_fail "$boundary could not inject $key"
    sleep 0.3
    after="$(window_caption "$id")"
    [[ "$after" == "$before" ]] \
        || e2e_fail "$boundary sent $key to the saved application"
}

keyboard_navigation() {
    e2e_json viewsData | python3 -c 'import json,sys
cid = int(sys.argv[1])
view = next(view for view in json.load(sys.stdin)
            if view["containmentId"] == cid)
print("true" if view["keyboardNavigation"] else "false")' "$cid"
}

wait_for_keyboard_navigation() {
    local expected="$1"
    for _ in $(seq 1 80); do
        [[ "$(keyboard_navigation)" == "$expected" ]] && return 0
        sleep 0.05
    done
    return 1
}

panel_focus_state() {
    e2e_json viewsData | python3 -c 'import json,sys
cid = int(sys.argv[1])
view = next(view for view in json.load(sys.stdin)
            if view["containmentId"] == cid)
print("%s %s %s" % (
    str(view["containmentAcceptsInput"]).lower(),
    str(view["keyboardNavigation"]).lower(),
    str(view["ownsPanelFocusSession"]).lower()))' "$cid"
}

wait_for_panel_focus_state() {
    local expected="$1"
    for _ in $(seq 1 80); do
        [[ "$(panel_focus_state)" == "$expected" ]] && return 0
        sleep 0.05
    done
    return 1
}

set_keyboard_navigation() {
    local enabled="$1" boundary="$2"
    e2e_call setViewKeyboardNavigation ub "$cid" "$enabled" >/dev/null \
        || e2e_fail "$boundary D-Bus call failed"
    wait_for_keyboard_navigation "$enabled" \
        || e2e_fail "$boundary did not reach keyboardNavigation=$enabled"
}

e2e_dock_start 90 || e2e_fail "dock did not settle with the panel-focus fixture"
cid="$(e2e_json viewsData | python3 -c 'import json,sys
views = json.load(sys.stdin)
print(views[0]["containmentId"] if len(views) == 1 else "")')"
[[ -n "$cid" ]] || e2e_fail "panel-focus fixture did not create exactly one view"

read -r accepting_x target_y approach_x approach_y <<<"$({
    e2e_json viewsData
    e2e_json viewAppletsData u "$cid"
} | python3 -c "
import json, sys
views = json.loads(sys.stdin.readline())
applets = json.loads(sys.stdin.readline())
view = next(view for view in views if view['containmentId'] == $cid)
applet = next(applet for applet in applets if applet['plugin'] == '$plugin')
origin_x = view['absoluteGeometry'][0] - view['localGeometry'][0]
origin_y = view['absoluteGeometry'][1] - view['localGeometry'][1]
x, y, width, height = applet['geometry']
print(round(origin_x + x + width / 6),
      round(origin_y + y + height / 2),
      view['screenGeometry'][0] + view['screenGeometry'][2] // 2,
      view['screenGeometry'][1] + view['screenGeometry'][3] // 2)
")" || e2e_fail "could not resolve the AcceptingInput status region"

click_accepting_input() {
    "$E2E_FAKEPOINTER" move "$approach_x" "$approach_y" \
        || e2e_fail "could not approach the AcceptingInput status region"
    "$E2E_FAKEPOINTER" click "$accepting_x" "$target_y" \
        || e2e_fail "could not click the AcceptingInput status region"
}

begin_accepting_input() {
    click_accepting_input
    wait_for_panel_focus_state "true false true" \
        || e2e_fail "AcceptingInput did not acquire the panel focus session; state=$(panel_focus_state)"
}

set_status_from_focused_panel() {
    local status="$1" key boundary="$2"
    case "$status" in
        passive) key=F8 ;;
        active) key=F9 ;;
        *) e2e_fail "unknown focused-panel status $status" ;;
    esac
    "$E2E_FAKEPOINTER" key "$key" \
        || e2e_fail "$boundary could not inject $key"
    sleep 0.25
}

launch_client A
client_a="$launched_client_id"
launch_client B
client_b="$launched_client_id"

activate_window_and_require "$client_a" "external-focus baseline"
send_key_and_require_delivery "$client_a" a "external-focus baseline"
begin_accepting_input
send_key_and_require_saved_client_unchanged "$client_a" Right "external-focus grant"
activate_window_and_require "$client_b" "external-focus winner"
wait_for_panel_focus_state "false false false" \
    || e2e_fail "external application focus did not discard the containment session"
send_key_and_require_delivery "$client_b" b "external-focus winner"
e2e_call setViewKeyboardNavigation ub "$cid" false >/dev/null \
    || e2e_fail "non-stealing idempotent exit call failed"
wait_for_panel_focus_state "false false false" \
    || e2e_fail "idempotent exit changed the discarded containment session"
send_key_and_require_delivery "$client_b" c "non-stealing idempotent exit"
echo "ok: external application focus ended containment input without stealing focus back"

activate_window_and_require "$client_a" "Passive baseline"
send_key_and_require_delivery "$client_a" c "Passive baseline"
begin_accepting_input
send_key_and_require_saved_client_unchanged "$client_a" Right "AcceptingInput focus"
set_status_from_focused_panel passive "Passive transition"
wait_for_panel_focus_state "false false false" \
    || e2e_fail "Passive transition did not release the panel focus session"
wait_for_active_window "$client_a" \
    || e2e_fail "Passive transition did not reactivate the saved application"
send_key_and_require_delivery "$client_a" d "Passive restoration"
echo "ok: AcceptingInput to Passive restored actual key delivery"

activate_window_and_require "$client_a" "Active baseline"
send_key_and_require_delivery "$client_a" e "Active baseline"
begin_accepting_input
send_key_and_require_saved_client_unchanged "$client_a" Left "Active focus precondition"
set_window_minimized "$client_a" true
set_status_from_focused_panel active "Active transition"
wait_for_panel_focus_state "false false false" \
    || e2e_fail "Active transition did not discard the panel focus session"
sleep 0.4
[[ "$(window_is_minimized "$client_a")" == true ]] \
    || e2e_fail "AcceptingInput to Active reactivated the saved application"
echo "ok: AcceptingInput to Active discarded the saved application target"

activate_window_and_require "$client_a" "Passive coexistence baseline"
send_key_and_require_delivery "$client_a" f "Passive coexistence baseline"
begin_accepting_input
set_keyboard_navigation true "Passive coexistence enter"
set_status_from_focused_panel passive "Passive coexistence transition"
wait_for_panel_focus_state "false true true" \
    || e2e_fail "Passive ended the shared session before keyboard navigation"
[[ "$(keyboard_navigation)" == true ]] \
    || e2e_fail "Passive status ended the independent keyboard-navigation reason"
send_key_and_require_saved_client_unchanged "$client_a" Down "Passive coexistence"
set_keyboard_navigation false "Passive coexistence final exit"
wait_for_panel_focus_state "false false false" \
    || e2e_fail "final keyboard exit did not release the shared session"
wait_for_active_window "$client_a" \
    || e2e_fail "Passive coexistence exit did not reactivate the saved application"
send_key_and_require_delivery "$client_a" g "Passive coexistence restoration"
echo "ok: Passive preserved the shared target until keyboard navigation ended"

activate_window_and_require "$client_a" "Active coexistence baseline"
send_key_and_require_delivery "$client_a" h "Active coexistence baseline"
begin_accepting_input
set_keyboard_navigation true "Active coexistence enter"
set_window_minimized "$client_a" true
set_status_from_focused_panel active "Active coexistence transition"
wait_for_panel_focus_state "false true true" \
    || e2e_fail "Active ended the shared session before keyboard navigation"
[[ "$(keyboard_navigation)" == true ]] \
    || e2e_fail "Active status ended the independent keyboard-navigation reason"
send_key_and_require_saved_client_unchanged "$client_a" Up "Active coexistence"
set_keyboard_navigation false "Active coexistence final exit"
wait_for_panel_focus_state "false false false" \
    || e2e_fail "final keyboard exit did not discard the shared session"
sleep 0.4
[[ "$(window_is_minimized "$client_a")" == true ]] \
    || e2e_fail "final keyboard exit reused the target invalidated by Active status"
echo "ok: Active invalidated the target while keyboard navigation kept focus"

e2e_dock_stop || e2e_fail "panel-focus fixture dock did not stop cleanly"
rm -rf "$E2E_CONFIG_HOME" || e2e_fail "could not clear the fixture config"
mkdir -p "$E2E_CONFIG_HOME" || e2e_fail "could not recreate the config directory"
cp -a "$backup/config/." "$E2E_CONFIG_HOME/" \
    || e2e_fail "could not restore the nested config"
diff -qr "$backup/config" "$E2E_CONFIG_HOME" >/dev/null \
    || e2e_fail "restored nested config differs from its backup"
rm -rf "$fixture_data" || e2e_fail "could not remove the fixture data"
recipe_finalized=true

echo "PASS: containment focus status restoration and coexistence"
