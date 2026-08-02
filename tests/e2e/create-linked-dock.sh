#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Create Linked Dock dual-output acceptance. This drives the public D-Bus
# actions and reads the atomic dock-system snapshot. It covers occupied-edge
# creation, a separated portrait output, direct-root lineage, local placement,
# visibility and edit ownership, applet synchronization, independent Duplicate,
# and persistence reload.
# e2e-mode: nested-only
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-multi-output-e2e.sh}/tests/e2e/lib.sh"
source "$E2E_REPO/tests/e2e/matrix/applet-reorder-driver.sh"
source "$E2E_REPO/tests/e2e/matrix/task-reorder-lib.sh"

[[ "${E2E_OUTPUT_COUNT:-1}" -eq 2 ]] \
    || e2e_fail "create-linked-dock needs the dual-output vehicle"

readonly original_layout="$E2E_ARTIFACTS/create-linked-dock.original.layout.latte"
cp "$E2E_LAYOUT" "$original_layout" \
    || e2e_fail "could not preserve the pre-scenario layout"
restore_original_layout() {
    e2e_dock_stop >/dev/null 2>&1 || true
    cp "$original_layout" "$E2E_LAYOUT" || return 1
    e2e_dock_start >/dev/null 2>&1 || return 1
}
trap restore_original_layout EXIT

snapshot() { e2e_json dockSystemData; }
views_data() { e2e_json viewsData; }

wait_for_snapshot() {
    local predicate="$1" label="$2" i current
    if ! python3 -c 'import sys; compile(sys.argv[1], "<snapshot predicate>", "exec")' "$predicate"; then
        printf '%s\n' "$predicate" >&2
        e2e_fail "invalid snapshot predicate for: $label"
    fi
    for ((i = 0; i < 120; ++i)); do
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

wait_for_views_data() {
    local predicate="$1" label="$2" i current
    if ! python3 -c 'import sys; compile(sys.argv[1], "<viewsData predicate>", "exec")' "$predicate"; then
        printf '%s\n' "$predicate" >&2
        e2e_fail "invalid viewsData predicate for: $label"
    fi
    for ((i = 0; i < 120; ++i)); do
        current="$(views_data)"
        if python3 -c "$predicate" <<<"$current" >/dev/null; then
            printf '%s\n' "$current"
            return 0
        fi
        sleep 0.25
    done
    printf 'last viewsData: %s\n' "$current" >&2
    e2e_fail "$label"
}

wait_for_topology() {
    local i current
    for ((i = 0; i < 120; ++i)); do
        current="$(e2e_json screensData)"
        if python3 -c '
import json, sys
active = [s for s in json.load(sys.stdin) if s["isActive"]]
if len(active) != 2:
    sys.exit(1)
primary = next(s for s in active if s["isPrimary"])
secondary = next(s for s in active if not s["isPrimary"])
px, py, pw, ph = primary["geometry"]
sx, sy, sw, sh = secondary["geometry"]
separated = sx > px + pw or px > sx + sw or sy > py + ph or py > sy + sh
sys.exit(0 if sw < sh and separated and sy != py else 1)
' <<<"$current"; then
            printf '%s\n' "$current"
            return 0
        fi
        sleep 0.25
    done
    return 1
}

wait_for_active_output_count() {
    local expected="$1" i current
    for ((i = 0; i < 120; ++i)); do
        current="$(e2e_json screensData)"
        if python3 -c '
import json, sys
expected = int(sys.argv[1])
active = [screen for screen in json.load(sys.stdin) if screen["isActive"]]
sys.exit(0 if len(active) == expected else 1)
' "$expected" <<<"$current"; then
            printf '%s\n' "$current"
            return 0
        fi
        sleep 0.25
    done
    return 1
}

view_plugins() {
    e2e_json viewAppletsData u "$1" | python3 -c '
import json, sys
print(" ".join(sorted(a["plugin"] for a in json.load(sys.stdin))))
'
}

view_applet_ids() {
    e2e_json viewAppletsData u "$1" | python3 -c '
import json, sys
print(" ".join(str(a["id"]) for a in json.load(sys.stdin)))
'
}

view_plugin_order() {
    e2e_json viewAppletsData u "$1" | python3 -c '
import json, sys
print(" ".join(a["plugin"] for a in json.load(sys.stdin)))
'
}

tasks_applet_id() {
    e2e_json viewAppletsData u "$1" | python3 -c '
import json, sys
ids = [a["id"] for a in json.load(sys.stdin) if a["plugin"] == "org.kde.latte.plasmoid"]
if len(ids) != 1:
    sys.exit("expected one Latte Tasks applet, got %r" % ids)
print(ids[0])
'
}

tasks_launchers_config() {
    local view="$1" applet
    applet="$(tasks_applet_id "$view")" || return 1
    e2e_json appletConfigData uu "$view" "$applet" | python3 -c '
import json, sys
config = json.load(sys.stdin)["config"]
values = {key: value for key, value in config.items() if key.startswith("launchers")}
print(json.dumps(values, sort_keys=True, separators=(",", ":")))
'
}

tasks_local_length() {
    local view="$1" applet
    applet="$(tasks_applet_id "$view")" || return 1
    e2e_json appletConfigData uu "$view" "$applet" | python3 -c '
import json, sys
config = json.load(sys.stdin)["config"]
print(json.dumps(config.get("length", "absent"), separators=(",", ":")))
'
}

stable_tasks_local_length() {
    local view="$1" previous= current= stable=0
    for _ in $(seq 1 80); do
        if ! current="$(tasks_local_length "$view" 2>/dev/null)"; then
            sleep 0.25
            continue
        fi
        if [[ "$current" == "$previous" ]]; then
            stable=$((stable + 1))
        else
            previous="$current"
            stable=1
        fi
        if [[ "$stable" -ge 4 ]]; then
            printf '%s\n' "$current"
            return 0
        fi
        sleep 0.25
    done
    return 1
}

view_content_fingerprint() {
    local view="$1" applets applet_id plugin config
    applets="$(e2e_json viewAppletsData u "$view")" || return 1
    while read -r applet_id plugin; do
        config="$(e2e_json appletConfigData uu "$view" "$applet_id")" || return 1
        python3 -c '
import json, sys
plugin = sys.argv[1]
data = json.load(sys.stdin)
data["config"].pop("length", None)
print(json.dumps({"plugin": plugin, "config": data["config"]},
                 sort_keys=True, separators=(",", ":")))
' "$plugin" <<<"$config" || return 1
    done < <(python3 -c '
import json, sys
for applet in json.load(sys.stdin):
    print(applet["id"], applet["plugin"])
' <<<"$applets")
}

read -r primary_id primary_name secondary_id secondary_name <<<"$(
    e2e_json screensData | python3 -c '
import json, sys
active = [s for s in json.load(sys.stdin) if s["isActive"]]
primary = [s for s in active if s["isPrimary"]]
secondary = [s for s in active if not s["isPrimary"]]
if len(primary) != 1 or len(secondary) != 1:
    sys.exit("expected one primary and one secondary output")
print(primary[0]["id"], primary[0]["name"], secondary[0]["id"], secondary[0]["name"])
')"

root_id="$(snapshot | python3 -c '
import json, sys
views = json.load(sys.stdin)["views"]
if len(views) != 1 or views[0]["relationship"] != "independent":
    sys.exit("expected one independent seed dock")
print(views[0]["persistentDockId"])
')" || e2e_fail "could not identify the independent seed dock"

# KWin owns output topology. Drive a portrait secondary with a horizontal gap
# and vertical offset, then require Latte to consume that exact QScreen geometry.
kscreen-doctor "output.${secondary_name}.rotation.left" \
               "output.${secondary_name}.position.2300,180" >/dev/null \
    || e2e_fail "could not configure the separated portrait output"

topology="$(wait_for_topology)" \
    || e2e_fail "Latte did not observe the separated portrait topology"

secondary_geometry="$(e2e_json screensData | python3 -c '
import json, sys
want = int(sys.argv[1])
s = next(s for s in json.load(sys.stdin) if s["id"] == want and s["isActive"])
print(",".join(str(v) for v in s["geometry"]))
' "$secondary_id")"

# Create on the root's occupied primary/bottom edge. This is supported
# stacking membership, not a free-edge fallback.
e2e_call createLinkedView uii "$root_id" "$primary_id" 4 >/dev/null \
    || e2e_fail "same-edge createLinkedView call failed"
same_edge_state="$(wait_for_snapshot "
import json, sys
state = json.load(sys.stdin)
views = state[\"views\"]
root = next((v for v in views if v[\"persistentDockId\"] == $root_id), None)
members = [v for v in views if v[\"relationship\"] == \"linkedMember\"]
sys.exit(0 if root and root[\"relationship\"] == \"linkedRoot\" and len(members) == 1
        and members[0][\"originalDockId\"] == $root_id
        and members[0][\"linkPlacement\"] == \"explicitTarget\"
        and members[0][\"screenId\"] == $primary_id
        and members[0][\"edge\"] == \"bottom\" else 1)
" 'linked dock did not appear on the occupied primary/bottom edge')" \
    || e2e_fail "same-edge linked member did not settle"
same_edge_id="$(python3 -c '
import json, sys
print(next(v["persistentDockId"] for v in json.load(sys.stdin)["views"] if v["relationship"] == "linkedMember"))
' <<<"$same_edge_state")"

# Create from a member. The new relationship must still point directly to the
# root, and its geometry must come from the selected non-adjacent output.
e2e_call createLinkedView uii "$same_edge_id" "$secondary_id" 5 >/dev/null \
    || e2e_fail "cross-output createLinkedView call failed"
remote_state="$(wait_for_snapshot "
import json, sys
state = json.load(sys.stdin)
members = [v for v in state[\"views\"] if v[\"relationship\"] == \"linkedMember\"]
remote = [v for v in members if v[\"persistentDockId\"] != $same_edge_id]
sys.exit(0 if len(members) == 2 and len(remote) == 1
        and remote[0][\"originalDockId\"] == $root_id
        and remote[0][\"logicalDockId\"] == $root_id
        and remote[0][\"screenId\"] == $secondary_id
        and remote[0][\"edge\"] == \"left\"
        and \",\".join(str(x) for x in remote[0][\"screenGeometry\"]) == \"$secondary_geometry\" else 1)
" 'linked member did not retain direct-root lineage and target-output geometry')" \
    || e2e_fail "cross-output linked member did not settle"
remote_id="$(python3 -c '
import json, sys
same = int(sys.argv[1])
print(next(v["persistentDockId"] for v in json.load(sys.stdin)["views"]
           if v["relationship"] == "linkedMember" and v["persistentDockId"] != same))
' "$same_edge_id" <<<"$remote_state")"

metrics_state="$(wait_for_snapshot "
import json, sys
v = next(v for v in json.load(sys.stdin)[\"views\"] if v[\"persistentDockId\"] == $remote_id)
fields = (v[\"configuredIconSize\"], v[\"effectiveIconSize\"], v[\"availablePrimaryLength\"])
sys.exit(0 if all(value is not None for value in fields) else 1)
" 'linked member sizing authorities did not become live')" \
    || e2e_fail "linked member sizing authorities did not settle"
before_remote_metrics="$(python3 -c '
import json, sys
want = int(sys.argv[1])
v = next(v for v in json.load(sys.stdin)["views"] if v["persistentDockId"] == want)
fields = (v["configuredIconSize"], v["effectiveIconSize"], v["availablePrimaryLength"])
print(" ".join(str(value) for value in fields))
' "$remote_id" <<<"$metrics_state")" || e2e_fail "linked member metrics were unavailable"
before_remote_local_length="$(stable_tasks_local_length "$remote_id")" \
    || e2e_fail "remote member applet length did not settle before the alignment test"

# Exact cross-dock sizing reproducer: changing the root bottom dock to start
# alignment must not alter the separate vertical member's sizing inputs.
e2e_call setViewPlacement uiii "$root_id" "$primary_id" 4 1 >/dev/null \
    || e2e_fail "root alignment change failed"
wait_for_snapshot "
import json, sys
views = {v[\"persistentDockId\"]: v for v in json.load(sys.stdin)[\"views\"]}
root, remote = views[$root_id], views[$remote_id]
metrics = (remote[\"configuredIconSize\"], remote[\"effectiveIconSize\"], remote[\"availablePrimaryLength\"])
sys.exit(0 if root[\"alignment\"] == \"left\" and root[\"edge\"] == \"bottom\"
         and remote[\"edge\"] == \"left\" and \" \".join(str(v) for v in metrics) == \"$before_remote_metrics\"
         else 1)
" 'root alignment changed the remote linked member sizing or placement' >/dev/null
[[ "$(stable_tasks_local_length "$remote_id")" == "$before_remote_local_length" ]] \
    || e2e_fail "root alignment copied applet-local length into the remote linked member"

# Relocate the explicit member through both axes and outputs. The semantic end
# alignment (2) normalizes to bottom on a vertical edge, while the root and the
# other explicit member remain in place.
e2e_call setViewPlacement uiii "$remote_id" "$primary_id" 6 2 >/dev/null \
    || e2e_fail "explicit member relocation to primary/right failed"
wait_for_snapshot "
import json, sys
views = {v[\"persistentDockId\"]: v for v in json.load(sys.stdin)[\"views\"]}
remote = views[$remote_id]
sys.exit(0 if remote[\"screenId\"] == $primary_id and remote[\"edge\"] == \"right\"
         and remote[\"orientation\"] == \"vertical\" and remote[\"alignment\"] == \"bottom\"
         and views[$root_id][\"edge\"] == \"bottom\" and views[$same_edge_id][\"edge\"] == \"bottom\"
         else 1)
" 'explicit member relocation leaked into another dock' >/dev/null

e2e_call setViewPlacement uiii "$remote_id" "$secondary_id" 5 1 >/dev/null \
    || e2e_fail "explicit member relocation back to secondary/left failed"
wait_for_snapshot "
import json, sys
v = next(v for v in json.load(sys.stdin)[\"views\"] if v[\"persistentDockId\"] == $remote_id)
sys.exit(0 if v[\"screenId\"] == $secondary_id and v[\"edge\"] == \"left\"
         and v[\"alignment\"] == \"top\" else 1)
" 'explicit member did not return to the portrait output' >/dev/null

# Visibility and edit presentation are local to explicit members.
old_member_mode="$(snapshot | python3 -c '
import json, sys
want = int(sys.argv[1])
print(next(v for v in json.load(sys.stdin)["views"] if v["persistentDockId"] == want)["visibilityMode"])
' "$remote_id")"
new_root_mode=autoHide
e2e_call setViewVisibilityMode us "$root_id" "$new_root_mode" >/dev/null \
    || e2e_fail "root visibility change failed"
wait_for_snapshot "
import json, sys
views = {v[\"persistentDockId\"]: v for v in json.load(sys.stdin)[\"views\"]}
sys.exit(0 if views[$root_id][\"visibilityMode\"] == \"$new_root_mode\"
         and views[$remote_id][\"visibilityMode\"] == \"$old_member_mode\"
         and views[$same_edge_id][\"visibilityMode\"] == \"$old_member_mode\" else 1)
" 'root visibility change leaked into an explicit member' >/dev/null
wait_for_views_data "
import json, sys
root = next(v for v in json.load(sys.stdin) if v[\"containmentId\"] == $root_id)
sys.exit(0 if root[\"isHidden\"] else 1)
" 'Auto Hide root did not hide before peer editing' >/dev/null

e2e_call setViewEditMode ub "$remote_id" true >/dev/null \
    || e2e_fail "could not enter the linked member edit presentation"
wait_for_views_data "
import json, sys
views = json.load(sys.stdin)
editing = {v[\"containmentId\"] for v in views if v[\"editMode\"]}
highlighted = {v[\"containmentId\"] for v in views if v[\"linkedEditHighlight\"]}
visible_highlights = all(not v[\"isHidden\"] for v in views if v[\"linkedEditHighlight\"])
passive = all(not v[\"inConfigureAppletsMode\"] for v in views if v[\"linkedEditHighlight\"])
sys.exit(0 if editing == {$remote_id}
         and highlighted == {$root_id, $same_edge_id}
         and visible_highlights and passive else 1)
" 'linked peers did not expose a visible passive edit highlight' >/dev/null
wait_for_snapshot "
import json, sys
views = {v[\"persistentDockId\"]: v for v in json.load(sys.stdin)[\"views\"]}
active = views[$remote_id]
peers = [views[$root_id], views[$same_edge_id]]
sys.exit(0 if active[\"editMode\"] and active[\"settingsWindowShown\"]
         and active[\"objects\"][\"configWindow\"] is not None
         and all(not peer[\"editMode\"] and not peer[\"settingsWindowShown\"]
                 and peer[\"objects\"][\"configWindow\"] is None for peer in peers)
         else 1)
" 'passive linked peers acquired edit or configuration-window ownership' >/dev/null
e2e_call setViewEditMode ub "$remote_id" false >/dev/null \
    || e2e_fail "could not leave the linked member edit presentation"
wait_for_views_data '
import json, sys
views = json.load(sys.stdin)
root = next(v for v in views if v["containmentId"] == '$root_id')
sys.exit(0 if not any(v["editMode"] or v["linkedEditHighlight"] for v in views)
         and root["isHidden"] else 1)
' 'linked member edit mode or peer highlight did not close' >/dev/null

# Temporarily expose the occupied-edge member on the primary top edge while
# pointer-driven mutations run. Same-edge overlap remains supported and is
# restored below; a covered view is not a valid pointer target.
e2e_call setViewPlacement uiii "$same_edge_id" "$primary_id" 3 0 >/dev/null \
    || e2e_fail "could not expose the linked member for pointer-driven mutations"
wait_for_snapshot "
import json, sys
views = {v[\"persistentDockId\"]: v for v in json.load(sys.stdin)[\"views\"]}
sys.exit(0 if views[$same_edge_id][\"edge\"] == \"top\"
         and views[$same_edge_id][\"alignment\"] == \"center\"
         and views[$root_id][\"edge\"] == \"bottom\" else 1)
" 'linked member did not settle on its temporary pointer-test edge' >/dev/null

# A Tasks launcher reorder is an applet-CONFIG mutation, not a containment
# applet reorder. Drive it from an editable linked member and require the live
# launchers config and rendered launcher order to converge across the group.
mapfile -t task_apps < <(taskdrag_order "$same_edge_id" | tr ' ' '\n')
[[ "${#task_apps[@]}" -ge 2 ]] \
    || e2e_fail "linked-member config test needs at least two launchers"
before_task_order="$(taskdrag_launcher_order "$same_edge_id")"
before_launchers_config="$(tasks_launchers_config "$same_edge_id")" \
    || e2e_fail "could not read linked-member launcher config"
after_task_order="$before_task_order"
for _ in $(seq 1 4); do
    taskdrag_reorder "$same_edge_id" "${task_apps[0]}" "${task_apps[1]}" \
        || e2e_fail "linked-member task reorder driver failed"
    after_task_order="$(taskdrag_launcher_order "$same_edge_id")"
    [[ "$after_task_order" != "$before_task_order" ]] && break
done
[[ "$after_task_order" != "$before_task_order" ]] \
    || e2e_fail "linked-member task reorder did not change launcher order"

config_sync=false
for _ in $(seq 1 80); do
    root_task_order="$(taskdrag_launcher_order "$root_id")"
    remote_task_order="$(taskdrag_launcher_order "$remote_id")"
    root_launchers_config="$(tasks_launchers_config "$root_id")"
    member_launchers_config="$(tasks_launchers_config "$same_edge_id")"
    remote_launchers_config="$(tasks_launchers_config "$remote_id")"
    if [[ "$root_task_order" == "$after_task_order" && "$remote_task_order" == "$after_task_order" \
            && "$member_launchers_config" != "$before_launchers_config" \
            && "$root_launchers_config" == "$member_launchers_config" \
            && "$remote_launchers_config" == "$member_launchers_config" ]]; then
        config_sync=true
        break
    fi
    sleep 0.25
done
[[ "$config_sync" == true ]] \
    || e2e_fail "member-originated applet config did not converge across the linked group"
[[ "$(stable_tasks_local_length "$remote_id")" == "$before_remote_local_length" ]] \
    || e2e_fail "shared launcher configuration overwrote the remote member's local applet length"

# Applet content remains linked. Add one resolvable plugin from a MEMBER and wait
# until every member has the same plugin multiset with disjoint instance ids.
before_plugin_count="$(view_plugins "$root_id" | wc -w)"
added_plugin=org.kde.plasma.minimizeall
e2e_call addApplet us "$remote_id" "$added_plugin" >/dev/null \
    || e2e_fail "could not add the installed plasmoid from a linked member"
applet_sync=false
for _ in $(seq 1 120); do
    root_plugins="$(view_plugins "$root_id")"
    same_plugins="$(view_plugins "$same_edge_id")"
    remote_plugins="$(view_plugins "$remote_id")"
    if [[ "$(wc -w <<<"$root_plugins")" -eq $((before_plugin_count + 1)) \
            && "$root_plugins" == "$same_plugins" && "$root_plugins" == "$remote_plugins" ]]; then
        applet_sync=true
        break
    fi
    sleep 0.5
done
if [[ "$applet_sync" != true ]]; then
    printf 'root plugins:   %s\nsame plugins:   %s\nremote plugins: %s\n' \
        "$root_plugins" "$same_plugins" "$remote_plugins" >&2
    e2e_fail "applet addition did not synchronize across linked members"
fi

all_applet_ids="$(for id in "$root_id" "$same_edge_id" "$remote_id"; do view_applet_ids "$id"; done)"
python3 -c '
import sys
ids = [int(value) for value in sys.stdin.read().split()]
if len(ids) != len(set(ids)):
    sys.exit("linked containments share applet instance identities")
' <<<"$all_applet_ids" || e2e_fail "linked members reused mutable applet identities"

# Reorder containment applets from a member. Instance ids differ by design, so
# the relationship coordinator translates the member order to root ids and the
# observable plugin sequence must then agree everywhere.
before_applet_order="$(view_plugin_order "$same_edge_id")"
applet_reorder_attempt "$same_edge_id" commit 0 1 \
    || e2e_fail "linked-member applet reorder did not commit"
after_applet_order="$(view_plugin_order "$same_edge_id")"
[[ "$after_applet_order" != "$before_applet_order" ]] \
    || e2e_fail "linked-member applet reorder left the plugin sequence unchanged"
order_sync=false
for _ in $(seq 1 80); do
    if [[ "$(view_plugin_order "$root_id")" == "$after_applet_order" \
            && "$(view_plugin_order "$remote_id")" == "$after_applet_order" ]]; then
        order_sync=true
        break
    fi
    sleep 0.25
done
if [[ "$order_sync" != true ]]; then
    printf 'expected plugin order: %s\nroot plugin order: %s\nremote plugin order: %s\n' \
        "$after_applet_order" "$(view_plugin_order "$root_id")" \
        "$(view_plugin_order "$remote_id")" >&2
    e2e_fail "member-originated applet order did not converge across the linked group"
fi

# Disconnect the explicit member's output while the root stays live, mutate
# the root, then reconnect. The persistent member must remain present on disk
# and its fresh runtime must reconcile exact applet structure, order, and
# configuration before it can publish a settled state.
kscreen-doctor "output.${secondary_name}.disable" >/dev/null \
    || e2e_fail "could not disable the explicit member output"
wait_for_active_output_count 1 >/dev/null \
    || e2e_fail "Latte did not observe the explicit member output disconnect"
wait_for_snapshot "
import json, sys
views = {v[\"persistentDockId\"] for v in json.load(sys.stdin)[\"views\"]}
sys.exit(0 if views == {$root_id, $same_edge_id} else 1)
" 'the disconnected-output member retained a runtime view' >/dev/null
[[ "$(kreadconfig6 --file "$E2E_LAYOUT" --group Containments --group "$remote_id" --key isClonedFrom --default -1)" == "$root_id" ]] \
    || e2e_fail "member output disconnect removed the persistent relationship"

offline_added_plugin=org.kde.plasma.minimizeall
e2e_call addApplet us "$root_id" "$offline_added_plugin" >/dev/null \
    || e2e_fail "could not mutate linked applets while one member output was disconnected"
offline_sync=false
for _ in $(seq 1 120); do
    root_plugins="$(view_plugins "$root_id")"
    same_plugins="$(view_plugins "$same_edge_id")"
    if [[ "$(tr ' ' '\n' <<<"$root_plugins" | grep -Fxc "$offline_added_plugin")" -eq 2 \
            && "$root_plugins" == "$same_plugins" ]]; then
        offline_sync=true
        break
    fi
    sleep 0.25
done
[[ "$offline_sync" == true ]] \
    || e2e_fail "live linked members did not converge during the output disconnect"
offline_content_sync=false
for _ in $(seq 1 160); do
    offline_expected_content="$(view_content_fingerprint "$root_id")" \
        || e2e_fail "could not fingerprint root content during the output disconnect"
    same_edge_offline_content="$(view_content_fingerprint "$same_edge_id")" \
        || e2e_fail "could not fingerprint the live member during the output disconnect"
    if [[ "$same_edge_offline_content" == "$offline_expected_content" ]]; then
        offline_content_sync=true
        break
    fi
    sleep 0.25
done
if [[ "$offline_content_sync" != true ]]; then
    printf 'root content:\n%s\nlive member content:\n%s\n' \
        "$offline_expected_content" "$same_edge_offline_content" >&2
    e2e_fail "live member content differed from the root during the output disconnect"
fi

kscreen-doctor "output.${secondary_name}.enable" \
               "output.${secondary_name}.rotation.left" \
               "output.${secondary_name}.position.2300,180" >/dev/null \
    || e2e_fail "could not reconnect the explicit member output"
wait_for_topology >/dev/null \
    || e2e_fail "Latte did not observe the reconnected portrait output"
wait_for_snapshot "
import json, sys
views = {v[\"persistentDockId\"]: v for v in json.load(sys.stdin)[\"views\"]}
remote = views.get($remote_id)
sys.exit(0 if set(views) == {$root_id, $same_edge_id, $remote_id}
         and remote and remote[\"originalDockId\"] == $root_id
         and remote[\"screenId\"] == $secondary_id and remote[\"edge\"] == \"left\" else 1)
" 'explicit member did not return after its output reconnected' >/dev/null
reconnect_sync=false
for _ in $(seq 1 160); do
    if [[ "$(view_content_fingerprint "$remote_id")" == "$offline_expected_content" ]]; then
        reconnect_sync=true
        break
    fi
    sleep 0.25
done
[[ "$reconnect_sync" == true ]] \
    || e2e_fail "reconnected member did not reconcile exact root applet content"
[[ "$(stable_tasks_local_length "$remote_id")" == "$before_remote_local_length" ]] \
    || e2e_fail "output reconnection overwrote the remote member's local applet length"

# Return the member to the already occupied edge. The relationship and content
# operations above must not rewrite placement ownership.
e2e_call setViewPlacement uiii "$same_edge_id" "$primary_id" 4 0 >/dev/null \
    || e2e_fail "could not restore the linked member to the occupied edge"
wait_for_snapshot "
import json, sys
views = {v[\"persistentDockId\"]: v for v in json.load(sys.stdin)[\"views\"]}
sys.exit(0 if views[$same_edge_id][\"edge\"] == \"bottom\"
         and views[$same_edge_id][\"alignment\"] == \"center\"
         and views[$root_id][\"edge\"] == \"bottom\" else 1)
" 'linked member did not return to the occupied edge' >/dev/null

# Duplicate the explicit member. The result must be independent and must not be
# added to the root's linkedDockIds.
before_ids="$(snapshot | python3 -c '
import json, sys
print(" ".join(str(v["persistentDockId"]) for v in json.load(sys.stdin)["views"]))
')"
e2e_call duplicateView u "$remote_id" >/dev/null \
    || e2e_fail "Duplicate Dock failed from an explicit linked member"
duplicate_state="$(wait_for_snapshot "
import json, sys
before = {int(v) for v in \"$before_ids\".split()}
views = json.load(sys.stdin)[\"views\"]
created = [v for v in views if v[\"persistentDockId\"] not in before]
root = next(v for v in views if v[\"persistentDockId\"] == $root_id)
sys.exit(0 if len(created) == 1 and created[0][\"relationship\"] == \"independent\" \
        and created[0][\"originalDockId\"] is None and created[0][\"linkPlacement\"] is None \
        and created[0][\"persistentDockId\"] not in root[\"linkedDockIds\"] else 1)
" 'Duplicate Dock retained the linked relationship')" \
    || e2e_fail "independent duplicate did not settle"
duplicate_id="$(python3 -c '
import json, sys
before = {int(v) for v in sys.argv[1].split()}
print(next(v["persistentDockId"] for v in json.load(sys.stdin)["views"]
           if v["persistentDockId"] not in before))
' "$before_ids" <<<"$duplicate_state")"

# Editing the relationship after Duplicate Dock exists must still highlight
# only the inactive relationship peers. The snapshot has no durable relation
# from the duplicate to infer, so a cue there would expose relationship leakage.
e2e_call setViewEditMode ub "$remote_id" true >/dev/null \
    || e2e_fail "could not re-enter edit mode for duplicate isolation"
wait_for_views_data "
import json, sys
views = json.load(sys.stdin)
editing = {v[\"containmentId\"] for v in views if v[\"editMode\"]}
highlighted = {v[\"containmentId\"] for v in views if v[\"linkedEditHighlight\"]}
duplicate = next(v for v in views if v[\"containmentId\"] == $duplicate_id)
sys.exit(0 if editing == {$remote_id}
         and highlighted == {$root_id, $same_edge_id}
         and not duplicate[\"linkedEditHighlight\"] and not duplicate[\"editMode\"] else 1)
" 'independent duplicate inherited the linked relationship edit highlight' >/dev/null
e2e_call setViewEditMode ub "$remote_id" false >/dev/null \
    || e2e_fail "could not leave edit mode after duplicate isolation"
wait_for_views_data '
import json, sys
sys.exit(0 if not any(v["editMode"] or v["linkedEditHighlight"] for v in json.load(sys.stdin)) else 1)
' 'duplicate isolation edit state did not clear' >/dev/null

# Recreate the root runtime through the same path used when an installed
# custom indicator changes. Every linked runtime must rotate to the new root
# generation, preserve its containment identity, and converge to identical
# content. The independent duplicate must remain untouched.
before_reload_runtime="$(snapshot | python3 -c '
import json, sys
print(" ".join("%s:%s" % (v["persistentDockId"], v["runtimeViewId"])
               for v in json.load(sys.stdin)["views"]))
')"
before_reload_content="$(view_content_fingerprint "$root_id")" \
    || e2e_fail "could not fingerprint linked content before root recreation"
e2e_call reloadView u "$root_id" >/dev/null \
    || e2e_fail "could not request root runtime recreation"
recreated="$(wait_for_snapshot "
import json, sys
before = {int(pair.split(\":\", 1)[0]): pair.split(\":\", 1)[1]
          for pair in \"$before_reload_runtime\".split()}
views = {v[\"persistentDockId\"]: v for v in json.load(sys.stdin)[\"views\"]}
group = [$root_id, $same_edge_id, $remote_id]
sys.exit(0 if set(views) == {$root_id, $same_edge_id, $remote_id, $duplicate_id}
         and all(views[identity][\"runtimeViewId\"] != before[identity] for identity in group)
         and views[$duplicate_id][\"runtimeViewId\"] == before[$duplicate_id]
         and all(views[identity][\"originalDockId\"] == $root_id
                 for identity in [$same_edge_id, $remote_id]) else 1)
" 'root recreation did not replace and rebind the whole linked runtime group')" \
    || e2e_fail "linked root runtime recreation did not settle"
wait_for_views_data '
import json, sys
sys.exit(0 if not any(v["linkedEditHighlight"] for v in json.load(sys.stdin)) else 1)
' 'root runtime recreation retained a stale linked edit highlight' >/dev/null
recreate_sync=false
for _ in $(seq 1 160); do
    if [[ "$(view_content_fingerprint "$root_id")" == "$before_reload_content" \
            && "$(view_content_fingerprint "$same_edge_id")" == "$before_reload_content" \
            && "$(view_content_fingerprint "$remote_id")" == "$before_reload_content" ]]; then
        recreate_sync=true
        break
    fi
    sleep 0.25
done
[[ "$recreate_sync" == true ]] \
    || e2e_fail "linked content changed or failed to converge after root recreation"
[[ "$(stable_tasks_local_length "$remote_id")" == "$before_remote_local_length" ]] \
    || e2e_fail "root runtime recreation overwrote the remote member's local applet length"

# Pin the unrelated duplicate to the primary output, move the root to the
# secondary output, then disconnect the root output. All linked runtimes must
# park together while every persistent containment survives. Reconnection
# creates a fresh root generation first and rebinds both explicit members.
e2e_call setViewPlacement uiii "$duplicate_id" "$primary_id" 3 0 >/dev/null \
    || e2e_fail "could not pin the independent duplicate to the primary output"
e2e_call setViewPlacement uiii "$remote_id" "$primary_id" 6 2 >/dev/null \
    || e2e_fail "could not move the remote member off the root test output"
e2e_call setViewPlacement uiii "$root_id" "$secondary_id" 4 1 >/dev/null \
    || e2e_fail "could not pin the linked root to the secondary output"
root_disconnect_ready="$(wait_for_snapshot "
import json, sys
views = {v[\"persistentDockId\"]: v for v in json.load(sys.stdin)[\"views\"]}
sys.exit(0 if views[$root_id][\"screenId\"] == $secondary_id
         and views[$remote_id][\"screenId\"] == $primary_id
         and views[$same_edge_id][\"screenId\"] == $primary_id
         and views[$duplicate_id][\"screenId\"] == $primary_id else 1)
" 'views did not settle before the root-output disconnect')" \
    || e2e_fail "root-output disconnect setup did not settle"
before_root_disconnect_runtime="$(python3 -c '
import json, sys
print(" ".join("%s:%s" % (v["persistentDockId"], v["runtimeViewId"])
               for v in json.load(sys.stdin)["views"]))
' <<<"$root_disconnect_ready")"

kscreen-doctor "output.${secondary_name}.disable" >/dev/null \
    || e2e_fail "could not disable the linked root output"
wait_for_active_output_count 1 >/dev/null \
    || e2e_fail "Latte did not observe the linked root output disconnect"
wait_for_snapshot "
import json, sys
views = json.load(sys.stdin)[\"views\"]
sys.exit(0 if len(views) == 1 and views[0][\"persistentDockId\"] == $duplicate_id else 1)
" 'linked member runtime survived without its root coordinator' >/dev/null
for id in "$root_id" "$same_edge_id" "$remote_id"; do
    grep -q "^\\[Containments\\]\\[$id\\]$" "$E2E_LAYOUT" \
        || e2e_fail "root-output disconnect removed persistent containment $id"
done

kscreen-doctor "output.${secondary_name}.enable" \
               "output.${secondary_name}.rotation.left" \
               "output.${secondary_name}.position.2300,180" >/dev/null \
    || e2e_fail "could not reconnect the linked root output"
wait_for_topology >/dev/null \
    || e2e_fail "Latte did not observe the reconnected root output"
root_reconnected="$(wait_for_snapshot "
import json, sys
before = {int(pair.split(\":\", 1)[0]): pair.split(\":\", 1)[1]
          for pair in \"$before_root_disconnect_runtime\".split()}
views = {v[\"persistentDockId\"]: v for v in json.load(sys.stdin)[\"views\"]}
group = [$root_id, $same_edge_id, $remote_id]
sys.exit(0 if set(views) == {$root_id, $same_edge_id, $remote_id, $duplicate_id}
         and all(views[identity][\"runtimeViewId\"] != before[identity] for identity in group)
         and views[$duplicate_id][\"runtimeViewId\"] == before[$duplicate_id]
         and all(views[identity][\"originalDockId\"] == $root_id
                 for identity in [$same_edge_id, $remote_id]) else 1)
" 'root-output reconnect did not create and rebind a fresh relationship generation')" \
    || e2e_fail "linked root output reconnect did not settle"
root_reconnect_sync=false
for _ in $(seq 1 160); do
    if [[ "$(view_content_fingerprint "$root_id")" == "$before_reload_content" \
            && "$(view_content_fingerprint "$same_edge_id")" == "$before_reload_content" \
            && "$(view_content_fingerprint "$remote_id")" == "$before_reload_content" ]]; then
        root_reconnect_sync=true
        break
    fi
    sleep 0.25
done
[[ "$root_reconnect_sync" == true ]] \
    || e2e_fail "linked content failed to converge after the root output reconnected"

# Restore the intended final placement before the process-reload assertion.
e2e_call setViewPlacement uiii "$root_id" "$primary_id" 4 1 >/dev/null \
    || e2e_fail "could not restore the root to the primary output"
e2e_call setViewPlacement uiii "$remote_id" "$secondary_id" 5 1 >/dev/null \
    || e2e_fail "could not restore the remote member to the portrait output"
wait_for_snapshot "
import json, sys
views = {v[\"persistentDockId\"]: v for v in json.load(sys.stdin)[\"views\"]}
sys.exit(0 if views[$root_id][\"screenId\"] == $primary_id
         and views[$root_id][\"edge\"] == \"bottom\"
         and views[$remote_id][\"screenId\"] == $secondary_id
         and views[$remote_id][\"edge\"] == \"left\" else 1)
" 'final linked placement did not settle after output lifecycle tests' >/dev/null

expected_ids="$root_id $same_edge_id $remote_id $duplicate_id"
e2e_dock_stop || e2e_fail "could not stop the dock for linked relationship persistence"
for id in "$same_edge_id" "$remote_id"; do
    [[ "$(kreadconfig6 --file "$E2E_LAYOUT" --group Containments --group "$id" --key isClonedFrom --default -1)" == "$root_id" ]] \
        || e2e_fail "linked member $id lost root $root_id on disk"
    [[ "$(kreadconfig6 --file "$E2E_LAYOUT" --group Containments --group "$id" --key linkPlacement --default -1)" == 1 ]] \
        || e2e_fail "linked member $id lost explicit placement ownership on disk"
done
[[ "$(kreadconfig6 --file "$E2E_LAYOUT" --group Containments --group "$duplicate_id" --key isClonedFrom --default -999)" == -1 ]] \
    || e2e_fail "independent duplicate persisted a relationship root"

e2e_dock_start || e2e_fail "dock did not restart after linked relationship persistence"
reloaded="$(wait_for_snapshot "
import json, sys
views = json.load(sys.stdin)[\"views\"]
expected = {int(v) for v in \"$expected_ids\".split()}
actual = {v[\"persistentDockId\"] for v in views}
root = next((v for v in views if v[\"persistentDockId\"] == $root_id), None)
members = [v for v in views if v[\"relationship\"] == \"linkedMember\"]
independent = next((v for v in views if v[\"persistentDockId\"] == $duplicate_id), None)
sys.exit(0 if actual == expected and root and root[\"linkedDockIds\"] == sorted([$same_edge_id, $remote_id])
         and len(members) == 2 and all(v[\"originalDockId\"] == $root_id and v[\"linkPlacement\"] == \"explicitTarget\" for v in members)
         and next(v for v in members if v[\"persistentDockId\"] == $same_edge_id)[\"screenId\"] == $primary_id
         and next(v for v in members if v[\"persistentDockId\"] == $same_edge_id)[\"edge\"] == \"bottom\"
         and next(v for v in members if v[\"persistentDockId\"] == $remote_id)[\"screenId\"] == $secondary_id
         and next(v for v in members if v[\"persistentDockId\"] == $remote_id)[\"edge\"] == \"left\"
         and independent and independent[\"relationship\"] == \"independent\"
         and not any(v[\"editMode\"] for v in views) else 1)
" 'persistence reload changed dock membership, lineage, or edit ownership')" \
    || e2e_fail "linked relationship reload did not settle"

python3 -c '
import json, sys
state = json.load(sys.stdin)
views = state["views"]
runtime_ids = [v["runtimeViewId"] for v in views]
containments = [v["persistentDockId"] for v in views]
if len(runtime_ids) != len(set(runtime_ids)) or len(containments) != len(set(containments)):
    sys.exit("reload produced a forbidden duplicate identity")
for key in ("view", "containment", "configuration", "layoutController", "geometryController", "editController"):
    values = [v["objects"][key] for v in views]
    if any(value is None for value in values) or len(values) != len(set(values)):
        sys.exit("reload shared per-view object %s: %s" % (key, values))
' <<<"$reloaded" || e2e_fail "runtime ownership was shared after reload"

echo "create linked dock: root $root_id, occupied-edge member $same_edge_id, portrait-output member $remote_id, independent duplicate $duplicate_id; placement, sizing, edit mode, applets, and reload passed"
