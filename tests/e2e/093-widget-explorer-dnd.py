#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""C-I9 / P8 acceptance (docs/tracking/e2e-interaction-test-plan.md, HC3): the
widget-explorer -> containment drag-and-drop driver (latte_harness.dnd).

HC3 says this driver's acceptance IS A REJECTION BY CONSTRUCTION: it must prove
a release over NOTHING yields ZERO applets and report that AS a rejected drop,
and that the mid-drag spacer is cleaned up (viewDropMarkerIndex back to -1)
after the abort. A driver that cannot SEE the rejected drop cannot be trusted
for the A1 abort scenario.

The rejection is only trustworthy PAIRED with a positive: a driver that never
adds anything would "pass" a zero-add test vacuously. So the recipe first
proves the SAME driver drives a REAL cross-surface Wayland DnD that reaches the
drop path - the dndSpacer goes live (marker >= 0) and a completed drop adds
EXACTLY ONE applet (no latte-dock-ng double-create) - and only then proves the
rejections. This is the sceneprobe good/bad/blank and run-e2e XPASS discipline
applied to the DND driver itself.

Legs:
  0. a bad containment id to showWidgetExplorer is REFUSED (qWarning, no window)
  1. COMMIT: drop a delegate onto the view -> spacer went live, +1 applet, marker back to -1
  2. ABORT hover-then-off (T2c): hover the dock (spacer live) then release over
     empty space -> ZERO added, spacer cleaned to -1  [THE HC3 REJECTION]
  3. ABORT straight-off (T2a): release over empty space without ever entering the
     dock -> ZERO added, no phantom insert (marker never left -1)

Ported from tests/e2e/093-widget-explorer-dnd.sh to latte_harness.recipe /
latte_harness.dnd (BP-3, the bash-to-python migration's driver-recipe batch).
The DnD drive rides the typed dnd driver; the leg-0 rejection greps E2E_DOCK_LOG
directly, as the bash did.
"""

import os
import sys
import time
from pathlib import Path

from latte_harness import dnd, recipe


def _dumpwin_count() -> int:
    """The bash ``e2e_dumpwins | grep -c DUMPWIN``: how many windows are mapped."""
    return sum("DUMPWIN" in line for line in recipe.dumpwins().splitlines())


def _dock_log_lines() -> list[str]:
    return Path(os.environ["E2E_DOCK_LOG"]).read_text(errors="replace").splitlines()


def _new_log_has(mark: int, needle: str) -> bool:
    """The bash ``tail -n +$((mark+1)) | grep -q``: a new dock-log line carries needle."""
    return any(needle in line for line in _dock_log_lines()[mark:])


def _dump_new_log(mark: int) -> None:
    for line in _dock_log_lines()[mark:]:
        print(line, file=sys.stderr, flush=True)


def main() -> None:
    try:
        view = recipe.tasks_view()
    except recipe.RecipeError:
        recipe.fail("no tasks view to drive explorer DnD onto")
    print(f"target view: {view}")

    # ---- leg 0: showWidgetExplorer refuses a bad containment id -----------------

    bad_cid = 987654
    if f'"containmentId":{bad_cid}' in recipe.json_payload("viewsData"):
        recipe.fail(f"test bug: {bad_cid} is a real view id, pick another")
    logmark = len(_dock_log_lines())
    winmark = _dumpwin_count()
    recipe.call("showWidgetExplorer", "u", str(bad_cid))
    time.sleep(1)
    if _dumpwin_count() != winmark:
        recipe.fail(
            f"REJECTION LEAK: a window appeared for showWidgetExplorer on bad containment id {bad_cid}"
        )
    if not _new_log_has(
        logmark, f"showWidgetExplorer requested for containment {bad_cid} which has no view"
    ):
        _dump_new_log(logmark)
        recipe.fail(
            f"no showWidgetExplorer refusal qWarning for bad containment id {bad_cid} in the dock log"
        )
    print(f"rejection observed: bad containment id {bad_cid} refused (qWarning + no window)")

    # ---- targets ----------------------------------------------------------------
    # The explorer window auto-closes after each drop (a completed QDrag drops
    # draggingWidget to false, re-enabling hideOnWindowDeactivate, and the
    # now-unfocused window deleteLater()s - the #332733 workaround only holds it open
    # WHILE dragging). So every leg re-opens its own explorer; dnd.open_explorer is
    # the fresh drag source for that leg.

    try:
        rect = dnd.open_explorer(view)
    except dnd.DndError:
        recipe.fail(f"widget explorer never opened for view {view}")
    print(f"widget explorer open: window {rect}")
    center = dnd.view_center(view)
    empty = dnd.empty_point()
    print(f"drop target (commit): {center[0]} {center[1]} ; off-dock target (abort): {empty[0]} {empty[1]}")

    # ---- leg 1: COMMIT proves a real drop adds EXACTLY ONE (positive tripwire) ---

    before = dnd.applet_count(view)
    peak = dnd.drag_widget_watched(rect, view, center[0], center[1], center[0], center[1])
    after = dnd.applet_count(view)
    marker = dnd.drop_marker(view)
    print(
        f"COMMIT: count {before} -> {after} (delta {after - before})  "
        f"peak_marker={peak}  marker_now={marker}"
    )
    if peak < 0:
        recipe.fail(
            f"COMMIT drag never made the dndSpacer live (peak marker {peak}): the DnD did not "
            "reach the drop area, so no outcome here is trustworthy"
        )
    if after != before + 1:
        recipe.fail(
            f"COMMIT drop did not add exactly one applet (before {before}, after {after}) - "
            "a 0 is a broken drag, a 2 is the ng double-create"
        )
    if marker != "-1":
        recipe.fail(f"COMMIT: dndSpacer not parked after the drop (marker {marker}, expected -1)")
    print(
        "COMMIT ok: the driver drives a real explorer->containment DnD that adds exactly one; "
        "spacer cleaned"
    )

    # ---- leg 2: ABORT hover-then-off -> ZERO added, spacer cleaned (THE HC3 REJECTION) ---

    try:
        rect = dnd.open_explorer(view)
    except dnd.DndError:
        recipe.fail("widget explorer never re-opened for the hover-then-off abort")
    before = dnd.applet_count(view)
    # hover the dock (spacer opens) THEN release over empty space
    peak = dnd.drag_widget_watched(
        rect, view, center[0], center[1], center[0], center[1], empty[0], empty[1], empty[0], empty[1]
    )
    after = dnd.applet_count(view)
    marker = dnd.drop_marker(view)
    print(
        f"ABORT(hover-then-off): count {before} -> {after} (delta {after - before})  "
        f"peak_marker={peak}  marker_now={marker}"
    )
    if peak < 0:
        recipe.fail(
            f"ABORT: the spacer never went live (peak {peak}) - the drag never hovered the dock, "
            "so 'no residue' here proves nothing"
        )
    if after != before:
        recipe.fail(
            f"REJECTION LEAK: a release over empty space added {after - before} applet(s) "
            f"(count {before} -> {after}) - the drop was NOT rejected"
        )
    if marker != "-1":
        recipe.fail(
            f"ABORT residue: mid-drag spacer NOT cleaned up (marker {marker}, expected -1) - "
            "an orphan dndSpacerIndex"
        )
    print(
        "rejection observed: a real DnD hovered the dock (spacer live at "
        f"{peak}) and was released over nothing -> ZERO applets added, spacer cleaned to -1"
    )

    # ---- leg 3: ABORT straight-off -> ZERO added, no phantom insert (T2a) --------

    try:
        rect = dnd.open_explorer(view)
    except dnd.DndError:
        recipe.fail("widget explorer never re-opened for the straight-off abort")
    before = dnd.applet_count(view)
    peak = dnd.drag_widget_watched(rect, view, empty[0], empty[1], empty[0], empty[1])
    after = dnd.applet_count(view)
    marker = dnd.drop_marker(view)
    print(
        f"ABORT(straight-off): count {before} -> {after} (delta {after - before})  "
        f"peak_marker={peak}  marker_now={marker}"
    )
    if after != before:
        recipe.fail(
            f"REJECTION LEAK: an off-dock release that never entered the dock added "
            f"{after - before} applet(s)"
        )
    if marker != "-1":
        recipe.fail(
            f"ABORT straight-off left a live marker ({marker}) - a phantom insert from the "
            "distance fallback"
        )
    print("rejection observed: an off-dock release that never entered the dock added ZERO (no phantom insert)")

    print(
        "PASS: explorer DnD adds exactly one on a drop, ZERO on a release over nothing, and the "
        "spacer is always cleaned"
    )


if __name__ == "__main__":
    recipe.run(main)
