# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
"""The widget-explorer drag-and-drop driver: the port of
tests/e2e/matrix/dnd-lib.sh (BP-3c).

What it drives is a REAL cross-surface Wayland drag: the widget explorer is a
separate KWin surface whose AppletDelegate carries an org.kde.draganddrop
DragArea offering the text/x-plasmoidservicename mime (the Phase 7 C++ add path,
View::event -> processMimeData). A fakepointer press-glide-release on a delegate
makes the delegate's QDrag::exec start a real wl_data_device drag; gliding onto
the dock delivers dnd enter/move to the containment DragDropArea, and the release
fires its onDrop (commit) or lands over nothing (abort). This is fundamentally
different from the 050 launcher reorder, an INTERNAL QtQuick drag inside one
surface; explorer -> containment crosses surfaces and exercises the whole Wayland
DnD path.

CADENCE TRAP (measured, load-bearing, carried verbatim from the bash): the
compositor's DnD grab is disrupted by a tight busctl spin. The first feasibility
probe polled the marker in a no-sleep loop and the drag never landed; the SAME
drag with a gentle ~40ms poll cadence lands cleanly and the marker is caught
live. So the watched drag polls with a sleep, never a spin.

Migration shape (the BP-2c/BP-3a fresh-module precedent): this is a fresh module,
not a bridge. The bash lib it ports (tests/e2e/matrix/dnd-lib.sh) was deleted with
its last consumer, the 093 recipe port, in the BP-3 driver batch.
Every readback is validated at the boundary through recipe.py's typed helpers
(recipe.view_applets, recipe.view, recipe.views, recipe.windows), busctl stays
the transport for the two non-JSON surfaces recipe does not expose
(viewDropMarkerIndex, showWidgetExplorer), and the addwidget verb registers into
the same matrix registry the bash matrix_verb_addwidget_* hooked, with
byte-identical refusal wording.
"""

from __future__ import annotations

import subprocess
import sys
import time
from collections.abc import Sequence

from latte_harness import matrix, recipe
from latte_harness.matrix import MatrixDriveError
from latte_harness.recipe import Rect

# The gentle poll cadence the watched drag uses (see the CADENCE TRAP header): a
# tight spin disrupts the compositor's DnD grab, a ~40ms sleep does not.
_WATCH_POLL_SECONDS = 0.04


class DndError(Exception):
    """A DnD driver step could not proceed (no explorer window, an unusable drag
    source, every abort candidate covered). The diagnostic is printed at the raise
    site (matching the bash ``echo ... >&2; return 1``); the addwidget verb driver
    translates it to a MatrixDriveError so a failed interaction is never silently a
    clean pass (the never-swallow rule).
    """


# ---- environment and low-level transport -----------------------------------


def _require_env(name: str) -> str:
    """This module's env accessor: recipe.require_env with the dnd prefix/error."""
    return recipe.require_env(name, prefix="dnd", error=DndError)


def _fakepointer_run(*args: str) -> subprocess.CompletedProcess[bytes]:
    """Fire one foreground fakepointer invocation (the drag primitives)."""
    return subprocess.run([_require_env("E2E_FAKEPOINTER"), *args], check=False)


def _marker_reply_quiet(cid: int) -> str:
    """The raw viewDropMarkerIndex reply with busctl stderr suppressed.

    The bash ``dnd_drop_marker "$cid" 2>/dev/null`` in the watched poll loop quiets
    busctl's not-up-yet chatter; the standalone drop_marker keeps stderr (it goes
    through recipe.call), so only this poll-side transport suppresses it - the
    shared recipe.call_status with quiet=True, dropping the status the poll does
    not read.
    """
    _code, stdout = recipe.call_status("viewDropMarkerIndex", "u", str(cid), quiet=True)
    return stdout


# ---- readbacks -------------------------------------------------------------


def applet_count(cid: int) -> int:
    """dnd_applet_count: how many applets the view carries right now
    (viewAppletsData length). The add/no-add witness."""
    return len(recipe.view_applets(cid))


def _last_field(reply: str) -> str:
    """The busctl ``awk '{print $NF}'``: the last whitespace field, '' when empty.

    Pure so the int-reply parse is unit-testable. viewDropMarkerIndex returns an
    int, which busctl prints as ``<sig> <n>``; the last field is the value.
    """
    fields = reply.split()
    return fields[-1] if fields else ""


def _is_int(text: str) -> bool:
    """The bash ``[[ "$m" =~ ^-?[0-9]+$ ]]`` guard: a signed integer literal.

    A leading ``+`` is rejected exactly as the bash regex rejects it; busctl
    never emits one, and the guard stays byte-faithful rather than tolerant.
    """
    return bool(text) and (text[1:] if text[0] == "-" else text).isdigit()


def drop_marker(cid: int) -> str:
    """dnd_drop_marker: the live dndSpacer visual index (G3, viewDropMarkerIndex),
    -1 when parked. >=0 means a drag is in flight over this view's drop area."""
    return _last_field(recipe.call("viewDropMarkerIndex", "u", str(cid)))


# ---- geometry: the drag source, and commit / abort targets ------------------


def _select_explorer_rect(windows: Sequence[recipe.Window], sw: int, sh: int) -> str | None:
    """The widget-explorer window rect as "x,y WxH", or None if none is open.

    Identified structurally (the bash awk predicate, exactly): among the dock's own
    latte-dock surfaces it is the only one TALL AND NARROW - width < half the
    screen, height >= half. The edge docks are wide-and-short; the shadow helper is
    tiny. Pure so the selection is unit-testable without a compositor.
    """
    for window in windows:
        if "latte-dock" not in window.resource_class:
            continue
        if window.width < sw / 2 and window.height >= sh / 2:
            return window.geometry_field
    return None


def explorer_rect() -> str | None:
    """dnd_explorer_rect: the widget-explorer window rect, or None if none is open."""
    sw, sh = recipe.screen_dims()
    return _select_explorer_rect(recipe.windows(), sw, sh)


def open_explorer(cid: int) -> str:
    """dnd_open_explorer: open the view's widget explorer via the showWidgetExplorer
    coarse action and wait for its window to map, returning its rect.

    Refuses loudly if the window never appears (a real driver failure, never a
    silent skip). Then lets the WidgetExplorer model populate (setModelTimer + the
    delegate grid; a press before the grid is laid out hits empty space) before
    returning, so a following press lands on a real card.
    """
    # showWidgetExplorer is a void action; a transport failure is the bash
    # ``|| return 1``. The dock qWarns and maps no window for a bad id, which the
    # poll-then-refuse below turns into the same loud failure the bash produced.
    # DndError (not recipe.call_or_fail's recipe.fail) is deliberate: the addwidget
    # verb driver translates a DndError into a MatrixDriveError, so this uses the
    # status-carrying call and keeps its own raise.
    code, _ = recipe.call_status("showWidgetExplorer", "u", str(cid))
    if code != 0:
        raise DndError(f"dnd_open_explorer: showWidgetExplorer call failed for containment {cid}")

    rect: str | None = None
    for _ in range(40):
        time.sleep(0.25)
        rect = explorer_rect()
        if rect:
            break
    if not rect:
        print(
            f"dnd_open_explorer: no widget-explorer window appeared for containment {cid}",
            file=sys.stderr,
            flush=True,
        )
        raise DndError("dnd_open_explorer: no widget-explorer window appeared")
    # 400ms syncGeometry + model set is comfortably covered by a second.
    time.sleep(1)
    return rect


def _delegate_point(rect: str) -> tuple[int, int]:
    """The screen point to PRESS to grab a widget delegate (the bash dnd_delegate_
    point arithmetic): centre of grid column 0 (cellWidth = width/3, so +width/6), a
    fixed 300px below the explorer's top so it lands in the first card's body, clear
    of the ~110px header. Pure so the math is unit-testable.

    ``rect`` is the "x,y WxH" the explorer readback returns; the bash unpacked it
    with ``sed 's/[,x]/ /g'`` into four fields.
    """
    position, _, size = rect.partition(" ")
    x_text, _, y_text = position.partition(",")
    w_text, _, _h_text = size.partition("x")
    ex, ey, ew = int(x_text), int(y_text), int(w_text)
    return ex + ew // 6, ey + 300


def delegate_point(rect: str) -> tuple[int, int]:
    """dnd_delegate_point: the press point in the explorer rect ``rect``."""
    if not rect:
        print("dnd_delegate_point: call dnd_open_explorer first", file=sys.stderr, flush=True)
        raise DndError("dnd_delegate_point: call dnd_open_explorer first")
    return _delegate_point(rect)


def _center_of(rect: Rect) -> tuple[int, int]:
    """The centre of an [x,y,w,h] rect (integer //, matching the bash)."""
    x, y, w, h = rect
    return x + w // 2, y + h // 2


def view_center(cid: int) -> tuple[int, int]:
    """dnd_view_center: the centre of a view's VISIBLE rect (absoluteGeometry), a
    valid drop target inside the dock's input region. A commit drops here."""
    return _center_of(recipe.view(cid).absolute_geometry)


def _empty_point_from(view_rects: Sequence[Rect], w: int, h: int) -> tuple[int, int] | None:
    """The first mid-screen candidate covered by NO view, or None if all covered.

    Pure so the coverage scan is unit-testable. The candidate order and the
    inclusive point-in-rect test are the bash formula, byte for byte.
    """

    def covered(px: int, py: int) -> bool:
        return any(x <= px <= x + vw and y <= py <= y + vh for x, y, vw, vh in view_rects)

    for px, py in [
        (w // 2, h * 2 // 5),
        (w // 2, h * 3 // 5),
        (w // 4, h // 2),
        (w * 3 // 4, h // 2),
    ]:
        if not covered(px, py):
            return px, py
    return None


def empty_point() -> tuple[int, int]:
    """dnd_empty_point: a screen point covered by NO view - a non-drop target an
    abort releases over. Refuses loudly if every candidate is covered (a degenerate
    full-screen config the caller must target explicitly), never returns a point
    silently on top of a view."""
    w, h = recipe.screen_dims()
    point = _empty_point_from([v.absolute_geometry for v in recipe.views()], w, h)
    if point is None:
        raise DndError(
            "dnd_empty_point: every candidate is inside a view; target an off-dock point explicitly"
        )
    return point


# ---- the drag primitives ---------------------------------------------------


def drag_widget(rect: str, *coords: int) -> None:
    """dnd_drag_widget: the DR-1 primitive. Press a widget delegate in the explorer
    at ``rect``, glide through the given target waypoint coords, release at the LAST.
    Settles the dock afterward (the drag grows/shrinks the view via needLength, so
    geometry must stop moving before the caller snapshots). The caller chooses the
    outcome by where the last waypoint lands: view_center = commit,
    empty_point / off-screen = abort.
    """
    px, py = delegate_point(rect)
    if _fakepointer_run("drag", str(px), str(py), *[str(c) for c in coords]).returncode != 0:
        raise DndError("dnd_drag_widget: fakepointer drag failed")
    _ = recipe.wait_settled(15)


def drag_widget_watched(rect: str, cid: int, *coords: int) -> int:
    """dnd_drag_widget_watched: same drag, run in the BACKGROUND while polling
    viewDropMarkerIndex on a gentle cadence (see the CADENCE TRAP header). Returns
    the PEAK marker index observed during the drag: >=0 proves the dndSpacer went
    live, i.e. the drag really reached the containment drop area - the tripwire that
    makes a subsequent "added" or "added nothing" outcome meaningful rather than a
    drag that silently never started.
    """
    px, py = delegate_point(rect)
    proc = subprocess.Popen(
        [_require_env("E2E_FAKEPOINTER"), "drag", str(px), str(py), *[str(c) for c in coords]]
    )
    peak = -1
    while proc.poll() is None:
        field = _last_field(_marker_reply_quiet(cid))
        if _is_int(field):
            peak = max(peak, int(field))
        time.sleep(_WATCH_POLL_SECONDS)
    _ = proc.wait()
    _ = recipe.wait_settled(15)
    return peak


# ---- matrix verb wrapper (for scenario_commit / scenario_abort) ------------
# The F2/A1 "addwidget" verb the scenario chunks drive through the matrix harness.
# commit = drop a delegate onto the view (adds one at the Qt5-faithful end);
# abort = hover the view so the spacer opens, then release over an empty point
# (adds zero, spacer cleaned). Probe = the applet count.


def verb_addwidget_drive(view: int, outcome: str) -> None:
    """The addwidget verb driver. A step failure is surfaced (DndError printed at
    its raise site) and re-raised as MatrixDriveError, which the matrix backbone
    translates to a scenario refusal - never a silently swallowed drag."""
    try:
        rect = open_explorer(view)
        cx, cy = view_center(view)
        if outcome == "abort":
            ex, ey = empty_point()
            # hover the view (spacer opens) THEN release over nothing: the canonical
            # orphan-spacer / T2c cleanup stress.
            drag_widget(rect, cx, cy, cx, cy, ex, ey, ex, ey)
        else:
            drag_widget(rect, cx, cy, cx, cy)
    except DndError as err:
        raise MatrixDriveError(str(err)) from err


def verb_addwidget_probe(view: int) -> str:
    """The addwidget verb probe: the applet count (F2 asserts +1; A1 rides the
    baseline capture/restore backbone)."""
    return str(applet_count(view))


matrix.register_verb("addwidget", verb_addwidget_drive, verb_addwidget_probe)
