#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Deterministic linked-dock operation storm. Every mutating action is recorded
# with resolved persistent identities so a failed seed can be replayed from the
# artifact without reconstructing random choices.
# e2e-mode: nested-only
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-multi-output-e2e.sh}/tests/e2e/lib.sh"

[[ "${E2E_OUTPUT_COUNT:-1}" -eq 2 ]] \
    || e2e_fail "linked-dock-operation-stress needs the dual-output vehicle"

readonly stress_seed="${LATTE_LINKED_STRESS_SEED:-127934575}"
readonly replay="${E2E_ARTIFACTS:?}/linked-dock-operation-stress.seed-${stress_seed}.replay"
readonly settled_before_file="${E2E_ARTIFACTS}/linked-dock-operation-stress.seed-${stress_seed}.settled-before.json"
readonly settled_after_file="${E2E_ARTIFACTS}/linked-dock-operation-stress.seed-${stress_seed}.settled-after.json"
operation_index=0
: >"$replay"
printf 'seed=%s\n' "$stress_seed" >>"$replay"

snapshot() { e2e_json dockSystemData; }

record_operation() {
    operation_index=$((operation_index + 1))
    printf '%03d|%s\n' "$operation_index" "$*" | tee -a "$replay" >/dev/null
}

wait_for_snapshot() {
    local predicate="$1" label="$2" i current
    if ! python3 -c 'import sys; compile(sys.argv[1], "<stress predicate>", "exec")' "$predicate"; then
        printf '%s\n' "$predicate" >&2
        e2e_fail "invalid stress predicate for: $label"
    fi

    for ((i = 0; i < 240; ++i)); do
        current="$(snapshot)"
        if python3 -c "$predicate" <<<"$current" >/dev/null; then
            printf '%s\n' "$current"
            return 0
        fi
        sleep 0.25
    done

    printf 'last dockSystemData: %s\nreplay: %s\n' "$current" "$replay" >&2
    e2e_fail "$label"
}

new_view_id() {
    local before_ids="$1" state="$2"
    python3 -c '
import json, sys
before = {int(value) for value in sys.argv[1].split()}
created = [v["persistentDockId"] for v in json.load(sys.stdin)["views"]
           if v["persistentDockId"] not in before]
if len(created) != 1:
    sys.exit("expected exactly one new view, got %r" % created)
print(created[0])
' "$before_ids" <<<"$state"
}

all_view_ids() {
    snapshot | python3 -c '
import json, sys
print(" ".join(str(v["persistentDockId"]) for v in json.load(sys.stdin)["views"]))
'
}

expected_alignment() {
    local edge="$1" alignment="$2"
    if [[ "$alignment" -eq 0 ]]; then
        echo center
    elif [[ "$edge" -eq 3 || "$edge" -eq 4 ]]; then
        [[ "$alignment" -eq 1 ]] && echo left || echo right
    else
        [[ "$alignment" -eq 1 ]] && echo top || echo bottom
    fi
}

edge_name() {
    case "$1" in
        3) echo top ;;
        4) echo bottom ;;
        5) echo left ;;
        6) echo right ;;
        *) e2e_fail "invalid generated edge $1" ;;
    esac
}

drive_placement() {
    local target="$1" screen="$2" edge="$3" alignment="$4"
    local expected_edge expected_align
    expected_edge="$(edge_name "$edge")"
    expected_align="$(expected_alignment "$edge" "$alignment")"
    record_operation "setViewPlacement containment=$target screen=$screen edge=$edge alignment=$alignment"
    e2e_call setViewPlacement uiii "$target" "$screen" "$edge" "$alignment" >/dev/null \
        || e2e_fail "placement action failed; replay: $replay"
    wait_for_snapshot "
import json, sys
view = next((v for v in json.load(sys.stdin)[\"views\"] if v[\"persistentDockId\"] == $target), None)
sys.exit(0 if view and view[\"screenId\"] == $screen
         and view[\"edge\"] == \"$expected_edge\"
         and view[\"alignment\"] == \"$expected_align\"
         and not view[\"inRelocationAnimation\"] else 1)
" "placement did not settle for containment $target" >/dev/null
}

drive_edit_round_trip() {
    local target="$1"
    record_operation "setViewEditMode containment=$target enabled=true"
    e2e_call setViewEditMode ub "$target" true >/dev/null \
        || e2e_fail "edit entry failed; replay: $replay"
    wait_for_snapshot "
import json, sys
editing = [v[\"persistentDockId\"] for v in json.load(sys.stdin)[\"views\"] if v[\"editMode\"]]
sys.exit(0 if editing == [$target] else 1)
" "edit ownership escaped containment $target" >/dev/null

    record_operation "setViewEditMode containment=$target enabled=false"
    e2e_call setViewEditMode ub "$target" false >/dev/null \
        || e2e_fail "edit exit failed; replay: $replay"
    wait_for_snapshot '
import json, sys
sys.exit(0 if not any(v["editMode"] for v in json.load(sys.stdin)["views"]) else 1)
' "edit presentation did not close for containment $target" >/dev/null
}

read -r primary_id secondary_id <<<"$(
    e2e_json screensData | python3 -c '
import json, sys
active = [s for s in json.load(sys.stdin) if s["isActive"]]
primary = next(s for s in active if s["isPrimary"])
secondary = next(s for s in active if not s["isPrimary"])
print(primary["id"], secondary["id"])
')"

root_id="$(snapshot | python3 -c '
import json, sys
views = json.load(sys.stdin)["views"]
if len(views) != 1 or views[0]["relationship"] != "independent":
    sys.exit("expected one independent seed dock")
print(views[0]["persistentDockId"])
')" || e2e_fail "could not identify the stress root"

before_ids="$(all_view_ids)"
record_operation "createLinkedView source=$root_id screen=$primary_id edge=4"
e2e_call createLinkedView uii "$root_id" "$primary_id" 4 >/dev/null \
    || e2e_fail "could not create the occupied-edge stress member"
created="$(wait_for_snapshot "
import json, sys
before = {int(v) for v in \"$before_ids\".split()}
created = [v for v in json.load(sys.stdin)[\"views\"] if v[\"persistentDockId\"] not in before]
sys.exit(0 if len(created) == 1 and created[0][\"relationship\"] == \"linkedMember\" else 1)
" "occupied-edge stress member did not settle")"
member_a="$(new_view_id "$before_ids" "$created")"

before_ids="$(all_view_ids)"
record_operation "createLinkedView source=$member_a screen=$secondary_id edge=5"
e2e_call createLinkedView uii "$member_a" "$secondary_id" 5 >/dev/null \
    || e2e_fail "could not create the cross-output stress member"
created="$(wait_for_snapshot "
import json, sys
before = {int(v) for v in \"$before_ids\".split()}
created = [v for v in json.load(sys.stdin)[\"views\"] if v[\"persistentDockId\"] not in before]
sys.exit(0 if len(created) == 1 and created[0][\"originalDockId\"] == $root_id else 1)
" "cross-output stress member did not settle")"
member_b="$(new_view_id "$before_ids" "$created")"

before_ids="$(all_view_ids)"
record_operation "duplicateView source=$member_a"
e2e_call duplicateView u "$member_a" >/dev/null || e2e_fail "first stress duplicate failed"
created="$(wait_for_snapshot "
import json, sys
before = {int(v) for v in \"$before_ids\".split()}
created = [v for v in json.load(sys.stdin)[\"views\"] if v[\"persistentDockId\"] not in before]
sys.exit(0 if len(created) == 1 and created[0][\"relationship\"] == \"independent\" else 1)
" "first independent stress duplicate did not settle")"
duplicate_a="$(new_view_id "$before_ids" "$created")"

targets=("$root_id" "$member_a" "$member_b" "$duplicate_a")
screens=("$primary_id" "$secondary_id")
mapfile -t first_plan < <(python3 - "$stress_seed" <<'PY'
import random, sys
rng = random.Random(int(sys.argv[1]))
for index in range(14):
    print(rng.randrange(4), rng.randrange(2), rng.choice((3, 4, 5, 6)), rng.choice((0, 1, 2)), index % 4 == 0)
PY
)
for row in "${first_plan[@]}"; do
    read -r target_slot screen_slot edge alignment edit_round <<<"$row"
    drive_placement "${targets[$target_slot]}" "${screens[$screen_slot]}" "$edge" "$alignment"
    [[ "$edit_round" == True ]] && drive_edit_round_trip "${targets[$target_slot]}"
done

before_ids="$(all_view_ids)"
record_operation "duplicateView source=$member_b"
e2e_call duplicateView u "$member_b" >/dev/null || e2e_fail "second stress duplicate failed"
created="$(wait_for_snapshot "
import json, sys
before = {int(v) for v in \"$before_ids\".split()}
created = [v for v in json.load(sys.stdin)[\"views\"] if v[\"persistentDockId\"] not in before]
sys.exit(0 if len(created) == 1 and created[0][\"relationship\"] == \"independent\" else 1)
" "second independent stress duplicate did not settle")"
duplicate_b="$(new_view_id "$before_ids" "$created")"

record_operation "removeView containment=$member_a"
e2e_call removeView u "$member_a" >/dev/null || e2e_fail "stress member removal failed"
wait_for_snapshot "
import json, sys
state = json.load(sys.stdin)
ids = {v[\"persistentDockId\"] for v in state[\"views\"]}
root = next(v for v in state[\"views\"] if v[\"persistentDockId\"] == $root_id)
sys.exit(0 if $member_a not in ids and $member_a not in root[\"linkedDockIds\"] else 1)
" "removed linked member remained registered" >/dev/null

# Plasma's removal undo window is part of the public action. Wait for the
# containment group itself to retire so restart cannot resurrect the member.
removed_on_disk=false
for _ in $(seq 1 150); do
    if [[ "$(kreadconfig6 --file "$E2E_LAYOUT" --group Containments --group "$member_a" --key plugin --default absent)" == absent ]]; then
        removed_on_disk=true
        break
    fi
    sleep 0.5
done
[[ "$removed_on_disk" == true ]] \
    || e2e_fail "removed linked member survived its undo window on disk; replay: $replay"

before_ids="$(all_view_ids)"
record_operation "createLinkedView source=$member_b screen=$primary_id edge=3"
e2e_call createLinkedView uii "$member_b" "$primary_id" 3 >/dev/null \
    || e2e_fail "replacement linked member creation failed"
created="$(wait_for_snapshot "
import json, sys
before = {int(v) for v in \"$before_ids\".split()}
created = [v for v in json.load(sys.stdin)[\"views\"] if v[\"persistentDockId\"] not in before]
sys.exit(0 if len(created) == 1 and created[0][\"relationship\"] == \"linkedMember\"
         and created[0][\"originalDockId\"] == $root_id else 1)
" "replacement linked member did not settle")"
member_c="$(new_view_id "$before_ids" "$created")"

targets=("$root_id" "$member_b" "$duplicate_a" "$duplicate_b" "$member_c")
mapfile -t second_plan < <(python3 - "$stress_seed" <<'PY'
import random, sys
rng = random.Random(int(sys.argv[1]) ^ 0x4c415454)
for index in range(14):
    print(rng.randrange(5), rng.randrange(2), rng.choice((3, 4, 5, 6)), rng.choice((0, 1, 2)), index % 5 == 0)
PY
)
for row in "${second_plan[@]}"; do
    read -r target_slot screen_slot edge alignment edit_round <<<"$row"
    drive_placement "${targets[$target_slot]}" "${screens[$screen_slot]}" "$edge" "$alignment"
    [[ "$edit_round" == True ]] && drive_edit_round_trip "${targets[$target_slot]}"
done

expected_ids="$root_id $member_b $duplicate_a $duplicate_b $member_c"
settled_before="$(wait_for_snapshot "
import json, sys
state = json.load(sys.stdin)
views = state[\"views\"]
expected = {int(v) for v in \"$expected_ids\".split()}
ids = {v[\"persistentDockId\"] for v in views}
root = next((v for v in views if v[\"persistentDockId\"] == $root_id), None)
members = [v for v in views if v[\"relationship\"] == \"linkedMember\"]
independent = [v for v in views if v[\"relationship\"] == \"independent\"]
runtime_ids = [v[\"runtimeViewId\"] for v in views]
sys.exit(0 if ids == expected and root and sorted(root[\"linkedDockIds\"]) == sorted([$member_b, $member_c])
         and len(members) == 2 and all(v[\"originalDockId\"] == $root_id for v in members)
         and len(independent) == 2 and len(runtime_ids) == len(set(runtime_ids))
         and all(v[\"availablePrimaryLength\"] is not None for v in views)
         and all(v[\"geometrySettled\"]
                 and v[\"relocationGeneration\"] == v[\"appliedRelocationGeneration\"] for v in views)
         and not any(v[\"editMode\"] or v[\"inStartup\"] or v[\"inRelocationAnimation\"]
                     or v[\"inRelocationShowing\"] for v in views)
         else 1)
" "operation storm did not converge")"
printf '%s\n' "$settled_before" >"$settled_before_file"
sleep 2
settled_after="$(snapshot)"
printf '%s\n' "$settled_after" >"$settled_after_file"
python3 -c '
import json, sys
def stable(payload):
    state = json.loads(payload)
    fields = ("persistentDockId", "relationship", "originalDockId", "linkPlacement",
              "screenId", "edge", "orientation", "alignment", "configuredIconSize",
              "effectiveIconSize", "availablePrimaryLength", "absoluteGeometry",
              "localGeometry", "windowGeometry", "visibleGeometry", "linkedDockIds")
    return [{key: view.get(key) for key in fields}
            for view in sorted(state["views"], key=lambda view: view["persistentDockId"])]
before, after = sys.stdin.read().split("\n---\n", 1)
before, after = stable(before), stable(after)
if before != after:
    before_by_id = {view["persistentDockId"]: view for view in before}
    after_by_id = {view["persistentDockId"]: view for view in after}
    for dock_id in sorted(before_by_id.keys() | after_by_id.keys()):
        old, new = before_by_id.get(dock_id, {}), after_by_id.get(dock_id, {})
        for key in sorted(old.keys() | new.keys()):
            if old.get(key) != new.get(key):
                print("dock %s %s: %r -> %r" % (dock_id, key, old.get(key), new.get(key)), file=sys.stderr)
    sys.exit("dock geometry or sizing changed after convergence")
' <<<"$settled_before
---
$settled_after" || e2e_fail "post-storm state did not converge; replay: $replay"

record_operation "restart expected=$expected_ids"
e2e_dock_stop || e2e_fail "stress dock did not stop cleanly"
e2e_dock_start 120 || e2e_fail "stress dock did not restart cleanly"
reloaded="$(wait_for_snapshot "
import json, sys
views = json.load(sys.stdin)[\"views\"]
expected = {int(v) for v in \"$expected_ids\".split()}
ids = {v[\"persistentDockId\"] for v in views}
root = next((v for v in views if v[\"persistentDockId\"] == $root_id), None)
sys.exit(0 if ids == expected and root and sorted(root[\"linkedDockIds\"]) == sorted([$member_b, $member_c])
         and all(v[\"originalDockId\"] == $root_id for v in views if v[\"relationship\"] == \"linkedMember\")
         and all(v[\"geometrySettled\"]
                 and v[\"relocationGeneration\"] == v[\"appliedRelocationGeneration\"] for v in views)
         and not any(v[\"editMode\"] or v[\"inStartup\"] or v[\"inRelocationAnimation\"]
                     or v[\"inRelocationShowing\"] for v in views)
         else 1)
" "operation storm did not survive persistence reload")"

python3 -c '
import json, sys
views = json.load(sys.stdin)["views"]
for key in ("runtimeViewId",):
    values = [v[key] for v in views]
    if len(values) != len(set(values)):
        sys.exit("reload duplicated %s: %r" % (key, values))
for key in ("view", "containment", "configuration", "layoutController", "geometryController", "editController"):
    values = [v["objects"][key] for v in views]
    if any(value is None for value in values) or len(values) != len(set(values)):
        sys.exit("reload shared per-view object %s: %r" % (key, values))
' <<<"$reloaded" || e2e_fail "reload violated runtime ownership; replay: $replay"

printf 'final=%s\n' "$reloaded" >>"$replay"
echo "linked dock stress: seed $stress_seed replay $replay; 5 intended views restored after 28 placements, 7 edit transitions, 2 duplicates, removal, replacement, and restart"
