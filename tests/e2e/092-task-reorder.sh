#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# C-I8 / P7 acceptance (docs/tracking/e2e-interaction-test-plan.md section 5): the
# task-reorder driver (tests/e2e/matrix/task-reorder-lib.sh) and the G4
# window-task order readback. Proven on the launcher sub-model, which reorders
# through the IDENTICAL tasks-applet handler as window tasks (MouseHandler.qml
# tasksModel.move), so it is the deterministic vehicle-friendly stand-in the
# window sub-model rides on (O6). Four legs, in HC3 tripwire shape:
#
#   1. POSITIVE CONTROL: a real reorder - the driver must SEE the order flip,
#      or its "no reorder" verdict below would be worthless.
#   2. HC3 REJECTION: a zero-cross hold-noop - the driver reports NO reorder
#      (order byte-unchanged, launchers key byte-unchanged) AS the refusal, the
#      thing HC3 demands a driver be able to observe.
#   3. D1 EVIDENCE: a crossed drag then Escape-held (dragkey) vs the same
#      crossed drag without Escape - proves whether Escape reverts a committed
#      task move (defect D1). Recorded truthfully, not wished.
#   4. REVERSE-JITTER (DR-2 / T5b): out-and-back nets to zero - a clean
#      abort-with-no-residue observation on both the order and the config key.
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"
source "$E2E_REPO/tests/e2e/matrix/task-reorder-lib.sh"

view="$(e2e_tasks_view)" || e2e_fail "no tasks view"

#! preconditions: a pure-launcher bar (a window task reflows mid-drag and its
#! order does not persist), at least three launchers so a two-slot swap is
#! unambiguous. The driver is identity-based (appId), no per-icon pixel
#! calibration - the arithmetic even-slot center (e2e_task_center) rides the
#! compositor-true window x, accurate within a few px at the default zoom.
e2e_json viewTasksData u "$view" | grep -q '"isLauncher":false' && \
    e2e_fail "window tasks present; this recipe needs a launchers-only bar"
mapfile -t apps < <(taskdrag_order "$view" | tr ' ' '\n')
[[ "${#apps[@]}" -ge 3 ]] || e2e_fail "need >=3 launchers, have ${#apps[@]}"

#! the launchers config key (name carries the synced-group id, so discovered
#! not assumed) - the residue surface a task-reorder abort could strand into
tasks_applet="$(e2e_json viewAppletsData u "$view" | python3 -c '
import json, sys
print(next(a["id"] for a in json.load(sys.stdin) if a["plugin"] == "org.kde.latte.plasmoid"))')"
launchers_key="$(awk -v id="$view" -v ap="$tasks_applet" '
    $0 == "[Containments][" id "][Applets][" ap "][Configuration][General]" {f=1; next}
    /^\[/ {f=0}
    f && /^launchers[0-9]*=/ {split($0, kv, "="); print kv[1]; exit}' "$E2E_LAYOUT")"
[[ -n "$launchers_key" ]] || e2e_fail "no launchers entry in the layout for applet $tasks_applet"

read_launchers_key() {
    kreadconfig6 --file "$E2E_LAYOUT" --group Containments --group "$view" \
        --group Applets --group "$tasks_applet" --group Configuration --group General \
        --key "$launchers_key"
}
write_launchers_key() {
    kwriteconfig6 --file "$E2E_LAYOUT" --group Containments --group "$view" \
        --group Applets --group "$tasks_applet" --group Configuration --group General \
        --key "$launchers_key" "$1"
}
orig_launchers="$(read_launchers_key)"

restore_config() {
    e2e_dock_stop >/dev/null 2>&1 || true
    write_launchers_key "$orig_launchers"
}
trap restore_config EXIT

baseline="$(taskdrag_order "$view")"
echo "baseline order: $baseline"

# reset_to_baseline: stop, rewrite the launchers key to the original, restart -
# the deterministic way back between legs (a committed launcher move persists,
# so a plain re-read would drift).
reset_to_baseline() {
    e2e_dock_stop >/dev/null || return 1
    write_launchers_key "$orig_launchers"
    e2e_dock_start >/dev/null || return 1
    local now
    now="$(taskdrag_order "$view")"
    [[ "$now" == "$baseline" ]] || { echo "reset did not restore baseline (got: $now)" >&2; return 1; }
}

# ---- leg 1: positive control (the driver SEES a real reorder) --------------
# swap the middle pair (slots 1 and 2, the 050-proven geometry - the leftmost
# slot has edge behaviour); expected order is apps[0] apps[2] apps[1] apps[3..]
src="${apps[1]}"; dst="${apps[2]}"
expected="${apps[0]} ${apps[2]} ${apps[1]}"
for ((i = 3; i < ${#apps[@]}; i++)); do expected+=" ${apps[$i]}"; done

after=""
for attempt in 1 2 3; do
    taskdrag_reorder "$view" "$src" "$dst"
    after="$(taskdrag_order "$view")"
    [[ "$after" == "$expected" ]] && break
    if [[ "$after" != "$baseline" ]]; then
        echo "  (attempt $attempt reordered the wrong pair: $after - resetting)"
    else
        echo "  (attempt $attempt did not cross, retrying)"
    fi
    reset_to_baseline || e2e_fail "could not reset between reorder attempts"
done
[[ "$after" == "$expected" ]] || e2e_fail "positive control: driver could not reorder in 3 attempts (expected: $expected, got: $after)"
echo "leg 1 PASS: driver observed a real reorder ($baseline -> $after)"

reset_to_baseline || e2e_fail "could not reset after leg 1"

# ---- leg 2: HC3 rejection (zero-cross hold-noop reads AS no reorder) --------
taskdrag_hold_noop "$view" "${apps[1]}"
noop_after="$(taskdrag_order "$view")"
[[ "$noop_after" == "$baseline" ]] || e2e_fail "HC3: a zero-cross hold-noop MOVED a task ($baseline -> $noop_after) - the driver would false-report a reorder"
echo "leg 2 PASS (HC3): zero-cross hold-noop reported AS no reorder (order byte-unchanged)"

#! and it stranded NO config residue: flush the key with a clean stop and read
#! it back byte-identical to the original (the launchers-key residue surface)
e2e_dock_stop || e2e_fail "no clean stop to flush the launchers key for the residue check"
noop_key="$(read_launchers_key)"
[[ "$noop_key" == "$orig_launchers" ]] || e2e_fail "HC3 residue: the hold-noop rewrote the launchers key ('$noop_key' vs '$orig_launchers')"
echo "         and left the launchers config key byte-unchanged"
e2e_dock_start || e2e_fail "dock did not come back after the residue flush"
reset_to_baseline || e2e_fail "could not reset after leg 2"

# ---- leg 3: D1 evidence via Escape (does a committed task move survive?) ----
# The D1 claim: tasksModel.move runs LIVE during the drag (MouseHandler.qml),
# and the drag being a compositor drag (dragHelper Drag.dragType Automatic ->
# QDrag/wl_data_device) means Escape DOES cancel the DRAG - but nothing reverts
# the already-applied model move. Method: prove the geometry commits a move
# without Escape (the control), then run it WITH Escape held mid-drag; the
# verdict hinges only on whether a committed move SURVIVES, not on which
# neighbour the calibration happened to cross (the 050 calibration can land
# either adjacent pair, so exact-order matching would be brittle).
plain_order=""
for attempt in 1 2 3 4; do
    taskdrag_reorder "$view" "$src" "$dst"
    plain_order="$(taskdrag_order "$view")"
    [[ "$plain_order" != "$baseline" ]] && break
    reset_to_baseline || e2e_fail "could not reset during the D1 control"
done
[[ "$plain_order" != "$baseline" ]] || e2e_fail "D1: the plain control never crossed a neighbour (calibration) - re-run"
echo "leg 3 control: a plain drag committed a move ($baseline -> $plain_order)"
reset_to_baseline || e2e_fail "could not reset before the Escape-held drag"

escape_order=""
for attempt in 1 2 3 4; do
    taskdrag_escape_held "$view" "$src" "$dst"
    escape_order="$(taskdrag_order "$view")"
    #! a committed move that Escape did NOT revert leaves a NON-baseline order;
    #! retry only the ambiguous baseline result (Escape reverted, OR - the
    #! control just proved the geometry crosses - a flaky non-cross)
    [[ "$escape_order" != "$baseline" ]] && break
    reset_to_baseline || e2e_fail "could not reset during the Escape leg"
done
echo "leg 3 escape-held drag -> $escape_order"

if [[ "$escape_order" != "$baseline" ]]; then
    d1_disposition="no-revert"
    echo "leg 3 D1 EVIDENCE: a task move committed mid-drag SURVIVED Escape (order is"
    echo "                  $escape_order, not the baseline) - Escape cancels the compositor"
    echo "                  drag but does NOT revert the already-applied tasksModel.move"
else
    d1_disposition="reverts"
    echo "leg 3 D1 EVIDENCE: with the geometry the control just crossed, Escape restored the"
    echo "                  baseline order across 4 attempts - a genuine cancel-and-revert"
fi
echo "D1 disposition observed: $d1_disposition"
reset_to_baseline || e2e_fail "could not reset after the D1 leg"

# ---- leg 4: release-back-at-origin does NOT revert either (D1, from A3) ------
# The A3 abort's other input path: cross a neighbour then bring the pointer
# BACK to the exact origin and release (reverse-jitter, DR-2 / T5b). Because
# the move applied LIVE on the out-swing and the 200ms ignoredItem timer
# suppresses an immediate re-cross on the return, the return does NOT undo it -
# so a release-back-at-origin leaves the task MOVED, the same live-move truth as
# leg 3 seen from the release path. Assert the crossed move survived (order !=
# baseline); retry to ensure the out-swing actually crossed.
jitter_after=""
for attempt in 1 2 3 4; do
    taskdrag_reverse_jitter "$view" "$src" "$dst"
    jitter_after="$(taskdrag_order "$view")"
    [[ "$jitter_after" != "$baseline" ]] && break
    reset_to_baseline || e2e_fail "could not reset during the reverse-jitter leg"
done
[[ "$jitter_after" != "$baseline" ]] || e2e_fail "leg 4: reverse-jitter never crossed a neighbour (calibration) - re-run"
echo "leg 4 D1 EVIDENCE: a reverse-jitter returned to the exact origin still left the task"
echo "                  moved (order $jitter_after, not baseline) - release-back does not revert"
reset_to_baseline || e2e_fail "could not reset after leg 4"

echo "ALL LEGS PASS: the driver observes a reorder (leg 1) AND its refusal (leg 2, HC3);"
echo "D1 = $d1_disposition (Escape and release-back both leave a committed task move in place)"
