#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""AU-6a control 2 (docs/tracking/edit-mode-settings-audit-plan.md, cluster CL-6): the
LIVE driven proof of the edit-background-wheel fork-rewire trap. In edit mode the
wheel over the canvas grid adjusts editBackgroundOpacity - the edit-mode grid
overlay's opacity, a Double in [0,1]. BOTH reference forks rewired this handler to
write panelTransparency instead (latte-dock-qt6 CanvasConfiguration.qml:153,
latte-dock-ng :98) - an Int in [0,100] that is the dock's REAL runtime background
opacity, a persistent user setting. Scrolling the edit overlay must never silently
move the running dock's transparency. This port keeps the Qt5-faithful
editBackgroundOpacity (CanvasConfiguration.qml:154-160).

The decisive check is P2 (right key, no stray write): snapshot the whole
containment config through viewConfigData (the in-process map), scroll the wheel,
snapshot again, and assert the EXACT changed-key set is {editBackgroundOpacity} -
a panelTransparency write would appear here as a stray key and FAIL. The step is
then confirmed to be one 0.1 detent in the driven direction.

Ported from tests/e2e/032-wheel-editbackground-opacity.sh to latte_harness.audit
/ .recipe (BP-3, the bash-to-python migration's recipe batch). The wheel delivery
over the canvas grid stays flaky, so the same single-invocation scroll + park +
poll + retry rhythm is preserved; the fakepointer stays a subprocess call.
"""

from __future__ import annotations

import io
import os
import subprocess
import sys
import tempfile
import time
from contextlib import redirect_stderr, suppress
from pathlib import Path

from latte_harness import audit, recipe
from pydantic import ValidationError


def _quiet_dock_stop() -> None:
    """e2e_dock_stop >/dev/null 2>&1 || true: best-effort, chatter suppressed."""
    with suppress(recipe.RecipeError), redirect_stderr(io.StringIO()):
        _ = recipe.dock_stop()


def _fakepointer(*args: str) -> None:
    """$E2E_FAKEPOINTER <args>: inject a pointer event, fire-and-forget."""
    subprocess.run([os.environ["E2E_FAKEPOINTER"], *args], check=False)


def _config_snapshot_text(view: int) -> str:
    """audit_config_snapshot > file (lenient): '' when the readback cannot
    validate, matching the bash redirect of a failed snapshot to an empty file."""
    try:
        return audit.config_snapshot(view)
    except ValidationError:
        return ""


def _snap_value(snapshot: str, key: str) -> str:
    """The json-value stored for `key` in a key<TAB>value snapshot, '' when absent."""
    for line in snapshot.split("\n"):
        if not line:
            continue
        candidate, _, value = line.partition("\t")
        if candidate == key:
            return value
    return ""


def _cfg_value(view: int, key: str) -> str:
    """cfg_op()/cfg_pt(): one config key from a fresh (lenient) config snapshot."""
    return _snap_value(_config_snapshot_text(view), key)


def main() -> None:
    try:
        view = recipe.tasks_view()
    except recipe.RecipeError:
        recipe.fail("no tasks view")

    layout = os.environ["E2E_LAYOUT"]
    orig = _cfg_value(view, "editBackgroundOpacity")
    try:
        if not audit.enter_editmode(view):
            recipe.fail("edit mode never turned on")

        # the canvas: the latte edit-mode window just mapped, screen-wide and thin
        scx, scy, scw, _sch = recipe.view(view).screen_geometry
        canvas = next((w for w in recipe.windows() if w.width == scw and w.height < 300), None)
        if canvas is None:
            recipe.fail("no canvas window mapped for edit mode")

        # screen-centre horizontally (clear of the left-end rearrange toggle),
        # canvas upper quarter: the editBackMouseArea band sits between the
        # ~13px ruler at the canvas top and the dock items area below. The
        # canvas' vertical MIDDLE lands inside the items area on the current
        # tree (probed 2026-08-05: the responsive band is roughly
        # canvas-relative 36..51 of 146, mid 73 is dead), so aim at ch/4,
        # mid-band, and let the retry loop absorb the flake.
        wx = scx + scw // 2
        wy = canvas.y + canvas.height // 4

        start_op = _cfg_value(view, "editBackgroundOpacity")
        if not start_op:
            recipe.fail("could not read editBackgroundOpacity")
        start_pt = _cfg_value(view, "panelTransparency")
        print(f"start: editBackgroundOpacity={start_op} panelTransparency={start_pt}")

        # pick a direction with headroom: below 0.85 scroll up (+0.1), else down (-0.1)
        detent = 1 if float(start_op) < 0.85 else -1

        with tempfile.TemporaryDirectory() as work_str:
            work = Path(work_str)
            before = work / "before"
            after = work / "after"
            before.write_text(_config_snapshot_text(view))

            # deliver one detent on the edit-background grid and wait for the
            # opacity to land (single-invocation scroll, then park off)
            landed = ""
            for attempt in range(1, 9):
                _fakepointer("scroll", str(wx), str(wy), str(detent), "100")
                _fakepointer("move", str(wx), str(scy))
                for _ in range(6):
                    cur = _cfg_value(view, "editBackgroundOpacity")
                    if cur != start_op:
                        landed = cur
                        break
                    time.sleep(0.5)
                if landed:
                    break
                print(f"  (edit-background detent not delivered on attempt {attempt}, retrying)")
            if not landed:
                recipe.fail(
                    "the edit-background wheel never moved editBackgroundOpacity after 8 attempts"
                )

            after.write_text(_config_snapshot_text(view))

            # THE decisive check (P2): the wheel changed editBackgroundOpacity and
            # ONLY editBackgroundOpacity. A panelTransparency write (the fork-rewire
            # trap) would show here as a second changed key and fail this assertion.
            if audit.assert_applies(before, after, "editBackgroundOpacity") != 0:
                recipe.fail("P1: the wheel did not move editBackgroundOpacity")
            if audit.assert_only_keys(before, after, "editBackgroundOpacity") != 0:
                recipe.fail(
                    "P2 fork-rewire trap: the edit-background wheel changed a key other than "
                    "editBackgroundOpacity (panelTransparency rewire?)"
                )

        # spell the trap out loud: panelTransparency is untouched
        end_pt = _cfg_value(view, "panelTransparency")
        if end_pt != start_pt:
            recipe.fail(
                f"panelTransparency moved ({start_pt} -> {end_pt}): the wheel rewired to the dock "
                "background opacity"
            )

        # and the move is exactly one 0.1 detent in the driven direction, clamped to [0,1]
        start = float(start_op)
        landed_value = float(landed)
        expected = min(1.0, start + 0.1) if detent > 0 else max(0.0, start - 0.1)
        if abs(landed_value - expected) > 0.001:
            print(
                f"editBackgroundOpacity {start} -> {landed_value}, expected {expected}",
                file=sys.stderr,
                flush=True,
            )
            recipe.fail("editBackgroundOpacity did not move by one 0.1 detent")

        print(
            f"edit-background wheel moved editBackgroundOpacity {start_op} -> {landed} "
            f"(only key changed; panelTransparency held at {start_pt})"
        )

        _ = audit.exit_editmode(view)
        print(
            "AU-6a control 2: the edit-background wheel writes editBackgroundOpacity, NOT "
            "panelTransparency (Qt5-faithful, fork-rewire trap avoided)"
        )
    finally:
        _quiet_dock_stop()
        subprocess.run(
            [
                "kwriteconfig6", "--file", layout,
                "--group", "Containments", "--group", str(view), "--group", "General",
                "--key", "editBackgroundOpacity", orig or "0.2",
            ],
            check=False,
        )


if __name__ == "__main__":
    recipe.run(main)
