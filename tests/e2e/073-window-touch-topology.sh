#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# Drive FP-4B multi-output and separated-span acceptance. Three independent
# partial floating panels retain per-view window-touch, presentation and input
# ownership while the physical outputs move through full-touching,
# partial-touching and disconnected arrangements. Output adjacency never
# changes the two output-identity-plus-edge reservation groups.
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-multi-output-e2e.sh}/tests/e2e/lib.sh"
source "$E2E_REPO/tests/e2e/matrix/matrix-lib.sh"
source "$E2E_REPO/tests/e2e/matrix/multi-output-lib.sh"

readonly ORACLE="$E2E_REPO/tests/e2e/fixtures/fp4b/oracle.py"
readonly WINDOW_QML="$E2E_REPO/tests/e2e/fixtures/fp4b/window.qml"
readonly CLIENT_TITLE="LATTE FP4B TOPOLOGY WINDOW"

layout=""
view_a=""
view_b=""
view_c=""
view_ids_csv=""
client_pid=0
fixture_transaction_active=0
topology_captured=0
original_topology=""
baseline_stable=""
previous_anchor_revisions=""

snapshot() {
    e2e_json dockSystemData
}

view_ids() {
    snapshot | python3 -c '
import json, sys
print(" ".join(str(view["persistentDockId"])
               for view in json.load(sys.stdin)["views"]))
'
}

created_view_id() {
    local before="$1" payload
    payload="$(snapshot 2>/dev/null)" || return 1
    [[ -n "$payload" ]] || return 1
    python3 -c '
import json, sys
before = {int(value) for value in sys.argv[1].split()}
created = [
    view for view in json.load(sys.stdin)["views"]
    if view["persistentDockId"] not in before
    and view["relationship"] == "independent"
]
print(created[0]["persistentDockId"] if len(created) == 1 else "")
' "$before" <<< "$payload" 2>/dev/null
}

duplicate_independently() {
    local source="$1" boundary="$2" before candidate=""
    before="$(view_ids)" \
        || e2e_fail "$boundary could not capture the pre-duplicate view set"
    e2e_call duplicateView u "$source" >/dev/null \
        || e2e_fail "$boundary duplicateView call failed"
    for _ in $(seq 1 80); do
        candidate="$(created_view_id "$before")" || true
        [[ -n "$candidate" ]] && break
        sleep 0.25
    done
    [[ -n "$candidate" ]] \
        || e2e_fail "$boundary did not create exactly one independent dock"
    echo "$candidate"
}

configure_panel() {
    local view="$1" icon_size="$2" length="$3"
    local -a group=(
        --file "$layout"
        --group Containments
        --group "$view"
        --group General
    )
    kwriteconfig6 "${group[@]}" --key iconSize "$icon_size" \
        || e2e_fail "could not set icon size $icon_size for panel $view"
    kwriteconfig6 "${group[@]}" --key minLength "$length" \
        || e2e_fail "could not set minimum length $length for panel $view"
    kwriteconfig6 "${group[@]}" --key maxLength "$length" \
        || e2e_fail "could not set maximum length $length for panel $view"
    kwriteconfig6 "${group[@]}" --key maximizeWhenMaximized false \
        || e2e_fail "could not disable maximize-driven length for panel $view"
    kwriteconfig6 "${group[@]}" --key hideFloatingGapForMaximized true \
        || e2e_fail "could not enable window-touch attachment for panel $view"
    kwriteconfig6 "${group[@]}" --key floatingGapHidingWaitsMouse false \
        || e2e_fail "could not disable pointer deferral for panel $view"
    kwriteconfig6 "${group[@]}" --key screenEdgeMargin 18 \
        || e2e_fail "could not set the floating gap for panel $view"
    kwriteconfig6 "${group[@]}" --key floatingInternalGapIsForced false \
        || e2e_fail "could not retain panel-owned floating gap for panel $view"
    kwriteconfig6 "${group[@]}" --key zoomLevel 0 \
        || e2e_fail "could not keep panel $view at resting scale"
    kwriteconfig6 "${group[@]}" --key useThemePanel true \
        || e2e_fail "could not retain theme panel behavior for panel $view"
    kwriteconfig6 "${group[@]}" --key panelSize 100 \
        || e2e_fail "could not retain full panel background thickness for panel $view"
}

screen_id_for_name() {
    local name="$1"
    mo_screens_json | python3 -c '
import json, sys
name = sys.argv[1]
matches = [screen for screen in json.load(sys.stdin)
           if screen["isActive"] and screen["name"] == name]
if len(matches) != 1:
    raise SystemExit("expected one active screen named %s, got %d"
                     % (name, len(matches)))
print(matches[0]["id"])
' "$name"
}

wait_for_fixture_placement() {
    local expected_primary_id="$1" expected_secondary_id="$2"
    for _ in $(seq 1 120); do
        if snapshot | python3 -c '
import json, sys
ids = [int(value) for value in sys.argv[1].split(",")]
primary = int(sys.argv[2])
secondary = int(sys.argv[3])
views = {view["persistentDockId"]: view
         for view in json.load(sys.stdin)["views"]}
expected = {
    ids[0]: (primary, "bottom", "left"),
    ids[1]: (primary, "bottom", "right"),
    ids[2]: (secondary, "left", "center"),
}
if set(views) != set(ids):
    raise SystemExit(1)
for dock_id, placement in expected.items():
    view = views[dock_id]
    actual = (view["screenId"], view["edge"], view["alignment"])
    if actual != placement or not view["geometrySettled"]:
        raise SystemExit(1)
' "$view_ids_csv" "$expected_primary_id" "$expected_secondary_id" 2>/dev/null; then
            return 0
        fi
        sleep 0.25
    done
    e2e_fail "the three independent panels did not settle at their requested output-edge placements"
}

assert_axis_change_publishes_once() {
    local view="$1" expected_screen_id="$2" expected_edge="$3" expected_alignment="$4"
    local before_revision="$5"
    local publication_observed=0
    for _ in $(seq 1 150); do
        if snapshot | python3 -c '
import json, sys
dock_id = int(sys.argv[1])
expected = (int(sys.argv[2]), sys.argv[3], sys.argv[4])
views = {view["persistentDockId"]: view
         for view in json.load(sys.stdin)["views"]}
view = views.get(dock_id)
if not view:
    raise SystemExit(1)
actual = (view["screenId"], view["edge"], view["alignment"])
if (actual != expected
        or view["relocationGeneration"] != view["appliedRelocationGeneration"]
        or int(view["surfaceGeometryPublicationRevision"]) <= int(sys.argv[5])
        or view["windowGeometry"] != view["surfaceGeometry"]):
    raise SystemExit(1)
' "$view" "$expected_screen_id" "$expected_edge" "$expected_alignment" "$before_revision" \
                2>/dev/null; then
            publication_observed=1
            break
        fi
        sleep 0.02
    done
    ((publication_observed == 1)) \
        || e2e_fail "axis-changing placement never reached its first complete publication"

    # Old coalescers could fire at 150 ms directly or at 650 ms after the
    # validator. Compare with the pre-mutation revision after both deadlines;
    # even a missed intermediate sample cannot hide an extra publication.
    sleep 0.8
    if ! snapshot | python3 -c '
import json, sys
dock_id = int(sys.argv[1])
expected = (int(sys.argv[2]), sys.argv[3], sys.argv[4])
before_revision = int(sys.argv[5])
views = {view["persistentDockId"]: view
         for view in json.load(sys.stdin)["views"]}
view = views.get(dock_id)
if not view:
    raise SystemExit("axis-changing panel disappeared")
actual = (view["screenId"], view["edge"], view["alignment"])
if actual != expected:
    raise SystemExit(f"axis-changing placement drifted: {actual!r}")
if int(view["surfaceGeometryPublicationRevision"]) != before_revision + 1:
    raise SystemExit("geometry validator republished a completed placement")
if view["windowGeometry"] != view["surfaceGeometry"]:
    raise SystemExit("QWindow and applied surface diverged after publication")
' "$view" "$expected_screen_id" "$expected_edge" "$expected_alignment" "$before_revision"; then
        snapshot > "$E2E_ARTIFACTS/fp4b-axis-change-extra-publication.json" \
            || e2e_fail "could not preserve the duplicate axis-change publication"
        printf '%s\n' "$before_revision" \
            > "$E2E_ARTIFACTS/fp4b-axis-change-before-revision.txt"
        e2e_fail "axis-changing placement scheduled a redundant geometry publication"
    fi

    # Settlement also includes longer-lived presentation bookkeeping and may
    # include a later content-driven publication. Wait for convergence without
    # conflating it with the validator deadline checked above.
    for _ in $(seq 1 120); do
        if snapshot | python3 -c '
import json, sys
dock_id = int(sys.argv[1])
expected = (int(sys.argv[2]), sys.argv[3], sys.argv[4])
views = {view["persistentDockId"]: view
         for view in json.load(sys.stdin)["views"]}
view = views.get(dock_id)
if not view:
    raise SystemExit(1)
actual = (view["screenId"], view["edge"], view["alignment"])
if (actual != expected
        or view["relocationGeneration"] != view["appliedRelocationGeneration"]
        or view["windowGeometry"] != view["surfaceGeometry"]):
    raise SystemExit(1)
if view["geometrySettled"]:
    raise SystemExit(0)
raise SystemExit(1)
' "$view" "$expected_screen_id" "$expected_edge" "$expected_alignment" \
            2>/dev/null; then
            return 0
        fi
        sleep 0.25
    done
    snapshot > "$E2E_ARTIFACTS/fp4b-axis-change-unsettled.json" \
        || e2e_fail "could not preserve the unsettled axis-change state"
    e2e_fail "axis-changing placement did not settle"
}

dock_windows_json() {
    local rows
    rows="$(e2e_kwin_js "for (const window of workspace.windowList()) {
        if (String(window.resourceClass) === 'latte-dock' && window.layer === 3) {
            print('@TAG@|' + JSON.stringify({
                id: String(window.internalId),
                geometry: [
                    Math.round(window.frameGeometry.x),
                    Math.round(window.frameGeometry.y),
                    Math.round(window.frameGeometry.width),
                    Math.round(window.frameGeometry.height)
                ],
                output: window.output ? window.output.name : null
            }));
        }
    }")" || return 1
    python3 -c '
import json, sys
rows = [line for line in sys.stdin.read().splitlines() if line]
print(json.dumps([json.loads(row) for row in rows], separators=(",", ":")))
' <<< "$rows"
}

assert_structure() {
    local windows_file="$E2E_ARTIFACTS/fp4b-dock-windows.json"
    dock_windows_json > "$windows_file" \
        || {
            echo "could not capture compositor-owned dock windows" >&2
            return 1
        }
    snapshot | python3 "$ORACLE" assert-structure \
        --ids "$view_ids_csv" --windows "$windows_file"
}

stable_projection() {
    snapshot | python3 "$ORACLE" stable-projection --ids "$view_ids_csv"
}

stable_matches_client_baseline() {
    local current
    current="$(stable_projection 2>/dev/null)" || return 1
    [[ "$current" == "$baseline_stable" ]]
}

persistent_projection() {
    snapshot | python3 "$ORACLE" persistent-projection --ids "$view_ids_csv"
}

wait_for_stable_topology() {
    local expected="$1" previous="" current="" verified=""
    local structure_error=""
    mo_assert_output_topology "$expected" \
        || e2e_fail "output helper did not observe $expected"
    mo_screens_json | python3 "$ORACLE" assert-topology "$expected" >/dev/null \
        || e2e_fail "typed rectangle oracle did not observe $expected"
    for _ in $(seq 1 120); do
        current="$(stable_projection 2>/dev/null)" || current=""
        if [[ -n "$current" && "$current" == "$previous" ]]; then
            if structure_error="$(assert_structure 2>&1)"; then
                verified="$(stable_projection 2>/dev/null)" \
                    || verified=""
                if [[ "$verified" == "$current" ]]; then
                    baseline_stable="$verified"
                    previous_anchor_revisions="$(
                        snapshot | python3 "$ORACLE" anchor-revisions --ids "$view_ids_csv"
                    )" || e2e_fail "could not capture popup anchor revisions"
                    return 0
                fi
            fi
        fi
        previous="$current"
        sleep 0.25
    done
    [[ -z "$structure_error" ]] \
        || echo "last unsettled structure: $structure_error" >&2
    e2e_fail "$expected output mutation did not converge to two identical stable snapshots"
}

assert_stable_after_client_change() {
    local boundary="$1" current anchors
    current="$(stable_projection)" \
        || e2e_fail "$boundary could not read the stable projection"
    if [[ "$current" != "$baseline_stable" ]]; then
        BASELINE_JSON="$baseline_stable" CURRENT_JSON="$current" python3 - <<'PY' >&2
import json
import os

baseline = json.loads(os.environ["BASELINE_JSON"])
current = json.loads(os.environ["CURRENT_JSON"])

def differences(left, right, path="$"):
    if type(left) is not type(right):
        yield f"{path}: type {type(left).__name__} -> {type(right).__name__}"
    elif isinstance(left, dict):
        for key in sorted(set(left) | set(right)):
            if key not in left:
                yield f"{path}.{key}: added {right[key]!r}"
            elif key not in right:
                yield f"{path}.{key}: removed {left[key]!r}"
            else:
                yield from differences(left[key], right[key], f"{path}.{key}")
    elif isinstance(left, list):
        if len(left) != len(right):
            yield f"{path}: length {len(left)} -> {len(right)}"
        for index, (before, after) in enumerate(zip(left, right)):
            yield from differences(before, after, f"{path}[{index}]")
    elif left != right:
        yield f"{path}: {left!r} -> {right!r}"

for line in list(differences(baseline, current))[:30]:
    print(line)
PY
        e2e_fail "$boundary changed stable surface, reservation, trigger, sizing, or authority state"
    fi
    anchors="$(
        snapshot | python3 "$ORACLE" anchor-revisions --ids "$view_ids_csv"
    )" || e2e_fail "$boundary could not read popup anchor revisions"
    python3 -c '
import sys
previous = [int(value) for value in sys.argv[1].split()]
current = [int(value) for value in sys.argv[2].split()]
if len(previous) != 3 or len(current) != 3:
    raise SystemExit("expected three popup-anchor revisions")
if any(after < before for before, after in zip(previous, current)):
    raise SystemExit("popup-anchor revision moved backwards")
' "$previous_anchor_revisions" "$anchors" \
        || e2e_fail "$boundary violated monotonic popup-anchor revisions"
    previous_anchor_revisions="$anchors"
}

client_rows() {
    e2e_kwin_js "for (const window of workspace.windowList()) {
        if (window.caption === '$CLIENT_TITLE') {
            print('@TAG@|' + String(window.internalId)
                + ' ' + Math.round(window.frameGeometry.x)
                + ' ' + Math.round(window.frameGeometry.y)
                + ' ' + Math.round(window.frameGeometry.width)
                + ' ' + Math.round(window.frameGeometry.height)
                + ' ' + String(window.minimized));
        }
    }" 0.05
}

wait_for_one_client() {
    local rows count
    for _ in $(seq 1 80); do
        rows="$(client_rows)" || rows=""
        count="$(grep -c . <<< "$rows")"
        [[ "$count" -eq 1 ]] && return 0
        sleep 0.1
    done
    e2e_fail "the topology fixture did not map exactly one tagged QML Window"
}

place_client() {
    local x="$1" y="$2" width="$3" height="$4" minimized="$5"
    local result actual_id actual_x actual_y actual_width actual_height actual_minimized
    result="$(e2e_kwin_js "for (const window of workspace.windowList()) {
        if (window.caption === '$CLIENT_TITLE') {
            window.minimized = false;
            const geometry = Object.assign({}, window.frameGeometry);
            geometry.x = $x;
            geometry.y = $y;
            geometry.width = $width;
            geometry.height = $height;
            window.frameGeometry = geometry;
            workspace.activeWindow = window;
            window.minimized = $minimized;
            print('@TAG@|' + String(window.internalId));
        }
    }" 0.05)" \
        || e2e_fail "KWin could not target the topology fixture window"
    [[ -n "$result" && "$result" != *$'\n'* ]] \
        || e2e_fail "KWin targeted an invalid number of topology fixture windows"
    for _ in $(seq 1 80); do
        read -r actual_id actual_x actual_y actual_width actual_height actual_minimized \
            <<< "$(client_rows)"
        if [[ "$actual_id" == "$result"
              && "$actual_x" == "$x"
              && "$actual_y" == "$y"
              && "$actual_width" == "$width"
              && "$actual_height" == "$height"
              && "$actual_minimized" == "$minimized" ]]; then
            echo "$actual_x $actual_y $actual_width $actual_height"
            return 0
        fi
        sleep 0.05
    done
    e2e_fail "KWin constrained the requested client frame $x,$y ${width}x${height} (actual=$actual_x,$actual_y ${actual_width}x${actual_height} minimized=$actual_minimized)"
}

wait_for_client_policy() {
    local frame="$1" expected="$2" minimized="$3" boundary="$4"
    local -a args=()
    [[ "$minimized" == true ]] && args+=(--minimized)
    for _ in $(seq 1 120); do
        if snapshot | python3 "$ORACLE" assert-client \
            --ids "$view_ids_csv" --frame $frame \
            --expected "$expected" "${args[@]}" >/dev/null 2>&1 \
                && stable_matches_client_baseline; then
            assert_stable_after_client_change "$boundary"
            return 0
        fi
        sleep 0.05
    done
    snapshot | python3 "$ORACLE" assert-client \
        --ids "$view_ids_csv" --frame $frame \
        --expected "$expected" "${args[@]}" \
        || e2e_fail "$boundary did not settle at the expected per-view touch policy"
    assert_stable_after_client_change "$boundary"
}

drive_client_case() {
    local case="$1" expected="$2" minimized="${3:-false}"
    local plan frame
    plan="$(snapshot | python3 "$ORACLE" client-plan \
        --ids "$view_ids_csv" --case "$case")" \
        || e2e_fail "could not plan the $case client geometry from live triggers"
    frame="$(place_client $plan "$minimized")"
    wait_for_client_policy "$frame" "$expected" "$minimized" "$case"
}

wait_for_no_client() {
    for _ in $(seq 1 80); do
        if snapshot | python3 "$ORACLE" assert-no-client \
            --ids "$view_ids_csv" >/dev/null 2>&1 \
                && stable_matches_client_baseline; then
            assert_stable_after_client_change "client teardown"
            return 0
        fi
        sleep 0.1
    done
    snapshot | python3 "$ORACLE" assert-no-client --ids "$view_ids_csv" \
        || e2e_fail "destroyed client remained in the per-view touch policy"
    assert_stable_after_client_change "client teardown"
}

drive_topology_cases() {
    local topology="$1"
    wait_for_stable_topology "$topology"
    [[ -z "$(client_rows)" ]] \
        || e2e_fail "a tagged QML Window existed before the $topology client run"
    qml "$WINDOW_QML" >/dev/null 2>&1 &
    client_pid=$!
    wait_for_one_client

    drive_client_case parked none
    drive_client_case a-only "$view_a"
    drive_client_case gap-only none
    drive_client_case full-primary "$view_a,$view_b"
    drive_client_case c-only "$view_c"
    drive_client_case spanning "$view_b,$view_c"
    drive_client_case minimized "$view_b,$view_c" true

    kill "$client_pid" \
        || e2e_fail "could not destroy the $topology QML Window"
    wait "$client_pid" 2>/dev/null || true
    client_pid=0
    for _ in $(seq 1 40); do
        [[ -z "$(client_rows)" ]] && break
        sleep 0.1
    done
    [[ -z "$(client_rows)" ]] \
        || e2e_fail "$topology QML Window remained mapped after destruction"
    wait_for_no_client
}

cleanup() {
    local body_status=$? cleanup_failed=0 dock_pid
    trap - EXIT
    if (( client_pid != 0 )); then
        kill "$client_pid" 2>/dev/null || true
        wait "$client_pid" 2>/dev/null || true
    fi
    if (( topology_captured == 1 )); then
        if ! mo_restore_output_topology "$original_topology"; then
            echo "FP-4B cleanup could not restore the captured output topology" >&2
            cleanup_failed=1
        fi
    fi
    if (( fixture_transaction_active == 1 )); then
        if ! e2e_dock_stop; then
            echo "FP-4B cleanup could not stop the fixture dock" >&2
            cleanup_failed=1
        fi
        rm -rf "${E2E_CONFIG_HOME:?}"
        cp -r "$MATRIX_PRISTINE" "$E2E_CONFIG_HOME" \
            || cleanup_failed=1
        dock_pid="$(e2e_dock_pid)"
        if [[ -n "$dock_pid" ]] && kill -0 "$dock_pid" 2>/dev/null; then
            cleanup_failed=1
        elif ! e2e_dock_start 90; then
            echo "FP-4B cleanup could not restart the pristine nested dock" >&2
            cleanup_failed=1
        fi
    fi
    if (( cleanup_failed != 0 )); then
        echo "FAIL: FP-4B topology cleanup did not restore output and dock state" >&2
        (( body_status == 0 )) && body_status=1
    fi
    exit "$body_status"
}
trap cleanup EXIT

[[ "${E2E_OUTPUT_COUNT:-1}" -eq 2 ]] \
    || e2e_fail "FP-4B topology acceptance requires exactly two nested outputs"

python3 "$ORACLE" negative-probes >/dev/null \
    || e2e_fail "controlled geometry and ownership negatives did not reject"

matrix_init \
    || e2e_fail "could not capture the pristine nested configuration"
fixture_transaction_active=1
mo_discover_outputs \
    || e2e_fail "could not discover the two nested output identities"
original_topology="$(mo_capture_output_topology)" \
    || e2e_fail "could not capture the original nested output topology"
topology_captured=1

matrix_stage panel-bottom-justify-1out \
    || e2e_fail "could not stage the FP-4B panel seed"
view_a="$(matrix_view_id)" \
    || e2e_fail "could not resolve the FP-4B seed panel"
view_b="$(duplicate_independently "$view_a" "first independent duplicate")"
view_c="$(duplicate_independently "$view_a" "second independent duplicate")"
view_ids_csv="$view_a,$view_b,$view_c"
layout="$E2E_LAYOUT"

e2e_dock_stop \
    || e2e_fail "dock did not stop before the three-panel fixture configuration"
configure_panel "$view_a" 32 28
configure_panel "$view_b" 48 28
configure_panel "$view_c" 64 45
e2e_dock_start 90 \
    || e2e_fail "dock did not restart with the three-panel fixture"

primary_id="$(screen_id_for_name "$E2E_MO_PRIMARY")" \
    || e2e_fail "could not resolve the primary Latte output id"
secondary_id="$(screen_id_for_name "$E2E_MO_SECONDARY")" \
    || e2e_fail "could not resolve the secondary Latte output id"
readonly primary_id secondary_id
e2e_call setViewPlacement uiii "$view_a" "$primary_id" 4 1 >/dev/null \
    || e2e_fail "could not place A at primary bottom start"
e2e_call setViewPlacement uiii "$view_b" "$primary_id" 4 2 >/dev/null \
    || e2e_fail "could not place B at primary bottom end"
e2e_call setViewPlacement uiii "$view_c" "$secondary_id" 5 0 >/dev/null \
    || e2e_fail "could not place C at secondary left center"
for view in "$view_a" "$view_b" "$view_c"; do
    e2e_call setViewVisibilityMode us "$view" alwaysVisible >/dev/null \
        || e2e_fail "could not set panel $view to Always Visible"
done
wait_for_fixture_placement "$primary_id" "$secondary_id"

before_axis_revision="$(snapshot | python3 -c '
import json, sys
dock_id = int(sys.argv[1])
views = {view["persistentDockId"]: view
         for view in json.load(sys.stdin)["views"]}
view = views.get(dock_id)
if not view:
    raise SystemExit(1)
print(view["surfaceGeometryPublicationRevision"])
' "$view_c")" \
    || e2e_fail "could not capture C publication revision before its axis change"
readonly before_axis_revision
e2e_call setViewPlacement uiii "$view_c" "$secondary_id" 3 0 >/dev/null \
    || e2e_fail "could not exercise C across vertical-to-horizontal placement"
assert_axis_change_publishes_once "$view_c" "$secondary_id" top center "$before_axis_revision"
e2e_call setViewPlacement uiii "$view_c" "$secondary_id" 5 0 >/dev/null \
    || e2e_fail "could not restore C to secondary left center"
wait_for_fixture_placement "$primary_id" "$secondary_id"

mo_place_secondary_for_topology full-touching >/dev/null \
    || e2e_fail "could not realize the full-touching output topology"
drive_topology_cases full-touching

mo_place_secondary_for_topology partial-touching >/dev/null \
    || e2e_fail "could not realize the partial-touching output topology"
drive_topology_cases partial-touching

mo_place_secondary_for_topology disconnected >/dev/null \
    || e2e_fail "could not realize the disconnected output topology"
drive_topology_cases disconnected

before_restart="$(persistent_projection)" \
    || e2e_fail "could not capture pre-restart persistent topology"
readonly before_restart
e2e_dock_stop \
    || e2e_fail "dock did not stop for disconnected-topology persistence reload"
e2e_dock_start 90 \
    || e2e_fail "dock did not restart for disconnected-topology persistence reload"
wait_for_stable_topology disconnected
after_restart="$(persistent_projection)" \
    || e2e_fail "could not capture post-restart persistent topology"
readonly after_restart
[[ "$after_restart" == "$before_restart" ]] \
    || e2e_fail "restart changed persistent identities, placement, stable spans, depths, or reservation groups"
snapshot | python3 "$ORACLE" assert-no-client --ids "$view_ids_csv" >/dev/null \
    || e2e_fail "restart created an unexpected window-touch participant"

echo "FP-4B topology acceptance passed three output arrangements, exact separated-span activation, spanning-window fanout, maximum-depth reservations, restart persistence, and controlled negative oracles"
