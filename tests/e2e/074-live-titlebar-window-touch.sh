#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# Prove that both floating Panels and floating Docks consume live KWin frame
# geometry during one real button-held titlebar drag. Each case crosses the
# per-view stable envelope and reverses before button release while the QWindow,
# reservation, layer-shell publication, and tracker authority remain stable.
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"
source "$E2E_REPO/tests/e2e/matrix/matrix-lib.sh"

view=""
layout=""
kpid=0
drag_pid=0
configured=0

fp() {
    "$E2E_FAKEPOINTER" "$@"
}

dock_field() {
    local expression="$1"
    e2e_json dockSystemData | python3 -c "
import json, sys
snapshot = json.load(sys.stdin)
if snapshot['schemaVersion'] != 8:
    sys.exit('expected dockSystemData schema 8')
matches = [record for record in snapshot['views']
           if record['persistentDockId'] == $view]
if len(matches) != 1:
    sys.exit('expected exactly one dockSystemData record for view $view')
v = matches[0]
print($expression)
"
}

stable_physical_snapshot() {
    dock_field 'json.dumps({
        "reservationStateGeneration": snapshot["reservationStateGeneration"],
        "windowGeometry": v["windowGeometry"],
        "surfaceGeometry": v["surfaceGeometry"],
        "canvasGeometry": v["canvasGeometry"],
        "stableTriggerGeometry": v["stableTriggerGeometry"],
        "screenEdgeMargin": v["screenEdgeMargin"],
        "normalThickness": v["normalThickness"],
        "maximumNormalThickness": v["maximumNormalThickness"],
        "reservationContributionDepth": v["reservationContributionDepth"],
        "reservationPublishedDepth": v["reservationPublishedDepth"],
        "reservationOutputId": v["reservationOutputId"],
        "reservationEdge": v["reservationEdge"],
        "reservationGroupGeneration": v["reservationGroupGeneration"],
        "reservationContributorDockIds": v["reservationContributorDockIds"],
        "reservationGeometry": v["reservationGeometry"],
        "layerShellMargins": v["layerShellMargins"],
        "layerShellAnchors": v["layerShellAnchors"],
        "layerShellExclusiveEdge": v["layerShellExclusiveEdge"],
        "layerShellExclusiveZone": v["layerShellExclusiveZone"],
        "publishedStruts": v["publishedStruts"],
        "surfaceGeometryPublicationRevision":
            v["surfaceGeometryPublicationRevision"],
        "layerShellConfigureRequestRevision":
            v["layerShellConfigureRequestRevision"],
        "windowTouchTracker": v["objects"]["windowTouchTracker"],
    }, sort_keys=True, separators=(",", ":"))'
}

policy_probe() {
    dock_field '"%s %d %s %s %s %s" % (
        v["type"],
        v["touchingWindowCount"],
        str(v["dockGapHideRequested"]).lower(),
        v["transitionTarget"],
        v["transitionPhase"],
        v["windowTouchGeometryRoleType"],
    )'
}

wait_for_policy_while_held() {
    local expected_type="$1" expected_count="$2"
    local expected_request="$3" expected_target="$4" boundary="$5"
    local require_held="${6:-true}"
    local actual_type=unread count=-1 request=unread target=unread
    local phase=unread role=unread

    for _ in $(seq 1 100); do
        read -r actual_type count request target phase role \
            <<< "$(policy_probe)"
        if [[ "$actual_type" == "$expected_type"
              && "$count" == "$expected_count"
              && "$request" == "$expected_request"
              && "$target" == "$expected_target"
              && "$role" == QRect ]]; then
            if [[ "$require_held" == true ]]; then
                (( drag_pid > 0 )) \
                    || e2e_fail "$boundary has no owned held-drag process"
                kill -0 "$drag_pid" 2>/dev/null \
                    || e2e_fail "$boundary appeared only after button release"
            fi
            return 0
        fi
        sleep 0.01
    done

    e2e_fail "$boundary did not appear during the held drag (type=$actual_type count=$count request=$request target=$target phase=$phase role=$role)"
}

konsole_row() {
    e2e_dumpwins \
        | grep '|org.kde.konsole|LATTE LIVE TITLEBAR TOUCH' \
        | tail -1
}

konsole_count() {
    e2e_dumpwins \
        | grep -c '|org.kde.konsole|LATTE LIVE TITLEBAR TOUCH' \
        || true
}

konsole_geometry() {
    local row
    row="$(konsole_row)" || return 1
    [[ -n "$row" ]] || return 1
    awk -F'|' '{
        split($4, geometry, " ");
        split(geometry[1], position, ",");
        split(geometry[2], size, "x");
        print position[1], position[2], size[1], size[2]
    }' <<< "$row"
}

move_konsole() {
    local x="$1" y="$2" width="$3" height="$4"
    e2e_kwin_js "for (const window of workspace.windowList()) {
        if (window.resourceClass === 'org.kde.konsole'
                && window.caption.includes('LATTE LIVE TITLEBAR TOUCH')) {
            window.setMaximize(false, false);
            const geometry = Object.assign({}, window.frameGeometry);
            geometry.x = $x;
            geometry.y = $y;
            geometry.width = $width;
            geometry.height = $height;
            window.frameGeometry = geometry;
            workspace.activeWindow = window;
            print('@TAG@|' + window.internalId);
        }
    }" | tail -1
}

wait_for_konsole_geometry() {
    local expected_x="$1" expected_y="$2"
    local expected_width="$3" expected_height="$4"
    local x=unread y=unread width=unread height=unread

    for _ in $(seq 1 60); do
        read -r x y width height <<< "$(konsole_geometry)"
        if [[ "$x" == "$expected_x"
              && "$y" == "$expected_y"
              && "$width" == "$expected_width"
              && "$height" == "$expected_height" ]]; then
            return 0
        fi
        sleep 0.05
    done

    e2e_fail "KWin did not place the titlebar client at $expected_x,$expected_y ${expected_width}x${expected_height} (actual=$x,$y ${width}x${height})"
}

stop_owned_konsole() {
    if (( kpid == 0 )); then
        return
    fi

    kill "$kpid" 2>/dev/null || true
    wait "$kpid" 2>/dev/null || true
    kpid=0
    for _ in $(seq 1 40); do
        [[ "$(konsole_count)" -eq 0 ]] && return
        sleep 0.05
    done
    e2e_fail "the owned titlebar client remained mapped after destruction"
}

configure_case() {
    local cell="$1"
    matrix_stage "$cell" \
        || e2e_fail "could not realize $cell"
    view="$(matrix_view_id)" \
        || e2e_fail "could not resolve $cell"
    layout="$E2E_LAYOUT"
    local group_args=(
        --file "$layout"
        --group Containments
        --group "$view"
        --group General
    )

    e2e_dock_stop \
        || e2e_fail "dock did not stop before configuring $cell"
    kwriteconfig6 "${group_args[@]}" --key maxLength 60 \
        || e2e_fail "could not set the partial primary span for $cell"
    kwriteconfig6 "${group_args[@]}" --key minLength 60 \
        || e2e_fail "could not keep the partial Panel span static for $cell"
    kwriteconfig6 "${group_args[@]}" --key hideFloatingGapForMaximized true \
        || e2e_fail "could not enable live attachment for $cell"
    kwriteconfig6 "${group_args[@]}" --key floatingGapHidingWaitsMouse false \
        || e2e_fail "could not disable the independent pointer-deferral policy for $cell"
    kwriteconfig6 "${group_args[@]}" --key screenEdgeMargin 18 \
        || e2e_fail "could not configure the floating gap for $cell"
    kwriteconfig6 "${group_args[@]}" --key floatingInternalGapIsForced false \
        || e2e_fail "could not retain one floating surface for $cell"
    e2e_dock_start 90 \
        || e2e_fail "dock did not restart for $cell"
    e2e_call setViewVisibilityMode us "$view" alwaysVisible >/dev/null \
        || e2e_fail "could not set $cell to alwaysVisible"

    for _ in $(seq 1 40); do
        [[ "$(dock_field 'v["visibilityMode"]')" == alwaysVisible ]] && return
        sleep 0.05
    done
    e2e_fail "$cell did not enter alwaysVisible mode"
}

exercise_held_drag() {
    local expected_type="$1"
    local expected_panel="$2"
    local expected_request="$3"
    local expected_target="$4"
    local type panel geometry_present edge floating_gap gap
    local normal maximum trigger_x trigger_y trigger_width trigger_height
    local screen_x screen_y screen_width screen_height

    read -r type panel geometry_present edge floating_gap gap normal maximum \
        trigger_x trigger_y trigger_width trigger_height \
        screen_x screen_y screen_width screen_height \
        <<< "$(dock_field '"%s %s %s %s %s %d %d %d %d %d %d %d %d %d %d %d" % (
            v["type"],
            str(v["floatingPanelConfigured"]).lower(),
            str(v["transitionGeometryPresent"]).lower(),
            v["edge"],
            str(v["floatingGapConfigured"]).lower(),
            v["screenEdgeMargin"],
            v["normalThickness"],
            v["maximumNormalThickness"],
            *v["stableTriggerGeometry"],
            *v["screenGeometry"],
        )')"
    local expected_envelope_depth="$maximum"
    local expected_normal="$((maximum - gap))"
    if [[ "$expected_panel" == true ]]; then
        expected_envelope_depth=$((normal + gap))
        expected_normal="$maximum"
    fi

    [[ "$type" == "$expected_type"
          && "$panel" == "$expected_panel"
          && "$geometry_present" == "$expected_panel"
          && "$edge" == top
          && "$floating_gap" == true
          && "$gap" -eq 18
          && "$trigger_width" -gt 0
          && "$trigger_height" -eq "$expected_envelope_depth"
          && "$trigger_y" -eq $((screen_y + 1))
          && "$normal" -eq "$expected_normal" ]] \
        || e2e_fail "invalid $expected_type stable-envelope fixture (type=$type panel=$panel geometry=$geometry_present edge=$edge floating=$floating_gap gap=$gap normal=$normal maximum=$maximum trigger=$trigger_x,$trigger_y ${trigger_width}x${trigger_height} screen=$screen_x,$screen_y ${screen_width}x${screen_height})"

    [[ "$(konsole_count)" -eq 0 ]] \
        || e2e_fail "a tagged titlebar client already exists"
    setsid konsole -p 'LocalTabTitleFormat=LATTE LIVE TITLEBAR TOUCH' \
        >/dev/null 2>&1 &
    kpid=$!
    for _ in $(seq 1 40); do
        [[ "$(konsole_count)" -eq 1 ]] && break
        sleep 0.1
    done
    [[ "$(konsole_count)" -eq 1 ]] \
        || e2e_fail "the titlebar client never mapped"

    local client_width=500
    local client_height=400
    local baseline_x=$((trigger_x + trigger_width / 2 - client_width / 2))
    local minimum_x=$((screen_x + 20))
    local maximum_x=$((screen_x + screen_width - client_width - 20))
    (( baseline_x < minimum_x )) && baseline_x=$minimum_x
    (( baseline_x > maximum_x )) && baseline_x=$maximum_x
    local baseline_y=$((trigger_y + trigger_height + 60))

    move_konsole "$baseline_x" "$baseline_y" \
        "$client_width" "$client_height" >/dev/null \
        || e2e_fail "KWin did not accept titlebar-client placement"
    wait_for_konsole_geometry "$baseline_x" "$baseline_y" \
        "$client_width" "$client_height"
    wait_for_policy_while_held \
        "$expected_type" 0 false floated \
        "$expected_type initial negative control" false

    local base_snapshot
    base_snapshot="$(stable_physical_snapshot)" \
        || e2e_fail "could not capture the stable $expected_type surface"

    local titlebar_offset=12
    local start_x=$((baseline_x + client_width / 2))
    local start_y=$((baseline_y + titlebar_offset))
    local touching_y=$((trigger_y + trigger_height - 8 + titlebar_offset))

    fp draghold 900 \
        "$start_x" "$start_y" \
        "$start_x" "$touching_y" \
        "$start_x" "$start_y" &
    drag_pid=$!

    wait_for_policy_while_held \
        "$expected_type" 1 "$expected_request" "$expected_target" \
        "$expected_type live inward crossing"
    [[ "$(stable_physical_snapshot)" == "$base_snapshot" ]] \
        || e2e_fail "$expected_type changed its QWindow, reservation, layer-shell publication, or tracker authority during live attachment"

    wait_for_policy_while_held \
        "$expected_type" 0 false floated \
        "$expected_type live outward reversal"
    [[ "$(stable_physical_snapshot)" == "$base_snapshot" ]] \
        || e2e_fail "$expected_type changed its stable physical contract during live reversal"

    wait "$drag_pid" \
        || e2e_fail "$expected_type held titlebar drag failed"
    drag_pid=0
    wait_for_konsole_geometry "$baseline_x" "$baseline_y" \
        "$client_width" "$client_height"
    [[ "$(stable_physical_snapshot)" == "$base_snapshot" ]] \
        || e2e_fail "$expected_type changed its stable physical contract after release"
    stop_owned_konsole
}

cleanup() {
    local body_status=$? cleanup_failed=0 dock_pid
    trap - EXIT
    if (( drag_pid != 0 )); then
        kill "$drag_pid" 2>/dev/null || true
        wait "$drag_pid" 2>/dev/null || true
    fi
    if (( kpid != 0 )); then
        kill "$kpid" 2>/dev/null || true
        wait "$kpid" 2>/dev/null || true
    fi
    if (( configured == 1 )); then
        e2e_dock_stop >/dev/null 2>&1 || cleanup_failed=1
        rm -rf "${E2E_CONFIG_HOME:?}"
        cp -r "$MATRIX_PRISTINE" "$E2E_CONFIG_HOME" \
            || cleanup_failed=1
        dock_pid="$(e2e_dock_pid)"
        if [[ -n "$dock_pid" ]] && kill -0 "$dock_pid" 2>/dev/null; then
            cleanup_failed=1
        elif ! e2e_dock_start 90 >/dev/null 2>&1; then
            cleanup_failed=1
        fi
    fi
    if (( cleanup_failed != 0 )); then
        echo "FAIL: live titlebar window-touch cleanup did not restore the nested dock" >&2
        (( body_status == 0 )) && body_status=1
    fi
    exit "$body_status"
}
trap cleanup EXIT

matrix_init \
    || e2e_fail "could not capture the pristine nested configuration"
configured=1

configure_case panel-top-center-1out
exercise_held_drag panel true false attached

configure_case dock-top-center-1out
exercise_held_drag dock false true floated

echo "Live titlebar window touch passed before button release for both Panel and Dock, including held reversal and zero stable physical-state drift"
