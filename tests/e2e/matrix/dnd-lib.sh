# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Widget-explorer drag-and-drop driver (C-I9 / P8,
# docs/tracking/e2e-interaction-test-plan.md). The reusable API the F2-add and A1-add-abort
# scenario chunks (C-S4/C-S5/C-A1/C-A1b) drive, plus the DR-1 arbitrary-target
# release primitive the adversarial abort matrix (T1/T2) needs.
#
# What it drives is a REAL cross-surface Wayland drag: the widget explorer is a
# separate KWin surface (a SubConfigView) whose AppletDelegate carries an
# org.kde.draganddrop DragArea offering the text/x-plasmoidservicename mime (the
# Phase 7 C++ add path, View::event -> processMimeData). A fakepointer
# press-glide-release on a delegate makes the delegate's QDrag::exec start a real
# wl_data_device drag; gliding onto the dock delivers dnd enter/move to the
# containment DragDropArea, and the release fires its onDrop (commit) or lands
# over nothing (abort). This is fundamentally different from the 050 launcher
# reorder, which is an INTERNAL QtQuick drag inside one surface; explorer ->
# containment crosses surfaces and exercises the whole Wayland DnD path.
#
# PROVEN in the nested vehicle (2026-07-18): a drop over the dock adds EXACTLY
# ONE applet (no latte-dock-ng double-create), the dndSpacer goes live
# (viewDropMarkerIndex >= 0) while the pointer hovers the dock and returns to -1
# after; a release over empty space adds ZERO and the spacer is cleaned back to
# -1. The old Phase 7 "defer to a GUI CI vm" note was stale.
#
# CADENCE TRAP (measured, load-bearing): the compositor's DnD grab is disrupted
# by a tight busctl spin. The FIRST feasibility probe polled the marker in a
# no-sleep loop and the drag never landed (marker never went live, nothing
# added); the SAME drag with a gentle ~40ms poll cadence lands cleanly and the
# marker is caught live. So the watched drag polls with a sleep, never a spin.
#
# Requires tests/e2e/lib.sh (e2e_call/e2e_json/e2e_dumpwins/e2e_wait_settled,
# E2E_FAKEPOINTER, E2E_SCREEN_W/H) already sourced.

# --- readbacks --------------------------------------------------------------

# dnd_applet_count <cid>: how many applets the view carries right now
# (viewAppletsData length). The add/no-add witness.
dnd_applet_count() {
    e2e_json viewAppletsData u "$1" | python3 -c 'import json,sys; print(len(json.load(sys.stdin)))'
}

# dnd_drop_marker <cid>: the live dndSpacer visual index (G3, viewDropMarkerIndex),
# -1 when parked. >=0 means a drag is in flight over this view's drop area.
dnd_drop_marker() {
    e2e_call viewDropMarkerIndex u "$1" | awk '{print $NF}'
}

# --- geometry: the drag source, and commit / abort targets ------------------

# dnd_explorer_rect: the widget-explorer window rect as "x,y wxh", or empty if
# no explorer window is open. Identified structurally: among the dock's own
# (latte-dock) surfaces it is the only one that is TALL AND NARROW - the
# screen-left panel WidgetExplorerView::syncGeometry anchors (height >= half the
# screen, width < half). The edge docks are wide-and-short; the shadow helper
# window is tiny. This needs no fixed caption or per-config geometry.
dnd_explorer_rect() {
    e2e_dumpwins | awk -F'|' -v sw="${E2E_SCREEN_W:?}" -v sh="${E2E_SCREEN_H:?}" '
        $2 ~ /latte-dock/ {
            split($4, g, " "); split(g[2], s, "x");
            if (s[1] < sw/2 && s[2] >= sh/2) { print $4; exit }
        }'
}

# dnd_open_explorer <cid> (sets DND_EXPLORER_RECT): open the view's widget
# explorer via the showWidgetExplorer coarse action and wait for its window to
# map. Refuses loudly if the window never appears (a real driver failure, never a
# silent skip). Also parks the pointer OFF the explorer so the first press is a
# clean enter, not a stale hover.
dnd_open_explorer() {
    local cid="$1" i
    e2e_call showWidgetExplorer u "$cid" >/dev/null || return 1
    for ((i = 0; i < 40; i++)); do
        sleep 0.25
        DND_EXPLORER_RECT="$(dnd_explorer_rect)"
        [[ -n "$DND_EXPLORER_RECT" ]] && break
    done
    if [[ -z "$DND_EXPLORER_RECT" ]]; then
        echo "dnd_open_explorer: no widget-explorer window appeared for containment $cid" >&2
        return 1
    fi
    #! let the WidgetExplorer model populate (setModelTimer + the delegate grid);
    #! a press before the grid is laid out hits empty space. 400ms syncGeometry +
    #! model set is comfortably covered by a second.
    sleep 1
    return 0
}

# dnd_delegate_point (reads DND_EXPLORER_RECT): the screen point to PRESS to grab
# a widget delegate - centre of grid column 0 (cellWidth = width/3), a fixed
# 300px below the explorer's top so it lands in the first card's body, clear of
# the ~110px header (title row + search row). Calibrated live: taps across
# y in [200,550] every hit a delegate and added a widget, and a drag from
# (col0, +300) lands a real drop. Echoes "x y".
dnd_delegate_point() {
    [[ -n "${DND_EXPLORER_RECT:-}" ]] || { echo "dnd_delegate_point: call dnd_open_explorer first" >&2; return 1; }
    local ex ey ew eh
    read -r ex ey ew eh <<<"$(echo "$DND_EXPLORER_RECT" | sed 's/[,x]/ /g')"
    echo "$(( ex + ew / 6 )) $(( ey + 300 ))"
}

# dnd_view_center <cid>: the centre of a view's VISIBLE rect (absoluteGeometry),
# a valid drop target inside the dock's input region. Echoes "x y". A commit
# drops here.
dnd_view_center() {
    e2e_view_field "$1" '"%d %d" % (v["absoluteGeometry"][0] + v["absoluteGeometry"][2] // 2, v["absoluteGeometry"][1] + v["absoluteGeometry"][3] // 2)'
}

# dnd_empty_point <cid>: a screen point covered by NO view - a non-drop target an
# abort releases over. Scans viewsData and returns the first of a few mid-screen
# candidates that sits inside no view rect. Refuses loudly if every candidate is
# covered (a degenerate full-screen config the caller must target explicitly),
# never returns a point silently on top of a view. Echoes "x y".
dnd_empty_point() {
    { echo "${E2E_SCREEN_W:?} ${E2E_SCREEN_H:?}"; e2e_json viewsData; } | python3 -c '
import json, sys
w, h = (int(x) for x in sys.stdin.readline().split())
views = json.load(sys.stdin)
def covered(px, py):
    for v in views:
        x, y, vw, vh = v["absoluteGeometry"]
        if x <= px <= x + vw and y <= py <= y + vh:
            return True
    return False
for px, py in [(w // 2, h * 2 // 5), (w // 2, h * 3 // 5), (w // 4, h // 2), (w * 3 // 4, h // 2)]:
    if not covered(px, py):
        print(px, py); break
else:
    sys.exit("dnd_empty_point: every candidate is inside a view; target an off-dock point explicitly")
'
}

# --- the drag primitives ----------------------------------------------------

# dnd_drag_widget <cid> <x> <y> [x2 y2 ...]: the DR-1 primitive. Press a widget
# delegate in cid's already-open explorer, glide through the given target
# waypoint(s), release at the LAST. Foreground; settles the dock afterward
# (the drag grows/shrinks the view via needLength, so geometry must stop moving
# before the caller snapshots). The caller chooses the outcome by where the last
# waypoint lands: dnd_view_center = commit, dnd_empty_point / a foreign window /
# off-screen = abort.
dnd_drag_widget() {
    local cid="$1"; shift
    local press; press="$(dnd_delegate_point)" || return 1
    # shellcheck disable=SC2086
    "${E2E_FAKEPOINTER:?}" drag $press "$@" || return 1
    e2e_wait_settled 15 || true
    return 0
}

# dnd_drag_widget_watched <cid> <x> <y> [x2 y2 ...]: same drag, run in the
# BACKGROUND while polling viewDropMarkerIndex on a gentle cadence (see the
# CADENCE TRAP header). Echoes the PEAK marker index observed during the drag:
# >=0 proves the dndSpacer went live, i.e. the drag really reached the
# containment drop area - the tripwire that makes a subsequent "added" or
# "added nothing" outcome meaningful rather than a drag that silently never
# started.
dnd_drag_widget_watched() {
    local cid="$1"; shift
    local press; press="$(dnd_delegate_point)" || return 1
    # shellcheck disable=SC2086
    "${E2E_FAKEPOINTER:?}" drag $press "$@" &
    local fpid=$! peak=-1 m
    while kill -0 "$fpid" 2>/dev/null; do
        m="$(dnd_drop_marker "$cid" 2>/dev/null)"
        [[ "$m" =~ ^-?[0-9]+$ ]] && (( m > peak )) && peak=$m
        sleep 0.04
    done
    wait "$fpid" 2>/dev/null || true
    e2e_wait_settled 15 || true
    echo "$peak"
}

# --- matrix verb wrapper (for matrix_scenario_commit / matrix_scenario_abort) --
# The F2/A1 "addwidget" verb the scenario chunks drive through the harness.
# commit = drop a delegate onto the view (adds one at the Qt5-faithful end);
# abort = hover the view so the spacer opens, then release over an empty point
# (adds zero, spacer cleaned). Probe = the applet count (F2 asserts +1; A1 rides
# the matrix_baseline_capture/restore backbone, so its assertion is the whole
# residue surface, not this probe alone).

matrix_verb_addwidget_drive() {
    local view="$1" outcome="$2"
    dnd_open_explorer "$view" || return 1
    local center empty
    center="$(dnd_view_center "$view")" || return 1
    if [[ "$outcome" == abort ]]; then
        empty="$(dnd_empty_point "$view")" || return 1
        # hover the view (spacer opens) THEN release over nothing: the
        # canonical orphan-spacer / T2c cleanup stress
        # shellcheck disable=SC2086
        dnd_drag_widget "$view" $center $center $empty $empty || return 1
    else
        # shellcheck disable=SC2086
        dnd_drag_widget "$view" $center $center || return 1
    fi
    return 0
}

matrix_verb_addwidget_probe() {
    dnd_applet_count "$1"
}
