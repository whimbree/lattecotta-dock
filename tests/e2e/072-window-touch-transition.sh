#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# Drive FP-4A stable window-touch attachment with one real Wayland toplevel.
# Direct frame placement establishes the negative control. KWin's persistent
# interactive-move mode then crosses the stable trigger in both directions,
# reverses both animation directions at fractional progress, and proves Escape
# restores both client geometry and policy. A committed maximize supplies the
# ordinary end-user path before destruction proves fail-closed count reset.
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"
source "$E2E_REPO/tests/e2e/matrix/matrix-lib.sh"

view=""
layout=""
group_args=()
kpid=0
configured=0
base_stable_snapshot=""
base_revisions=""
base_popup_primary_x=""
base_popup_primary_width=""

fp() {
    "$E2E_FAKEPOINTER" "$@"
}

dock_field() {
    local expr="$1"
    e2e_json dockSystemData | python3 -c "
import json, math, sys
snapshot = json.load(sys.stdin)
if snapshot['schemaVersion'] != 10:
    sys.exit('expected dockSystemData schema 10')
match = [v for v in snapshot['views'] if v['persistentDockId'] == $view]
if len(match) != 1:
    sys.exit('expected exactly one dockSystemData record for containment $view')
v = match[0]
print($expr)
"
}

stable_snapshot() {
    dock_field 'json.dumps({
        "reservationStateGeneration": snapshot["reservationStateGeneration"],
        "geometry": {key: v[key] for key in (
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
            "view": v["objects"]["view"],
            "geometryController": v["objects"]["geometryController"],
            "transitionController": v["objects"]["transitionController"],
            "windowTouchTracker": v["objects"]["windowTouchTracker"],
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
    local boundary="$1"
    local x y width height paint_y paint_height
    read -r x y width height paint_y paint_height \
        <<< "$(popup_anchor_probe)" \
        || e2e_fail "$boundary could not read popup-anchor geometry"
    [[ "$x $width" == "$base_popup_primary_x $base_popup_primary_width" ]] \
        || e2e_fail "$boundary changed the popup anchor primary span: base=$base_popup_primary_x/$base_popup_primary_width current=$x/$width"
    [[ "$y $height" == "$paint_y $paint_height" ]] \
        || e2e_fail "$boundary popup anchor did not follow the outward-aligned visible mask: anchor=$y/$height paint=$paint_y/$paint_height"
}

assert_stable_contract() {
    local boundary="$1" current revisions
    current="$(stable_snapshot)" \
        || e2e_fail "$boundary could not read the stable window-touch contract"
    [[ "$current" == "$base_stable_snapshot" ]] \
        || e2e_fail "$boundary changed stable QWindow, reservation, applet, trigger, or authority state: base=$base_stable_snapshot current=$current"
    revisions="$(revision_snapshot)" \
        || e2e_fail "$boundary could not read physical publication revisions"
    [[ "$revisions" == "$base_revisions" ]] \
        || e2e_fail "$boundary changed physical publication revisions: base=$base_revisions current=$revisions"
    assert_popup_anchor_contract "$boundary"
}

policy_probe() {
    dock_field '"%s %s %s %s %s %s %d %s %s %s %.9f" % (
        str(v["floatingPanelEligible"]).lower(),
        str(v["attachOnWindowTouchConfigured"]).lower(),
        str(v["attachmentWaitsForPointerExitConfigured"]).lower(),
        str(v["pointerInsideView"]).lower(),
        str(v["attachmentDeferredByPointer"]).lower(),
        str(v["dockGapHideRequested"]).lower(),
        v["touchingWindowCount"],
        v["transitionTarget"],
        v["transitionPhase"],
        str(v["transitionRunning"]).lower(),
        v["transitionProgress"],
    )'
}

wait_for_policy() {
    local expected_pointer_inside="$1" expected_deferred="$2"
    local expected_count="$3" expected_target="$4"
    local expected_progress="$5" boundary="$6"
    local eligible=unread configured_touch=unread configured_wait=unread
    local pointer_inside=unread deferred=unread dock_request=unread count=-1
    local target=unread phase=unread running=unread progress=-1
    for _ in $(seq 1 100); do
        read -r eligible configured_touch configured_wait pointer_inside \
            deferred dock_request count target phase running progress \
            <<< "$(policy_probe)"
        if [[ "$eligible" == true
              && "$configured_touch" == true
              && "$configured_wait" == true
              && "$pointer_inside" == "$expected_pointer_inside"
              && "$deferred" == "$expected_deferred"
              && "$dock_request" == false
              && "$count" == "$expected_count"
              && "$target" == "$expected_target"
              && "$phase" == resting
              && "$running" == false ]] \
                && awk -v actual="$progress" -v expected="$expected_progress" \
                    'BEGIN { difference = actual - expected; if (difference < 0) difference = -difference; exit !(difference < 0.000001) }'; then
            if [[ -n "$base_stable_snapshot" && -n "$base_revisions" ]]; then
                assert_stable_contract "$boundary settled"
            fi
            return 0
        fi
        sleep 0.05
    done
    e2e_fail "$boundary did not settle at pointerInside=$expected_pointer_inside deferred=$expected_deferred count=$expected_count target=$expected_target/$expected_progress (eligible=$eligible configured=$configured_touch waits=$configured_wait pointerInside=$pointer_inside deferred=$deferred dockRequest=$dock_request count=$count target=$target phase=$phase running=$running progress=$progress)"
}

capture_fractional_policy() {
    local expected_pointer_inside="$1" expected_deferred="$2"
    local expected_count="$3" expected_target="$4"
    local expected_phase="$5" boundary="$6"
    local eligible=unread configured_touch=unread configured_wait=unread
    local pointer_inside=unread deferred=unread dock_request=unread count=-1
    local target=unread phase=unread running=unread progress=-1
    for _ in $(seq 1 100); do
        read -r eligible configured_touch configured_wait pointer_inside \
            deferred dock_request count target phase running progress \
            <<< "$(policy_probe)"
        if [[ "$eligible" == true
              && "$configured_touch" == true
              && "$configured_wait" == true
              && "$pointer_inside" == "$expected_pointer_inside"
              && "$deferred" == "$expected_deferred"
              && "$dock_request" == false
              && "$count" == "$expected_count"
              && "$target" == "$expected_target"
              && "$phase" == "$expected_phase"
              && "$running" == true ]] \
                && awk -v progress="$progress" \
                    'BEGIN { exit !(progress > 0.0 && progress < 1.0) }'; then
            assert_stable_contract "$boundary fractional"
            return 0
        fi
        sleep 0.01
    done
    e2e_fail "$boundary exposed no fractional $expected_phase state (eligible=$eligible configured=$configured_touch waits=$configured_wait pointerInside=$pointer_inside deferred=$deferred dockRequest=$dock_request count=$count target=$target phase=$phase running=$running progress=$progress)"
}

konsole_count() {
    e2e_dumpwins | grep -c '|org.kde.konsole|LATTE FP4 WINDOW TOUCH' || true
}

konsole_geometry() {
    local row
    row="$(e2e_dumpwins \
        | grep '|org.kde.konsole|LATTE FP4 WINDOW TOUCH' \
        | tail -1)" || return 1
    [[ -n "$row" ]] || return 1
    awk -F'|' '{
        split($4, g, " ");
        split(g[1], p, ",");
        split(g[2], s, "x");
        print p[1], p[2], s[1], s[2], $5
    }' <<< "$row"
}

move_konsole_for_setup() {
    local x="$1" y="$2" width="$3" height="$4"
    e2e_kwin_js "for (const w of workspace.windowList()) {
        if (w.resourceClass === 'org.kde.konsole'
                && w.caption.includes('LATTE FP4 WINDOW TOUCH')) {
            const geometry = Object.assign({}, w.frameGeometry);
            geometry.x = $x;
            geometry.y = $y;
            geometry.width = $width;
            geometry.height = $height;
            w.frameGeometry = geometry;
            workspace.activeWindow = w;
            print('@TAG@|' + w.internalId);
        }
    }" | tail -1
}

wait_for_konsole_geometry() {
    local expected_x="$1" expected_y="$2"
    local expected_width="$3" expected_height="$4"
    local x=unread y=unread width=unread height=unread output=unread
    for _ in $(seq 1 60); do
        read -r x y width height output <<< "$(konsole_geometry)"
        if [[ "$x" == "$expected_x"
              && "$y" == "$expected_y"
              && "$width" == "$expected_width"
              && "$height" == "$expected_height" ]]; then
            return 0
        fi
        sleep 0.05
    done
    e2e_fail "KWin did not place the single client at $expected_x,$expected_y ${expected_width}x${expected_height} (actual=$x,$y ${width}x${height})"
}

active_window_id() {
    e2e_kwin_js \
        'print("@TAG@|" + (workspace.activeWindow ? workspace.activeWindow.internalId : "none"));' \
        | tail -1
}

invoke_window_move() {
    busctl --user call org.kde.kglobalaccel /component/kwin \
        org.kde.kglobalaccel.Component invokeShortcut s "Window Move" \
        >/dev/null 2>&1
}

nudge_vertical() {
    local key="$1" count="$2"
    for _ in $(seq 1 "$count"); do
        fp key "$key" || e2e_fail "could not deliver $key during KWin interactive move"
        sleep 0.02
    done
}

set_konsole_maximized() {
    local enabled="$1"
    e2e_kwin_js "for (const w of workspace.windowList()) {
        if (w.resourceClass === 'org.kde.konsole'
                && w.caption.includes('LATTE FP4 WINDOW TOUCH')) {
            workspace.activeWindow = w;
            w.setMaximize($enabled, $enabled);
            print('@TAG@|' + w.internalId);
        }
    }" 0.05 | tail -1
}

konsole_maximize_mode() {
    e2e_kwin_js "for (const w of workspace.windowList()) {
        if (w.resourceClass === 'org.kde.konsole'
                && w.caption.includes('LATTE FP4 WINDOW TOUCH')) {
            print('@TAG@|' + w.maximizeMode);
        }
    }" 0.01 | tail -1
}

wait_for_maximize_mode() {
    local expected="$1" actual=unread
    for _ in $(seq 1 60); do
        actual="$(konsole_maximize_mode)"
        [[ "$actual" == "$expected" ]] && return 0
        sleep 0.05
    done
    e2e_fail "KWin maximize mode did not become $expected (actual=$actual)"
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
        echo "FAIL: FP-4A window-touch fixture cleanup did not restore the dock configuration" >&2
        (( body_status == 0 )) && body_status=1
    fi
    exit "$body_status"
}
trap cleanup EXIT

matrix_init \
    || e2e_fail "could not capture the pristine nested configuration"
configured=1
matrix_stage panel-bottom-justify-1out \
    || e2e_fail "could not realize the stable window-touch fixture"
view="$(matrix_view_id)" \
    || e2e_fail "could not resolve the stable window-touch fixture"
layout="$E2E_LAYOUT"
group_args=(
    --file "$layout"
    --group Containments
    --group "$view"
    --group General
)

e2e_dock_stop \
    || e2e_fail "dock did not stop before window-touch fixture configuration"
kwriteconfig6 "${group_args[@]}" --key maximizeWhenMaximized false \
    || e2e_fail "could not disable maximize-driven panel length"
kwriteconfig6 "${group_args[@]}" --key maxLength 60 \
    || e2e_fail "could not configure a partial panel length"
kwriteconfig6 "${group_args[@]}" --key hideFloatingGapForMaximized true \
    || e2e_fail "could not configure attachment on stable window touch"
kwriteconfig6 "${group_args[@]}" --key floatingGapHidingWaitsMouse true \
    || e2e_fail "could not configure pointer deferral"
kwriteconfig6 "${group_args[@]}" --key screenEdgeMargin 18 \
    || e2e_fail "could not configure the floating gap"
kwriteconfig6 "${group_args[@]}" --key floatingInternalGapIsForced false \
    || e2e_fail "could not keep the floating gap under panel-surface ownership"
kwriteconfig6 "${group_args[@]}" --key alignment 10 \
    || e2e_fail "could not configure Justify alignment"
kwriteconfig6 "${group_args[@]}" --key alignmentUpgraded true \
    || e2e_fail "could not mark Justify alignment as upgraded"
e2e_dock_start 90 \
    || e2e_fail "dock did not restart with the window-touch fixture"
e2e_call setViewVisibilityMode us "$view" alwaysVisible >/dev/null \
    || e2e_fail "could not set the fixture view to alwaysVisible"
for _ in $(seq 1 40); do
    [[ "$(e2e_view_field "$view" 'v["visibilityMode"]')" == alwaysVisible ]] \
        && break
    sleep 0.25
done
[[ "$(e2e_view_field "$view" 'v["visibilityMode"]')" == alwaysVisible ]] \
    || e2e_fail "view $view did not enter alwaysVisible mode"

fp move 20 20 \
    || e2e_fail "could not park the pointer outside the panel"

read -r view_type floating_gap_configured configured_panel eligible_panel \
    geometry_present alignment \
    <<< "$(dock_field '"%s %s %s %s %s %s" % (
        v["type"],
        str(v["floatingGapConfigured"]).lower(),
        str(v["floatingPanelConfigured"]).lower(),
        str(v["floatingPanelEligible"]).lower(),
        str(v["transitionGeometryPresent"]).lower(),
        v["alignment"],
    )')"
[[ "$view_type" == panel
      && "$floating_gap_configured" == true
      && "$configured_panel" == true
      && "$eligible_panel" == true
      && "$geometry_present" == true
      && "$alignment" == justify ]] \
    || e2e_fail "fixture is not one eligible Justify panel with stable geometry (type=$view_type floatingGapConfigured=$floating_gap_configured configuredPanel=$configured_panel eligible=$eligible_panel geometry=$geometry_present alignment=$alignment)"

wait_for_policy false false 0 floated 1 "initial negative control"

read -r trigger_x trigger_y trigger_width trigger_height \
    screen_x screen_y screen_width screen_height screen_name \
    <<< "$(dock_field '"%d %d %d %d %d %d %d %d %s" % (
        *v["stableTriggerGeometry"],
        *v["screenGeometry"],
        v["screen"],
    )')"
(( trigger_width > 0 && trigger_height > 0 )) \
    || e2e_fail "stable trigger is invalid: $trigger_x,$trigger_y ${trigger_width}x${trigger_height}"

base_stable_snapshot="$(stable_snapshot)" \
    || e2e_fail "could not capture the base stable window-touch contract"
base_revisions="$(revision_snapshot)" \
    || e2e_fail "could not capture base physical publication revisions"
read -r base_popup_primary_x _ base_popup_primary_width _ _ _ \
    <<< "$(popup_anchor_probe)" \
    || e2e_fail "could not capture the base popup-anchor primary span"
assert_stable_contract "initial settled panel"
transition_token="$(dock_field 'v["objects"]["transitionController"]')"
tracker_token="$(dock_field 'v["objects"]["windowTouchTracker"]')"
[[ -n "$transition_token"
      && -n "$tracker_token"
      && "$transition_token" != "$tracker_token" ]] \
    || e2e_fail "transition and window-touch authorities are absent or aliased"

[[ "$(konsole_count)" -eq 0 ]] \
    || e2e_fail "a tagged Konsole already exists; this recipe owns one client"
setsid konsole -p 'LocalTabTitleFormat=LATTE FP4 WINDOW TOUCH' \
    >/dev/null 2>&1 &
kpid=$!
for _ in $(seq 1 30); do
    [[ "$(konsole_count)" -eq 1 ]] && break
    sleep 0.5
done
[[ "$(konsole_count)" -eq 1 ]] \
    || e2e_fail "the single Konsole window-touch client never mapped"

geometry_role_type=""
for _ in $(seq 1 40); do
    geometry_role_type="$(dock_field 'v["windowTouchGeometryRoleType"]')"
    [[ "$geometry_role_type" == QRect ]] && break
    sleep 0.05
done
[[ "$geometry_role_type" == QRect ]] \
    || e2e_fail "the live TasksModel Geometry role was not observed as QRect (type=$geometry_role_type)"

fixture_id="$(set_konsole_maximized false)" \
    || e2e_fail "KWin did not identify the single client for setup normalization"
[[ -n "$fixture_id" && "$fixture_id" != *$'\n'* ]] \
    || e2e_fail "KWin found an invalid number of tagged clients"
wait_for_maximize_mode 0

read -r _ _ client_width client_height _ <<< "$(konsole_geometry)"
(( client_width > 0 && client_height > 0 )) \
    || e2e_fail "the single client reported invalid geometry"

step=8
touch_nudges=3
baseline_x=$((trigger_x + trigger_width / 2 - client_width / 2))
minimum_x=$((screen_x + 20))
maximum_x=$((screen_x + screen_width - client_width - 20))
(( baseline_x < minimum_x )) && baseline_x=$minimum_x
(( baseline_x > maximum_x )) && baseline_x=$maximum_x
baseline_y=$((trigger_y - client_height - (touch_nudges - 1) * step))

placement_id="$(move_konsole_for_setup \
    "$baseline_x" "$baseline_y" "$client_width" "$client_height")" \
    || e2e_fail "KWin did not identify the single client for negative-control placement"
[[ "$placement_id" == "$fixture_id" ]] \
    || e2e_fail "KWin setup placement targeted a different client (fixture=$fixture_id placement=$placement_id)"
wait_for_konsole_geometry \
    "$baseline_x" "$baseline_y" "$client_width" "$client_height"
wait_for_policy false false 0 floated 1 "direct-placement negative control"

fp click "$((baseline_x + client_width / 2))" \
         "$((baseline_y + client_height / 2))" \
    || e2e_fail "could not activate the owned client"
for _ in $(seq 1 20); do
    [[ "$(active_window_id)" == "$fixture_id" ]] && break
    sleep 0.05
done
[[ "$(active_window_id)" == "$fixture_id" ]] \
    || e2e_fail "the owned client did not become KWin's active window"

#! One persistent interactive move crosses in, out, in, and out. The first
#! two reversals happen before either animation reaches an endpoint.
invoke_window_move \
    || e2e_fail "could not start KWin interactive move"
sleep 0.2
nudge_vertical Down "$touch_nudges"
capture_fractional_policy false false 1 attached attaching \
    "interactive drag into stable trigger"
nudge_vertical Up "$touch_nudges"
capture_fractional_policy false false 0 floated floating \
    "fractional attaching-to-floating reversal"
nudge_vertical Down "$touch_nudges"
capture_fractional_policy false false 1 attached attaching \
    "fractional floating-to-attaching reversal"
nudge_vertical Up "$touch_nudges"
capture_fractional_policy false false 0 floated floating \
    "interactive drag back out"
fp key Return \
    || e2e_fail "could not commit the interactive move outside the trigger"
wait_for_konsole_geometry \
    "$baseline_x" "$baseline_y" "$client_width" "$client_height"
wait_for_policy false false 0 floated 1 "interactive out-of-trigger commit"

#! Escape must cancel a second in-flight move after it has crossed the trigger.
invoke_window_move \
    || e2e_fail "could not start the cancelable KWin interactive move"
sleep 0.2
nudge_vertical Down "$touch_nudges"
capture_fractional_policy false false 1 attached attaching \
    "cancel trial trigger crossing"
fp key Escape \
    || e2e_fail "could not cancel the in-flight move with Escape"
wait_for_konsole_geometry \
    "$baseline_x" "$baseline_y" "$client_width" "$client_height"
wait_for_policy false false 0 floated 1 \
    "Escape geometry and policy restoration"

#! A committed full maximize is the normal non-scripted geometry path into
#! the reserved edge. MaximizeMode 3 proves KWin accepted both axes.
[[ "$(set_konsole_maximized true)" == "$fixture_id" ]] \
    || e2e_fail "KWin did not identify the owned client for committed maximize"
wait_for_maximize_mode 3
capture_fractional_policy false false 1 attached attaching \
    "committed maximize attachment"
wait_for_policy false false 1 attached 0 \
    "committed maximize attachment"

read -r canvas_x canvas_y visible_x visible_y visible_width visible_height \
    <<< "$(dock_field '"%d %d %d %d %d %d" % (
        v["stableCanvasGeometry"][0],
        v["stableCanvasGeometry"][1],
        v["currentVisibleGeometry"][0],
        v["currentVisibleGeometry"][1],
        v["currentVisibleGeometry"][2],
        v["currentVisibleGeometry"][3],
    )')"
pointer_x=$((canvas_x + visible_x + visible_width / 2))
pointer_y=$((canvas_y + visible_y + visible_height / 2))
fp glide 20 20 "$pointer_x" "$pointer_y" \
    || e2e_fail "could not move the pointer inside the attached panel"
wait_for_policy true false 1 attached 0 \
    "pointer entry preserves the existing attachment"

[[ "$(set_konsole_maximized false)" == "$fixture_id" ]] \
    || e2e_fail "KWin did not identify the owned client for pointer-held touch loss"
wait_for_maximize_mode 0
capture_fractional_policy true false 0 floated floating \
    "pointer-held touch loss"
wait_for_policy true false 0 floated 1 \
    "pointer-held touch loss"

[[ "$(set_konsole_maximized true)" == "$fixture_id" ]] \
    || e2e_fail "KWin did not identify the owned client for pointer-present attachment"
wait_for_maximize_mode 3
wait_for_policy true true 1 floated 1 \
    "pointer-present attachment deferral"

fp glide "$pointer_x" "$pointer_y" 20 20 \
    || e2e_fail "could not move the pointer out of the panel"
capture_fractional_policy false false 1 attached attaching \
    "pointer deferral release"
wait_for_policy false false 1 attached 0 \
    "pointer deferral release"

kill "$kpid" \
    || e2e_fail "could not destroy the single window-touch client"
capture_fractional_policy false false 0 floated floating \
    "client destruction reset"
wait "$kpid" 2>/dev/null || true
kpid=0
for _ in $(seq 1 40); do
    [[ "$(konsole_count)" -eq 0 ]] && break
    sleep 0.1
done
[[ "$(konsole_count)" -eq 0 ]] \
    || e2e_fail "the single window-touch client remained mapped after destruction"
wait_for_policy false false 0 floated 1 \
    "client destruction reset"

echo "FP-4A stable window touch passed interactive reversals, Escape restoration, existing-attachment preservation, pointer-present deferral, destruction reset, and zero stable-surface revision drift"
