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
    and views[partial]["reservationGeometry"] == views[partial]["publishedStruts"]
    and views[partial]["reservationLayerShellAnchors"] == ["bottom", "left", "right"]
    and views[partial]["reservationLayerShellExclusiveEdge"] == "bottom"
    and views[partial]["reservationLayerShellExclusiveZone"]
        == views[partial]["strutsThickness"]
    and views[full]["visibilityMode"] == "dodgeActive"
    and views[full]["publishedStruts"] == [0, 0, 0, 0]
    and views[full]["reservationLayerShellExclusiveZone"] == 0
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

echo "PASS: partial bottom reservation kept right dock at $rx,$ry ${rw}x${rh}"
