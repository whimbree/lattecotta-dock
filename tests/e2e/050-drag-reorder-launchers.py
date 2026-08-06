#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""EX-14 live check 1 (docs/agent-logs/EX-14.md): drag-reorder a pinned
launcher - press on the second launcher, GLIDE along the dock axis past
the third (small steps; jumps miss parabolic-shifted icons, the
documented trap fakepointer's interpolated drag exists for), release.
The launcher order must flip in the live readback (viewTasksData), reach
the launchers config entry once the dock stops, and SURVIVE a restart.

Ported from tests/e2e/050-drag-reorder-launchers.sh to latte_harness.recipe
(BP-3, the bash-to-python migration's input/wheel recipe batch R8). The
order/launcher readbacks and the calibration geometry ride recipe.py's typed
boundary; the launchers config key is discovered from E2E_LAYOUT and
round-tripped through kreadconfig6 / kwriteconfig6, exactly as the bash did.
The pixel calibration keeps the magick screenshot crop and the spotify-green
detection unchanged (recipe.screenshot is e2e_screenshot), so a real green disc
in the bar is still required - this recipe needs a >=3-launcher seed carrying
spotify (the clean default seed does not; drive with an E2E_CONFIG_BASE scratch
seed, the R7 precedent). Every assertion, retry count and failure message is
byte-identical to the bash.
"""

import contextlib
import io
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from collections.abc import Iterator
from pathlib import Path

from latte_harness import recipe


def _fakepointer(*args: str) -> None:
    subprocess.run([os.environ["E2E_FAKEPOINTER"], *args], check=False)


def _kread(*args: str) -> str:
    result = subprocess.run(
        ["kreadconfig6", "--file", os.environ["E2E_LAYOUT"], *args],
        capture_output=True,
        text=True,
        check=False,
    )
    return result.stdout.rstrip("\n")


def _kwrite(*args: str) -> None:
    subprocess.run(["kwriteconfig6", "--file", os.environ["E2E_LAYOUT"], *args], check=False)


@contextlib.contextmanager
def _muted_stderr() -> Iterator[None]:
    """The cleanup dock stop's `>/dev/null 2>&1`: keep its diagnostics off the recipe output."""
    with contextlib.redirect_stderr(io.StringIO()):
        yield


def _find_launchers_key(layout: str, view: int, applet: int) -> str:
    """The launchers config KEY name in the tasks applet's General group.

    The bash awk: scan E2E_LAYOUT for the exact group header, then the first
    ``launchers[0-9]*=`` line under it, returning the key name before ``=``. A
    ``[`` line other than the header closes the group, exactly as the awk
    ``/^\\[/ {f=0}`` reset did.
    """
    target = f"[Containments][{view}][Applets][{applet}][Configuration][General]"
    in_group = False
    key = re.compile(r"^launchers[0-9]*=")
    for line in Path(layout).read_text().splitlines():
        if line == target:
            in_group = True
            continue
        if line.startswith("["):
            in_group = False
        if in_group and key.match(line):
            return line.split("=", 1)[0]
    return ""


def _model(view: int) -> tuple[int, int]:
    """The icon-row y-center and the even-slot pixel width (the bash model line).

    The surface's true x cannot be trusted from any report here (see the header),
    so the centers are derived by pixel calibration from spotify's real green
    disc; only the row y and the per-icon slot width come from the even-division
    model (viewsData origin + the tasks applet geometry).
    """
    target = recipe.view(view)
    ay = target.absolute_geometry[1]
    ly = target.local_geometry[1]
    oy = ay - ly
    applet = next(a for a in recipe.view_applets(view) if a.plugin == "org.kde.latte.plasmoid")
    _px, py, pw, ph = applet.geometry
    n = len(recipe.view_tasks(view))
    return int(oy + py + ph / 2), int(pw / n)


def _calibrate(shot: str, cy: int, slot: int, spotify_idx: int) -> tuple[int, int] | None:
    """The launcher-2 and launcher-3 screen x from spotify's real green center.

    A screenshot row through the icon centers gives spotify's REAL center (a
    saturated green disc unique in the bar), and every other center derives from
    it by the even-slot model. Returns (c1x, c2x) or None on the no-clean-run
    refusal, printing the same stderr diagnostic the bash python one-liner did;
    the caller maps None to the loud "pixel calibration failed" e2e_fail.
    """
    crop = subprocess.run(
        ["magick", shot, "-crop", f"1600x1+0+{cy}", "-depth", "8", "txt:-"],
        capture_output=True,
        text=True,
        check=False,
    )
    greens = [
        int(m.group(1))
        for m in (
            re.match(r"(\d+),0: \((\d+),(\d+),(\d+)", line) for line in crop.stdout.splitlines()
        )
        if m and int(m.group(3)) > 150 and int(m.group(2)) < 120 and int(m.group(4)) < 140
    ]
    if not greens or max(greens) - min(greens) > 80:
        print(
            f"no clean spotify-green run on the icon row (got {len(greens)} green px)",
            file=sys.stderr,
            flush=True,
        )
        return None
    spotify = (min(greens) + max(greens)) / 2
    return int(spotify + (1 - spotify_idx) * slot), int(spotify + (2 - spotify_idx) * slot)


def main() -> None:
    try:
        view = recipe.tasks_view()
    except recipe.RecipeError:
        recipe.fail("no tasks view")

    layout = os.environ["E2E_LAYOUT"]

    def order() -> str:
        tasks = json.loads(recipe.json_payload("viewTasksData", "u", str(view)))
        return " ".join(t["launcherUrl"] for t in tasks)

    #! preconditions: pure launchers (a window task would reflow mid-drag),
    #! at least three of them
    launchers = order().split()
    if len(launchers) < 3:
        recipe.fail(f"need >=3 pinned launchers, have {len(launchers)}")
    if '"isLauncher":false' in recipe.json_payload("viewTasksData", "u", str(view)):
        recipe.fail("window tasks present; this recipe needs a launchers-only bar")

    #! the launchers config entry (key name carries the synced-group id, so it
    #! is discovered, not assumed) - the persistence witness after the stop
    tasks_applet = next(
        a.id for a in recipe.view_applets(view) if a.plugin == "org.kde.latte.plasmoid"
    )
    launchers_key = _find_launchers_key(layout, view, tasks_applet)
    if not launchers_key:
        recipe.fail(f"no launchers entry found in the layout for applet {tasks_applet}")
    general = (
        "--group",
        "Containments",
        "--group",
        str(view),
        "--group",
        "Applets",
        "--group",
        str(tasks_applet),
        "--group",
        "Configuration",
        "--group",
        "General",
    )
    orig_launchers = _kread(*general, "--key", launchers_key)

    def restore_config() -> None:
        with _muted_stderr():
            recipe.dock_stop()
        _kwrite(*general, "--key", launchers_key, orig_launchers)

    try:
        #! the zoom stays ON, deliberately: with zoomLevel=0 the auto-sized bar
        #! keeps trailing space inside the tasks applet and the even-division
        #! center model breaks (calibrated: the press landed a whole slot over);
        #! at the default zoom the division matches the rendered icons within a
        #! few px and the drag stream itself handles the parabolic shifts.

        before = order()
        print(f"before: {before}")
        expected = " ".join([launchers[0], launchers[2], launchers[1], *launchers[3:]])

        #! pixel calibration needs the spotify launcher: the spotify icon is a
        #! saturated green disc unique in the bar, so a screenshot row through the
        #! icon centers gives its REAL center (see attempt_drag)
        spotify_idx = next(
            (i for i, t in enumerate(recipe.view_tasks(view)) if t.app_id == "spotify.desktop"),
            None,
        )
        if spotify_idx is None:
            recipe.fail(
                "pixel calibration needs the spotify launcher in the bar "
                "(my staged config carries it)"
            )

        # attempt_drag: derive the launcher centers by PIXEL CALIBRATION at drag
        # time - the surface's true x cannot be trusted from any report (the
        # window drifts from viewsData's implied origin, re-anchors on clock
        # minute ticks, and the sidebar's same-sized window makes the compositor
        # dump ambiguous; all three bit during calibration).
        def attempt_drag() -> None:
            cy, slot = _model(view)
            handle, shot = tempfile.mkstemp(suffix=".png")
            os.close(handle)
            try:
                try:
                    recipe.screenshot(shot)
                except recipe.RecipeError:
                    recipe.fail("calibration screenshot failed")
                calib = _calibrate(shot, cy, slot, spotify_idx)
                if calib is None:
                    recipe.fail("pixel calibration failed")
                c1x, c2x = calib
            finally:
                with contextlib.suppress(OSError):
                    os.unlink(shot)
            c1y = c2y = cy

            #! settle ONTO the launcher first (glide, not jump - the vehicle's
            #! enter race), then press and glide to launcher 3's rest center; the
            #! model reorders LIVE while the drag crosses a neighbor
            #! (decideTasksDragMove's MoveDragSource), so releasing at the rest
            #! center means exactly ONE crossing - releasing half a slot further
            #! rides into the next neighbor and swaps twice (calibrated). The
            #! interpolated drag stream (24 steps per waypoint pair, ~12ms apart)
            #! keeps every step small for the live hit testing.
            _fakepointer("move", str(c1x), "500")
            time.sleep(0.3)
            _fakepointer("glide", str(c1x), "500", str(c1x), str(c1y))
            time.sleep(0.4)
            _fakepointer(
                "drag",
                str(c1x),
                str(c1y),
                str((c1x + c2x) // 2),
                str(c1y),
                str(c2x),
                str(c2y),
            )
            time.sleep(2)

        def reset_order() -> bool:
            if not recipe.dock_stop():
                return False
            _kwrite(*general, "--key", launchers_key, orig_launchers)
            return recipe.dock_start()

        after = ""
        for attempt in (1, 2, 3):
            attempt_drag()
            after = order()
            if after == expected:
                break
            if after != before:
                #! an adjacent pair moved, just not the intended one: the press
                #! landed a slot over on stale geometry - reset and re-aim
                print(f"  (attempt {attempt} reordered the wrong pair: {after} - resetting)")
                if not reset_order():
                    recipe.fail("could not reset the launcher order between attempts")
            else:
                print(f"  (attempt {attempt} did not reorder anything, retrying)")
        print(f"after:  {after}")
        if after != expected:
            recipe.fail(f"drag did not swap launchers 2 and 3 in 3 attempts (expected: {expected})")
        print("live order flipped (launcher 2 dropped past launcher 3)")

        #! the config witness needs the flush a clean stop guarantees
        if not recipe.dock_stop():
            recipe.fail("no clean stop to flush the launcher order")
        persisted = _kread(*general, "--key", launchers_key)
        persisted_ids = persisted.replace(",", " ")
        if persisted_ids != expected:
            recipe.fail(f"config order after stop: '{persisted}' (expected '{expected}')")
        print(f"config entry {launchers_key} carries the new order")

        #! and the new order must survive a restart
        if not recipe.dock_start():
            recipe.fail("dock did not come back for the persistence check")
        final = order()
        if final != expected:
            recipe.fail(f"order did not survive the restart (got: {final})")
        print("reorder survived the dock restart")
    finally:
        restore_config()


if __name__ == "__main__":
    recipe.run(main)
