#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# Drive the FP-2 stable-canvas maximize transition with a real Wayland
# toplevel. The QWindow, stable applet measurements, layer-shell placement,
# per-view reservation contribution, and maximum-depth group reservation must
# stay fixed while only the internal qreal presentation progress changes.
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"
source "$E2E_REPO/tests/e2e/matrix/matrix-lib.sh"

view=""
layout=""
group_args=()
kpid=0
configured=0

set_konsole_maximized() {
    local enabled="$1"
    e2e_kwin_js "for (const w of workspace.windowList()) {
        if (w.resourceClass === 'org.kde.konsole' && w.caption.includes('LATTE FP2 STABLE CANVAS')) {
            workspace.activeWindow = w;
            w.setMaximize($enabled, $enabled);
            print('@TAG@|' + w.internalId);
        }
    }" 0.01
}

active_window_id() {
    e2e_kwin_js 'print("@TAG@|" + (workspace.activeWindow ? workspace.activeWindow.internalId : "none"));' | tail -1
}

dock_field() {
    local expr="$1"
    e2e_json dockSystemData | python3 -c "
import json, math, sys
snapshot = json.load(sys.stdin)
match = [v for v in snapshot['views'] if v['persistentDockId'] == $view]
if len(match) != 1:
    sys.exit('expected exactly one dockSystemData record for containment $view')
v = match[0]
print($expr)
"
}

stable_snapshot() {
    dock_field 'json.dumps({
        "stable": {key: v[key] for key in (
            "windowGeometry",
            "absoluteGeometry",
            "surfaceGeometry",
            "canvasGeometry",
            "stableCanvasGeometry",
            "attachedPresentationGeometry",
            "floatedPresentationGeometry",
            "stableTriggerGeometry",
            "stableAppletMeasurementBounds",
            "stablePrimaryAxisStart",
            "stablePrimaryAxisLength",
            "availablePrimaryLength",
            "configuredIconSize",
            "effectiveIconSize",
            "maximumLengthRatio",
            "alignment",
            "geometrySettled",
            "stableLayerShellMargin",
            "requestedReservationDepth",
            "reservationContributionDepth",
            "reservationPublishedDepth",
            "reservationOutputId",
            "reservationEdge",
            "reservationGroupGeneration",
            "reservationContributorDockIds",
            "reservationGeometry",
            "layerShellMargins",
            "layerShellAnchors",
            "layerShellExclusiveEdge",
            "layerShellExclusiveZone",
            "publishedStruts",
        )},
        "objects": {
            "transitionController": v["objects"]["transitionController"],
            "reservationPublisher": v["objects"]["reservationPublisher"],
        },
    }, sort_keys=True, separators=(",", ":"))'
}

revision_snapshot() {
    dock_field '"%s %s %s" % (
        v["transitionGeometryRevision"],
        v["surfaceGeometryPublicationRevision"],
        v["layerShellConfigureRequestRevision"],
    )'
}

popup_anchor_probe() {
    dock_field '"%d %d %d %d %d %d" % (
        *v["appletsLayoutGeometry"],
        math.floor(v["computedPaintMaskGeometry"][1]),
        math.ceil(v["computedPaintMaskGeometry"][1]
                  + v["computedPaintMaskGeometry"][3])
            - math.floor(v["computedPaintMaskGeometry"][1]),
    )'
}

assert_popup_anchor_contract() {
    local phase="$1"
    local x y width height paint_y paint_height
    read -r x y width height paint_y paint_height \
        <<< "$(popup_anchor_probe)" \
        || e2e_fail "$phase could not read popup-anchor geometry"
    [[ "$x $width" == "$base_popup_primary_x $base_popup_primary_width" ]] \
        || e2e_fail "$phase changed the popup anchor primary span: base=$base_popup_primary_x/$base_popup_primary_width current=$x/$width"
    [[ "$y $height" == "$paint_y $paint_height" ]] \
        || e2e_fail "$phase popup anchor did not follow the outward-aligned visible mask: anchor=$y/$height paint=$paint_y/$paint_height"
}

transition_probe() {
    dock_field '"%s %s %s %.9f %s %s %s" % (
        v["transitionTarget"],
        v["transitionPhase"],
        str(v["transitionRunning"]).lower(),
        v["transitionProgress"],
        v["transitionGeometryRevision"],
        v["surfaceGeometryPublicationRevision"],
        v["layerShellConfigureRequestRevision"],
    )'
}

assert_stable_contract() {
    local phase="$1" current revisions
    current="$(stable_snapshot)" || e2e_fail "$phase could not read the stable geometry snapshot"
    [[ "$current" == "$base_stable_snapshot" ]] \
        || e2e_fail "$phase changed the stable panel contract: base=$base_stable_snapshot current=$current"
    revisions="$(revision_snapshot)" || e2e_fail "$phase could not read stable-controller and physical-geometry revisions"
    [[ "$revisions" == "$base_revisions" ]] \
        || e2e_fail "$phase reconfigured stable geometry or published physical geometry during progress: base=$base_revisions current=$revisions"
    assert_popup_anchor_contract "$phase"
}

wait_for_resting_target() {
    local expected_target="$1" expected_progress="$2"
    local target phase running progress geometry_revision surface_revision layer_revision
    for _ in $(seq 1 80); do
        read -r target phase running progress geometry_revision surface_revision layer_revision <<< "$(transition_probe)"
        if [[ "$target" == "$expected_target" && "$phase" == resting && "$running" == false ]] \
                && awk -v actual="$progress" -v expected="$expected_progress" \
                    'BEGIN { difference = actual - expected; if (difference < 0) difference = -difference; exit !(difference < 0.000001) }'; then
            return 0
        fi
        sleep 0.05
    done
    e2e_fail "transition did not settle at $expected_target/$expected_progress (target=$target phase=$phase running=$running progress=$progress)"
}

capture_progress_only_transition() {
    local expected_target="$1" expected_phase="$2"
    local target phase running progress geometry_revision surface_revision layer_revision
    for _ in $(seq 1 100); do
        read -r target phase running progress geometry_revision surface_revision layer_revision <<< "$(transition_probe)"
        if [[ "$target" == "$expected_target" && "$phase" == "$expected_phase" && "$running" == true ]] \
                && awk -v progress="$progress" 'BEGIN { exit !(progress > 0.0 && progress < 1.0) }'; then
            [[ "$geometry_revision $surface_revision $layer_revision" == "$base_revisions" ]] \
                || e2e_fail "$expected_phase transition changed stable-controller or physical-geometry revisions at progress $progress"
            assert_stable_contract "$expected_phase midpoint"
            return 0
        fi
        sleep 0.01
    done
    e2e_fail "no qreal midpoint observed for $expected_phase transition (target=$target phase=$phase running=$running progress=$progress)"
}

wait_for_in_flight_target() {
    local expected_target="$1" expected_phase="$2"
    local target phase running progress geometry_revision surface_revision layer_revision
    for _ in $(seq 1 80); do
        read -r target phase running progress geometry_revision surface_revision layer_revision <<< "$(transition_probe)"
        if [[ "$target" == "$expected_target" && "$phase" == "$expected_phase" && "$running" == true ]] \
                && awk -v progress="$progress" 'BEGIN { exit !(progress > 0.0 && progress < 1.0) }'; then
            [[ "$geometry_revision $surface_revision $layer_revision" == "$base_revisions" ]] \
                || e2e_fail "rapid reversal to $expected_target changed stable-controller or physical-geometry revisions at progress $progress"
            return 0
        fi
        sleep 0.01
    done
    e2e_fail "rapid reversal never entered $expected_phase for target $expected_target (target=$target phase=$phase running=$running progress=$progress)"
}

wait_for_tracker_and_target() {
    local expected_maximized="$1" expected_target="$2" expected_progress="$3"
    local active_maximized exists_maximized target phase running progress geometry_revision surface_revision layer_revision
    for _ in $(seq 1 80); do
        read -r active_maximized exists_maximized <<< "$(e2e_json trackerData u "$view" | python3 -c '
import json, sys
tracker = json.load(sys.stdin)
print(str(tracker["activeWindowMaximized"]).lower(), str(tracker["existsWindowMaximized"]).lower())
')"
        read -r target phase running progress geometry_revision surface_revision layer_revision <<< "$(transition_probe)"
        if [[ "$active_maximized" == "$expected_maximized"
              && "$exists_maximized" == "$expected_maximized"
              && "$target" == "$expected_target"
              && "$phase" == resting
              && "$running" == false ]] \
                && awk -v actual="$progress" -v expected="$expected_progress" \
                    'BEGIN { difference = actual - expected; if (difference < 0) difference = -difference; exit !(difference < 0.000001) }'; then
            return 0
        fi
        sleep 0.05
    done
    e2e_fail "tracker/controller did not settle together (active=$active_maximized exists=$exists_maximized target=$target phase=$phase progress=$progress)"
}

wait_for_zero_gap_floated_snapshot() {
    local snapshot=""
    local configured_panel=unread eligible_panel=unread view_type=unread visibility_mode=unread
    local target=unread phase=unread running=unread progress=unread
    for _ in $(seq 1 80); do
        snapshot="$(dock_field '"%s %s %s %s %s %s %s %.9f" % (
            str(v["floatingPanelConfigured"]).lower(),
            str(v["floatingPanelEligible"]).lower(),
            v["type"],
            v["visibilityMode"],
            v["transitionTarget"],
            v["transitionPhase"],
            str(v["transitionRunning"]).lower(),
            v["transitionProgress"],
        )' 2>/dev/null)" || {
            sleep 0.05
            continue
        }
        read -r configured_panel eligible_panel view_type visibility_mode \
            target phase running progress <<< "$snapshot"
        if [[ "$view_type" == panel
              && "$visibility_mode" == alwaysVisible
              && "$configured_panel" == false
              && "$eligible_panel" == false
              && "$target" == floated
              && "$phase" == resting
              && "$running" == false ]] \
                && awk -v actual="$progress" \
                    'BEGIN { difference = actual - 1.0; if (difference < 0) difference = -difference; exit !(difference < 0.000001) }'; then
            return 0
        fi
        sleep 0.05
    done
    e2e_fail "zero-gap panel never exposed one consistent floated endpoint snapshot (type=$view_type visibility=$visibility_mode configured=$configured_panel eligible=$eligible_panel target=$target phase=$phase running=$running progress=$progress)"
}

wait_for_dock_gap_policy() {
    local expected_visibility="$1" expected_maximized="$2"
    local expected_request="$3" expected_target="$4"
    local expected_progress="$5"
    local active_maximized=unread exists_maximized=unread
    local view_type=unread visibility_mode=unread
    local floating_gap_configured=unread
    local configured_panel=unread eligible_panel=unread
    local configured_hide=unread dock_request=unread
    local transition_geometry=unread panel_geometry_absent=unread
    local floating_popups=unread
    local target=unread phase=unread running=unread progress=-1
    local transition_duration=-1 configured_gap=-1 presented_gap=-1
    for _ in $(seq 1 80); do
        read -r active_maximized exists_maximized \
            <<< "$(e2e_json trackerData u "$view" | python3 -c '
import json, sys
tracker = json.load(sys.stdin)
print(str(tracker["activeWindowMaximized"]).lower(), str(tracker["existsWindowMaximized"]).lower())
')"
        read -r view_type visibility_mode floating_gap_configured \
            configured_panel eligible_panel configured_hide \
            dock_request target phase running progress \
            transition_duration \
            transition_geometry panel_geometry_absent floating_popups \
            configured_gap presented_gap \
            <<< "$(dock_field '"%s %s %s %s %s %s %s %s %s %s %.9f %d %s %s %s %d %d" % (
                v["type"],
                v["visibilityMode"],
                str(v["floatingGapConfigured"]).lower(),
                str(v["floatingPanelConfigured"]).lower(),
                str(v["floatingPanelEligible"]).lower(),
                str(v["attachOnWindowTouchConfigured"]).lower(),
                str(v["dockGapHideRequested"]).lower(),
                v["transitionTarget"],
                v["transitionPhase"],
                str(v["transitionRunning"]).lower(),
                v["transitionProgress"],
                v["transitionAnimationDuration"],
                str(v["transitionGeometryPresent"]).lower(),
                str(all(v[key] is None for key in (
                    "stableCanvasGeometry",
                    "attachedPresentationGeometry",
                    "floatedPresentationGeometry",
                    "currentVisibleGeometry",
                    "computedPaintMaskGeometry",
                    "computedInputBridgeGeometry",
                ))).lower(),
                str(v["floatingAppletPopupsPreferred"]).lower(),
                v["screenEdgeMargin"],
                v["presentedScreenEdgeGap"],
            )')"
        if [[ "$active_maximized" == "$expected_maximized"
              && "$exists_maximized" == "$expected_maximized"
              && "$view_type" == dock
              && "$visibility_mode" == "$expected_visibility"
              && "$floating_gap_configured" == true
              && "$configured_panel" == false
              && "$eligible_panel" == false
              && "$configured_hide" == true
              && "$dock_request" == "$expected_request"
              && "$target" == "$expected_target"
              && "$phase" == resting
              && "$running" == false
              && "$transition_duration" -eq 200
              && "$transition_geometry" == false
              && "$panel_geometry_absent" == true
              && "$floating_popups" == false
              && "$presented_gap" -eq "$((
                  configured_gap * expected_progress
              ))" ]] \
                && awk -v actual="$progress" -v expected="$expected_progress" \
                    'BEGIN { difference = actual - expected; if (difference < 0) difference = -difference; exit !(difference < 0.000001) }'; then
            return 0
        fi
        sleep 0.05
    done
    e2e_fail "Dock maximized-gap policy did not settle (active=$active_maximized exists=$exists_maximized type=$view_type visibility=$visibility_mode floatingGapConfigured=$floating_gap_configured configuredPanel=$configured_panel panelEligible=$eligible_panel configuredHide=$configured_hide dockRequest=$dock_request target=$target phase=$phase running=$running progress=$progress configuredGap=$configured_gap presentedGap=$presented_gap transitionGeometry=$transition_geometry panelGeometryAbsent=$panel_geometry_absent floatingPopups=$floating_popups)"
}

konsole_frame_geometry() {
    local window geometry x y width height output extra
    window="$(e2e_dumpwins | grep '|org.kde.konsole|LATTE FP2 STABLE CANVAS' | tail -1)" || return 1
    [[ -n "$window" ]] || return 1
    geometry="$(awk -F'|' '{ split($4, g, " "); split(g[1], p, ","); split(g[2], s, "x"); print p[1], p[2], s[1], s[2] }' <<<"$window")"
    read -r x y width height extra <<< "$geometry"
    output="$(awk -F'|' '{ print $5 }' <<<"$window")"
    [[ -z "$extra" && "$x" =~ ^-?[0-9]+$ && "$y" =~ ^-?[0-9]+$ && "$width" =~ ^[0-9]+$ && "$height" =~ ^[0-9]+$ ]] || return 1
    (( width > 0 && height > 0 )) || return 1
    [[ -n "$output" ]] || return 1
    printf '%s %s %s %s %s\n' "$x" "$y" "$width" "$height" "$output"
}

assert_konsole_work_area() {
    local phase="$1" geometry kx ky kw kh output expected_x expected_y expected_w expected_h
    geometry="$(konsole_frame_geometry)" || e2e_fail "$phase maximize has no valid Konsole frame geometry"
    read -r kx ky kw kh output <<< "$geometry"
    [[ "$output" == "$screen" ]] || e2e_fail "$phase maximize placed the Konsole fixture on output '$output'; expected '$screen'"
    expected_x=$screen_x
    expected_w=$screen_w
    expected_h=$((screen_h - stable_reservation_depth))
    if [[ "$edge" == top ]]; then
        expected_y=$((screen_y + stable_reservation_depth))
    else
        expected_y=$screen_y
    fi
    (( kx == expected_x && ky == expected_y && kw == expected_w && kh == expected_h )) \
        || e2e_fail "$phase maximize has frame $kx,$ky ${kw}x${kh}; expected exact $edge work area $expected_x,$expected_y ${expected_w}x${expected_h} from the stable ${stable_reservation_depth}px group reservation"
}

cleanup() {
    local body_status=$? cleanup_failed=0 dock_pid
    trap - EXIT
    if (( kpid != 0 )); then
        kill "$kpid" 2>/dev/null || true
        wait "$kpid" 2>/dev/null || true
    fi
    if (( configured == 1 )); then
        if ! e2e_dock_stop >/dev/null 2>&1; then
            cleanup_failed=1
        fi
        rm -rf "${E2E_CONFIG_HOME:?}"
        cp -r "$MATRIX_PRISTINE" "$E2E_CONFIG_HOME" || cleanup_failed=1
        dock_pid="$(e2e_dock_pid)"
        if [[ -n "$dock_pid" ]] && kill -0 "$dock_pid" 2>/dev/null; then
            cleanup_failed=1
        elif ! e2e_dock_start 90 >/dev/null 2>&1; then
            cleanup_failed=1
        fi
    fi
    if (( cleanup_failed != 0 )); then
        echo "FAIL: FP-2 stable-canvas fixture cleanup did not restore the dock configuration" >&2
        (( body_status == 0 )) && body_status=1
    fi
    exit "$body_status"
}
trap cleanup EXIT

matrix_init || e2e_fail "could not capture the pristine nested configuration"
configured=1
matrix_stage panel-bottom-justify-1out \
    || e2e_fail "could not realize the floating-panel fixture"
view="$(matrix_view_id)" || e2e_fail "could not resolve the floating-panel fixture"
layout="$E2E_LAYOUT"
group_args=(--file "$layout" --group Containments --group "$view" --group General)

e2e_dock_stop || e2e_fail "dock did not stop before fixture configuration"
kwriteconfig6 "${group_args[@]}" --key maximizeWhenMaximized false || e2e_fail "could not disable maximize-driven panel length"
kwriteconfig6 "${group_args[@]}" --key maxLength 60 || e2e_fail "could not configure a partial panel length"
kwriteconfig6 "${group_args[@]}" --key hideFloatingGapForMaximized true || e2e_fail "could not configure floating-gap attachment"
kwriteconfig6 "${group_args[@]}" --key screenEdgeMargin 18 || e2e_fail "could not configure the floating gap"
kwriteconfig6 "${group_args[@]}" --key floatingInternalGapIsForced false || e2e_fail "could not keep the floating gap under panel-surface ownership"
kwriteconfig6 "${group_args[@]}" --key alignment 10 || e2e_fail "could not configure Justify alignment"
kwriteconfig6 "${group_args[@]}" --key alignmentUpgraded true || e2e_fail "could not mark the Justify alignment as upgraded"
e2e_dock_start 90 || e2e_fail "dock did not restart with the stable-canvas fixture"
e2e_call setViewVisibilityMode us "$view" alwaysVisible >/dev/null || e2e_fail "could not set the fixture view to alwaysVisible"
for _ in $(seq 1 40); do
    [[ "$(e2e_view_field "$view" 'v["visibilityMode"]')" == alwaysVisible ]] && break
    sleep 0.25
done
[[ "$(e2e_view_field "$view" 'v["visibilityMode"]')" == alwaysVisible ]] || e2e_fail "view $view did not enter alwaysVisible mode"

read -r configured_panel eligible_panel geometry_present alignment <<< "$(dock_field '"%s %s %s %s" % (
    str(v["floatingPanelConfigured"]).lower(),
    str(v["floatingPanelEligible"]).lower(),
    str(v["transitionGeometryPresent"]).lower(),
    v["alignment"],
)')"
[[ "$configured_panel" == true && "$eligible_panel" == true && "$geometry_present" == true ]] \
    || e2e_fail "view $view did not expose an eligible configured floating-panel controller"
[[ "$alignment" == justify ]] || e2e_fail "view $view did not retain Justify alignment"
wait_for_resting_target floated 1

read -r base_window_width screen_x screen_y screen_w screen_h edge screen stable_reservation_depth contribution_depth requested_depth <<< "$(dock_field '"%d %d %d %d %d %s %s %d %d %d" % (
    v["windowGeometry"][2],
    v["screenGeometry"][0],
    v["screenGeometry"][1],
    v["screenGeometry"][2],
    v["screenGeometry"][3],
    v["edge"],
    v["screen"],
    v["reservationPublishedDepth"],
    v["reservationContributionDepth"],
    v["requestedReservationDepth"],
)')"
(( screen_w > 0 && screen_h > 0 )) || e2e_fail "view $view reported invalid output dimensions ${screen_w}x${screen_h}"
[[ -n "$screen" ]] || e2e_fail "view $view did not report its output name"
(( base_window_width * 100 < screen_w * 90 )) || e2e_fail "fixture view $view is not partial ($base_window_width of ${screen_w}px)"
(( requested_depth == contribution_depth )) || e2e_fail "requested depth $requested_depth differs from the view contribution $contribution_depth"
(( stable_reservation_depth >= contribution_depth && contribution_depth > 0 )) \
    || e2e_fail "maximum-depth reservation $stable_reservation_depth does not cover contribution $contribution_depth"

base_stable_snapshot="$(stable_snapshot)" || e2e_fail "could not capture the base stable geometry contract"
base_revisions="$(revision_snapshot)" || e2e_fail "could not capture base stable-controller and physical-geometry revisions"
read -r base_popup_primary_x _ base_popup_primary_width _ _ _ \
    <<< "$(popup_anchor_probe)" \
    || e2e_fail "could not capture the base popup-anchor primary span"

setsid konsole -p 'LocalTabTitleFormat=LATTE FP2 STABLE CANVAS' >/dev/null 2>&1 &
kpid=$!
for _ in $(seq 1 30); do
    konsole="$(e2e_dumpwins | grep '|org.kde.konsole|LATTE FP2 STABLE CANVAS' | tail -1)"
    [[ -n "$konsole" ]] && break
    sleep 0.5
done
[[ -n "${konsole:-}" ]] || e2e_fail "Konsole stable-canvas fixture never mapped"

fixture_id="$(set_konsole_maximized false)" || e2e_fail "KWin did not normalize the Konsole fixture"
[[ -n "$fixture_id" && "$fixture_id" != *$'\n'* ]] || e2e_fail "KWin found multiple tagged Konsole fixtures"
wait_for_tracker_and_target false floated 1
assert_stable_contract "normalized floated state"

[[ "$(set_konsole_maximized true)" == "$fixture_id" ]] || e2e_fail "KWin did not maximize the tagged Konsole fixture"
capture_progress_only_transition attached attaching
wait_for_tracker_and_target true attached 0
assert_stable_contract "attached resting state"
[[ "$(active_window_id)" == "$fixture_id" ]] || e2e_fail "tagged Konsole was not active after attachment"
assert_konsole_work_area "attached"

[[ "$(set_konsole_maximized false)" == "$fixture_id" ]] || e2e_fail "KWin did not restore the tagged Konsole fixture"
capture_progress_only_transition floated floating
wait_for_tracker_and_target false floated 1
assert_stable_contract "floated resting state"

for maximized in true false true false true false true false; do
    expected_target=attached
    expected_phase=attaching
    if [[ "$maximized" == false ]]; then
        expected_target=floated
        expected_phase=floating
    fi
    [[ "$(set_konsole_maximized "$maximized")" == "$fixture_id" ]] \
        || e2e_fail "KWin did not drive the $expected_target storm target"
    wait_for_in_flight_target "$expected_target" "$expected_phase"
done

[[ "$(set_konsole_maximized true)" == "$fixture_id" ]] || e2e_fail "KWin did not settle the storm at attached"
wait_for_tracker_and_target true attached 0
assert_stable_contract "rapid reversal storm"
assert_konsole_work_area "post-storm attached"

e2e_dock_stop || e2e_fail "dock did not stop before the zero-gap boundary check"
kwriteconfig6 "${group_args[@]}" --key screenEdgeMargin 0 \
    || e2e_fail "could not configure the legal zero-pixel floating gap"
e2e_dock_start 90 || e2e_fail "dock did not restart for the zero-gap boundary check"
wait_for_zero_gap_floated_snapshot

matrix_stage dock-bottom-center-1out \
    || e2e_fail "could not realize the legacy floating-Dock fixture"
view="$(matrix_view_id)" \
    || e2e_fail "could not resolve the legacy floating-Dock fixture"
group_args=(--file "$layout" --group Containments --group "$view" --group General)

e2e_dock_stop \
    || e2e_fail "dock did not stop before legacy Dock policy configuration"
kwriteconfig6 "${group_args[@]}" --key hideFloatingGapForMaximized true \
    || e2e_fail "could not configure legacy Dock maximized-gap hiding"
kwriteconfig6 "${group_args[@]}" --key screenEdgeMargin 18 \
    || e2e_fail "could not configure the legacy Dock floating gap"
kwriteconfig6 "${group_args[@]}" --key floatingInternalGapIsForced false \
    || e2e_fail "could not keep the legacy Dock gap under transition ownership"
kwriteconfig6 "${group_args[@]}" --key floatingGapHidingWaitsMouse false \
    || e2e_fail "could not configure immediate Dock attachment under the pointer"
kwriteconfig6 "${group_args[@]}" --key durationTime 2 \
    || e2e_fail "could not configure normal animation speed"
e2e_dock_start 90 \
    || e2e_fail "dock did not restart with the legacy floating-Dock fixture"
e2e_call setViewVisibilityMode us "$view" alwaysVisible >/dev/null \
    || e2e_fail "could not set the legacy Dock fixture to alwaysVisible"

[[ "$(set_konsole_maximized false)" == "$fixture_id" ]] \
    || e2e_fail "KWin did not normalize the client for the legacy Dock check"
wait_for_dock_gap_policy alwaysVisible false false floated 1
[[ "$(set_konsole_maximized true)" == "$fixture_id" ]] \
    || e2e_fail "KWin did not maximize the client for the legacy Dock check"
wait_for_dock_gap_policy alwaysVisible true true attached 0
[[ "$(set_konsole_maximized false)" == "$fixture_id" ]] \
    || e2e_fail "KWin did not restore the client for the legacy Dock check"
wait_for_dock_gap_policy alwaysVisible false false floated 1

e2e_call setViewVisibilityMode us "$view" windowsGoBelow >/dev/null \
    || e2e_fail "could not set the legacy Dock fixture to windowsGoBelow"
wait_for_dock_gap_policy windowsGoBelow false false floated 1
[[ "$(set_konsole_maximized true)" == "$fixture_id" ]] \
    || e2e_fail "KWin did not maximize the client for the WindowsGoBelow Dock check"
wait_for_dock_gap_policy windowsGoBelow true true attached 0
[[ "$(set_konsole_maximized false)" == "$fixture_id" ]] \
    || e2e_fail "KWin did not restore the client for the WindowsGoBelow Dock check"
wait_for_dock_gap_policy windowsGoBelow false false floated 1

echo "FP-2/FP-4A stable canvas held its maximum-depth reservation across qreal reversals and preserved the separate Always Visible and Windows Go Below Dock maximized-gap arm"
