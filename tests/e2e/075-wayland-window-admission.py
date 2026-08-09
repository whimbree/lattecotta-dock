#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""Prove that Dodge Active follows current KWin admission state instead of a
cached tracker row. One persistent Wayland toplevel overlaps a left dock,
becomes taskbar-and-switcher-skipped without being unmapped, becomes eligible
again under the same KWin identity, then disappears while a coalesced
eligibility refresh is pending.

Ported from tests/e2e/075-wayland-window-admission.sh to latte_harness.recipe
and latte_harness.matrix (BP-3, the bash-to-python migration's window-touch
recipe batch R6). dockSystemData carries the whole window-touch surface (dozens
of fields none of the typed models model), so the snapshot is read as raw JSON
via recipe.read_json at the same boundary the bash python one-liners used; a
refused reply raises the pollable DbusUnavailableError, exactly the
empty-command-substitution channel every bash poller swallowed. trackerData
reads the same way. The coarse setViewVisibilityMode action stays a busctl call
that fails loudly on a D-Bus error, matching the bash `e2e_call ... || e2e_fail`.
"""

from __future__ import annotations

import os
import shutil
import signal
import subprocess
import sys
import time
from contextlib import redirect_stderr, redirect_stdout, suppress
from io import StringIO
from typing import Any

from latte_harness import matrix, recipe

_CLIENT_TITLE = "LATTE D264 WINDOW ADMISSION"
_KONSOLE_MATCH = f"|org.kde.konsole|{_CLIENT_TITLE}"


class _State:
    def __init__(self) -> None:
        self.konsole: subprocess.Popen[bytes] | None = None
        self.configured = False


def _dock_record(view: int) -> dict[str, Any]:
    """dock_field's context: the single dockSystemData record for ``view``.

    Mirrors the bash python one-liner (schema-11 guard, exactly-one guard); a
    refused/failed reply raises recipe.read_json's pollable
    DbusUnavailableError, the same transient non-answer every bash poller read
    as an empty command substitution.
    """
    snapshot = recipe.read_json("dockSystemData")
    if snapshot["schemaVersion"] != 11:
        raise recipe.RecipeError("expected dockSystemData schema 11")
    matches = [record for record in snapshot["views"] if record["persistentDockId"] == view]
    if len(matches) != 1:
        raise recipe.RecipeError(f"expected exactly one dockSystemData record for view {view}")
    return matches[0]


def _lower(value: bool) -> str:
    return "true" if value else "false"


def _tracker_probe(view: int) -> tuple[str, str, str, str, str]:
    tracker = recipe.read_json("trackerData", "u", str(view))
    return (
        _lower(tracker["activeWindowTouching"]),
        _lower(tracker["activeWindowTouchingEdge"]),
        _lower(tracker["existsWindowTouching"]),
        _lower(tracker["existsWindowTouchingEdge"]),
        _lower(tracker["existsWindowActive"]),
    )


def _wait_for_dodge_state(
    view: int, expected_touching: str, expected_hidden: str, phase: str
) -> None:
    active_touching = active_edge = "unread"
    exists_touching = exists_edge = exists_active = "unread"
    hidden = "unread"
    for _ in range(160):
        try:
            active_touching, active_edge, exists_touching, exists_edge, exists_active = (
                _tracker_probe(view)
            )
            hidden = _lower(_dock_record(view)["isHidden"])
        except recipe.RecipeError:
            time.sleep(0.05)
            continue
        if (
            active_touching == expected_touching
            and exists_touching == expected_touching
            and exists_active == expected_touching
            and hidden == expected_hidden
        ):
            return
        time.sleep(0.05)
    recipe.fail(
        f"{phase} did not settle (activeTouching={active_touching} activeEdge={active_edge} "
        f"existsTouching={exists_touching} existsEdge={exists_edge} "
        f"existsActive={exists_active} hidden={hidden})"
    )


def _konsole_count() -> int:
    return sum(1 for line in recipe.dumpwins().splitlines() if _KONSOLE_MATCH in line)


def _kwin_tagged_last(body: str, collection_delay: float = 0.5) -> str:
    lines = recipe.kwin_js(body, collection_delay).splitlines()
    return lines[-1] if lines else ""


def _set_konsole_geometry(x: int, y: int, width: int, height: int) -> str:
    return _kwin_tagged_last(
        f"""for (const window of workspace.windowList()) {{
        if (window.resourceClass === 'org.kde.konsole'
                && window.caption.includes('{_CLIENT_TITLE}')) {{
            const geometry = Object.assign({{}}, window.frameGeometry);
            geometry.x = {x};
            geometry.y = {y};
            geometry.width = {width};
            geometry.height = {height};
            window.frameGeometry = geometry;
            workspace.activeWindow = window;
            print('@TAG@|' + window.internalId);
        }}
    }}"""
    )


def _set_konsole_admission(accepted: bool) -> str:
    skipped = "false" if accepted else "true"
    return _kwin_tagged_last(
        f"""for (const window of workspace.windowList()) {{
        if (window.resourceClass === 'org.kde.konsole'
                && window.caption.includes('{_CLIENT_TITLE}')) {{
            window.skipTaskbar = {skipped};
            window.skipSwitcher = {skipped};
            workspace.activeWindow = window;
            print('@TAG@|' + window.internalId + '|'
                + window.skipTaskbar + '|' + window.skipSwitcher);
        }}
    }}""",
        0.05,
    )


def _konsole_identity() -> str:
    return _kwin_tagged_last(
        f"""for (const window of workspace.windowList()) {{
        if (window.resourceClass === 'org.kde.konsole'
                && window.caption.includes('{_CLIENT_TITLE}')) {{
            print('@TAG@|' + window.internalId + '|'
                + window.skipTaskbar + '|' + window.skipSwitcher);
        }}
    }}""",
        0.01,
    )


def _muted_dock_start(timeout: int) -> bool:
    with redirect_stdout(StringIO()), redirect_stderr(StringIO()):
        return recipe.dock_start(timeout)


def _body(state: _State, view_box: list[int]) -> None:
    if matrix.init() != 0:
        recipe.fail("could not capture the pristine nested configuration")
    state.configured = True
    if matrix.stage("dock-left-center-1out") != 0:
        recipe.fail("could not realize the D264 left-dock fixture")
    try:
        view = matrix.view_id()
    except matrix.MatrixProbeError:
        recipe.fail("could not resolve the D264 left-dock fixture")
    view_box[0] = view

    recipe.call_or_fail(
        "could not set the left dock to Dodge Active",
        "setViewVisibilityMode",
        "us",
        str(view),
        "dodgeActive",
    )
    for _ in range(40):
        with suppress(recipe.RecipeError):
            if _dock_record(view)["visibilityMode"] == "dodgeActive":
                break
        time.sleep(0.05)
    try:
        entered = _dock_record(view)["visibilityMode"] == "dodgeActive"
    except recipe.RecipeError:
        entered = False
    if not entered:
        recipe.fail("left dock did not enter Dodge Active")

    record = _dock_record(view)
    screen_x, screen_y, screen_width, screen_height = record["screenGeometry"]
    _, dock_y, _, dock_height = record["absoluteGeometry"]

    if (
        recipe.fakepointer(
            "move",
            str(screen_x + screen_width - 20),
            str(screen_y + screen_height // 2),
        )
        != 0
    ):
        recipe.fail("could not park the nested pointer away from the left dock")
    _wait_for_dodge_state(view, "false", "false", "initial no-window control")

    if _konsole_count() != 0:
        recipe.fail("a tagged Konsole already exists; this recipe owns one client")
    state.konsole = subprocess.Popen(
        ["konsole", "-p", f"LocalTabTitleFormat={_CLIENT_TITLE}"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        start_new_session=True,
    )
    for _ in range(40):
        if _konsole_count() == 1:
            break
        time.sleep(0.25)
    if _konsole_count() != 1:
        recipe.fail("the D264 Wayland client never mapped")

    client_width = screen_width * 2 // 3
    client_height = screen_height * 2 // 3
    client_x = screen_x
    client_y = dock_y + dock_height // 2 - client_height // 2
    if client_y < screen_y:
        client_y = screen_y
    if client_y + client_height > screen_y + screen_height:
        client_y = screen_y + screen_height - client_height

    fixture_id = _set_konsole_geometry(client_x, client_y, client_width, client_height)
    if not fixture_id or "\n" in fixture_id:
        recipe.fail("KWin returned an invalid D264 client identity")
    _wait_for_dodge_state(view, "true", "true", "accepted overlapping window")

    rejected = _set_konsole_admission(False)
    if rejected != f"{fixture_id}|true|true":
        recipe.fail(
            f"ineligible transition changed identity or flags "
            f"(expected={fixture_id}|true|true actual={rejected})"
        )
    _wait_for_dodge_state(view, "false", "false", "same-window rejection")
    if _konsole_identity() != f"{fixture_id}|true|true":
        recipe.fail("the rejected client was unmapped or replaced")

    accepted = _set_konsole_admission(True)
    if accepted != f"{fixture_id}|false|false":
        recipe.fail(
            f"re-admission changed identity or flags "
            f"(expected={fixture_id}|false|false actual={accepted})"
        )
    _wait_for_dodge_state(view, "true", "true", "same-window re-admission")

    # Queue a coalesced invalidation and immediately destroy the client. A stale
    # timer must not recreate either tracker touch state or hidden presentation.
    _ = _set_konsole_admission(False)
    try:
        os.kill(state.konsole.pid, signal.SIGTERM)
    except ProcessLookupError:
        recipe.fail("could not destroy the D264 client")
    with suppress(Exception):
        state.konsole.wait()
    state.konsole = None
    for _ in range(80):
        if _konsole_count() == 0:
            break
        time.sleep(0.05)
    if _konsole_count() != 0:
        recipe.fail("the D264 client survived destruction")
    _wait_for_dodge_state(view, "false", "false", "destruction during pending refresh")
    time.sleep(0.25)
    _wait_for_dodge_state(view, "false", "false", "post-debounce convergence")

    print(
        "PASS: Dodge Active rejected and re-admitted one persistent KWin window, "
        "then discarded its pending update on destruction"
    )


def _cleanup(state: _State) -> bool:
    cleanup_failed = False
    if state.konsole is not None:
        with suppress(ProcessLookupError):
            os.kill(state.konsole.pid, signal.SIGTERM)
        with suppress(Exception):
            state.konsole.wait()
    if state.configured:
        if not matrix.stop_dock():
            cleanup_failed = True
        config_home = os.environ["E2E_CONFIG_HOME"]
        if not config_home:
            raise recipe.RecipeError("E2E_CONFIG_HOME is unset")
        shutil.rmtree(config_home, ignore_errors=True)
        try:
            _ = shutil.copytree(matrix.pristine_seed_dir(), config_home)
        except OSError:
            cleanup_failed = True
        pid = recipe.dock_pid()
        if (pid is not None and recipe.pid_alive(pid)) or not _muted_dock_start(90):
            cleanup_failed = True
    if cleanup_failed:
        print(
            "FAIL: D264 window-admission cleanup did not restore the dock configuration",
            file=sys.stderr,
            flush=True,
        )
    return cleanup_failed


def main() -> int:
    state = _State()
    view_box = [0]
    status = 0
    try:
        _body(state, view_box)
    except SystemExit as exc:
        status = exc.code if isinstance(exc.code, int) else 1
    except recipe.RecipeError as exc:
        print(str(exc), file=sys.stderr, flush=True)
        status = 1
    if _cleanup(state) and status == 0:
        status = 1
    return status


if __name__ == "__main__":
    sys.exit(main())
