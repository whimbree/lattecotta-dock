#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# C-I9 / P8 acceptance (docs/tracking/e2e-interaction-test-plan.md, HC3): the
# widget-explorer -> containment drag-and-drop driver (tests/e2e/matrix/dnd-lib.sh).
#
# HC3 says this driver's acceptance IS A REJECTION BY CONSTRUCTION: it must prove
# a release over NOTHING yields ZERO applets and report that AS a rejected drop,
# and that the mid-drag spacer is cleaned up (viewDropMarkerIndex back to -1)
# after the abort. A driver that cannot SEE the rejected drop cannot be trusted
# for the A1 abort scenario.
#
# The rejection is only trustworthy PAIRED with a positive: a driver that never
# adds anything would "pass" a zero-add test vacuously. So the recipe first
# proves the SAME driver drives a REAL cross-surface Wayland DnD that reaches the
# drop path - the dndSpacer goes live (marker >= 0) and a completed drop adds
# EXACTLY ONE applet (no latte-dock-ng double-create) - and only then proves the
# rejections. This is the sceneprobe good/bad/blank and run-e2e XPASS discipline
# applied to the DND driver itself.
#
# Legs:
#   0. a bad containment id to showWidgetExplorer is REFUSED (qWarning, no window)
#   1. COMMIT: drop a delegate onto the view -> spacer went live, +1 applet, marker back to -1
#   2. ABORT hover-then-off (T2c): hover the dock (spacer live) then release over
#      empty space -> ZERO added, spacer cleaned to -1  [THE HC3 REJECTION]
#   3. ABORT straight-off (T2a): release over empty space without ever entering the
#      dock -> ZERO added, no phantom insert (marker never left -1)
set -uo pipefail
repo="${E2E_REPO:?run through scripts/run-e2e.sh}"
source "$repo/tests/e2e/lib.sh"
source "$repo/tests/e2e/matrix/dnd-lib.sh"

view="$(e2e_tasks_view)" || e2e_fail "no tasks view to drive explorer DnD onto"
echo "target view: $view"

# ---- leg 0: showWidgetExplorer refuses a bad containment id -----------------

bad_cid=987654
e2e_json viewsData | grep -q "\"containmentId\":$bad_cid" && e2e_fail "test bug: $bad_cid is a real view id, pick another"
logmark="$(wc -l < "$E2E_DOCK_LOG")"
winmark="$(e2e_dumpwins | grep -c DUMPWIN || true)"
e2e_call showWidgetExplorer u "$bad_cid" >/dev/null 2>&1 || true
sleep 1
[[ "$(e2e_dumpwins | grep -c DUMPWIN || true)" -eq "$winmark" ]] \
    || e2e_fail "REJECTION LEAK: a window appeared for showWidgetExplorer on bad containment id $bad_cid"
if ! tail -n +$((logmark + 1)) "$E2E_DOCK_LOG" \
        | grep -q "showWidgetExplorer requested for containment $bad_cid which has no view"; then
    tail -n +$((logmark + 1)) "$E2E_DOCK_LOG" >&2
    e2e_fail "no showWidgetExplorer refusal qWarning for bad containment id $bad_cid in the dock log"
fi
echo "rejection observed: bad containment id $bad_cid refused (qWarning + no window)"

# ---- targets ----------------------------------------------------------------
# The explorer window auto-closes after each drop (a completed QDrag drops
# draggingWidget to false, re-enabling hideOnWindowDeactivate, and the
# now-unfocused window deleteLater()s - the #332733 workaround only holds it open
# WHILE dragging). So every leg re-opens its own explorer; dnd_open_explorer is
# the fresh drag source for that leg.

dnd_open_explorer "$view" || e2e_fail "widget explorer never opened for view $view"
echo "widget explorer open: window $DND_EXPLORER_RECT"
center="$(dnd_view_center "$view")" || e2e_fail "could not compute view centre"
empty="$(dnd_empty_point "$view")" || e2e_fail "could not compute an off-dock point"
echo "drop target (commit): $center ; off-dock target (abort): $empty"

# ---- leg 1: COMMIT proves a real drop adds EXACTLY ONE (positive tripwire) ---

before="$(dnd_applet_count "$view")"
# shellcheck disable=SC2086
peak="$(dnd_drag_widget_watched "$view" $center $center)"
after="$(dnd_applet_count "$view")"
marker="$(dnd_drop_marker "$view")"
echo "COMMIT: count $before -> $after (delta $((after - before)))  peak_marker=$peak  marker_now=$marker"
[[ "$peak" -ge 0 ]] \
    || e2e_fail "COMMIT drag never made the dndSpacer live (peak marker $peak): the DnD did not reach the drop area, so no outcome here is trustworthy"
[[ "$after" -eq $((before + 1)) ]] \
    || e2e_fail "COMMIT drop did not add exactly one applet (before $before, after $after) - a 0 is a broken drag, a 2 is the ng double-create"
[[ "$marker" == "-1" ]] \
    || e2e_fail "COMMIT: dndSpacer not parked after the drop (marker $marker, expected -1)"
echo "COMMIT ok: the driver drives a real explorer->containment DnD that adds exactly one; spacer cleaned"

# ---- leg 2: ABORT hover-then-off -> ZERO added, spacer cleaned (THE HC3 REJECTION) ---

dnd_open_explorer "$view" || e2e_fail "widget explorer never re-opened for the hover-then-off abort"
before="$(dnd_applet_count "$view")"
# hover the dock (spacer opens) THEN release over empty space
# shellcheck disable=SC2086
peak="$(dnd_drag_widget_watched "$view" $center $center $empty $empty)"
after="$(dnd_applet_count "$view")"
marker="$(dnd_drop_marker "$view")"
echo "ABORT(hover-then-off): count $before -> $after (delta $((after - before)))  peak_marker=$peak  marker_now=$marker"
[[ "$peak" -ge 0 ]] \
    || e2e_fail "ABORT: the spacer never went live (peak $peak) - the drag never hovered the dock, so 'no residue' here proves nothing"
[[ "$after" -eq "$before" ]] \
    || e2e_fail "REJECTION LEAK: a release over empty space added $((after - before)) applet(s) (count $before -> $after) - the drop was NOT rejected"
[[ "$marker" == "-1" ]] \
    || e2e_fail "ABORT residue: mid-drag spacer NOT cleaned up (marker $marker, expected -1) - an orphan dndSpacerIndex"
echo "rejection observed: a real DnD hovered the dock (spacer live at $peak) and was released over nothing -> ZERO applets added, spacer cleaned to -1"

# ---- leg 3: ABORT straight-off -> ZERO added, no phantom insert (T2a) --------

dnd_open_explorer "$view" || e2e_fail "widget explorer never re-opened for the straight-off abort"
before="$(dnd_applet_count "$view")"
# shellcheck disable=SC2086
peak="$(dnd_drag_widget_watched "$view" $empty $empty)"
after="$(dnd_applet_count "$view")"
marker="$(dnd_drop_marker "$view")"
echo "ABORT(straight-off): count $before -> $after (delta $((after - before)))  peak_marker=$peak  marker_now=$marker"
[[ "$after" -eq "$before" ]] \
    || e2e_fail "REJECTION LEAK: an off-dock release that never entered the dock added $((after - before)) applet(s)"
[[ "$marker" == "-1" ]] \
    || e2e_fail "ABORT straight-off left a live marker ($marker) - a phantom insert from the distance fallback"
echo "rejection observed: an off-dock release that never entered the dock added ZERO (no phantom insert)"

echo "PASS: explorer DnD adds exactly one on a drop, ZERO on a release over nothing, and the spacer is always cleaned"
