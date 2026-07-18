# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# The TASK-REORDER driver for the e2e matrix (C-I8 / P7,
# docs/e2e-interaction-test-plan.md section 5, families F4 and A3). Sourced by
# task-reorder recipes AFTER tests/e2e/lib.sh. Reusable across the launcher and
# window-task sub-models: both reorder through the SAME tasks-applet handler
# (plasmoid/.../taskslayout/MouseHandler.qml), keyed here by appId - the stable
# per-task identity viewTasksData reports (G4). Launchers additionally persist
# their order to the tasks-applet `launchers` config key; window tasks do not.
#
# This is a DISTINCT sub-model DND from applet reordering (C-I7): applets move
# through ConfigOverlay's MouseArea in edit mode; tasks move through the tasks
# plasmoid's own DropArea, whose tasksModel.move() runs LIVE during onDragMove
# (MouseHandler.qml:184), not on drop. The abort primitives below exist to
# expose that live-move truth (defect D1, docs/known-defects.md): a crossed
# reorder commits immediately and neither Escape nor a release-back reverts it;
# only a drag that never crossed a neighbour is a true no-op.
#
# All drags approach the bar from OUTSIDE (the parabolic zoom shifts icons once
# the pointer is inside, so a rest-center computed cold must be reached by a
# glide from outside, never a teleport onto a zoomed layout - the 050 lesson).

# taskdrag_order <view>: the model-order appId list, space-separated. The
# window-task ORDER readback (G4): each entry is one task's stable appId in
# viewTasksData index order, so a reorder shows as a permutation of this list.
taskdrag_order() {
    e2e_json viewTasksData u "$1" | python3 -c '
import json, sys
print(" ".join(t["appId"] for t in json.load(sys.stdin)))'
}

# taskdrag_launcher_order <view>: the launcherUrl list in model order - the
# launcher sub-model identity, which also persists to the `launchers` config
# key. Empty entries (window tasks have no launcherUrl) are kept in place so
# the list length still matches the bar.
taskdrag_launcher_order() {
    e2e_json viewTasksData u "$1" | python3 -c '
import json, sys
print(" ".join(t["launcherUrl"] for t in json.load(sys.stdin)))'
}

# _taskdrag_center <view> <appId>: the cold rest center "cx cy" of the task
# with this appId (the arithmetic even-slot model, from the compositor-true
# window x - the same math as lib.sh e2e_task_center).
_taskdrag_center() { e2e_task_center "$1" "$2"; }

# _taskdrag_approach <view> <cx> <cy>: a staging point "ax ay" OUTSIDE the dock
# on the axis the icons split along, so the drag settles onto the task by a
# glide from clear space. Horizontal bars stage at mid-screen height on the
# task's x; vertical bars stage at mid-screen width on the task's y.
_taskdrag_approach() {
    local view="$1" cx="$2" cy="$3" edge
    edge="$(e2e_view_field "$view" 'v["edge"]')"
    case "$edge" in
        bottom|top) echo "$cx $(( E2E_SCREEN_H / 2 ))" ;;
        left|right) echo "$(( E2E_SCREEN_W / 2 )) $cy" ;;
        *) echo "$(( E2E_SCREEN_W / 2 )) $(( E2E_SCREEN_H / 2 ))" ;;
    esac
}

# taskdrag_reorder <view> <src_appId> <dst_appId>: drag the src task onto the
# dst task's rest slot in ONE hold. Releasing at the neighbour's rest center is
# exactly ONE crossing (releasing further rides into the next neighbour and
# swaps twice - the 050 calibration). Commits the reorder LIVE while crossing;
# the release only ends the (internal Qt-Quick) drag, it does not decide the
# move. Returns 0 always; the caller reads taskdrag_order to see the effect.
taskdrag_reorder() {
    local view="$1" src="$2" dst="$3" sc dc sx sy dx dy ax ay
    sc="$(_taskdrag_center "$view" "$src")" || return 1
    dc="$(_taskdrag_center "$view" "$dst")" || return 1
    read -r sx sy <<< "$sc"
    read -r dx dy <<< "$dc"
    read -r ax ay <<< "$(_taskdrag_approach "$view" "$sx" "$sy")"

    #! settle onto the source first (glide, not jump - the vehicle enter race),
    #! then press and glide through the midpoint to the destination rest center
    "$E2E_FAKEPOINTER" move "$ax" "$ay"; sleep 0.3
    "$E2E_FAKEPOINTER" glide "$ax" "$ay" "$sx" "$sy"; sleep 0.4
    "$E2E_FAKEPOINTER" drag "$sx" "$sy" \
        $(( (sx + dx) / 2 )) $(( (sy + dy) / 2 )) \
        "$dx" "$dy"
    sleep 2
}

# taskdrag_hold_noop <view> <appId>: press and release on the task WITHOUT
# moving (T5a zero-delta). The true no-op: insertIndexAt never fires, the model
# never moves. A driver that reported this AS a reorder would be untrustworthy -
# the recipe asserts the order is UNCHANGED, the HC3 rejection observation.
taskdrag_hold_noop() {
    local view="$1" app="$2" c cx cy ax ay
    c="$(_taskdrag_center "$view" "$app")" || return 1
    read -r cx cy <<< "$c"
    read -r ax ay <<< "$(_taskdrag_approach "$view" "$cx" "$cy")"
    "$E2E_FAKEPOINTER" move "$ax" "$ay"; sleep 0.3
    "$E2E_FAKEPOINTER" glide "$ax" "$ay" "$cx" "$cy"; sleep 0.4
    #! drag from the center to the SAME center: press, a zero-length glide,
    #! release - no crossing, so no move
    "$E2E_FAKEPOINTER" drag "$cx" "$cy" "$cx" "$cy"
    sleep 1.5
}

# taskdrag_reverse_jitter <view> <appId> <toward_appId>: press on the task,
# glide toward a neighbour and BACK to origin, release (DR-2 / T5b). Whether
# this nets a move depends on how far the out-swing crossed: an out-swing that
# crosses the neighbour's midpoint commits the move LIVE, and the return
# re-crosses to undo it (net zero); an out-swing that stops short of the
# midpoint never moves at all. Either way the net is meant to be zero - the
# caller asserts the order and the launchers key are byte-unchanged.
taskdrag_reverse_jitter() {
    local view="$1" app="$2" toward="$3" c cx cy tc tx ty ax ay
    c="$(_taskdrag_center "$view" "$app")" || return 1
    tc="$(_taskdrag_center "$view" "$toward")" || return 1
    read -r cx cy <<< "$c"
    read -r tx ty <<< "$tc"
    read -r ax ay <<< "$(_taskdrag_approach "$view" "$cx" "$cy")"
    "$E2E_FAKEPOINTER" move "$ax" "$ay"; sleep 0.3
    "$E2E_FAKEPOINTER" glide "$ax" "$ay" "$cx" "$cy"; sleep 0.4
    #! out toward the neighbour, then back to the exact origin, release there
    "$E2E_FAKEPOINTER" drag "$cx" "$cy" "$tx" "$ty" "$cx" "$cy"
    sleep 2
}

# taskdrag_escape_held <view> <src_appId> <dst_appId>: press on src, glide to
# dst's rest center CROSSING the neighbour, tap Escape WITH THE BUTTON STILL
# HELD, then release (DR-6, via fakepointer dragkey). This asks what Escape
# does to the tasks drag. The drag is a Qt-Quick internal Drag.active, not a
# compositor data-device drag, so Escape is delivered to the dock's focused
# surface, NOT a compositor drag-cancel; and the tasksModel.move already ran
# live while crossing. The caller reads taskdrag_order to record Escape's REAL
# effect (D1) - do not assume it reverts.
taskdrag_escape_held() {
    local view="$1" src="$2" dst="$3" sc dc sx sy dx dy ax ay
    sc="$(_taskdrag_center "$view" "$src")" || return 1
    dc="$(_taskdrag_center "$view" "$dst")" || return 1
    read -r sx sy <<< "$sc"
    read -r dx dy <<< "$dc"
    read -r ax ay <<< "$(_taskdrag_approach "$view" "$sx" "$sy")"
    "$E2E_FAKEPOINTER" move "$ax" "$ay"; sleep 0.3
    "$E2E_FAKEPOINTER" glide "$ax" "$ay" "$sx" "$sy"; sleep 0.4
    "$E2E_FAKEPOINTER" dragkey Escape "$sx" "$sy" \
        $(( (sx + dx) / 2 )) $(( (sy + dy) / 2 )) \
        "$dx" "$dy"
    sleep 2
}

# taskdrag_out_of_applet <view> <appId>: press on the task and glide fully OUT
# of the tasks applet (off the dock band), then release (T1d / task-dragged-
# outside). onDragLeave clears the dropping flags; a task released over the
# containment/desktop adds nothing to the bar. The caller asserts no crash and
# the order reflects only in-applet crossings.
taskdrag_out_of_applet() {
    local view="$1" app="$2" c cx cy ax ay edge ox oy
    c="$(_taskdrag_center "$view" "$app")" || return 1
    read -r cx cy <<< "$c"
    read -r ax ay <<< "$(_taskdrag_approach "$view" "$cx" "$cy")"
    edge="$(e2e_view_field "$view" 'v["edge"]')"
    #! a point well clear of the dock band, off the axis the bar occupies
    case "$edge" in
        bottom) ox="$cx"; oy=$(( E2E_SCREEN_H / 3 )) ;;
        top)    ox="$cx"; oy=$(( E2E_SCREEN_H * 2 / 3 )) ;;
        left)   ox=$(( E2E_SCREEN_W * 2 / 3 )); oy="$cy" ;;
        right)  ox=$(( E2E_SCREEN_W / 3 )); oy="$cy" ;;
        *)      ox=$(( E2E_SCREEN_W / 2 )); oy=$(( E2E_SCREEN_H / 2 )) ;;
    esac
    "$E2E_FAKEPOINTER" move "$ax" "$ay"; sleep 0.3
    "$E2E_FAKEPOINTER" glide "$ax" "$ay" "$cx" "$cy"; sleep 0.4
    "$E2E_FAKEPOINTER" drag "$cx" "$cy" "$ox" "$oy"
    sleep 2
}
