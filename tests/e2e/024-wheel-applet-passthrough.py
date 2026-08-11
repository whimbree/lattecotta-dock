#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""Phase 8 wheel-routing pin (the per-applet-type wheel-event bypass plan
item): an external applet that consumes wheel events receives them AHEAD of
the containment-level scroll action, and the containment action still fires on
empty dock area - the routing both ways.

The routing needs no bypass list because it is structural: the containment
wheel handler is the EnvironmentActions MouseArea declared FIRST in
LayoutsContainer (bottom of the sibling stacking order), with every
AppletsContainer above it. Qt Quick delivers a wheel event to the topmost
item under the cursor that accepts it, so a wheel-consuming applet takes its
own events and only unconsumed wheels fall through to the containment action.
The C++ View::event wheel case emits the wheelScrolled signal for the
applet-expand feature and passes the event on to normal scene delivery; it
never consumes (outside the floating-gap projection). Qt5 Latte had the
identical arrangement (EnvironmentActions first, applets above, same
handler), so this is Qt5-faithful, not a divergence. This recipe pins the
arrangement against a regression such as raising the EnvironmentActions
z-order, which would silently break every wheel-consuming applet.

Drive: seed org.kde.plasma.icontasks (the stock task manager) onto the dock
via the coarse addApplet action, opt its wheel in with wheelEnabled=AllTask
(Plasma 6.6 demoted the old always-on Bool to an Enum defaulting to None, so
the stock task manager consumes no wheel until configured), set scrollAction=4
(ScrollToggleMinimized) so the applet effect (activation cycles) and the
containment effect (active window minimizes) are disjoint observables, and
spawn two ungrouped konsole fixtures. A wheel over empty dock area must
minimize the active fixture (containment action fires); a wheel over the
icontasks task buttons must cycle activation to the other fixture with nobody
minimized (the applet consumed the wheel; the swallow case is detected as its
own distinct failure). KWin window state is the independent witness for both
legs, same as SC-WT1 (022-empty-area-window-actions). Bring-up also observed
the third routing case: a wheel INSIDE the applet's cell at a point the applet
declines (its launcher strip, or any point before the AllTask opt-in) falls
through to the containment action, which is the same fall-through Qt5
delivered.
"""

from __future__ import annotations

import contextlib
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass, field
from pathlib import Path

from latte_harness import proc, recipe

_APPLET_PLUGIN = "org.kde.plasma.icontasks"
_TITLE_A = "LATTE WHEELPT A"
_TITLE_B = "LATTE WHEELPT B"


@dataclass
class _State:
    view: int = 0
    layout: str = ""
    backup: str = ""
    applet_id: int = 0
    fixtures: dict[str, subprocess.Popen[bytes]] = field(default_factory=dict)
    ids: dict[str, str] = field(default_factory=dict)
    recipe_finalized: bool = False


_S = _State()


def _inject(label: str, *args: str) -> None:
    rc = recipe.fakepointer(*args)
    if rc != 0:
        recipe.fail(f"{label}: fakepointer '{' '.join(args)}' failed with status {rc}")


def _fixture_states() -> dict[str, tuple[str, str]]:
    """Read every WHEELPT konsole fixture from KWin: title -> (minimized, active)."""
    js = (
        "for (const w of workspace.windowList()) {\n"
        "    if (w.resourceClass === 'org.kde.konsole' && w.caption.includes('LATTE WHEELPT')) {\n"
        "        const t = w.caption.includes('" + _TITLE_A + "') ? 'A' : 'B';\n"
        "        print('@TAG@|' + t + '|' + w.minimized + '|' + (workspace.activeWindow === w));\n"
        "    }\n"
        "}"
    )
    states: dict[str, tuple[str, str]] = {}
    for line in recipe.kwin_js(js).splitlines():
        #! kwin_js returns the tagged lines with the @TAG@| prefix stripped
        title, minimized, active = line.split("|")
        states[title] = (minimized, active)
    return states


def _activate_fixture(title: str, label: str) -> None:
    js = (
        "for (const w of workspace.windowList()) {\n"
        "    if (w.resourceClass === 'org.kde.konsole' && w.caption.includes('" + title + "')) {\n"
        "        w.minimized = false;\n"
        "        workspace.activeWindow = w;\n"
        "        print('@TAG@|' + w.internalId);\n"
        "    }\n"
        "}"
    )
    result = recipe.kwin_js(js)
    if not result or "\n" in result:
        recipe.fail(f"{label}: KWin did not identify exactly one '{title}' fixture")


def _wait_states(expected: dict[str, tuple[str, str]], label: str) -> None:
    states: dict[str, tuple[str, str]] = {}
    for _ in range(40):
        states = _fixture_states()
        if states == expected:
            return
        time.sleep(0.25)
    recipe.fail(f"{label}: fixture states settled at {states}; expected {expected}")


def _spawn_fixture(title: str) -> None:
    process = subprocess.Popen(
        ["konsole", "-p", f"LocalTabTitleFormat={title}"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        start_new_session=True,
    )
    _S.fixtures[title] = process
    for _ in range(40):
        if title[-1] in _fixture_states():
            return
        time.sleep(0.25)
    recipe.fail(f"{title}: fixture window never mapped")


def _applet_point(view: int, applet_id: int, from_right: int) -> tuple[int, int]:
    """A point inside the applet's cell, ``from_right`` pixels left of its right
    edge, drift-corrected like the empty-strip math the wheel recipes share.
    icontasks renders its launchers left and its task buttons right-aligned
    (read from the aim screenshot during bring-up), and the stock task manager
    consumes wheel over task delegates, not over launchers, so the
    receives-its-own-wheel leg aims from the right edge."""
    winx = recipe.view_window_x(view)
    target = recipe.view(view)
    ax, ay, _, ah = target.absolute_geometry
    lx = target.local_geometry[0]
    ox = winx if winx is not None else ax - lx
    applet = next((a for a in recipe.view_applets(view) if a.id == applet_id), None)
    if applet is None:
        recipe.fail(f"applet {applet_id} vanished from viewAppletsData")
    gx, _, gw, _ = applet.geometry
    if gw < from_right + 8:
        recipe.fail(
            f"applet {applet_id} is only {gw}px wide; cannot aim {from_right}px from its right edge"
        )
    return int(ox + gx + gw - from_right), int(ay + ah / 2)


def _empty_area_point(view: int) -> tuple[int, int]:
    """The widest applet-free strip's midpoint (the 010/022 shared math)."""
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
    if best[1] - best[0] < 8:
        recipe.fail(f"widest empty-area gap is under 8px: {gaps}")
    return int((best[0] + best[1]) / 2), int(ay + ah / 2)


def _settle_and_scroll(px: int, py: int, label: str) -> None:
    """Settle the pointer outside then on the target before wheeling: an axis
    event racing its own enter never reaches the QML scene (the 010 dance)."""
    _inject(f"{label}: settle outside", "move", str(px), "500")
    time.sleep(0.3)
    _inject(f"{label}: settle on target", "move", str(px), str(py))
    time.sleep(0.8)
    _inject(f"{label}: wheel", "scroll", str(px), str(py), "-1", "0")


def _drive_empty_area_minimize() -> None:
    """Leg B: a wheel on empty dock area fires the containment ToggleMinimized
    action against the active fixture."""
    for attempt in (1, 2, 3, 4):
        px, py = _empty_area_point(_S.view)
        _settle_and_scroll(px, py, f"empty-area attempt {attempt}")
        for _ in range(8):
            states = _fixture_states()
            if states.get("B", ("", ""))[0] == "true":
                print("ok: empty-area wheel minimized the active fixture (containment fired)")
                return
            time.sleep(0.25)
        print(f"  (empty-area wheel not delivered on attempt {attempt}, retrying)")
    recipe.fail("empty-area wheel never fired the containment ToggleMinimized action")


def _drive_applet_wheel_cycles() -> None:
    """Leg A: a wheel over the icontasks applet cycles activation to the other
    fixture and minimizes nothing; a minimize here means the containment
    swallowed the applet's wheel and is its own distinct failure."""
    #! the two task buttons sit at the cell's right edge; alternate between
    #! their two expected centers across the delivery retries
    for attempt, from_right in ((1, 36), (2, 108), (3, 36), (4, 108)):
        px, py = _applet_point(_S.view, _S.applet_id, from_right)
        print(f"applet-wheel attempt {attempt}: {from_right}px from the right edge at ({px},{py})")
        _settle_and_scroll(px, py, f"applet-wheel attempt {attempt}")
        for _ in range(8):
            states = _fixture_states()
            a_min, a_act = states.get("A", ("", ""))
            b_min = states.get("B", ("", ""))[0]
            if a_min == "true" or b_min == "true":
                recipe.fail(
                    "containment ToggleMinimized swallowed the wheel aimed at the "
                    f"icontasks applet (states {states}); the applet never received it"
                )
            if a_act == "true":
                print(
                    "ok: applet wheel cycled activation B -> A with nobody minimized "
                    "(the applet consumed its own wheel event)"
                )
                return
            time.sleep(0.25)
        print(f"  (applet wheel not delivered on attempt {attempt}, retrying)")
    recipe.fail("applet wheel never observed: neither a cycle nor a swallow after 4 attempts")


def _add_icontasks_applet() -> None:
    before = {a.id for a in recipe.view_applets(_S.view)}
    recipe.call("addApplet", "us", str(_S.view), _APPLET_PLUGIN)
    for _ in range(30):
        time.sleep(1)
        added = [
            a
            for a in recipe.view_applets(_S.view)
            if a.id not in before and a.plugin == _APPLET_PLUGIN
        ]
        if added:
            _S.applet_id = added[0].id
            print(f"icontasks applet added as id {_S.applet_id}")
            return
    recipe.fail(f"addApplet never materialized a {_APPLET_PLUGIN} applet on view {_S.view}")


def _configure_wheel_mode() -> None:
    """scrollAction=4 (ScrollToggleMinimized) with the drag/close features off,
    written with the dock stopped, then verified via the readback."""
    if not recipe.dock_stop():
        recipe.fail("could not stop the dock for the wheel-mode config")
    general = ("--group", "Containments", "--group", str(_S.view), "--group", "General")
    for key, value in (
        ("scrollAction", "4"),
        ("dragActiveWindowEnabled", "false"),
        ("closeActiveWindowEnabled", "false"),
        #! parabolic zoom off: the pin is about ROUTING, and a static row makes
        #! the applet-center aim exact instead of zoom-shifted
        ("zoomLevel", "0"),
    ):
        if recipe.kwriteconfig("--file", _S.layout, *general, "--key", key, "--", value) != 0:
            recipe.fail(f"could not write {key}={value}")
    _start_dock_always_visible("wheel mode")
    cfg = recipe.read_json("viewConfigData", "u", str(_S.view))["config"]
    if cfg["scrollAction"] != 4:
        recipe.fail(f"scrollAction readback is {cfg['scrollAction']}, expected 4")


def _start_dock_always_visible(label: str) -> None:
    if not recipe.dock_start(90):
        recipe.fail(f"{label}: dock did not start")
    recipe.call_or_fail(
        f"{label}: could not select alwaysVisible",
        "setViewVisibilityMode",
        "us",
        str(_S.view),
        "alwaysVisible",
    )


def _configure_icontasks() -> None:
    """Ungroup the seeded icontasks so two konsole windows stay two task
    buttons and a single wheel step lands on the other window, and OPT IN to
    wheel consumption: Plasma 6.6 made taskmanager's wheelEnabled an Enum
    (None/AllTask/TaskOnly) defaulting to None, so the stock task manager
    consumes no wheel at all until AllTask is chosen (verified against
    applets/taskmanager/main.xml at v6.6.5). Written with the dock stopped
    like every layout mutation."""
    if not recipe.dock_stop():
        recipe.fail("could not stop the dock for the icontasks config")
    applet_general = (
        "--group",
        "Containments",
        "--group",
        str(_S.view),
        "--group",
        "Applets",
        "--group",
        str(_S.applet_id),
        "--group",
        "Configuration",
        "--group",
        "General",
    )
    for key, value in (("groupingStrategy", "0"), ("wheelEnabled", "AllTask")):
        args = ("--file", _S.layout, *applet_general, "--key", key, "--", value)
        if recipe.kwriteconfig(*args) != 0:
            recipe.fail(f"could not write icontasks {key}={value}")
    _start_dock_always_visible("icontasks config")


def _terminate_fixtures(label: str) -> bool:
    ok = True
    for title, process in list(_S.fixtures.items()):
        if recipe.pid_alive(process.pid):
            with contextlib.suppress(ProcessLookupError):
                os.kill(process.pid, signal.SIGTERM)
        with contextlib.suppress(Exception):
            process.wait(timeout=10)
        if recipe.pid_alive(process.pid):
            print(
                f"FAIL: {label}: fixture '{title}' survived SIGTERM",
                file=sys.stderr,
                flush=True,
            )
            ok = False
    _S.fixtures.clear()
    return ok


def _body() -> None:
    #! leg B first, on the plain seed: the auto-sized dock has its proven empty
    #! strips only BEFORE icontasks joins the row (a fill-expanding applet
    #! swallows them; observed live in the recipe's bring-up)
    _configure_wheel_mode()

    _spawn_fixture(_TITLE_A)
    _spawn_fixture(_TITLE_B)
    _activate_fixture(_TITLE_A, "arrange A")
    _activate_fixture(_TITLE_B, "arrange B")
    _wait_states({"A": ("false", "false"), "B": ("false", "true")}, "fixture arrangement")

    _drive_empty_area_minimize()

    #! leg A: seed icontasks, ungroup it, restore the minimized fixture and
    #! re-arm the arrangement
    _activate_fixture(_TITLE_B, "restore B")
    _add_icontasks_applet()
    _configure_icontasks()
    _activate_fixture(_TITLE_B, "re-arm B after restart")
    _wait_states({"A": ("false", "false"), "B": ("false", "true")}, "leg A arrangement")

    _drive_applet_wheel_cycles()

    #! finalize: stop the dock and put the layout back before the verdict
    if not _terminate_fixtures("finalization"):
        recipe.fail("finalization could not terminate the konsole fixtures")
    if not recipe.dock_stop():
        recipe.fail("finalization could not stop the dock")
    shutil.copyfile(_S.backup, _S.layout)
    if Path(_S.backup).read_bytes() != Path(_S.layout).read_bytes():
        recipe.fail("finalization restored different layout bytes")
    _S.recipe_finalized = True
    print("PASS: wheel routing pinned both ways (applet consumes, empty area falls through)")


def _cleanup(status: int) -> int:
    cleanup_failed = False
    if not _S.recipe_finalized:
        if not _terminate_fixtures("cleanup"):
            cleanup_failed = True
        pid = recipe.dock_pid()
        if pid is not None and recipe.pid_alive(pid):
            with recipe.muted_stderr():
                if not recipe.dock_stop():
                    print(
                        f"FAIL: cleanup could not stop dock pid {pid}",
                        file=sys.stderr,
                        flush=True,
                    )
                    cleanup_failed = True
        try:
            shutil.copyfile(_S.backup, _S.layout)
            restored = Path(_S.backup).read_bytes() == Path(_S.layout).read_bytes()
        except OSError:
            restored = False
        if not restored:
            print(
                f"FAIL: cleanup could not restore layout {_S.layout}",
                file=sys.stderr,
                flush=True,
            )
            cleanup_failed = True
    try:
        os.unlink(_S.backup)
    except OSError:
        print(f"FAIL: cleanup could not remove {_S.backup}", file=sys.stderr, flush=True)
        cleanup_failed = True
    return recipe.worsen_status_on_cleanup_failure(status, cleanup_failed)


def main() -> None:
    proc.install_conventional_signal_exits()
    try:
        _S.view = recipe.tasks_view()
    except recipe.RecipeError:
        recipe.fail("no tasks view")
    _S.layout = os.environ["E2E_LAYOUT"]
    _S.backup = tempfile.mkstemp()[1]
    shutil.copyfile(_S.layout, _S.backup)
    recipe.run_with_cleanup(_body, _cleanup, install_signal_exits=False)


if __name__ == "__main__":
    main()
