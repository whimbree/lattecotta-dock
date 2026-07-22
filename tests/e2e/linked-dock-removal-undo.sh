#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Executable linked-member removal Undo. A test notification service receives
# libplasma's real removal notification and emits its advertised Undo action;
# no production state is forged and no log text is used as a verdict.
# e2e-mode: nested-only
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-multi-output-e2e.sh}/tests/e2e/lib.sh"

[[ "${E2E_OUTPUT_COUNT:-1}" -eq 2 ]] \
    || e2e_fail "linked-dock-removal-undo needs the dual-output vehicle"

readonly notification_helper="$E2E_BUILD/bin/latte-test-notification-service"
readonly notification_log="$E2E_ARTIFACTS/linked-dock-removal-undo.notifications.log"
[[ -x "$notification_helper" ]] \
    || e2e_fail "notification test service is missing at $notification_helper"

"$notification_helper" >"$notification_log" 2>&1 &
notification_pid=$!
cleanup_notifications() {
    kill "$notification_pid" 2>/dev/null || true
    wait "$notification_pid" 2>/dev/null || true
}
trap cleanup_notifications EXIT

notification_ready=false
for _ in $(seq 1 40); do
    # Query the bus owner list without invoking D-Bus activation. Calling the
    # service name before this helper owns it can race an installed daemon.
    if busctl --user --list --no-legend \
            | grep -q '^org\.freedesktop\.Notifications '; then
        notification_ready=true
        break
    fi
    kill -0 "$notification_pid" 2>/dev/null \
        || e2e_fail "notification test service exited; see $notification_log"
    sleep 0.1
done
[[ "$notification_ready" == true ]] \
    || e2e_fail "notification test service did not acquire its D-Bus name; see $notification_log"

snapshot() { e2e_json dockSystemData; }

wait_for_state() {
    local predicate="$1" label="$2" i current
    for ((i = 0; i < 160; ++i)); do
        current="$(snapshot)"
        if python3 -c "$predicate" <<<"$current" >/dev/null; then
            printf '%s\n' "$current"
            return 0
        fi
        sleep 0.25
    done
    printf 'last dockSystemData: %s\n' "$current" >&2
    e2e_fail "$label"
}

notification_delivery_count() {
    gdbus call --session \
        --dest org.freedesktop.Notifications \
        --object-path /org/freedesktop/Notifications \
        --method org.freedesktop.Notifications.DeliveryCount \
        | grep -o '[0-9]\+' | tail -1
}

wait_for_undo_after() {
    local previous_count="$1" i actions count
    for ((i = 0; i < 80; ++i)); do
        count="$(notification_delivery_count 2>/dev/null || echo 0)"
        actions="$(gdbus call --session \
            --dest org.freedesktop.Notifications \
            --object-path /org/freedesktop/Notifications \
            --method org.freedesktop.Notifications.LastActions 2>/dev/null || true)"
        if (( count > previous_count )) && [[ "${actions,,}" == *undo* ]]; then
            return 0
        fi
        sleep 0.1
    done
    printf 'latest notification actions: %s\n' "$actions" >&2
    return 1
}

invoke_notification_undo() {
    local result
    result="$(gdbus call --session \
        --dest org.freedesktop.Notifications \
        --object-path /org/freedesktop/Notifications \
        --method org.freedesktop.Notifications.InvokeUndo)" || return 1
    [[ "$result" == *true* ]]
}

read -r root_id secondary_id <<<"$(
    { snapshot; e2e_json screensData; } | python3 -c '
import json, sys
state = json.loads(sys.stdin.readline())
screens = json.loads(sys.stdin.readline())
views = state["views"]
if len(views) != 1 or views[0]["relationship"] != "independent":
    sys.exit("expected one independent seed dock")
secondary = next(s for s in screens if s["isActive"] and not s["isPrimary"])
print(views[0]["persistentDockId"], secondary["id"])
' )" || e2e_fail "could not resolve the seed root and secondary output"

e2e_call createLinkedView uii "$root_id" "$secondary_id" 5 >/dev/null \
    || e2e_fail "could not create the linked member for Undo"
created="$(wait_for_state "
import json, sys
views = json.load(sys.stdin)[\"views\"]
members = [v for v in views if v[\"relationship\"] == \"linkedMember\"]
sys.exit(0 if len(views) == 2 and len(members) == 1
         and members[0][\"originalDockId\"] == $root_id else 1)
" "linked member did not settle before removal")"
member_id="$(python3 -c '
import json, sys
print(next(v["persistentDockId"] for v in json.load(sys.stdin)["views"]
           if v["relationship"] == "linkedMember"))
' <<<"$created")"
member_runtime_id="$(python3 -c '
import json, sys
want = int(sys.argv[1])
print(next(v["runtimeViewId"] for v in json.load(sys.stdin)["views"]
           if v["persistentDockId"] == want))
' "$member_id" <<<"$created")"

# Structural applet add/remove starts from the linked member. The direct root
# owns the transaction, while both containments keep distinct applet ids and
# mirror the real libplasma Undo state.
added_plugin=org.kde.plasma.minimizeall
e2e_call addApplet us "$member_id" "$added_plugin" >/dev/null \
    || e2e_fail "member-originated applet addition failed"
applet_addition_visible=false
for _ in $(seq 1 80); do
    states="$(for view in "$root_id" "$member_id"; do
        e2e_json viewAppletsData u "$view" | python3 -c '
import json, sys
plugin = sys.argv[1]
matches = [a for a in json.load(sys.stdin) if a["plugin"] == plugin]
ready = (len(matches) == 1
         and not matches[0]["inScheduledDestruction"]
         and matches[0]["z"] is not None
         and matches[0]["geometry"][2] > 0
         and matches[0]["geometry"][3] > 0)
print("ready" if ready else "missing")
' "$added_plugin"
    done)"
    if [[ "$states" == $'ready\nready' ]]; then
        applet_addition_visible=true
        break
    fi
    sleep 0.1
done
[[ "$applet_addition_visible" == true ]] \
    || e2e_fail "member-originated applet addition did not reach every linked containment"

member_applet_id="$(e2e_json viewAppletsData u "$member_id" | python3 -c '
import json, sys
plugin = sys.argv[1]
print(next(a["id"] for a in json.load(sys.stdin) if a["plugin"] == plugin))
' "$added_plugin")" || e2e_fail "could not resolve the member-local applet id"

before_applet_notification="$(notification_delivery_count)"
e2e_call removeApplet uu "$member_id" "$member_applet_id" >/dev/null \
    || e2e_fail "member-originated applet removal failed"
applet_removal_visible=false
for _ in $(seq 1 80); do
    root_scheduled="$(e2e_json viewAppletsData u "$root_id" | python3 -c '
import json, sys
plugin = sys.argv[1]
a = next((a for a in json.load(sys.stdin) if a["plugin"] == plugin), None)
print("true" if a and a["inScheduledDestruction"] else "false")
' "$added_plugin")"
    member_scheduled="$(e2e_json viewAppletsData u "$member_id" | python3 -c '
import json, sys
plugin = sys.argv[1]
a = next((a for a in json.load(sys.stdin) if a["plugin"] == plugin), None)
print("true" if a and a["inScheduledDestruction"] else "false")
' "$added_plugin")"
    if [[ "$root_scheduled" == true && "$member_scheduled" == true ]]; then
        applet_removal_visible=true
        break
    fi
    sleep 0.1
done
if [[ "$applet_removal_visible" != true ]]; then
    printf 'root applets after removal: %s\nmember applets after removal: %s\n' \
        "$(e2e_json viewAppletsData u "$root_id")" \
        "$(e2e_json viewAppletsData u "$member_id")" >&2
    e2e_fail "linked applet removal Undo state was not visible in every containment"
fi
wait_for_undo_after "$before_applet_notification" \
    || e2e_fail "libplasma did not advertise applet-removal Undo"
invoke_notification_undo || e2e_fail "could not invoke applet-removal Undo"

applet_undo_restored=false
for _ in $(seq 1 80); do
    states="$(for view in "$root_id" "$member_id"; do
        e2e_json viewAppletsData u "$view" | python3 -c '
import json, sys
plugin = sys.argv[1]
a = next((a for a in json.load(sys.stdin) if a["plugin"] == plugin), None)
print("restored" if a and not a["inScheduledDestruction"] else "missing")
' "$added_plugin"
    done)"
    if [[ "$states" == $'restored\nrestored' ]]; then
        applet_undo_restored=true
        break
    fi
    sleep 0.1
done
[[ "$applet_undo_restored" == true ]] \
    || e2e_fail "applet-removal Undo did not restore every linked applet instance"

before_dock_notification="$(notification_delivery_count)"
e2e_call removeView u "$member_id" >/dev/null \
    || e2e_fail "linked-member removal request failed"
wait_for_state "
import json, sys
views = json.load(sys.stdin)[\"views\"]
root = next(v for v in views if v[\"persistentDockId\"] == $root_id)
sys.exit(0 if len(views) == 1 and $member_id not in root[\"linkedDockIds\"] else 1)
" "linked member remained active during its Undo window" >/dev/null

[[ "$(kreadconfig6 --file "$E2E_LAYOUT" --group Containments --group "$member_id" --key plugin --default absent)" == absent ]] \
    || e2e_fail "removal tombstone was not persisted before Undo"

wait_for_undo_after "$before_dock_notification" \
    || e2e_fail "libplasma did not advertise dock-removal Undo"
invoke_notification_undo || e2e_fail "could not invoke the real dock-removal Undo action"

restored="$(wait_for_state "
import json, sys
views = json.load(sys.stdin)[\"views\"]
root = next((v for v in views if v[\"persistentDockId\"] == $root_id), None)
member = next((v for v in views if v[\"persistentDockId\"] == $member_id), None)
sys.exit(0 if len(views) == 2 and root and member
         and member[\"runtimeViewId\"] == \"$member_runtime_id\"
         and member[\"relationship\"] == \"linkedMember\"
         and member[\"originalDockId\"] == $root_id
         and root[\"linkedDockIds\"] == [$member_id] else 1)
" "Undo did not restore the same linked runtime instance and relationship")"

[[ "$(kreadconfig6 --file "$E2E_LAYOUT" --group Containments --group "$member_id" --key isClonedFrom --default -1)" == "$root_id" ]] \
    || e2e_fail "Undo did not restore the linked root in persistence"
[[ "$(kreadconfig6 --file "$E2E_LAYOUT" --group Containments --group "$member_id" --key linkPlacement --default -1)" == 1 ]] \
    || e2e_fail "Undo did not restore explicit placement ownership"

e2e_dock_stop || e2e_fail "could not stop after linked-member Undo"
e2e_dock_start || e2e_fail "could not restart after linked-member Undo"
wait_for_state "
import json, sys
views = json.load(sys.stdin)[\"views\"]
root = next((v for v in views if v[\"persistentDockId\"] == $root_id), None)
member = next((v for v in views if v[\"persistentDockId\"] == $member_id), None)
sys.exit(0 if len(views) == 2 and root and member
         and member[\"originalDockId\"] == $root_id
         and root[\"linkedDockIds\"] == [$member_id] else 1)
" "reloading after Undo did not reproduce the restored relationship" >/dev/null

echo "linked-member removal Undo restored containment $member_id under root $root_id and survived reload"
