#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""The settings chrome is shared across docks. Retargeting it while hidden runs
the generic config-window setup, which deliberately clears the old layer
anchors before the concrete window reapplies its placement. Two left docks on
the same output have the same canvas rectangle, so CanvasConfigView's old
geometry-only early return mistook the second dock for "already placed" and
skipped that reapply. KWin then centred the unanchored vertical canvas across
the output even though Latte still reported the correct left-edge
canvasGeometry. Exercise that exact same-edge retarget boundary and compare
compositor truth with the reported rect.

Ported from tests/e2e/033-canvas-remap-placement.sh to latte_harness.recipe
(BP-3, the bash-to-python migration's R10 dock-lifecycle recipe batch).
dockSystemData carries fields the typed DockView model does not (screenId,
edge, alignment, editMode, geometrySettled, type, relationship), so it is read
as raw JSON - the same boundary the bash python one-liners used. A dbusreports
refusal (a view without an accepted placement, transient during an edit-mode
enter) maps to the pollable RecipeError, exactly as the retarget-cancel port
did; the polling callers read it as a non-match, the settled-point callers let
it surface. The coarse duplicateView / setViewEditMode / setViewPlacement
actions stay busctl calls; the compositor canvas window is read through
recipe.kwin_js, the same transient KWin script the bash e2e_kwin_js ran.
"""

import contextlib
import io
import json
import time
from collections.abc import Iterator
from typing import Any

from latte_harness import recipe


@contextlib.contextmanager
def _muted_stderr() -> Iterator[None]:
    """The restore dock stop's `>/dev/null 2>&1`: keep its diagnostics off output."""
    with contextlib.redirect_stderr(io.StringIO()):
        yield


_EDGE_VALUES = {"top": 3, "bottom": 4, "left": 5, "right": 6}
_ALIGNMENT_VALUES = {
    "center": 0,
    "left": 1,
    "right": 2,
    "top": 3,
    "bottom": 4,
    "justify": 10,
}


def _snapshot_views() -> list[dict[str, Any]]:
    """dockSystemData's view list as raw JSON; a refused reply raises the pollable
    RecipeError (the retarget-cancel refusal channel)."""
    payload = recipe.json_payload("dockSystemData")
    try:
        return json.loads(payload)["views"]
    except (json.JSONDecodeError, KeyError, TypeError):
        raise recipe.RecipeError(
            "dockSystemData refused or returned no JSON (view placement not accepted)"
        ) from None


def _edge_value(edge: str) -> int:
    if edge not in _EDGE_VALUES:
        raise recipe.RecipeError(f"cannot restore unknown edge '{edge}'")
    return _EDGE_VALUES[edge]


def _alignment_value(alignment: str) -> int:
    if alignment not in _ALIGNMENT_VALUES:
        raise recipe.RecipeError(f"cannot restore unknown alignment '{alignment}'")
    return _ALIGNMENT_VALUES[alignment]


def _placement_record(dock_id: int) -> tuple[str, str, str]:
    """screenId, edge, alignment of one dock (the bash placement_record)."""
    view = next(v for v in _snapshot_views() if v["persistentDockId"] == dock_id)
    return str(view["screenId"]), view["edge"], view["alignment"]


def _canvas_geometry(dock_id: int) -> str:
    """The dock's canvasGeometry as "x y w h" (the bash print(*canvasGeometry))."""
    view = next(v for v in _snapshot_views() if v["persistentDockId"] == dock_id)
    return " ".join(str(component) for component in view["canvasGeometry"])


def _created_view_ids(before_ids: set[int]) -> list[int]:
    """persistentDockIds not present at start (the bash created_view_ids); a
    refused snapshot yields nothing, matching the bash `snapshot 2>/dev/null`."""
    try:
        views = _snapshot_views()
    except recipe.RecipeError:
        return []
    return [v["persistentDockId"] for v in views if v["persistentDockId"] not in before_ids]


def _wait_for_view_state(dock_id: int, edge: str, editing: bool) -> bool:
    """Poll 60x0.5s: the dock is on ``edge``, editMode==``editing``, geometry
    settled. A transient refused snapshot counts as a non-match (poll through)."""
    for _ in range(60):
        try:
            view = next((v for v in _snapshot_views() if v["persistentDockId"] == dock_id), None)
        except recipe.RecipeError:
            view = None
        if (
            view is not None
            and view["edge"] == edge
            and view["editMode"] is editing
            and view["geometrySettled"]
        ):
            return True
        time.sleep(0.5)
    return False


def _canvas_window(expected: str) -> str | None:
    """The single layer-3 latte-dock window whose size matches ``expected``.

    Runs the same transient KWin script the bash canvas_window ran, filtering
    latte-dock layer-3 windows to the expected width/height and printing
    "internalId x y w h"; polls 20x0.25s until exactly one line, returns it (or
    None on timeout)."""
    _ex, _ey, ew, eh = expected.split()
    body = f"""const matches = workspace.windowList().filter(w =>
            String(w.resourceClass) === 'latte-dock'
            && w.layer === 3
            && Math.round(w.frameGeometry.width) === {ew}
            && Math.round(w.frameGeometry.height) === {eh});
        for (const w of matches) {{
            print('@TAG@|' + w.internalId
                + ' ' + Math.round(w.frameGeometry.x)
                + ' ' + Math.round(w.frameGeometry.y)
                + ' ' + Math.round(w.frameGeometry.width)
                + ' ' + Math.round(w.frameGeometry.height));
        }}"""
    for _ in range(20):
        rows = [line for line in recipe.kwin_js(body).splitlines() if line]
        if len(rows) == 1:
            return rows[0]
        time.sleep(0.25)
    return None


def _assert_canvas_agrees(dock_id: int, label: str) -> str:
    """The compositor canvas must be exactly one window sitting at the reported
    canvasGeometry; returns its window id (the bash last_canvas_window_id)."""
    expected = _canvas_geometry(dock_id)
    mapped = _canvas_window(expected)
    if mapped is None:
        recipe.fail(
            f"{label}: expected exactly one compositor canvas with reported size {expected}"
        )
    window_id, x, y, width, height = mapped.split()
    if f"{x} {y} {width} {height}" != expected:
        recipe.fail(
            f"{label}: canvas {window_id} rendered at {x} {y} {width} {height} "
            f"but Latte reported {expected}"
        )
    print(f"{label}: canvas {window_id} and reported geometry agree at {x} {y} {width} {height}")
    return window_id


def _restore(
    view_a: int, screen_a: str, edge_a: str, alignment_a: str, before_ids: set[int]
) -> None:
    """The trap restore: put view_a back on its original placement, remove the
    duplicated peer, and stop the throwaway dock. Every step is best-effort."""
    recipe.call("setViewEditMode", "ub", str(view_a), "false")
    with contextlib.suppress(recipe.RecipeError):
        recipe.call(
            "setViewPlacement",
            "uiii",
            str(view_a),
            screen_a,
            str(_edge_value(edge_a)),
            str(_alignment_value(alignment_a)),
        )
    _wait_for_view_state(view_a, edge_a, False)
    removed = False
    for created_id in _created_view_ids(before_ids):
        recipe.call("setViewEditMode", "ub", str(created_id), "false")
        #! Removal immediately tombstones persistent state; stopping the
        #! throwaway dock commits it without waiting through Plasma's Undo
        #! notification window. The next recipe therefore restarts from the
        #! original topology.
        recipe.call("removeView", "u", str(created_id))
        removed = True
    if removed:
        time.sleep(1)
    with _muted_stderr():
        recipe.dock_stop()


def main() -> None:
    independent = sorted(
        v["persistentDockId"]
        for v in _snapshot_views()
        if v["type"] == "dock" and v["relationship"] == "independent"
    )
    if not independent:
        recipe.fail("no independent dock is available for the shared-canvas retarget test")
    view_a = independent[0]

    screen_a, edge_a, alignment_a = _placement_record(view_a)

    before_ids = {v["persistentDockId"] for v in _snapshot_views()}

    try:
        recipe.call("duplicateView", "u", str(view_a))
        view_b: int | None = None
        for _ in range(60):
            try:
                created = [
                    v
                    for v in _snapshot_views()
                    if v["persistentDockId"] not in before_ids
                    and v["relationship"] == "independent"
                ]
            except recipe.RecipeError:
                created = []
            if len(created) == 1:
                view_b = created[0]["persistentDockId"]
                break
            time.sleep(0.5)
        if view_b is None:
            recipe.fail("Duplicate Dock did not create one independent retarget peer")

        recipe.call("setViewEditMode", "ub", str(view_a), "false")
        recipe.call("setViewEditMode", "ub", str(view_b), "false")
        recipe.call("setViewPlacement", "uiii", str(view_a), screen_a, "5", "0")
        recipe.call("setViewPlacement", "uiii", str(view_b), screen_a, "5", "0")
        if not _wait_for_view_state(view_a, "left", False):
            recipe.fail(f"dock {view_a} did not settle on the left edge")
        if not _wait_for_view_state(view_b, "left", False):
            recipe.fail(f"dock {view_b} did not settle beside dock {view_a}")

        canvas_a = _canvas_geometry(view_a)
        canvas_b = _canvas_geometry(view_b)
        if canvas_a != canvas_b:
            recipe.fail(
                f"same-edge peers do not share the cache-key geometry: {canvas_a} versus {canvas_b}"
            )
        print(f"same-edge peers share canvas geometry {canvas_a}")

        recipe.call("setViewEditMode", "ub", str(view_a), "true")
        if not _wait_for_view_state(view_a, "left", True):
            recipe.fail("first dock's edit session did not open")
        first_canvas_window_id = _assert_canvas_agrees(view_a, "first mapping")

        #! Retarget the still-shared chrome directly to a second dock with the same
        #! canvas rectangle. The handoff closes the old presentation, clears generic
        #! layer placement, then maps the same CanvasConfigView for view_b.
        recipe.call("setViewEditMode", "ub", str(view_b), "true")
        if not _wait_for_view_state(view_b, "left", True):
            recipe.fail("second dock's edit session did not open")
        if not _wait_for_view_state(view_a, "left", False):
            recipe.fail("first dock stayed in edit mode after retarget")
        last_canvas_window_id = _assert_canvas_agrees(view_b, "same-edge retarget")
        if last_canvas_window_id == first_canvas_window_id:
            recipe.fail(
                f"same canvas surface {last_canvas_window_id} survived a retarget "
                "that must remap it"
            )
        print(f"canvas generation changed from {first_canvas_window_id} to {last_canvas_window_id}")

        print("vertical edit canvas preserves its edge placement across same-edge chrome retarget")
    finally:
        _restore(view_a, screen_a, edge_a, alignment_a, before_ids)


if __name__ == "__main__":
    recipe.run(main)
