#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""EX-15 live check 2 (docs/agent-logs/EX-15.md): with scrollAction=Desktops,
a wheel detent over EMPTY dock area switches the virtual desktop - one
ADJACENT switch per detent (the EnvironmentActions cutover through
LatteCore.WheelStepper, SignedExtreme pick, threshold 80). Three desktops
are used so an overshoot (two switches from one detent) is detectable.
fakepointer detents are 120 angleDelta units, so the sub-threshold half of
the contract stays with the unit tests (wheelaccumulatortest).

VEHICLE LIMITATION, established with dock-side instrumentation
(2026-07-17, this unit's ledger): the nested kwin stops delivering
pointer/wheel events to the dock's layer surface after a desktop switch -
repeated wheels deliver fine as long as no switch happens (verified 3-in-a-
row), the first real switch kills delivery, motion does not restore it, and
the dock's input regions stay intact throughout (viewsData readback), so
the fault is compositor-side input-focus bookkeeping under fake input, not
the dock. The recipe therefore restarts the dock between the two
directions (the only reliable delivery reset found) and retries each
detent's delivery; the ASSERTIONS stay semantic: a delivered detent moves
to exactly the adjacent desktop.

Ported from tests/e2e/010-wheel-desktops.sh to latte_harness.recipe (BP-3,
the bash-to-python migration's input/wheel recipe batch). The
VirtualDesktopManager get/set-property reads stay busctl calls (the same
transport the bash vdm() helper used); the empty-strip geometry and the
viewsData/viewAppletsData join ride recipe.py's typed boundary.
"""

import os
import re
import subprocess
import sys
import time

from latte_harness import recipe


def _fakepointer(*args: str) -> None:
    _ = recipe.fakepointer(*args)


def _kread(*args: str) -> str:
    result = subprocess.run(
        ["kreadconfig6", "--file", os.environ["E2E_LAYOUT"], *args],
        capture_output=True,
        text=True,
        check=False,
    )
    return result.stdout.rstrip("\n")


def _kwrite(*args: str) -> None:
    _ = recipe.kwriteconfig("--file", os.environ["E2E_LAYOUT"], *args)


def _vdm(verb: str, *args: str, quiet: bool = False) -> str:
    """The bash vdm(): a KWin VirtualDesktopManager get/set-property or call.

    ``quiet`` mirrors the bash ``2>&1`` on the cleanup removeDesktop calls; the
    get/set-property and createDesktop calls forwarded busctl's stderr.
    """
    result = subprocess.run(
        [
            "busctl",
            "--user",
            verb,
            "org.kde.KWin",
            "/VirtualDesktopManager",
            "org.kde.KWin.VirtualDesktopManager",
            *args,
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.stderr and not quiet:
        sys.stderr.write(result.stderr)
    return result.stdout


def _desktop_count() -> str:
    """desktop_count: field 2 of the count property (the bash awk '{print $2}')."""
    fields = _vdm("get-property", "count").split()
    return fields[1] if len(fields) >= 2 else ""


def _current_desktop() -> str:
    """current_desktop: field 2 of the current property, quotes stripped (tr -d '"')."""
    fields = _vdm("get-property", "current").split()
    return fields[1].replace('"', "") if len(fields) >= 2 else ""


def _desktop_id(pos: int) -> str:
    """desktop_id: the pos-th 36-char uuid in the desktops property (1-indexed)."""
    ids = re.findall(r'"[0-9a-f-]{36}"', _vdm("get-property", "desktops"))
    return ids[pos - 1].replace('"', "") if len(ids) >= pos else ""


def _empty_area_point(view: int) -> tuple[int, int] | None:
    """empty_area_point: the widest applet-free strip's x-midpoint (and the strip's
    y-center). Returns None on the under-6px refusal, printing the same diagnostic
    the bash python one-liner sent to stderr; the caller maps None to the loud
    "no empty-area point" e2e_fail, exactly as the bash `|| e2e_fail` did.

    The surface can drift left of the reported frame (e2e_view_window_x); both the
    strip bounds and the applet spans move with it.
    """
    winx = recipe.view_window_x(view)
    target = recipe.view(view)
    ax, ay, aw, ah = target.absolute_geometry
    lx = target.local_geometry[0]
    ox = winx if winx is not None else ax - lx
    drift = ox - (ax - lx)
    ax += drift
    spans = sorted(
        (ox + a.geometry[0], ox + a.geometry[0] + a.geometry[2]) for a in recipe.view_applets(view)
    )
    gaps: list[tuple[int, int]] = []
    cursor = ax
    for start, end in spans:
        if start > cursor:
            gaps.append((cursor, start))
        cursor = max(cursor, end)
    if ax + aw > cursor:
        gaps.append((cursor, ax + aw))
    best = max(gaps, key=lambda g: g[1] - g[0], default=(0, 0))
    if best[1] - best[0] < 6:
        print(f"widest empty-area gap is under 6px: {gaps}", file=sys.stderr, flush=True)
        return None
    return int((best[0] + best[1]) / 2), int(ay + ah / 2)


def main() -> None:
    try:
        view = recipe.tasks_view()
    except recipe.RecipeError:
        recipe.fail("no tasks view")

    general = ("--group", "Containments", "--group", str(view), "--group", "General")

    #! preconditions: three desktops (overshoot detection), dock starting on the
    #! first; both verified to have taken effect before the dock (re)starts
    created: list[int] = []
    while int(_desktop_count()) < 3:
        n = int(_desktop_count()) + 1
        _vdm("call", "createDesktop", "us", str(n - 1), f"E2E Desk {n}")
        if int(_desktop_count()) != n:
            recipe.fail("createDesktop did not take effect")
        created.append(n)
    _vdm("set-property", "current", "s", _desktop_id(1))

    orig_scroll = _kread(*general, "--key", "scrollAction")

    def cleanup() -> None:
        with recipe.muted_stderr():
            recipe.dock_stop()
        if orig_scroll:
            _kwrite(*general, "--key", "scrollAction", orig_scroll)
        else:
            _kwrite(*general, "--key", "scrollAction", "--delete")
        for pos in created:
            _vdm("call", "removeDesktop", "s", _desktop_id(pos), quiet=True)

    try:
        #! config flip while the dock is STOPPED: scrollAction=1 (ScrollDesktops)
        if not recipe.dock_stop():
            recipe.fail("could not stop the dock for the config flip")
        _kwrite(*general, "--key", "scrollAction", "1")
        if not recipe.dock_start():
            recipe.fail("dock did not come back after the config flip")

        # wheel_switch <detent> <expect-from> <expect-to>: deliver one detent over
        # empty dock area (with the enter dance and delivery retries per the header)
        # and assert exactly one adjacent switch.
        def wheel_switch(detent: int, from_: str, to: str) -> None:
            point = _empty_area_point(view)
            if point is None:
                recipe.fail("no empty-area point")
            px, py = point
            for attempt in (1, 2, 3, 4):
                if _current_desktop() != from_:
                    recipe.fail("not on the expected start desktop")
                #! settle the pointer OUTSIDE then INSIDE the strip before wheeling:
                #! an axis event racing its own enter never reaches the QML scene
                _fakepointer("move", str(px), "500")
                time.sleep(0.3)
                _fakepointer("move", str(px), str(py))
                time.sleep(0.6)
                _fakepointer("scroll", str(px), str(py), str(detent), "100")
                now = from_
                for _ in range(6):
                    time.sleep(0.4)
                    now = _current_desktop()
                    if now != from_:
                        break
                if now != from_:
                    if now != to:
                        recipe.fail(f"detent {detent} overshot: {from_} -> {now} (expected {to})")
                    return
                print(f"  (detent {detent} not delivered on attempt {attempt}, retrying)")
            recipe.fail(
                f"detent {detent} never delivered after 4 attempts "
                "(vehicle input-delivery limitation exceeded)"
            )

        d1 = _desktop_id(1)
        d2 = _desktop_id(2)

        wheel_switch(-1, d1, d2)
        print("down-detent: exactly one adjacent switch (1 -> 2)")

        #! delivery reset (see header); the dock restart does not touch desktops
        if not recipe.dock_stop():
            recipe.fail("could not restart between directions")
        if not recipe.dock_start():
            recipe.fail("dock did not come back for the up direction")

        wheel_switch(1, d2, d1)
        print("up-detent: exactly one adjacent switch (2 -> 1)")
    finally:
        cleanup()


if __name__ == "__main__":
    recipe.run(main)
