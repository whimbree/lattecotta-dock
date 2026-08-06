#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""C-I4 / P4, the G3 readback (docs/tracking/e2e-interaction-test-plan.md): the live
drop-marker readback viewDropMarkerIndex - the direct insert(-1)
observability an add/reorder abort leans on.

This recipe proves the readback is WIRED end-to-end and reports the CLEAN
sentinel (-1) for a real view AT REST - which is exactly the post-abort
baseline (no drag in flight => the dndSpacer is parked off the layouts =>
no live marker). A bad containment id is refused (-1 + qWarning).

The LIVE side (a mid-drag marker reads >=0, index 0 being a live leading
marker) is proven two ways: the value contract is pinned deterministically
by tests/units/dbusreportstest.cpp (dropMarkerIsLiveSeparatesLiveFromClean),
and the full mid-drag end-to-end drive lands with the widget-explorer DND
driver (C-I9/P8) that the A1 abort scenario (C-A1) uses - this readback is
the surface that scenario asserts on. Driving a real Wayland drag needs a
drag SOURCE surface, which is exactly what C-I9 builds; there is no coarse
action that makes the spacer live without it, so it is not re-implemented
here.

Ported from tests/e2e/091-drop-marker.sh to latte_harness.recipe /
latte_harness.dnd (BP-3, the bash-to-python migration's recipe batch). The
drop-marker readback rides dnd.drop_marker (the same busctl awk '{print $NF}'
last-field parse); the bad-id rejection greps E2E_DOCK_LOG directly, as the
bash did.
"""

import os
import sys
from pathlib import Path

from latte_harness import dnd, recipe


def _all_view_ids() -> list[int]:
    """all_view_ids: every current containment id (the bash viewsData scan)."""
    return [v.containment_id for v in recipe.views()]


def _dock_log_lines() -> list[str]:
    return Path(os.environ["E2E_DOCK_LOG"]).read_text(errors="replace").splitlines()


def _new_log_has(mark: int, needle: str) -> bool:
    """The bash ``tail -n +$((mark+1)) | grep -q``: a new dock-log line carries needle."""
    return any(needle in line for line in _dock_log_lines()[mark:])


def _dump_new_log(mark: int) -> None:
    print("---- new dock-log lines ----", file=sys.stderr, flush=True)
    for line in _dock_log_lines()[mark:]:
        print(line, file=sys.stderr, flush=True)


def main() -> None:
    # ---- clean at rest: every real view reports -1 (no live marker) ---------

    any_view = False
    for cid in _all_view_ids():
        any_view = True
        idx = dnd.drop_marker(cid)
        if idx != "-1":
            recipe.fail(
                f"view {cid} reports drop-marker index {idx} at rest; "
                "expected -1 (no live marker / no orphan spacer)"
            )
        print(f"view {cid}: drop marker clean at rest (index -1)")
    if not any_view:
        recipe.fail("no views found to query the drop marker on")

    # ---- a bad containment id is refused (-1 + qWarning) --------------------

    bad_cid = 987654
    if bad_cid in _all_view_ids():
        recipe.fail(f"test bug: {bad_cid} is a real view id, pick another")

    logmark = len(_dock_log_lines())
    idx = dnd.drop_marker(bad_cid)
    if idx != "-1":
        recipe.fail(
            f"viewDropMarkerIndex on a bad containment id returned {idx}; expected the -1 sentinel"
        )
    if not _new_log_has(
        logmark, f"viewDropMarkerIndex queried for containment {bad_cid} which has no view"
    ):
        _dump_new_log(logmark)
        recipe.fail(
            f"no viewDropMarkerIndex refusal qWarning for bad containment id {bad_cid} "
            "in the dock log"
        )
    print(f"rejection observed: bad containment id {bad_cid} returns -1 + qWarning")

    print("PASS: viewDropMarkerIndex reads clean (-1) at rest and refuses a bad id")


if __name__ == "__main__":
    recipe.run(main)
