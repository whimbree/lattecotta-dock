#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# Keyboard navigation gives the dock layer surface keyboard focus. Pin the
# complete focus-return session with actual key delivery: explicit QML and D-Bus
# exits restore the saved client, external focus loss does not steal focus
# back, another dock cannot consume the session, and destroyed clients/views
# do not leave stale focus state.
set -uo pipefail

source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"

scratch="$(mktemp -d /tmp/latte-kbnav-focus.XXXXXX)"
declare -A client_pids=()
duplicate_cid=""
launched_client_id=""

cleanup() {
    local status=$? label pid
    trap - EXIT
    for label in "${!client_pids[@]}"; do
        pid="${client_pids[$label]}"
        if [[ -n "$pid" ]]; then
            kill "$pid" 2>/dev/null || true
            wait "$pid" 2>/dev/null || true
        fi
    done
    if [[ -n "$duplicate_cid" ]]; then
        e2e_call setViewKeyboardNavigation ub "$duplicate_cid" false >/dev/null 2>&1 || true
        e2e_call removeView u "$duplicate_cid" >/dev/null 2>&1 || true
    fi
    rm -rf "$scratch"
    exit "$status"
}
trap cleanup EXIT

cat > "$scratch/key-client.qml" <<'EOF'
import QtQuick

Window {
    id: window
    readonly property string label: Qt.application.arguments[Qt.application.arguments.length - 1]
    visible: true
    width: 700
    height: 500
    title: "LATTE KBNAV " + label + " READY"
    color: "#334455"

    Item {
        anchors.fill: parent
        focus: true
        Keys.onPressed: (event) => {
            window.title = "LATTE KBNAV " + window.label + " KEY " + event.key
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

window_exists() {
    local id="$1"
    e2e_kwin_js 'for (const window of workspace.windowList()) {
        if (String(window.internalId) === "'"$id"'") {
            print("@TAG@|yes");
        }
    }' 0.05 | tail -1 | grep -qx yes
}

activate_window() {
    local id="$1"
    e2e_kwin_js 'for (const window of workspace.windowList()) {
        if (String(window.internalId) === "'"$id"'") {
            workspace.activeWindow = window;
        }
    }' 0.05 >/dev/null
}

launch_client() {
    local label="$1" expected id=""
    expected="LATTE KBNAV $label READY"
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

stop_client() {
    local label="$1" id="$2" pid
    pid="${client_pids[$label]}"
    kill "$pid" 2>/dev/null || true
    wait "$pid" 2>/dev/null || true
    client_pids[$label]=""
    for _ in $(seq 1 100); do
        window_exists "$id" || return 0
        sleep 0.05
    done
    e2e_fail "controlled key client $label remained mapped after exit"
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
    local client_id="$1" key="$2" boundary="$3" before after
    before="$(window_caption "$client_id")"
    "$E2E_FAKEPOINTER" key "$key" || e2e_fail "$boundary could not inject $key"
    sleep 0.3
    after="$(window_caption "$client_id")"
    [[ "$after" == "$before" ]] \
        || e2e_fail "$boundary sent $key to the saved client instead of the dock"
}

keyboard_navigation() {
    local cid="$1"
    e2e_json viewsData | python3 -c 'import json,sys
cid = int(sys.argv[1])
matches = [view for view in json.load(sys.stdin)
           if view["containmentId"] == cid]
print("missing" if not matches else
      ("true" if matches[0]["keyboardNavigation"] else "false"))' "$cid"
}

wait_for_keyboard_navigation() {
    local cid="$1" expected="$2"
    for _ in $(seq 1 100); do
        [[ "$(keyboard_navigation "$cid")" == "$expected" ]] && return 0
        sleep 0.05
    done
    return 1
}

panel_focus_session_owner() {
    local cid="$1"
    e2e_json viewsData | python3 -c 'import json,sys
cid = int(sys.argv[1])
matches = [view for view in json.load(sys.stdin)
           if view["containmentId"] == cid]
print("missing" if not matches else
      ("true" if matches[0]["ownsPanelFocusSession"] else "false"))' "$cid"
}

wait_for_panel_focus_session_owner() {
    local cid="$1" expected="$2"
    for _ in $(seq 1 100); do
        [[ "$(panel_focus_session_owner "$cid")" == "$expected" ]] && return 0
        sleep 0.05
    done
    return 1
}

enter_keyboard_navigation() {
    local cid="$1" boundary="$2"
    e2e_call setViewKeyboardNavigation ub "$cid" true >/dev/null \
        || e2e_fail "$boundary enter call failed"
    wait_for_keyboard_navigation "$cid" true \
        || e2e_fail "$boundary did not enter keyboard navigation"
    wait_for_panel_focus_session_owner "$cid" true \
        || e2e_fail "$boundary did not acquire the panel focus session"
}

exit_keyboard_navigation() {
    local cid="$1" boundary="$2"
    e2e_call setViewKeyboardNavigation ub "$cid" false >/dev/null \
        || e2e_fail "$boundary exit call failed"
    wait_for_keyboard_navigation "$cid" false \
        || e2e_fail "$boundary did not exit keyboard navigation"
    wait_for_panel_focus_session_owner "$cid" false \
        || e2e_fail "$boundary left the panel focus session owned"
}

e2e_wait_settled 60 || e2e_fail "vehicle dock never settled"
source_cid="$(e2e_json viewsData | python3 -c 'import json,sys
views = json.load(sys.stdin)
print(views[0]["containmentId"] if views else "")')"
[[ -n "$source_cid" ]] || e2e_fail "vehicle has no dock view"

launch_client A
client_a="$launched_client_id"
activate_window "$client_a"
send_key_and_require_delivery "$client_a" a "baseline"

enter_keyboard_navigation "$source_cid" "QML Escape"
# Escape is handled by the real containment KeyboardNavigationHandler. If the
# layer surface never received keyboard focus, the client title changes and
# the mode remains active.
"$E2E_FAKEPOINTER" key Escape || e2e_fail "QML Escape injection failed"
wait_for_keyboard_navigation "$source_cid" false \
    || e2e_fail "real QML Escape did not exit keyboard navigation"
send_key_and_require_delivery "$client_a" b "QML Escape restoration"
echo "ok: QML Escape restored actual key delivery"

activate_window "$client_a"
send_key_and_require_delivery "$client_a" c "D-Bus baseline"
enter_keyboard_navigation "$source_cid" "D-Bus exit"
exit_keyboard_navigation "$source_cid" "D-Bus exit"
send_key_and_require_delivery "$client_a" d "D-Bus restoration"
echo "ok: D-Bus toggle-off restored actual key delivery"

before_ids="$(e2e_json viewsData | python3 -c 'import json,sys
print(" ".join(str(view["containmentId"]) for view in json.load(sys.stdin)))')"
e2e_call duplicateView u "$source_cid" >/dev/null \
    || e2e_fail "second-view focus-session duplicate call failed"
for _ in $(seq 1 120); do
    duplicate_cid="$(e2e_json viewsData | python3 -c 'import json,sys
before = {int(value) for value in sys.argv[1].split()}
created = [view["containmentId"] for view in json.load(sys.stdin)
           if view["containmentId"] not in before]
print(created[0] if len(created) == 1 else "")' "$before_ids")"
    [[ -n "$duplicate_cid" ]] && break
    sleep 0.25
done
[[ -n "$duplicate_cid" ]] || e2e_fail "independent second dock never appeared"

activate_window "$client_a"
enter_keyboard_navigation "$source_cid" "first focus-session owner"
e2e_call setViewKeyboardNavigation ub "$duplicate_cid" true >/dev/null \
    || e2e_fail "competing-owner call failed at D-Bus"
sleep 0.3
[[ "$(keyboard_navigation "$source_cid")" == true ]] \
    || e2e_fail "competing dock consumed the first dock's focus session"
[[ "$(keyboard_navigation "$duplicate_cid")" == false ]] \
    || e2e_fail "two docks entered keyboard navigation simultaneously"
[[ "$(panel_focus_session_owner "$source_cid")" == true \
    && "$(panel_focus_session_owner "$duplicate_cid")" == false ]] \
    || e2e_fail "two docks reported ownership of one panel focus session"
exit_keyboard_navigation "$source_cid" "first focus-session owner"
echo "ok: a second dock cannot replace or consume the active focus session"

activate_window "$client_a"
enter_keyboard_navigation "$duplicate_cid" "destroyed focus-session owner"
e2e_call removeView u "$duplicate_cid" >/dev/null \
    || e2e_fail "could not remove the dock that owned keyboard focus"
for _ in $(seq 1 120); do
    [[ "$(keyboard_navigation "$duplicate_cid")" == missing ]] && break
    sleep 0.25
done
[[ "$(keyboard_navigation "$duplicate_cid")" == missing ]] \
    || e2e_fail "removed focus-owning dock remained in viewsData"
duplicate_cid=""
activate_window "$client_a"
enter_keyboard_navigation "$source_cid" "post-owner-destruction session"
exit_keyboard_navigation "$source_cid" "post-owner-destruction session"
send_key_and_require_delivery "$client_a" e "owner-destruction cleanup"
echo "ok: destroying the owning dock ends its focus session"

activate_window "$client_a"
send_key_and_require_delivery "$client_a" f "focus-loss baseline"
enter_keyboard_navigation "$source_cid" "focus-loss discard"
# KWin does not expose layer surfaces through workspace.activeWindow. Exercise
# one real navigation key after the compositor grant instead of treating that
# application-window property as a focus oracle.
sleep 3
send_key_and_require_saved_client_unchanged "$client_a" Right "focus-loss grant"
launch_client B
client_b="$launched_client_id"
activate_window "$client_b"
wait_for_keyboard_navigation "$source_cid" false \
    || e2e_fail "external client focus did not discard keyboard navigation"
send_key_and_require_delivery "$client_b" g "focus-loss winner"
e2e_call setViewKeyboardNavigation ub "$source_cid" false >/dev/null \
    || e2e_fail "non-stealing idempotent exit call failed"
send_key_and_require_delivery "$client_b" h "non-stealing idempotent exit"
echo "ok: external focus loss ends the session without stealing focus back"
stop_client B "$client_b"

activate_window "$client_a"
send_key_and_require_delivery "$client_a" i "destroyed-target baseline"
enter_keyboard_navigation "$source_cid" "destroyed saved target"
sleep 3
send_key_and_require_saved_client_unchanged "$client_a" Left "destroyed-target grant"
stop_client A "$client_a"
[[ "$(keyboard_navigation "$source_cid")" == true ]] \
    || e2e_fail "destroying the inactive saved target unexpectedly exited the dock mode"
exit_keyboard_navigation "$source_cid" "destroyed saved target"
[[ "$(e2e_call lifecycleState | awk '{print $2}')" == '"running"' ]] \
    || e2e_fail "dock died while clearing a destroyed saved target"
launch_client C
client_c="$launched_client_id"
activate_window "$client_c"
send_key_and_require_delivery "$client_c" j "post-target-destruction focus"
echo "ok: destroying the saved target clears safely"

echo "PASS: keyboard-navigation-focus-restoration"
