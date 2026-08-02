#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# A partial-width bottom dock may publish Wayland's edge-wide scalar exclusive
# zone for ordinary windows, but a Latte-owned right dock that does not
# intersect that footprint must retain Positioner's exact lower extent.
# e2e-mode: nested-only
set -u

source "${E2E_REPO:?run through scripts/run-multi-output-e2e.sh}/tests/e2e/lib.sh"

[[ "${E2E_OUTPUT_COUNT:-1}" -eq 2 ]] \
    || e2e_fail "061 partial-reservation placement needs the dual-output vehicle"

e2e_wait_settled 45 || e2e_fail "vehicle dock never settled"

initial="$(e2e_json dockSystemData)"
read -r partial_bottom full_bottom right_view <<< "$(python3 -c '
import json, sys
views = json.load(sys.stdin)["views"]
bottom = [v for v in views if v["edge"] == "bottom"]
partial = [v for v in bottom if v["maximumLengthRatio"] < 0.9]
full = [v for v in bottom if v["maximumLengthRatio"] >= 0.9
        and v["visibilityMode"] == "alwaysVisible"]
right = [v for v in views if v["edge"] == "right"]
if len(partial) != 1 or len(full) != 1 or len(right) != 1:
    raise SystemExit(
        "fixture needs one partial bottom, one reserving full bottom, and one right dock"
    )
print(partial[0]["persistentDockId"],
      full[0]["persistentDockId"],
      right[0]["persistentDockId"])
' <<<"$initial")" || e2e_fail "could not classify the partial-reservation fixture"

e2e_call setViewVisibilityMode us "$full_bottom" dodgeActive >/dev/null \
    || e2e_fail "could not release the full-width bottom reservation"
e2e_call setViewVisibilityMode us "$partial_bottom" dodgeActive >/dev/null \
    || e2e_fail "could not establish the non-reserving partial-dock baseline"

baseline=""
for _ in $(seq 1 100); do
    current="$(e2e_json dockSystemData)"
    if python3 -c '
import json, sys
state = json.load(sys.stdin)
views = {v["persistentDockId"]: v for v in state["views"]}
partial, full, right = (int(value) for value in sys.argv[1:4])
selected = {partial, full}
ok = (
    views[partial]["visibilityMode"] == "dodgeActive"
    and views[partial]["publishedStruts"] == [0, 0, 0, 0]
    and views[full]["visibilityMode"] == "dodgeActive"
    and views[full]["publishedStruts"] == [0, 0, 0, 0]
    and views[right]["geometrySettled"]
    and not any(
        selected.intersection(group["contributorDockIds"])
        for group in state["reservationGroups"]
    )
)
raise SystemExit(0 if ok else 1)
' "$partial_bottom" "$full_bottom" "$right_view" <<<"$current"; then
        baseline="$current"
        break
    fi
    sleep 0.2
done
[[ -n "$baseline" ]] \
    || e2e_fail "non-reserving partial-dock baseline did not settle"

read -r baseline_generation baseline_right <<< "$(python3 -c '
import json, sys
state = json.load(sys.stdin)
view = next(v for v in state["views"]
            if v["persistentDockId"] == int(sys.argv[1]))
print(state["reservationStateGeneration"],
      ",".join(str(value) for value in view["surfaceGeometry"]))
' "$right_view" <<<"$baseline")"
baseline_right="${baseline_right//,/ }"

e2e_call setViewVisibilityMode us "$partial_bottom" alwaysVisible >/dev/null \
    || e2e_fail "could not publish the partial-width bottom reservation"

settled=""
last=""
for _ in $(seq 1 100); do
    current="$(e2e_json dockSystemData)"
    last="$current"
    if python3 -c '
import json, sys
state = json.load(sys.stdin)
views = {v["persistentDockId"]: v for v in state["views"]}
partial, full, right = (int(value) for value in sys.argv[1:4])
surface = views[right]["surfaceGeometry"]
screen = views[right]["screenGeometry"]
groups = [
    group for group in state["reservationGroups"]
    if partial in group["contributorDockIds"]
]
group = groups[0] if len(groups) == 1 else None
expected_margins = [
    surface[0] - screen[0],
    surface[1] - screen[1],
    0,
    0,
]
ok = (
    views[partial]["visibilityMode"] == "alwaysVisible"
    and views[partial]["publishedStruts"][3] > 0
    and views[partial]["reservationSurfacePresent"]
    and views[partial]["reservationOutputId"] == views[partial]["screenId"]
    and views[partial]["reservationEdge"] == "bottom"
    and views[partial]["reservationContributionDepth"]
        == views[partial]["strutsThickness"]
    and views[partial]["reservationPublishedDepth"]
        == views[partial]["strutsThickness"]
    and views[partial]["reservationGroupMemberCount"] == 1
    and views[partial]["reservationContributorDockIds"] == [partial]
    and group is not None
    and group["outputId"] == views[partial]["screenId"]
    and group["edge"] == "bottom"
    and group["contributorDockIds"] == [partial]
    and group["memberCount"] == 1
    and group["publishedDepth"] == views[partial]["strutsThickness"]
    and group["publisher"] == views[partial]["objects"]["reservationPublisher"]
    and group["layerShellPresent"]
    and group["geometry"] == views[partial]["reservationGeometry"]
    and group["generation"] == views[partial]["reservationGroupGeneration"]
    and int(group["generation"]) > int(sys.argv[4])
    and int(state["reservationStateGeneration"]) >= int(group["generation"])
    and views[partial]["reservationGeometry"] == [
        views[partial]["screenGeometry"][0],
        views[partial]["screenGeometry"][1]
            + views[partial]["screenGeometry"][3]
            - views[partial]["strutsThickness"],
        views[partial]["screenGeometry"][2],
        views[partial]["strutsThickness"],
    ]
    and views[partial]["reservationLayerShellAnchors"] == ["bottom", "left", "right"]
    and views[partial]["reservationLayerShellExclusiveEdge"] == "bottom"
    and views[partial]["reservationLayerShellExclusiveZone"]
        == views[partial]["strutsThickness"]
    and views[partial]["objects"]["reservationPublisher"] is not None
    and views[full]["visibilityMode"] == "dodgeActive"
    and views[full]["publishedStruts"] == [0, 0, 0, 0]
    and not views[full]["reservationSurfacePresent"]
    and views[full]["objects"]["reservationPublisher"] is None
    and views[right]["geometrySettled"]
    and views[right]["layerShellPresent"]
    and views[right]["layerShellAnchors"] == ["top", "left"]
    and views[right]["layerShellMargins"] == expected_margins
    and views[right]["layerShellExclusiveEdge"] == "none"
    and views[right]["layerShellExclusiveZone"] == -1
)
raise SystemExit(0 if ok else 1)
' "$partial_bottom" "$full_bottom" "$right_view" \
        "$baseline_generation" <<<"$current"; then
        settled="$current"
        break
    fi
    sleep 0.2
done
if [[ -z "$settled" ]]; then
    python3 -m json.tool <<<"$last" >&2
    e2e_fail "partial reservation did not converge to the exact layer-shell placement"
fi

read -r partial_generation rx ry rw rh bottom_end right_start <<< "$(python3 -c '
import json, sys
state = json.load(sys.stdin)
views = {v["persistentDockId"]: v for v in state["views"]}
partial, right = (int(value) for value in sys.argv[1:3])
r = views[right]
p = views[partial]
print(p["reservationGroupGeneration"],
      *r["surfaceGeometry"],
      p["absoluteGeometry"][0] + p["absoluteGeometry"][2],
      r["absoluteGeometry"][0])
' "$partial_bottom" "$right_view" <<<"$settled")"

(( bottom_end < right_start )) \
    || e2e_fail "fixture footprints intersect, so the partial-reservation case was not exercised"

[[ "$rx $ry $rw $rh" == "$baseline_right" ]] \
    || e2e_fail "partial reservation moved the non-intersecting right surface"

rendered="$(e2e_dumpwins | awk -F'|' \
    -v x="$rx" -v y="$ry" -v w="$rw" -v h="$rh" '
    $2 ~ /latte-dock/ && $6 == "layer=3" {
        split($4, geometry, " ")
        split(geometry[1], position, ",")
        split(geometry[2], size, "x")
        if (position[1] == x && position[2] == y &&
                size[1] == w && size[2] == h) {
            print $0
            exit
        }
    }')"
[[ -n "$rendered" ]] \
    || e2e_fail "KWin did not render the right surface at Positioner's exact rectangle"

e2e_call setViewVisibilityMode us "$full_bottom" alwaysVisible >/dev/null \
    || e2e_fail "could not add the second bottom reservation contribution"

shared=""
last=""
for _ in $(seq 1 100); do
    current="$(e2e_json dockSystemData)"
    last="$current"
    if python3 -c '
import json, sys
state = json.load(sys.stdin)
views = {v["persistentDockId"]: v for v in state["views"]}
partial, full, right = (int(value) for value in sys.argv[1:4])
p, f, r = views[partial], views[full], views[right]
depth = max(p["reservationContributionDepth"],
            f["reservationContributionDepth"])
screen = p["screenGeometry"]
expected = [screen[0], screen[1] + screen[3] - depth, screen[2], depth]
groups = [
    group for group in state["reservationGroups"]
    if partial in group["contributorDockIds"]
    or full in group["contributorDockIds"]
]
group = groups[0] if len(groups) == 1 else None
ok = (
    p["visibilityMode"] == "alwaysVisible"
    and f["visibilityMode"] == "alwaysVisible"
    and p["reservationOutputId"] == f["reservationOutputId"] == p["screenId"]
    and p["reservationGroupMemberCount"] == f["reservationGroupMemberCount"] == 2
    and p["reservationEdge"] == f["reservationEdge"] == "bottom"
    and p["reservationGroupGeneration"] == f["reservationGroupGeneration"]
    and p["reservationContributorDockIds"]
        == f["reservationContributorDockIds"] == sorted([partial, full])
    and p["reservationPublishedDepth"] == f["reservationPublishedDepth"] == depth
    and depth != (
        p["reservationContributionDepth"] + f["reservationContributionDepth"])
    and p["reservationGeometry"] == f["reservationGeometry"] == expected
    and p["reservationLayerShellExclusiveZone"]
        == f["reservationLayerShellExclusiveZone"] == depth
    and p["objects"]["reservationPublisher"]
        == f["objects"]["reservationPublisher"]
    and p["objects"]["reservationPublisher"] is not None
    and group is not None
    and group["outputId"] == p["screenId"]
    and group["edge"] == "bottom"
    and group["contributorDockIds"] == sorted([partial, full])
    and group["memberCount"] == 2
    and group["publishedDepth"] == depth
    and group["publisher"] == p["objects"]["reservationPublisher"]
    and group["generation"] == p["reservationGroupGeneration"]
    and group["layerShellPresent"]
    and int(group["generation"]) > int(sys.argv[8])
    and int(state["reservationStateGeneration"]) >= int(group["generation"])
    and r["surfaceGeometry"] == [int(value) for value in sys.argv[4:8]]
)
raise SystemExit(0 if ok else 1)
' "$partial_bottom" "$full_bottom" "$right_view" \
        "$rx" "$ry" "$rw" "$rh" "$partial_generation" <<<"$current"; then
        shared="$current"
        break
    fi
    sleep 0.2
done
if [[ -z "$shared" ]]; then
    python3 -m json.tool <<<"$last" >&2
    e2e_fail "same-edge reservations did not converge on one maximum-depth publisher"
fi

read -r screen_id shared_depth shared_members full_alignment shared_generation <<< "$(python3 -c '
import json, sys
state = json.load(sys.stdin)
views = {v["persistentDockId"]: v for v in state["views"]}
p = views[int(sys.argv[1])]
f = views[int(sys.argv[2])]
print(p["screenId"], p["reservationPublishedDepth"],
      p["reservationGroupMemberCount"],
      {
          "center": 0,
          "left": 1,
          "right": 2,
          "top": 3,
          "bottom": 4,
          "justify": 10,
      }[f["alignment"]],
      p["reservationGroupGeneration"])
' "$partial_bottom" "$full_bottom" <<<"$shared")"

[[ "$shared_members" == 2 ]] \
    || e2e_fail "shared reservation member count drifted before migration"

e2e_call setViewPlacement uiii "$full_bottom" "$screen_id" 3 "$full_alignment" >/dev/null \
    || e2e_fail "could not migrate the second reservation to the top edge"

migrated=""
last=""
for _ in $(seq 1 150); do
    current="$(e2e_json dockSystemData)"
    last="$current"
    if python3 -c '
import json, sys
state = json.load(sys.stdin)
views = {v["persistentDockId"]: v for v in state["views"]}
partial, moved = (int(value) for value in sys.argv[1:3])
p, m = views[partial], views[moved]
screen = p["screenGeometry"]
if (m["reservationOutputId"] is None
        or m["reservationContributionDepth"] is None):
    raise SystemExit(1)
top_members = [
    view for view in views.values()
    if view["edge"] == "top"
    and view["reservationOutputId"] == m["reservationOutputId"]
    and view["reservationContributionDepth"] is not None
]
top_depth = max(
    view["reservationContributionDepth"] for view in top_members
)
top_publisher = m["objects"]["reservationPublisher"]
p_groups = [
    group for group in state["reservationGroups"]
    if partial in group["contributorDockIds"]
]
m_groups = [
    group for group in state["reservationGroups"]
    if moved in group["contributorDockIds"]
]
p_group = p_groups[0] if len(p_groups) == 1 else None
m_group = m_groups[0] if len(m_groups) == 1 else None
p_expected = [
    screen[0],
    screen[1] + screen[3] - p["reservationContributionDepth"],
    screen[2],
    p["reservationContributionDepth"],
]
m_expected = [
    screen[0],
    screen[1],
    screen[2],
    top_depth,
]
ok = (
    p["edge"] == "bottom"
    and m["edge"] == "top"
    and p["reservationEdge"] == "bottom"
    and m["reservationEdge"] == "top"
    and p["geometrySettled"] and m["geometrySettled"]
    and p["reservationGroupMemberCount"] == 1
    and m["reservationGroupMemberCount"] == len(top_members)
    and p["reservationPublishedDepth"] == p["reservationContributionDepth"]
    and m["reservationPublishedDepth"] == top_depth
    and p["reservationGeometry"] == p_expected
    and m["reservationGeometry"] == m_expected
    and top_publisher is not None
    and all(
        view["reservationGroupMemberCount"] == len(top_members)
        and view["reservationPublishedDepth"] == top_depth
        and view["reservationGeometry"] == m_expected
        and view["objects"]["reservationPublisher"] == top_publisher
        for view in top_members
    )
    and p["objects"]["reservationPublisher"]
        != top_publisher
    and p_group is not None
    and m_group is not None
    and p_group["edge"] == "bottom"
    and m_group["edge"] == "top"
    and p_group["publisher"] == p["objects"]["reservationPublisher"]
    and m_group["publisher"] == top_publisher
    and p_group["contributorDockIds"] == [partial]
    and m_group["contributorDockIds"]
        == sorted(view["persistentDockId"] for view in top_members)
    and p_group["generation"] == p["reservationGroupGeneration"]
    and m_group["generation"] == m["reservationGroupGeneration"]
    and int(p_group["generation"]) == int(m_group["generation"])
    and int(p_group["generation"]) > int(sys.argv[3])
    and int(state["reservationStateGeneration"])
        == int(p_group["generation"])
)
raise SystemExit(0 if ok else 1)
' "$partial_bottom" "$full_bottom" "$shared_generation" <<<"$current"; then
        migrated="$current"
        break
    fi
    sleep 0.2
done
if [[ -z "$migrated" ]]; then
    python3 -m json.tool <<<"$last" >&2
    e2e_fail "edge migration left stale or incompatible reservation membership"
fi

read -r fallback_depth migration_generation <<< "$(python3 -c '
import json, sys
state = json.load(sys.stdin)
view = next(v for v in state["views"]
            if v["persistentDockId"] == int(sys.argv[1]))
print(view["reservationPublishedDepth"],
      state["reservationStateGeneration"])
' "$partial_bottom" <<<"$migrated")"
[[ "$fallback_depth" -le "$shared_depth" ]] \
    || e2e_fail "removing a same-edge member increased the surviving depth"

read -r target_screen_id target_screen_name <<< "$(python3 -c '
import json, sys
secondary = [
    screen for screen in json.load(sys.stdin)
    if screen["isActive"] and not screen["isPrimary"]
]
if len(secondary) != 1:
    raise SystemExit(
        "fixture needs exactly one active secondary output"
    )
print(secondary[0]["id"], secondary[0]["name"])
' <<<"$(e2e_json screensData)")" \
    || e2e_fail "could not discover the secondary output"

e2e_call setViewPlacement uiii "$full_bottom" \
    "$target_screen_id" 3 "$full_alignment" >/dev/null \
    || e2e_fail "could not migrate the reservation to the secondary output"

# setViewPlacement schedules an animated relocation and returns before the
# output change commits. Poll the older per-view surface only until its
# publishedStruts lies on the target output; a merely nonempty rectangle can
# still be the committed source-output contribution while the relocation is
# between its compositor-screen and reservation transactions. The rectangle
# changes only after the coordinator transaction succeeds. The first
# dockSystemData read after this observable boundary must therefore expose
# schema 11 and the new membership. It is deliberately not retried: an empty
# snapshot here was the stale-QWindow-screen collector race.
member_move_committed=""
for _ in $(seq 1 150); do
    current="$(e2e_json viewsData)"
    if python3 -c '
import json, sys
views = {
    view["containmentId"]: view
    for view in json.load(sys.stdin)
}
moved = views[int(sys.argv[1])]
sx, sy, sw, sh = moved["screenGeometry"]
px, py, pw, ph = moved["publishedStruts"]
struts_on_target = (
    pw > 0 and ph > 0
    and sx <= px and sy <= py
    and px + pw <= sx + sw
    and py + ph <= sy + sh
    and py == sy
)
ok = (
    moved["screen"] == sys.argv[2]
    and moved["edge"] == "top"
    and struts_on_target
    and not moved["inStartup"]
    and not moved["isOffScreen"]
)
raise SystemExit(0 if ok else 1)
' "$full_bottom" "$target_screen_name" <<<"$current"; then
        member_move_committed="$current"
        break
    fi
    sleep 0.2
done
[[ -n "$member_move_committed" ]] \
    || e2e_fail "output migration never reached the member publication boundary"

immediate_output_move="$(e2e_json dockSystemData)" \
    || e2e_fail "first coordinator snapshot after output migration failed"
[[ -n "$immediate_output_move" ]] \
    || e2e_fail "first coordinator snapshot after output migration was empty"
python3 -c '
import json, sys
state = json.load(sys.stdin)
views = {v["persistentDockId"]: v for v in state["views"]}
moved, source_output, target_output = (
    int(value) for value in sys.argv[1:4]
)
m = views[moved]
groups = [
    group for group in state["reservationGroups"]
    if moved in group["contributorDockIds"]
]
group = groups[0] if len(groups) == 1 else None
ok = (
    state["schemaVersion"] == 11
    and m["screenId"] == target_output
    and m["reservationOutputId"] == target_output
    and m["edge"] == m["reservationEdge"] == "top"
    and m["reservationSurfacePresent"]
    and m["publishedStruts"] != [0, 0, 0, 0]
    and group is not None
    and group["outputId"] == target_output
    and group["edge"] == "top"
    and group["publisher"] == m["objects"]["reservationPublisher"]
    and group["generation"] == m["reservationGroupGeneration"]
    and not any(
        moved in candidate["contributorDockIds"]
        and candidate["outputId"] == source_output
        for candidate in state["reservationGroups"]
    )
)
raise SystemExit(0 if ok else 1)
' "$full_bottom" "$screen_id" "$target_screen_id" \
    <<<"$immediate_output_move" \
    || e2e_fail "first coordinator snapshot did not atomically expose the new output membership"

output_moved=""
last=""
for _ in $(seq 1 150); do
    current="$(e2e_json dockSystemData)"
    last="$current"
    if [[ -n "$current" ]] && python3 -c '
import json, sys
state = json.load(sys.stdin)
views = {v["persistentDockId"]: v for v in state["views"]}
partial, moved, source_output, target_output = (
    int(value) for value in sys.argv[1:5]
)
target_name = sys.argv[5]
previous_generation = int(sys.argv[6])
p, m = views[partial], views[moved]
p_groups = [
    group for group in state["reservationGroups"]
    if partial in group["contributorDockIds"]
]
m_groups = [
    group for group in state["reservationGroups"]
    if moved in group["contributorDockIds"]
]
p_group = p_groups[0] if len(p_groups) == 1 else None
m_group = m_groups[0] if len(m_groups) == 1 else None
ok = (
    p["screenId"] == source_output
    and p["edge"] == p["reservationEdge"] == "bottom"
    and m["screenId"] == target_output
    and m["screen"] == target_name
    and m["edge"] == m["reservationEdge"] == "top"
    and p["geometrySettled"] and m["geometrySettled"]
    and p_group is not None
    and m_group is not None
    and p_group["outputId"] == source_output
    and p_group["edge"] == "bottom"
    and m_group["outputId"] == target_output
    and m_group["edge"] == "top"
    and p_group["publisher"] == p["objects"]["reservationPublisher"]
    and m_group["publisher"] == m["objects"]["reservationPublisher"]
    and p_group["generation"] == p["reservationGroupGeneration"]
    and m_group["generation"] == m["reservationGroupGeneration"]
    and p_group["layerShellPresent"]
    and m_group["layerShellPresent"]
    and not any(
        moved in group["contributorDockIds"]
        and group["outputId"] == source_output
        for group in state["reservationGroups"]
    )
    and int(state["reservationStateGeneration"]) > previous_generation
    and int(m_group["generation"]) == int(state["reservationStateGeneration"])
)
raise SystemExit(0 if ok else 1)
' "$partial_bottom" "$full_bottom" "$screen_id" \
            "$target_screen_id" "$target_screen_name" \
            "$migration_generation" <<<"$current"; then
        output_moved="$current"
        break
    fi
    sleep 0.2
done
if [[ -z "$output_moved" ]]; then
    python3 -m json.tool <<<"$last" >&2
    e2e_fail "output migration left stale or incompatible reservation ownership"
fi

e2e_dock_stop \
    || e2e_fail "could not stop the dock for reservation persistence replay"
e2e_dock_start 120 \
    || e2e_fail "dock did not restart for reservation persistence replay"

restarted=""
last=""
for _ in $(seq 1 150); do
    current="$(e2e_json dockSystemData)"
    last="$current"
    if [[ -n "$current" ]] && python3 -c '
import json, sys
state = json.load(sys.stdin)
views = {v["persistentDockId"]: v for v in state["views"]}
partial, moved, source_output, target_output, right = (
    int(value) for value in sys.argv[1:6]
)
target_name = sys.argv[6]
right_geometry = [int(value) for value in sys.argv[7:11]]
p, m, r = views[partial], views[moved], views[right]
p_groups = [
    group for group in state["reservationGroups"]
    if partial in group["contributorDockIds"]
]
m_groups = [
    group for group in state["reservationGroups"]
    if moved in group["contributorDockIds"]
]
p_group = p_groups[0] if len(p_groups) == 1 else None
m_group = m_groups[0] if len(m_groups) == 1 else None
selected_occurrences = sum(
    int(partial in group["contributorDockIds"])
    + int(moved in group["contributorDockIds"])
    for group in state["reservationGroups"]
)
ok = (
    p["screenId"] == source_output
    and p["edge"] == p["reservationEdge"] == "bottom"
    and p["visibilityMode"] == "alwaysVisible"
    and m["screenId"] == target_output
    and m["screen"] == target_name
    and m["edge"] == m["reservationEdge"] == "top"
    and m["visibilityMode"] == "alwaysVisible"
    and p["geometrySettled"] and m["geometrySettled"]
    and p_group is not None
    and m_group is not None
    and p_group["outputId"] == source_output
    and p_group["edge"] == "bottom"
    and m_group["outputId"] == target_output
    and m_group["edge"] == "top"
    and p_group["publisher"] == p["objects"]["reservationPublisher"]
    and m_group["publisher"] == m["objects"]["reservationPublisher"]
    and p_group["generation"] == p["reservationGroupGeneration"]
    and m_group["generation"] == m["reservationGroupGeneration"]
    and int(p_group["generation"]) > 0
    and int(m_group["generation"]) > 0
    and p_group["layerShellPresent"]
    and m_group["layerShellPresent"]
    and selected_occurrences == 2
    and int(state["reservationStateGeneration"])
        >= max(int(p_group["generation"]), int(m_group["generation"]))
    and r["surfaceGeometry"] == right_geometry
)
raise SystemExit(0 if ok else 1)
' "$partial_bottom" "$full_bottom" "$screen_id" \
            "$target_screen_id" "$right_view" "$target_screen_name" \
            "$rx" "$ry" "$rw" "$rh" <<<"$current"; then
        restarted="$current"
        break
    fi
    sleep 0.2
done
if [[ -z "$restarted" ]]; then
    python3 -m json.tool <<<"$last" >&2
    e2e_fail "reservation output and edge ownership did not survive restart"
fi

restart_generation="$(python3 -c '
import json, sys
print(json.load(sys.stdin)["reservationStateGeneration"])
' <<<"$restarted")"

e2e_call setViewVisibilityMode us "$full_bottom" dodgeActive >/dev/null \
    || e2e_fail "could not release the migrated top reservation"
e2e_call setViewPlacement uiii "$full_bottom" "$screen_id" 4 "$full_alignment" >/dev/null \
    || e2e_fail "could not restore the full dock to the bottom edge"
e2e_call setViewVisibilityMode us "$partial_bottom" dodgeActive >/dev/null \
    || e2e_fail "could not release the partial bottom reservation"

for _ in $(seq 1 150); do
    current="$(e2e_json dockSystemData)"
    if [[ -n "$current" ]] && python3 -c '
import json, sys
state = json.load(sys.stdin)
views = {v["persistentDockId"]: v for v in state["views"]}
partial, full, source_output, target_output, previous_generation = (
    int(value) for value in sys.argv[1:6]
)
ids = [partial, full]
selected = set(ids)
groups = state["reservationGroups"]
ok = all(
    views[view_id]["edge"] == "bottom"
    and views[view_id]["visibilityMode"] == "dodgeActive"
    and not views[view_id]["reservationSurfacePresent"]
    and views[view_id]["objects"]["reservationPublisher"] is None
    for view_id in ids
)
ok = (
    ok
    and int(state["reservationStateGeneration"]) > previous_generation
    and not any(
        selected.intersection(group["contributorDockIds"])
        for group in groups
    )
    and not any(
        group["outputId"] == source_output
        and group["edge"] == "bottom"
        for group in groups
    )
    and (
        target_output == source_output
        or not any(
            group["outputId"] == target_output
            and group["edge"] == "top"
            for group in groups
        )
    )
    and all(
        group["publisher"] is not None
        and group["layerShellPresent"]
        and group["memberCount"] == len(group["contributorDockIds"])
        and group["memberCount"] > 0
        and all(
            contributor in views
            and views[contributor]["objects"]["reservationPublisher"]
                == group["publisher"]
            for contributor in group["contributorDockIds"]
        )
        for group in groups
    )
)
raise SystemExit(0 if ok else 1)
' "$partial_bottom" "$full_bottom" "$screen_id" \
        "$target_screen_id" "$restart_generation" <<<"$current"; then
        echo "PASS: one bottom publisher used max depth $shared_depth, migration fell back to $fallback_depth, output $target_screen_id and restart persisted, teardown left no orphan, and right dock stayed at $rx,$ry ${rw}x${rh}"
        exit 0
    fi
    sleep 0.2
done

python3 -m json.tool <<<"$current" >&2
e2e_fail "reservation fixture did not return to a publisher-free baseline"
