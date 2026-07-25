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

source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"

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
ok = (
    views[partial]["visibilityMode"] == "dodgeActive"
    and views[partial]["publishedStruts"] == [0, 0, 0, 0]
    and views[full]["visibilityMode"] == "dodgeActive"
    and views[full]["publishedStruts"] == [0, 0, 0, 0]
    and views[right]["geometrySettled"]
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

baseline_right="$(python3 -c '
import json, sys
state = json.load(sys.stdin)
view = next(v for v in state["views"]
            if v["persistentDockId"] == int(sys.argv[1]))
print(" ".join(str(value) for value in view["surfaceGeometry"]))
' "$right_view" <<<"$baseline")"

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
    and views[partial]["reservationContributionDepth"]
        == views[partial]["strutsThickness"]
    and views[partial]["reservationPublishedDepth"]
        == views[partial]["strutsThickness"]
    and views[partial]["reservationGroupMemberCount"] == 1
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
' "$partial_bottom" "$full_bottom" "$right_view" <<<"$current"; then
        settled="$current"
        break
    fi
    sleep 0.2
done
if [[ -z "$settled" ]]; then
    python3 -m json.tool <<<"$last" >&2
    e2e_fail "partial reservation did not converge to the exact layer-shell placement"
fi

read -r rx ry rw rh bottom_end right_start <<< "$(python3 -c '
import json, sys
state = json.load(sys.stdin)
views = {v["persistentDockId"]: v for v in state["views"]}
partial, right = (int(value) for value in sys.argv[1:3])
r = views[right]
p = views[partial]
print(*r["surfaceGeometry"],
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
ok = (
    p["visibilityMode"] == "alwaysVisible"
    and f["visibilityMode"] == "alwaysVisible"
    and p["reservationOutputId"] == f["reservationOutputId"] == p["screenId"]
    and p["reservationGroupMemberCount"] == f["reservationGroupMemberCount"] == 2
    and p["reservationPublishedDepth"] == f["reservationPublishedDepth"] == depth
    and depth != (
        p["reservationContributionDepth"] + f["reservationContributionDepth"])
    and p["reservationGeometry"] == f["reservationGeometry"] == expected
    and p["reservationLayerShellExclusiveZone"]
        == f["reservationLayerShellExclusiveZone"] == depth
    and p["objects"]["reservationPublisher"]
        == f["objects"]["reservationPublisher"]
    and p["objects"]["reservationPublisher"] is not None
    and r["surfaceGeometry"] == [int(value) for value in sys.argv[4:8]]
)
raise SystemExit(0 if ok else 1)
' "$partial_bottom" "$full_bottom" "$right_view" \
        "$rx" "$ry" "$rw" "$rh" <<<"$current"; then
        shared="$current"
        break
    fi
    sleep 0.2
done
if [[ -z "$shared" ]]; then
    python3 -m json.tool <<<"$last" >&2
    e2e_fail "same-edge reservations did not converge on one maximum-depth publisher"
fi

read -r screen_id shared_depth shared_members full_alignment <<< "$(python3 -c '
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
      }[f["alignment"]])
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
)
raise SystemExit(0 if ok else 1)
' "$partial_bottom" "$full_bottom" <<<"$current"; then
        migrated="$current"
        break
    fi
    sleep 0.2
done
if [[ -z "$migrated" ]]; then
    python3 -m json.tool <<<"$last" >&2
    e2e_fail "edge migration left stale or incompatible reservation membership"
fi

fallback_depth="$(python3 -c '
import json, sys
state = json.load(sys.stdin)
view = next(v for v in state["views"]
            if v["persistentDockId"] == int(sys.argv[1]))
print(view["reservationPublishedDepth"])
' "$partial_bottom" <<<"$migrated")"
[[ "$fallback_depth" -le "$shared_depth" ]] \
    || e2e_fail "removing a same-edge member increased the surviving depth"

e2e_call setViewVisibilityMode us "$full_bottom" dodgeActive >/dev/null \
    || e2e_fail "could not release the migrated top reservation"
e2e_call setViewPlacement uiii "$full_bottom" "$screen_id" 4 "$full_alignment" >/dev/null \
    || e2e_fail "could not restore the full dock to the bottom edge"
e2e_call setViewVisibilityMode us "$partial_bottom" dodgeActive >/dev/null \
    || e2e_fail "could not release the partial bottom reservation"

for _ in $(seq 1 150); do
    current="$(e2e_json dockSystemData)"
    if python3 -c '
import json, sys
views = {v["persistentDockId"]: v
         for v in json.load(sys.stdin)["views"]}
ids = [int(value) for value in sys.argv[1:]]
ok = all(
    views[view_id]["edge"] == "bottom"
    and views[view_id]["visibilityMode"] == "dodgeActive"
    and not views[view_id]["reservationSurfacePresent"]
    and views[view_id]["objects"]["reservationPublisher"] is None
    for view_id in ids
)
raise SystemExit(0 if ok else 1)
' "$partial_bottom" "$full_bottom" <<<"$current"; then
        echo "PASS: one bottom publisher used max depth $shared_depth, migration fell back to $fallback_depth, and right dock stayed at $rx,$ry ${rw}x${rh}"
        exit 0
    fi
    sleep 0.2
done

python3 -m json.tool <<<"$current" >&2
e2e_fail "reservation fixture did not return to a publisher-free baseline"
