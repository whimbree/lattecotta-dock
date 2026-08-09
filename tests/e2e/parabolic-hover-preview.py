#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""E2E: gliding the pointer onto a window-owning task maps its preview dialog
(the EX-01/02/03 hover-preview paths end to end). Screen-agnostic: derives the
widest bottom/top tasks dock from viewsData and glides onto the konsole icon
with small steps.

Why the glide is REPEATED until the dialog maps (root cause, traced live
2026-07-17): the preview trigger is the task MouseArea's onEntered, which the
parabolic layer only emits while the dock is at REST - hoverEnabled gates OFF
during the zoom animation (Qt5-faithful: mid-animation hover jitter is
deliberately ignored). A single synthetic glide crosses konsole's boundary at a
moment that races that animation, so onEntered fires on only ~60% of attempts,
and once the warped pointer comes to rest inside the icon no further boundary
crossing ever re-fires it - the preview then never maps. A real hand re-nudges
when a hover does not "take"; this recipe does the same, re-gliding onto the icon
until the dialog maps. The assertion still requires a genuine layer=6 dialog -
the retry makes the DRIVE reliable, it never fakes the result. (fakepointer warps
discrete positions; the animation race is a synthetic-injection artifact, not a
dock defect - previews are reliable live with a real, continuously-moving mouse.)

Ported from tests/e2e/parabolic-hover-preview.sh to latte_harness.recipe (BP-3,
the bash-to-python migration's last plain recipe). The dumpwins scan, the
viewsData geometry, the task-center pointer math and the presentation-coverage
oracle ride recipe.py's typed boundary; the konsole fixture and the fakepointer
glide are the same subprocess drives the bash used. Every assertion, poll bound,
retry count, sleep and failure message is byte-identical; an SPDX header is
added (the bash had none) and the exec bit stays 100755 (D273). The GLIDE stays a glide (small
steps that cross konsole's boundary), never a jump - a warped pointer that lands
inside the icon never re-fires onEntered.
"""

from __future__ import annotations

import contextlib
import io
import os
import re
import subprocess
import sys
import time

from latte_harness import recipe

_KONSOLE_CLASS = "|org.kde.konsole|"
# A genuine latte-dock preview dialog: an empty-caption latte-dock window at
# layer=6 (the double pipe is the empty caption field).
_PREVIEW_RE = re.compile(r"\|latte-dock\|\|[0-9.,-]+ [0-9]+x[0-9]+\|[^|]+\|layer=6")


class _State:
    konsole: subprocess.Popen[bytes] | None = None


_S = _State()


def _fakepointer(*args: object) -> None:
    _ = recipe.fakepointer(*args)


def _konsole_mapped() -> bool:
    return _KONSOLE_CLASS in recipe.dumpwins()


def _capture_coverage_failure(phase: str) -> None:
    if os.environ.get("E2E_MODE") == "nested":
        with contextlib.suppress(recipe.RecipeError, OSError):
            recipe.screenshot(
                f"{os.environ['E2E_ARTIFACTS']}/parabolic-coverage-{phase}.png",
                "include-cursor",
                "b",
                "false",
            )


def _assert_covered(tasks_view: int, phase: str, message: str) -> None:
    try:
        _ = recipe.assert_applets_covered_by_background(tasks_view)
    except recipe.RecipeError as exc:
        print(str(exc), file=sys.stderr, flush=True)
        _capture_coverage_failure(phase)
        recipe.fail(message)


def _cleanup_konsole() -> None:
    #! previews need a window-owning task; fresh vehicles carry only launchers,
    #! so a konsole launched here (never a pre-existing one) is torn down.
    if _S.konsole is not None:
        with contextlib.suppress(OSError):
            _S.konsole.terminate()
        with contextlib.suppress(Exception):
            _ = _S.konsole.wait()
        _S.konsole = None


def _body() -> None:
    #! previews need a window-owning task; fresh vehicles carry only launchers.
    #! konsole is part of the pinned environment (the vehicle proof's client).
    if not _konsole_mapped():
        _S.konsole = subprocess.Popen(
            ["konsole"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            start_new_session=True,
        )
        for _ in range(30):
            if _konsole_mapped():
                break
            time.sleep(1)
        if not _konsole_mapped():
            recipe.fail("konsole never mapped in the session")

    #! let the dock settle after konsole's task appears: adding a window task
    #! reflows the row and runs the zoom-in animation, and a glide driven while
    #! that animation is live hits the hoverEnabled gate far more often (the
    #! trigger race is worst on a not-yet-settled dock, e.g. right after the
    #! suite's preceding recipe restarted it - see the header)
    with contextlib.redirect_stderr(io.StringIO()):
        _ = recipe.wait_settled(30)
    time.sleep(3)

    #! geometry comes from viewsData, not the window dump: dock WINDOWS are
    #! larger than the visible strip (shadow/free space) and several views can
    #! tie on width, so window-rect picking is ambiguous - the view's
    #! absoluteGeometry is the strip itself
    try:
        tasks_view = recipe.tasks_view()
    except recipe.RecipeError:
        recipe.fail("no tasks view")
    dx, _dy, dw, _dh = recipe.view(tasks_view).absolute_geometry

    _assert_covered(
        tasks_view, "rest", "the resting applet row escapes its background or output canvas"
    )

    #! the glide must END on a window-owning task or no preview can appear;
    #! resolve the konsole icon's rest position before the pointer distorts
    #! anything (e2e_task_center is only honest with the pointer outside)
    try:
        konx, kony = recipe.task_center(tasks_view, "org.kde.konsole.desktop")
    except recipe.RecipeError:
        recipe.fail("could not locate the konsole task icon")

    #! hover line: the icon centers, NOT the strip's outer rows - the last
    #! few pixels at the screen edge are the edge margin (empty-area input),
    #! and a hover there never reaches the task items
    hovery = kony

    #! start the glide a few icon slots to the side of konsole that has the most
    #! room, so it always CROSSES konsole's boundary (a glide that begins on the
    #! icon never emits the onEntered the preview needs)
    span = 180
    if konx - dx > dx + dw - konx:
        glidestart = konx - span
        step = 16
    else:
        glidestart = konx + span
        step = -16

    #! ~60% of single glides land the onEntered (the animation race in the
    #! header); the failure probability is 0.4^attempts, so 12 attempts is
    #! ~1.7e-5 - a decisive margin without dozens of gestures
    max_attempts = 12
    mapped = False
    for _ in range(max_attempts):
        #! reset between attempts: leave the dock and let the zoom fully restore
        #! to rest before the next glide - a glide started while the previous
        #! attempt's restore animation is still running crosses the boundary
        #! mid-animation and misses (the hoverEnabled gate); 0.8s covers the
        #! restore even on a sluggish just-restarted dock
        _fakepointer("move", konx, hovery - 200)
        time.sleep(0.8)
        _fakepointer("move", glidestart, hovery)
        time.sleep(0.2)
        x = glidestart
        while (step > 0 and x < konx) or (step < 0 and x > konx):
            _fakepointer("move", x, hovery)
            x += step
        #! finish over the konsole task so the preview delay elapses on it
        _fakepointer("move", konx, hovery)
        time.sleep(1.1)  #! previewsDelay (throwaway default 650ms) + build + margin
        if _PREVIEW_RE.search(recipe.dumpwins()):
            _assert_covered(
                tasks_view,
                "hover",
                "the hovered applet row escapes its background or output canvas",
            )
            mapped = True
            break

    #! leave the dock so zoom restores and the preview hides
    _fakepointer("move", konx, hovery - 400)
    time.sleep(1.2)

    #! Auto-hidden views have no painted background after the pointer leaves.
    #! Always-visible fixtures still provide the final rest leg, which catches a
    #! stale hover generation that fails to restore the composition.
    if not recipe.view(tasks_view).is_hidden:
        _assert_covered(
            tasks_view,
            "restored",
            "the restored applet row escapes its background or output canvas",
        )

    if mapped:
        print("parabolic glide engaged; preview dialog mapped (layer=6)")
        return
    recipe.fail(
        f"no preview dialog mapped after gliding onto the konsole task ({max_attempts} attempts)"
    )


def _cleanup(status: int) -> int:
    """Tear down the recipe-launched konsole on every exit path; the body's status
    stands (this teardown has nothing that can fail the run)."""
    _cleanup_konsole()
    return status


def main() -> None:
    # run_with_cleanup owns the install-signals / try-body / finally-cleanup shape
    # this recipe hand-rolled; _cleanup runs on every exit path (the bash trap).
    recipe.run_with_cleanup(_body, _cleanup)


if __name__ == "__main__":
    main()
