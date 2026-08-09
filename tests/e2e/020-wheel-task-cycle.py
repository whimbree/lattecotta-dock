#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""EX-15 live check 3 (docs/agent-logs/EX-15.md): wheel over a task icon with
taskScrollAction enabled cycles the task's windows - one activation per
detent-sized event past the threshold (the TaskMouseArea cutover through
LatteCore.WheelStepper, DominantAxis pick, threshold 96). Two konsole windows
group under one icon; each detent must hand activation to the OTHER window,
wrapping A -> B -> A, asserted on KWin's activeWindow. No config flip:
taskScrollAction defaults to ScrollTasks.
"""

import contextlib
import io
import os
import subprocess
import time
from collections.abc import Iterator

from latte_harness import proc, recipe


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


def _konsole_windows() -> int:
    return sum("|org.kde.konsole|" in line for line in recipe.dumpwins().splitlines())


def _active_window() -> str:
    lines = recipe.kwin_js(
        'print("@TAG@|" + (workspace.activeWindow ? workspace.activeWindow.internalId : "none"));'
    ).splitlines()
    return lines[-1] if lines else ""


def main() -> None:
    try:
        view = recipe.tasks_view()
    except recipe.RecipeError:
        recipe.fail("no tasks view")

    #! the default-config premise, checked instead of assumed: an explicit
    #! ScrollNone in the base config would make every wheel a silent no-op
    tasks_applet = next(
        a.id for a in recipe.view_applets(view) if a.plugin == "org.kde.latte.plasmoid"
    )
    applet_general = (
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
    tsa = _kread(*applet_general, "--key", "taskScrollAction")
    if not (tsa == "" or tsa == "1"):
        recipe.fail(
            f"base config sets taskScrollAction={tsa}; this recipe expects the ScrollTasks default"
        )

    #! hover previews are OFF for this recipe: the icon dwell before each detent
    #! exceeds previewsDelay, and the preview dialog mapping right at the wheel
    #! moment disturbs the nested compositor's pointer focus the same way the
    #! desktop switch does in 010 (dropped events). Previews have their own
    #! recipes (parabolic-hover-preview, 040); this one is about the wheel.
    orig_hover = _kread(*applet_general, "--key", "hoverAction")
    if not recipe.dock_stop():
        recipe.fail("could not stop the dock for the hover flip")
    _kwrite(*applet_general, "--key", "hoverAction", "0")

    def restore_config() -> None:
        with _muted_stderr():
            recipe.dock_stop()
        if orig_hover:
            _kwrite(*applet_general, "--key", "hoverAction", orig_hover)
        else:
            _kwrite(*applet_general, "--key", "hoverAction", "--delete")

    if not recipe.dock_start():
        recipe.fail("dock did not come back after the hover flip")

    konsoles: list[proc.SessionProcess] = []

    def cleanup() -> None:
        for konsole in konsoles:
            with contextlib.suppress(Exception):
                konsole.terminate_group()
        restore_config()

    try:
        if _konsole_windows() != 0:
            recipe.fail("konsole windows already present; this recipe owns its clients")
        for _ in range(2):
            konsoles.append(
                proc.SessionProcess.spawn(
                    ["konsole"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
                )
            )
            time.sleep(2)
        for _ in range(20):
            if _konsole_windows() >= 2:
                break
            time.sleep(1)
        if _konsole_windows() < 2:
            recipe.fail("two konsole windows never mapped")
        time.sleep(2)  #! let the grouped task settle into the bar

        rows = recipe.view_tasks(view)
        konsole_rows = [x for x in rows if x.app_id == "org.kde.konsole.desktop"]
        grouped = (
            bool(konsole_rows[0].is_grouped and konsole_rows[0].child_count == 2)
            if konsole_rows
            else False
        )
        if not grouped:
            recipe.fail("konsole task did not group two windows")

        try:
            kx, ky = recipe.task_center(view, "org.kde.konsole.desktop")
        except recipe.RecipeError:
            recipe.fail("could not locate the konsole task icon")

        #! settle the pointer on the icon from outside (see 010's header: an axis
        #! event racing its own enter is dropped by the nested compositor)
        _fakepointer("move", str(kx), "500")
        time.sleep(0.3)
        _fakepointer("move", str(kx), str(ky))
        time.sleep(0.8)

        a = _active_window()
        if not (a and a != "none"):
            recipe.fail("no active window to cycle from")

        #! detents spaced past TaskMouseArea's 200ms scrollDelayer block, so each
        #! event is entitled to exactly one cycle step
        _fakepointer("scroll", str(kx), str(ky), "-1", "100")
        time.sleep(0.8)
        b = _active_window()
        if not (b and b != a):
            recipe.fail(f"first detent did not activate the other window (still {a})")

        _fakepointer("scroll", str(kx), str(ky), "-1", "100")
        time.sleep(0.8)
        c = _active_window()
        if c != a:
            recipe.fail(f"second detent did not wrap back (got {c}, expected {a})")

        print(f"task wheel cycled A -> B -> A over the grouped konsole icon ({a} / {b})")
    finally:
        cleanup()


if __name__ == "__main__":
    recipe.run(main)
