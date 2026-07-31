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
if snapshot['schemaVersion'] != 9:
    sys.exit('expected dockSystemData schema 9')
matches = [record for record in snapshot['views']
           if record['persistentDockId'] == $view]
if len(matches) != 1:
    sys.exit('expected exactly one dockSystemData record for view $view')
v = matches[0]
print($expression)
"
}

view_config_field() {
    local expression="$1"
    e2e_json viewConfigData u "$view" | python3 -c "
import json, sys
payload = json.load(sys.stdin)
config = payload['config']
print($expression)
"
}

stable_physical_snapshot() {
    dock_field 'json.dumps({
        "reservationStateGeneration": snapshot["reservationStateGeneration"],
        "windowGeometry": v["windowGeometry"],
        "absoluteGeometry": v["absoluteGeometry"],
        "localGeometry": v["localGeometry"],
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
        "configuredIconSize": v["configuredIconSize"],
        "effectiveIconSize": v["effectiveIconSize"],
        "availablePrimaryLength": v["availablePrimaryLength"],
    }, sort_keys=True, separators=(",", ":"))'
}

configured_length_independent_snapshot() {
    dock_field 'json.dumps({
        "windowGeometry": v["windowGeometry"],
        "surfaceGeometry": v["surfaceGeometry"],
        "canvasGeometry": v["canvasGeometry"],
        "screenGeometry": v["screenGeometry"],
        "configuredIconSize": v["configuredIconSize"],
        "effectiveIconSize": v["effectiveIconSize"],
        "screenEdgeMargin": v["screenEdgeMargin"],
        "reservationContributionDepth": v["reservationContributionDepth"],
        "reservationPublishedDepth": v["reservationPublishedDepth"],
        "layerShellMargins": v["layerShellMargins"],
        "layerShellAnchors": v["layerShellAnchors"],
        "layerShellExclusiveEdge": v["layerShellExclusiveEdge"],
        "layerShellExclusiveZone": v["layerShellExclusiveZone"],
    }, sort_keys=True, separators=(",", ":"))'
}

assert_stable_physical_snapshot() {
    local boundary="$1" expected="$2" actual
    actual="$(stable_physical_snapshot)" \
        || e2e_fail "could not read the $boundary physical contract"
    [[ "$actual" == "$expected" ]] \
        || e2e_fail "$boundary changed stable physical state (expected=$expected actual=$actual)"
}

policy_probe() {
    dock_field '"%s %d %s %s %s %.9f %s %d %d" % (
        v["type"],
        v["touchingWindowCount"],
        str(v["dockGapHideRequested"]).lower(),
        v["transitionTarget"],
        v["transitionPhase"],
        v["transitionProgress"],
        v["windowTouchGeometryRoleType"],
        v["screenEdgeMargin"],
        v["presentedScreenEdgeGap"],
    )'
}

presented_gap_matches_progress() {
    python3 - "$1" "$2" "$3" <<'PY'
import math
import sys

progress = float(sys.argv[1])
configured = int(sys.argv[2])
presented = int(sys.argv[3])
expected = math.floor(configured * progress + 0.5)
raise SystemExit(0 if presented == expected else 1)
PY
}

dock_length_matches_progress() {
    python3 - "$1" "$2" "$3" "$4" <<'PY'
import math
import sys

progress = float(sys.argv[1])
configured_ratio = float(sys.argv[2])
presented_length = int(sys.argv[3])
output_length = int(sys.argv[4])
expected = output_length * (
    configured_ratio + (1.0 - configured_ratio) * (1.0 - progress)
)
raise SystemExit(0 if math.isclose(presented_length, expected, abs_tol=2.0)
                 else 1)
PY
}

presentation_probe() {
    dock_field '"%.9f %.9f %d %d %d %d %s" % (
        v["transitionProgress"],
        v["maximumLengthRatio"],
        v["windowGeometry"][0] + v["effectsRect"][0],
        v["effectsRect"][2],
        v["screenGeometry"][0],
        v["screenGeometry"][2],
        ",".join(sorted(v["enabledBorders"])),
    )'
}

fractional_presentation_probe() {
    dock_field '"%s %d %s %s %s %.9f %s %d %d %.9f %d %d" % (
        v["type"],
        v["touchingWindowCount"],
        str(v["dockGapHideRequested"]).lower(),
        v["transitionTarget"],
        v["transitionPhase"],
        v["transitionProgress"],
        v["windowTouchGeometryRoleType"],
        v["screenEdgeMargin"],
        v["presentedScreenEdgeGap"],
        v["maximumLengthRatio"],
        v["effectsRect"][2],
        v["screenGeometry"][2],
    )'
}

wait_for_dock_attached_presentation_while_held() {
    local require_held="${1:-true}"
    local progress=unread configured_ratio=unread
    local presented_x=-1 presented_length=-1 output_x=-1 output_length=-1
    local borders=unread

    for _ in $(seq 1 100); do
        read -r progress configured_ratio presented_x presented_length \
            output_x output_length borders <<< "$(presentation_probe)"
        if [[ "$progress" == 0.000000000
              && "$presented_x" -eq "$output_x"
              && "$presented_length" -eq "$output_length"
              && "$borders" == bottom ]]; then
            if [[ "$require_held" == true ]]; then
                (( drag_pid > 0 )) \
                    || e2e_fail "attached Dock presentation has no held drag"
                kill -0 "$drag_pid" 2>/dev/null \
                    || e2e_fail "Dock reached full span only after button release"
            fi
            return 0
        fi
        sleep 0.01
    done

    e2e_fail "Dock did not reach the full attached span with only the inward border (progress=$progress configuredRatio=$configured_ratio presentation=$presented_x+$presented_length output=$output_x+$output_length borders=$borders)"
}

wait_for_dock_floated_presentation() {
    local expected_x="$1" expected_length="$2"
    local progress=unread configured_ratio=unread
    local presented_x=-1 presented_length=-1 output_x=-1 output_length=-1
    local borders=unread

    for _ in $(seq 1 100); do
        read -r progress configured_ratio presented_x presented_length \
            output_x output_length borders <<< "$(presentation_probe)"
        if [[ "$progress" == 1.000000000
              && "$presented_x" -eq "$expected_x"
              && "$presented_length" -eq "$expected_length"
              && "$borders" == bottom,left,right,top ]]; then
            return 0
        fi
        sleep 0.01
    done

    e2e_fail "Dock did not restore its configured floated span and corners (progress=$progress configuredRatio=$configured_ratio presentation=$presented_x+$presented_length expected=$expected_x+$expected_length borders=$borders)"
}

assert_partial_dock_presentation() {
    local boundary="$1" expected_x="$2" expected_length="$3"
    local progress configured_ratio presented_x presented_length
    local output_x output_length borders

    read -r progress configured_ratio presented_x presented_length \
        output_x output_length borders <<< "$(presentation_probe)"
    [[ "$presented_x" -eq "$expected_x"
          && "$presented_length" -eq "$expected_length"
          && "$presented_length" -lt "$output_length"
          && "$borders" == bottom,left,right,top ]] \
        || e2e_fail "$boundary changed a partial Dock's primary presentation (progress=$progress configuredRatio=$configured_ratio presentation=$presented_x+$presented_length expected=$expected_x+$expected_length output=$output_x+$output_length borders=$borders)"
}

wait_for_policy_while_held() {
    local expected_type="$1" expected_count="$2"
    local expected_request="$3" expected_target="$4" boundary="$5"
    local require_held="${6:-true}"
    local actual_type=unread count=-1 request=unread target=unread
    local phase=unread progress=unread role=unread
    local configured_gap=-1 presented_gap=-1

    for _ in $(seq 1 100); do
        read -r actual_type count request target phase progress role \
            configured_gap presented_gap \
            <<< "$(policy_probe)"
        if [[ "$actual_type" == "$expected_type"
              && "$count" == "$expected_count"
              && "$request" == "$expected_request"
              && "$target" == "$expected_target"
              && "$role" == QRect ]] \
                && presented_gap_matches_progress \
                    "$progress" "$configured_gap" "$presented_gap"; then
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

    e2e_fail "$boundary did not appear during the held drag (type=$actual_type count=$count request=$request target=$target phase=$phase progress=$progress configuredGap=$configured_gap presentedGap=$presented_gap role=$role)"
}

wait_for_fractional_progress_while_held() {
    local expected_type="$1" expected_phase="$2" boundary="$3"
    local expect_dock_expansion="${4:-false}"
    local actual_type=unread count=-1 request=unread target=unread
    local phase=unread progress=unread role=unread
    local configured_gap=-1 presented_gap=-1
    local configured_ratio=0 presented_length=0 output_length=0

    for _ in $(seq 1 100); do
        read -r actual_type count request target phase progress role \
            configured_gap presented_gap configured_ratio \
            presented_length output_length \
            <<< "$(fractional_presentation_probe)"
        if [[ "$actual_type" == "$expected_type"
              && "$phase" == "$expected_phase"
              && "$role" == QRect ]] \
                && presented_gap_matches_progress \
                    "$progress" "$configured_gap" "$presented_gap" \
                && python3 - "$progress" <<'PY'
import sys

progress = float(sys.argv[1])
raise SystemExit(0 if 0.0 < progress < 1.0 else 1)
PY
        then
            if [[ "$expected_type" == dock
                  && "$expect_dock_expansion" == true ]]; then
                dock_length_matches_progress \
                    "$progress" "$configured_ratio" \
                    "$presented_length" "$output_length" \
                    || continue
            fi
            (( drag_pid > 0 )) \
                || e2e_fail "$boundary has no owned held-drag process"
            kill -0 "$drag_pid" 2>/dev/null \
                || e2e_fail "$boundary appeared only after button release"
            return 0
        fi
        sleep 0.01
    done

    e2e_fail "$boundary exposed no fractional transition frame (type=$actual_type count=$count request=$request target=$target phase=$phase progress=$progress configuredGap=$configured_gap presentedGap=$presented_gap role=$role)"
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

set_konsole_maximized() {
    local enabled="$1"
    e2e_kwin_js "for (const window of workspace.windowList()) {
        if (window.resourceClass === 'org.kde.konsole'
                && window.caption.includes('LATTE LIVE TITLEBAR TOUCH')) {
            workspace.activeWindow = window;
            window.setMaximize($enabled, $enabled);
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
    if [[ "$cell" == dock-* ]]; then
        kwriteconfig6 "${group_args[@]}" --key autoSizeEnabled false \
            || e2e_fail "could not disable automatic sizing for $cell"
        kwriteconfig6 "${group_args[@]}" --key backgroundRadius 50 \
            || e2e_fail "could not retain Dock presentation for $cell"
        kwriteconfig6 "${group_args[@]}" --key maximizeWhenMaximized true \
            || e2e_fail "could not enable live maximize-length presentation for $cell"
    else
        kwriteconfig6 "${group_args[@]}" --key maximizeWhenMaximized false \
            || e2e_fail "could not keep the partial Panel span stable for $cell"
    fi
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
    local expect_dock_expansion="${5:-false}"
    local type panel geometry_present edge floating_gap gap
    local normal maximum trigger_x trigger_y trigger_width trigger_height
    local screen_x screen_y screen_width screen_height
    local base_presented_x=-1 base_presented_length=-1
    local configured_ratio=unread output_length=-1 borders=unread

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
    if [[ "$expected_type" == dock ]]; then
        read -r _ configured_ratio base_presented_x base_presented_length \
            _ output_length borders <<< "$(presentation_probe)"
        if ! python3 - "$configured_ratio" <<'PY'
import math
import sys
raise SystemExit(0 if math.isclose(float(sys.argv[1]), 0.6, abs_tol=1e-6)
                 else 1)
PY
        then
            e2e_fail "Dock fixture did not retain its configured 60% resting length"
        fi
        (( base_presented_length < output_length )) \
            || e2e_fail "Dock fixture is not partial before live attachment"
        [[ "$borders" == bottom,left,right,top ]] \
            || e2e_fail "floated Dock fixture did not begin with all corners"
    fi

    local titlebar_offset=12
    local start_x=$((baseline_x + client_width / 2))
    local start_y=$((baseline_y + titlebar_offset))
    local touching_y=$((trigger_y + trigger_height - 8 + titlebar_offset))

    fp draghold 900 \
        "$start_x" "$start_y" \
        "$start_x" "$touching_y" \
        "$start_x" "$start_y" &
    drag_pid=$!

    # Sample the short-lived fractional phase before its stable endpoint. The
    # endpoint policy remains observable for the rest of the held interval.
    wait_for_fractional_progress_while_held \
        "$expected_type" attaching \
        "$expected_type live inward presentation" \
        "$expect_dock_expansion"
    if [[ "$expected_type" == dock
          && "$expect_dock_expansion" == true ]]; then
        #! The complete-span endpoint is the shortest-lived assertion in the
        #! held crossing. Observe it before the policy query spends another
        #! D-Bus round trip; the endpoint itself also proves the attached
        #! target and the still-owned button hold.
        wait_for_dock_attached_presentation_while_held true
    fi
    wait_for_policy_while_held \
        "$expected_type" 1 "$expected_request" "$expected_target" \
        "$expected_type live inward crossing"
    if [[ "$expected_type" == dock
          && "$expect_dock_expansion" != true ]]; then
        assert_partial_dock_presentation \
            "$expected_type live attachment" \
            "$base_presented_x" "$base_presented_length"
    fi
    assert_stable_physical_snapshot \
        "$expected_type live attachment" "$base_snapshot"

    wait_for_fractional_progress_while_held \
        "$expected_type" floating \
        "$expected_type live outward presentation" \
        "$expect_dock_expansion"
    wait_for_policy_while_held \
        "$expected_type" 0 false floated \
        "$expected_type live outward reversal"
    assert_stable_physical_snapshot \
        "$expected_type live reversal" "$base_snapshot"

    wait "$drag_pid" \
        || e2e_fail "$expected_type held titlebar drag failed"
    drag_pid=0
    wait_for_konsole_geometry "$baseline_x" "$baseline_y" \
        "$client_width" "$client_height"
    if [[ "$expected_type" == dock ]]; then
        wait_for_dock_floated_presentation \
            "$base_presented_x" "$base_presented_length"
    fi
    assert_stable_physical_snapshot \
        "$expected_type after release" "$base_snapshot"
    stop_owned_konsole
}

exercise_attached_maximum_length_change() {
    local screen_x screen_y screen_width screen_height
    local initial_maximum initial_absolute_x initial_absolute_width
    local initial_trigger_x initial_trigger_width

    [[ "$(view_config_field 'json.dumps(config["autoSizeEnabled"])')" == false ]] \
        || e2e_fail "attached length-mutation fixture did not disable automatic sizing"
    initial_maximum="$(view_config_field 'config["maxLength"]')" \
        || e2e_fail "could not read the initial configured maximum length"
    [[ "$initial_maximum" == 60 ]] \
        || e2e_fail "attached length-mutation fixture began at ${initial_maximum}% instead of 60%"

    read -r screen_x screen_y screen_width screen_height \
        <<< "$(dock_field '"%d %d %d %d" % tuple(v["screenGeometry"])')"
    read -r initial_absolute_x initial_absolute_width \
        initial_trigger_x initial_trigger_width \
        <<< "$(dock_field '"%d %d %d %d" % (
            v["absoluteGeometry"][0], v["absoluteGeometry"][2],
            v["stableTriggerGeometry"][0], v["stableTriggerGeometry"][2],
        )')"
    local expected_initial_width=$((screen_width * initial_maximum / 100))
    local expected_initial_x=$((screen_x + (screen_width - expected_initial_width) / 2))
    [[ "$initial_absolute_x $initial_absolute_width" \
          == "$expected_initial_x $expected_initial_width"
          && "$initial_trigger_width" -eq "$expected_initial_width"
          && "$initial_trigger_x" -ge $((expected_initial_x - 1))
          && "$initial_trigger_x" -le $((expected_initial_x + 1)) ]] \
        || e2e_fail "initial configured authorities do not describe the centered 60% rest span (absolute=$initial_absolute_x+$initial_absolute_width trigger=$initial_trigger_x+$initial_trigger_width expected=$expected_initial_x+$expected_initial_width)"

    [[ "$(konsole_count)" -eq 0 ]] \
        || e2e_fail "a tagged titlebar client already exists before the attached length mutation"
    setsid konsole -p 'LocalTabTitleFormat=LATTE LIVE TITLEBAR TOUCH' \
        >/dev/null 2>&1 &
    kpid=$!
    for _ in $(seq 1 40); do
        [[ "$(konsole_count)" -eq 1 ]] && break
        sleep 0.1
    done
    [[ "$(konsole_count)" -eq 1 ]] \
        || e2e_fail "the attached length-mutation client never mapped"
    [[ -n "$(set_konsole_maximized true)" ]] \
        || e2e_fail "KWin did not maximize the attached length-mutation client"
    wait_for_dock_attached_presentation_while_held false

    local windows_before canvas cx cy cw ch
    windows_before="$(e2e_dumpwins | grep '|latte-dock|' | sort)"
    "$E2E_FAKEPOINTER" move \
        $((screen_x + screen_width / 2)) \
        $((screen_y + screen_height / 2))
    e2e_call setViewEditMode ub "$view" true >/dev/null \
        || e2e_fail "could not enter edit mode for the attached length mutation"
    sleep 3
    wait_for_dock_attached_presentation_while_held false

    canvas="$(comm -13 \
        <(printf '%s\n' "$windows_before") \
        <(e2e_dumpwins | grep '|latte-dock|' | sort) \
        | awk -F'|' -v sx="$screen_x" -v sw="$screen_width" '
            { split($4, g, " "); split(g[1], p, ","); split(g[2], s, "x");
              if (p[1] == sx && s[1] == sw && s[2] < 300) {
                  printf "%d %d %d %d\n", p[1], p[2], s[1], s[2];
                  exit;
              } }')"
    [[ -n "$canvas" ]] \
        || e2e_fail "no edit canvas mapped for the attached length mutation"
    read -r cx cy cw ch <<< "$canvas"

    local stable_before
    stable_before="$(configured_length_independent_snapshot)" \
        || e2e_fail "could not capture stable state before the attached length mutation"

    local ruler_x=$((screen_x + screen_width / 2))
    local ruler_y=$((cy + ch - 7))
    local changed_maximum="$initial_maximum"
    for attempt in 1 2 3 4 5; do
        "$E2E_FAKEPOINTER" scroll "$ruler_x" "$ruler_y" -1 100
        "$E2E_FAKEPOINTER" move \
            "$ruler_x" $((screen_y + screen_height / 2))
        for _ in $(seq 1 8); do
            sleep 0.5
            changed_maximum="$(view_config_field 'config["maxLength"]')" \
                || continue
            [[ "$changed_maximum" != "$initial_maximum" ]] && break 2
        done
    done
    [[ "$changed_maximum" == 54 ]] \
        || e2e_fail "one attached ruler detent changed Maximum Length from $initial_maximum to $changed_maximum instead of 54"

    local expected_width=$((screen_width * changed_maximum / 100))
    local expected_x=$((screen_x + (screen_width - expected_width) / 2))
    local progress=unread target=unread absolute_x=-1 absolute_width=-1
    local trigger_x=-1 trigger_width=-1 paint_x=-1 paint_width=-1
    local stable_after=unread
    for _ in $(seq 1 100); do
        read -r progress target absolute_x absolute_width \
            trigger_x trigger_width paint_x paint_width \
            <<< "$(dock_field '"%.9f %s %d %d %d %d %d %d" % (
                v["transitionProgress"], v["transitionTarget"],
                v["absoluteGeometry"][0], v["absoluteGeometry"][2],
                v["stableTriggerGeometry"][0], v["stableTriggerGeometry"][2],
                v["windowGeometry"][0] + v["effectsRect"][0],
                v["effectsRect"][2],
            )' 2>/dev/null)" || {
                sleep 0.05
                continue
            }
        stable_after="$(configured_length_independent_snapshot 2>/dev/null)" || {
                sleep 0.05
                continue
            }
        if [[ "$progress" == 0.000000000
              && "$target" == attached
              && "$absolute_x $absolute_width" == "$expected_x $expected_width"
              && "$trigger_width" -eq "$expected_width"
              && "$trigger_x" -ge $((expected_x - 1))
              && "$trigger_x" -le $((expected_x + 1))
              && "$paint_x $paint_width" == "$screen_x $screen_width"
              && "$stable_after" == "$stable_before" ]]; then
            break
        fi
        sleep 0.05
    done
    [[ "$progress" == 0.000000000
          && "$target" == attached
          && "$absolute_x $absolute_width" == "$expected_x $expected_width"
          && "$trigger_width" -eq "$expected_width"
          && "$trigger_x" -ge $((expected_x - 1))
          && "$trigger_x" -le $((expected_x + 1))
          && "$paint_x $paint_width" == "$screen_x $screen_width"
          && "$stable_after" == "$stable_before" ]] \
        || e2e_fail "attached configured-length authorities did not converge without presentation feedback (progress=$progress target=$target absolute=$absolute_x+$absolute_width trigger=$trigger_x+$trigger_width paint=$paint_x+$paint_width expectedRest=$expected_x+$expected_width expectedPaint=$screen_x+$screen_width stableBefore=$stable_before stableAfter=$stable_after)"

    e2e_call setViewEditMode ub "$view" false >/dev/null \
        || e2e_fail "could not leave edit mode after the attached length mutation"
    [[ -n "$(set_konsole_maximized false)" ]] \
        || e2e_fail "KWin did not restore the attached length-mutation client"
    wait_for_dock_floated_presentation "$expected_x" "$expected_width"
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
exercise_held_drag dock false true attached

configure_case dock-top-justify-1out
exercise_held_drag dock false true attached true
exercise_attached_maximum_length_change

echo "Live titlebar window touch passed before button release for Panel, partial Center Dock, and expanding Justify Dock; attached Maximum Length mutation refreshed stable occupancy and touch authority without changing presentation or surface ownership"
