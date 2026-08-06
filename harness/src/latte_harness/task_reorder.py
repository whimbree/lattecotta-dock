# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
"""The task-reorder driver: the port of tests/e2e/matrix/task-reorder-lib.sh
(BP-3c).

Reusable across the launcher and window-task sub-models: both reorder through the
SAME tasks-applet handler (plasmoid/.../taskslayout/MouseHandler.qml), keyed here
by appId - the stable per-task identity viewTasksData reports (G4). Launchers
additionally persist their order to the tasks-applet ``launchers`` config key;
window tasks do not.

This is a DISTINCT sub-model DnD from applet reordering (C-I7): applets move
through ConfigOverlay's MouseArea in edit mode; tasks move through the tasks
plasmoid's own DropArea, whose tasksModel.move() runs LIVE during onDragMove
(MouseHandler.qml), not on drop. The abort primitives here exist to expose that
live-move truth (defect D1, docs/tracking/known-defects.md): a crossed reorder
commits immediately and neither Escape nor a release-back reverts it; only a drag
that never crossed a neighbour is a true no-op.

All drags approach the bar from OUTSIDE (the parabolic zoom shifts icons once the
pointer is inside, so a rest-center computed cold must be reached by a glide from
outside, never a teleport onto a zoomed layout - the 050 lesson).

Migration shape (the BP-2c/BP-3a fresh-module precedent): a fresh module, not a
bridge. task-reorder-lib.sh was retired in the R12 batch when create-linked-dock
(its last bash consumer) was ported onto this module; the 092 recipe port had
already replaced the earlier consumer. The order/launcher readbacks go
through recipe.py's typed viewTasksData boundary (recipe.view_tasks for appId; a
launcherUrl-carrying twin here since recipe.Task does not surface it), and the
rest-center math is recipe.task_center - one implementation, shared with the bash
via lib.sh's e2e_task_center formula.
"""

from __future__ import annotations

import os
import subprocess
import time

from pydantic import BaseModel, ConfigDict, Field, TypeAdapter

from latte_harness import recipe


class _LauncherTask(BaseModel):
    """One viewTasksData entry with the launcher sub-model identity.

    recipe.Task carries only appId (the order identity); the launcher order also
    reads launcherUrl, which persists to the tasks-applet ``launchers`` config key.
    Window tasks have an empty launcherUrl, kept in place so the list length still
    matches the bar (the bash comment). extra="ignore" tolerates a dock-side field
    addition, like every readback model.
    """

    model_config = ConfigDict(extra="ignore", populate_by_name=True, frozen=True)

    app_id: str = Field(alias="appId")
    launcher_url: str = Field(alias="launcherUrl")


_LAUNCHER_TASKS = TypeAdapter(list[_LauncherTask])


def _require_env(name: str) -> str:
    """The bash ``${VAR:?}``: return the value, or refuse loudly naming the var."""
    value = os.environ.get(name)
    if not value:
        raise recipe.RecipeError(f"task_reorder: required environment variable {name} is unset")
    return value


def _screen_dims() -> tuple[int, int]:
    """E2E_SCREEN_W/H (the bash ``$(( E2E_SCREEN_W / 2 ))`` staging math)."""
    return int(_require_env("E2E_SCREEN_W")), int(_require_env("E2E_SCREEN_H"))


def _fakepointer(*args: str) -> None:
    """Fire one fakepointer invocation (the taskdrag choreography does not gate on
    its status, matching the bash, which runs move/glide/drag unconditionally)."""
    _ = subprocess.run([_require_env("E2E_FAKEPOINTER"), *args], check=False)


# ---- order readbacks -------------------------------------------------------


def taskdrag_order(view: int) -> str:
    """taskdrag_order: the model-order appId list, space-separated (G4).

    Each entry is one task's stable appId in viewTasksData index order, so a
    reorder shows as a permutation of this list.
    """
    return " ".join(t.app_id for t in recipe.view_tasks(view))


def taskdrag_launcher_order(view: int) -> str:
    """taskdrag_launcher_order: the launcherUrl list in model order - the launcher
    sub-model identity, which also persists to the ``launchers`` config key. Empty
    entries (window tasks have no launcherUrl) are kept in place so the list length
    still matches the bar."""
    tasks = _LAUNCHER_TASKS.validate_json(recipe.json_payload("viewTasksData", "u", str(view)))
    return " ".join(t.launcher_url for t in tasks)


# ---- the cold rest center and the outside-the-bar approach -----------------


def _taskdrag_center(view: int, app_id: str) -> tuple[int, int]:
    """The cold rest center of the task with this appId (the arithmetic even-slot
    model, from the compositor-true window x - recipe.task_center is lib.sh's
    e2e_task_center, the one shared formula)."""
    return recipe.task_center(view, app_id)


def _approach_point(edge: str, cx: int, cy: int, screen_w: int, screen_h: int) -> tuple[int, int]:
    """A staging point OUTSIDE the dock on the axis the icons split along, so the
    drag settles onto the task by a glide from clear space. Pure so the edge->point
    mapping is unit-testable. Horizontal bars stage at mid-screen height on the
    task's x; vertical bars stage at mid-screen width on the task's y."""
    if edge in ("bottom", "top"):
        return cx, screen_h // 2
    if edge in ("left", "right"):
        return screen_w // 2, cy
    return screen_w // 2, screen_h // 2


def _taskdrag_approach(view: int, cx: int, cy: int) -> tuple[int, int]:
    """_taskdrag_approach: the outside-the-bar staging point for this view's edge."""
    screen_w, screen_h = _screen_dims()
    return _approach_point(recipe.view(view).edge, cx, cy, screen_w, screen_h)


def _midpoint(a: int, b: int) -> int:
    """The bash ``$(( (a + b) / 2 ))`` integer midpoint."""
    return (a + b) // 2


# ---- the drag primitives ---------------------------------------------------


def taskdrag_reorder(view: int, src: str, dst: str) -> None:
    """taskdrag_reorder: drag the src task onto the dst task's rest slot in ONE hold.
    Releasing at the neighbour's rest center is exactly ONE crossing (releasing
    further rides into the next neighbour and swaps twice - the 050 calibration).
    Commits the reorder LIVE while crossing; the release only ends the internal
    Qt-Quick drag, it does not decide the move. The caller reads taskdrag_order to
    see the effect."""
    sx, sy = _taskdrag_center(view, src)
    dx, dy = _taskdrag_center(view, dst)
    ax, ay = _taskdrag_approach(view, sx, sy)
    # settle onto the source first (glide, not jump - the vehicle enter race), then
    # press and glide through the midpoint to the destination rest center.
    _fakepointer("move", str(ax), str(ay))
    time.sleep(0.3)
    _fakepointer("glide", str(ax), str(ay), str(sx), str(sy))
    time.sleep(0.4)
    _fakepointer(
        "drag",
        str(sx),
        str(sy),
        str(_midpoint(sx, dx)),
        str(_midpoint(sy, dy)),
        str(dx),
        str(dy),
    )
    time.sleep(2)


def taskdrag_hold_noop(view: int, app: str) -> None:
    """taskdrag_hold_noop: press and release on the task WITHOUT moving (T5a
    zero-delta). The true no-op: insertIndexAt never fires, the model never moves. A
    driver that reported this AS a reorder would be untrustworthy - the recipe
    asserts the order is UNCHANGED, the HC3 rejection observation."""
    cx, cy = _taskdrag_center(view, app)
    ax, ay = _taskdrag_approach(view, cx, cy)
    _fakepointer("move", str(ax), str(ay))
    time.sleep(0.3)
    _fakepointer("glide", str(ax), str(ay), str(cx), str(cy))
    time.sleep(0.4)
    # drag from the center to the SAME center: press, a zero-length glide, release -
    # no crossing, so no move.
    _fakepointer("drag", str(cx), str(cy), str(cx), str(cy))
    time.sleep(1.5)


def taskdrag_reverse_jitter(view: int, app: str, toward: str) -> None:
    """taskdrag_reverse_jitter: press on the task, glide toward a neighbour and BACK
    to origin, release (DR-2 / T5b). Whether this nets a move depends on how far the
    out-swing crossed; either way the net is meant to be zero - the caller asserts
    the order and the launchers key are byte-unchanged."""
    cx, cy = _taskdrag_center(view, app)
    tx, ty = _taskdrag_center(view, toward)
    ax, ay = _taskdrag_approach(view, cx, cy)
    _fakepointer("move", str(ax), str(ay))
    time.sleep(0.3)
    _fakepointer("glide", str(ax), str(ay), str(cx), str(cy))
    time.sleep(0.4)
    # out toward the neighbour, then back to the exact origin, release there.
    _fakepointer("drag", str(cx), str(cy), str(tx), str(ty), str(cx), str(cy))
    time.sleep(2)


def taskdrag_escape_held(view: int, src: str, dst: str) -> None:
    """taskdrag_escape_held: press on src, glide to dst's rest center CROSSING the
    neighbour, tap Escape WITH THE BUTTON STILL HELD, then release (DR-6, via
    fakepointer dragkey). The drag is a REAL compositor drag (Drag.dragType
    Automatic -> QDrag/wl_data_device), so Escape DOES cancel it at the compositor -
    but tasksModel.move already ran LIVE while crossing, so the committed move does
    NOT revert. The caller reads taskdrag_order to record that (D1)."""
    sx, sy = _taskdrag_center(view, src)
    dx, dy = _taskdrag_center(view, dst)
    ax, ay = _taskdrag_approach(view, sx, sy)
    _fakepointer("move", str(ax), str(ay))
    time.sleep(0.3)
    _fakepointer("glide", str(ax), str(ay), str(sx), str(sy))
    time.sleep(0.4)
    _fakepointer(
        "dragkey",
        "Escape",
        str(sx),
        str(sy),
        str(_midpoint(sx, dx)),
        str(_midpoint(sy, dy)),
        str(dx),
        str(dy),
    )
    time.sleep(2)


def _out_of_applet_point(
    edge: str, cx: int, cy: int, screen_w: int, screen_h: int
) -> tuple[int, int]:
    """A point well clear of the dock band, off the axis the bar occupies. Pure so
    the edge->off-dock mapping is unit-testable (the bash case block, exactly)."""
    if edge == "bottom":
        return cx, screen_h // 3
    if edge == "top":
        return cx, screen_h * 2 // 3
    if edge == "left":
        return screen_w * 2 // 3, cy
    if edge == "right":
        return screen_w // 3, cy
    return screen_w // 2, screen_h // 2


def taskdrag_out_of_applet(view: int, app: str) -> None:
    """taskdrag_out_of_applet: press on the task and glide fully OUT of the tasks
    applet (off the dock band), then release (T1d / task-dragged-outside). onDragLeave
    clears the dropping flags; a task released over the containment/desktop adds
    nothing to the bar. The caller asserts no crash and the order reflects only
    in-applet crossings."""
    cx, cy = _taskdrag_center(view, app)
    ax, ay = _taskdrag_approach(view, cx, cy)
    edge = recipe.view(view).edge
    screen_w, screen_h = _screen_dims()
    ox, oy = _out_of_applet_point(edge, cx, cy, screen_w, screen_h)
    _fakepointer("move", str(ax), str(ay))
    time.sleep(0.3)
    _fakepointer("glide", str(ax), str(ay), str(cx), str(cy))
    time.sleep(0.4)
    _fakepointer("drag", str(cx), str(cy), str(ox), str(oy))
    time.sleep(2)
