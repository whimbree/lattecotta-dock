#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""SC-W1 (the regression guard for D56, pure-launcher task wheel uses inherited
asymmetric activation): real fakepointer wheel input reaches TaskMouseArea.
Positive activation can only continue through TaskItem.activateLauncher() to
TasksModel.requestActivate; process, KWin, and task-model effects distinguish
it from the accepted negative, ScrollNone, threshold, and rate-limit no-ops.

Ported from tests/e2e/021-launcher-wheel.sh to latte_harness.recipe (BP-3, the
bash-to-python migration's input/wheel recipe batch R8). The launched-app
fixtures (fixtures/sc-w1/launcher.sh, rate-launcher.sh) stay bash (they are tiny
shell executables the point of which is to BE launched, retained by the plan);
they are staged with the same @BASH@/@LAUNCHER@ substitution and reach the dock
through XDG_DATA_HOME + kbuildsycoca6 exactly as the bash did. The task-model,
window and config readbacks ride recipe.py's typed boundary or the same raw JSON
one-liners the bash used; every assertion, poll bound, retry count and failure
message is byte-identical, the SPDX header is preserved, and the exec bit stays
100755 (D273).
"""

import contextlib
import io
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from collections.abc import Iterator
from pathlib import Path

from latte_harness import recipe

DESKTOP_ID = "org.kde.latte.sc-w1.desktop"
RATE_DESKTOP_ID = "org.kde.latte.sc-w1-rate.desktop"
WINDOW_TITLE = "latte-sc-w1-launcher"


def _fakepointer(*args: str) -> None:
    subprocess.run([os.environ["E2E_FAKEPOINTER"], *args], check=False)


def _kwrite(*args: str) -> None:
    subprocess.run(["kwriteconfig6", "--file", os.environ["E2E_LAYOUT"], *args], check=False)


@contextlib.contextmanager
def _muted_stderr() -> Iterator[None]:
    """The cleanup dock stop's `>/dev/null 2>&1`: keep its diagnostics off the recipe output."""
    with contextlib.redirect_stderr(io.StringIO()):
        yield


def _pid_alive(pid: int) -> bool:
    """The bash ``kill -0``: alive iff a signal could be delivered."""
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    return True


def _tail1(path: str) -> str:
    """The bash ``tail -1 ... 2>/dev/null || true``: the last line, or '' if absent."""
    try:
        lines = Path(path).read_text().splitlines()
    except OSError:
        return ""
    return lines[-1] if lines else ""


def _line_count(path: str) -> int:
    """The bash ``wc -l`` over a launch log that may not exist yet (0 when absent)."""
    p = Path(path)
    if not p.is_file():
        return 0
    return len(p.read_text().splitlines())


def _find_launchers_key(layout: str, view: int, applet: int) -> str:
    """The launcher-list config KEY name in the tasks applet's General group.

    The bash scanned E2E_LAYOUT for the exact group header, then the first
    ``launchers[0-9]*=`` line under it, returning the key name before ``=``. A
    ``[`` line other than the header closes the group.
    """
    header = f"[Containments][{view}][Applets][{applet}][Configuration][General]"
    inside = False
    key = re.compile(r"launchers[0-9]*=")
    for raw in Path(layout).read_text(encoding="utf-8").splitlines():
        if raw.startswith("["):
            inside = raw == header
        elif inside and key.match(raw):
            return raw.split("=", 1)[0]
    return ""


def main() -> None:
    fixture = Path(os.environ["E2E_REPO"]) / "tests" / "e2e" / "fixtures" / "sc-w1"
    launcher_url = f"applications:{DESKTOP_ID}"
    rate_launcher_url = f"applications:{RATE_DESKTOP_ID}"
    launcher_span = 0
    wheel_x = 0
    wheel_y = 0
    backup = tempfile.mkstemp()[1]
    layout = os.environ["E2E_LAYOUT"]
    shutil.copyfile(layout, backup)

    try:
        view = recipe.tasks_view()
    except recipe.RecipeError:
        recipe.fail("no tasks view")
    tasks_applet = next(
        a.id for a in recipe.view_applets(view) if a.plugin == "org.kde.latte.plasmoid"
    )
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
    launchers_key = _find_launchers_key(layout, view, tasks_applet)
    if not launchers_key:
        recipe.fail(f"tasks applet {tasks_applet} has no launcher-list key")

    rt = os.environ["E2E_RT"]
    xdg_data_home = f"{rt}/sc-w1-data"
    launch_log = f"{rt}/sc-w1-launches"
    pid_log = f"{rt}/sc-w1-pids"
    rate_launch_log = f"{rt}/sc-w1-rate-launches"
    rate_pid_log = f"{rt}/sc-w1-rate-pids"
    os.environ["XDG_DATA_HOME"] = xdg_data_home
    os.environ["SC_W1_LAUNCH_LOG"] = launch_log
    os.environ["SC_W1_PID_LOG"] = pid_log
    os.environ["SC_W1_RATE_LAUNCH_LOG"] = rate_launch_log
    os.environ["SC_W1_RATE_PID_LOG"] = rate_pid_log
    os.environ["SC_W1_QML"] = shutil.which("qml") or ""
    Path(f"{xdg_data_home}/applications").mkdir(parents=True, exist_ok=True)

    def stage_desktop(desktop_id: str, launcher: str) -> None:
        text = (fixture / "applications" / desktop_id).read_text()
        Path(f"{xdg_data_home}/applications/{desktop_id}").write_text(
            text.replace("@BASH@", shutil.which("bash") or "").replace(
                "@LAUNCHER@", str(fixture / launcher)
            )
        )

    stage_desktop(DESKTOP_ID, "launcher.sh")
    stage_desktop(RATE_DESKTOP_ID, "rate-launcher.sh")
    if (
        subprocess.run(
            ["kbuildsycoca6", "--noincremental"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        ).returncode
        != 0
    ):
        recipe.fail("fixture desktop-service cache failed")

    def window_count() -> int:
        return sum(f"|{WINDOW_TITLE}|" in line for line in recipe.dumpwins().splitlines())

    def active_window_title() -> str:
        lines = recipe.kwin_js(
            'print("@TAG@|" + (workspace.activeWindow ? workspace.activeWindow.caption : "none"));'
        ).splitlines()
        return lines[-1] if lines else ""

    def target_state() -> str:
        rows = [
            r
            for r in json.loads(recipe.json_payload("viewTasksData", "u", str(view)))
            if r["launcherUrl"] == launcher_url
        ]
        if any(not r["isLauncher"] and r["isActive"] for r in rows):
            return "active"
        if any(not r["isLauncher"] for r in rows):
            return "window"
        if rows:
            return "launcher"
        return "missing"

    def wait_for_state(expected: str) -> bool:
        state = ""
        for _ in range(40):
            state = target_state()
            if state == expected:
                return True
            time.sleep(0.25)
        print(f"last target state: {state}", file=sys.stderr, flush=True)
        print(
            recipe.json_payload("viewTasksData", "u", str(view)),
            file=sys.stderr,
            flush=True,
        )
        print(recipe.dumpwins(), file=sys.stderr, flush=True)
        return False

    def assert_pure_launcher() -> None:
        rows = json.loads(recipe.json_payload("viewTasksData", "u", str(view)))
        target = [r for r in rows if r["launcherUrl"] == launcher_url]
        if not (len(rows) == 1 and len(target) == 1 and target[0]["isLauncher"]):
            recipe.fail("target is not the only pure launcher")
        if window_count() != 0:
            recipe.fail("fixture window exists before activation")

    def stop_fixture_app() -> None:
        pid = _tail1(pid_log)
        if pid:
            with contextlib.suppress(ProcessLookupError, ValueError):
                os.kill(int(pid), signal.SIGTERM)
        if not wait_for_state("launcher"):
            recipe.fail("launched window did not return to a pure launcher")
        for _ in range(20):
            if window_count() == 0:
                return
            time.sleep(0.25)
        recipe.fail("fixture window survived process termination")

    def cleanup() -> None:
        for plog in (pid_log, rate_pid_log):
            p = Path(plog)
            if not p.is_file():
                continue
            for line in p.read_text().splitlines():
                with contextlib.suppress(ProcessLookupError, ValueError):
                    os.kill(int(line), signal.SIGTERM)
        with _muted_stderr():
            recipe.dock_stop()
        shutil.copyfile(backup, layout)
        with contextlib.suppress(OSError):
            os.unlink(backup)

    def configure_mode(action: int, scrolling: str, manual: int) -> None:
        nonlocal launcher_span
        pid = recipe.dock_pid()
        if pid is not None and _pid_alive(pid) and not recipe.dock_stop():
            recipe.fail(f"could not stop dock for task-wheel mode {action}/{scrolling}/{manual}")
        _kwrite(*general, "--key", launchers_key, launcher_url)
        _kwrite(*general, "--key", "hoverAction", "0")
        _kwrite(*general, "--key", "animationLauncherBouncing", "false")
        _kwrite(*general, "--key", "taskScrollAction", str(action))
        _kwrite(*general, "--key", "scrollTasksEnabled", scrolling)
        _kwrite(*general, "--key", "manualScrollTasksType", str(manual))
        _kwrite(
            "--group", "Containments", "--group", str(view), "--group", "General",
            "--key", "alignment", "1",
        )  # fmt: skip
        _kwrite(
            "--group", "Containments", "--group", str(view), "--group", "General",
            "--key", "alignmentUpgraded", "true",
        )  # fmt: skip
        if not recipe.dock_start():
            recipe.fail(f"dock did not settle for task-wheel mode {action}/{scrolling}/{manual}")
        config = json.loads(
            recipe.json_payload("appletConfigData", "uu", str(view), str(tasks_applet))
        )["config"]
        if not (
            config["taskScrollAction"] == action
            and config["scrollTasksEnabled"] == (scrolling == "true")
            and config["manualScrollTasksType"] == manual
        ):
            recipe.fail("running task-wheel config does not match the requested mode")
        if not wait_for_state("launcher"):
            recipe.fail("fixture launcher never entered the task model")
        assert_pure_launcher()
        applet_width = next(
            a.geometry[2] for a in recipe.view_applets(view) if a.id == tasks_applet
        )
        if not launcher_span:
            launcher_span = applet_width
        if scrolling == "true" and applet_width < launcher_span:
            recipe.fail("manual-scroll fixture shrank the one-item tasks viewport into overflow")

    def settle_pointer() -> None:
        nonlocal wheel_x, wheel_y
        winx = recipe.view_window_x(view)
        try:
            target = recipe.view(view)
            applet = next(a for a in recipe.view_applets(view) if a.id == tasks_applet)
        except recipe.RecipeError, StopIteration:
            recipe.fail("could not locate the live launcher row")
        ax, ay = target.absolute_geometry[0], target.absolute_geometry[1]
        lx, ly = target.local_geometry[0], target.local_geometry[1]
        _px, py, _pw, ph = applet.geometry
        ox = winx if winx is not None else ax - lx
        wheel_x = int(ox + _px + launcher_span / 2)
        wheel_y = int(ay - ly + py + ph / 2)
        _fakepointer("move", str(wheel_x), "500")
        time.sleep(0.3)
        _fakepointer("glide", str(wheel_x), "500", str(wheel_x), str(wheel_y))
        time.sleep(0.8)

    def expect_no_launch(label: str, *cmd: str) -> None:
        before = _line_count(launch_log)
        _fakepointer(*cmd)
        time.sleep(1)
        if _line_count(launch_log) != before:
            recipe.fail(f"{label} launched the fixture")
        assert_pure_launcher()
        print(f"ok: {label} was a process/window/model no-op")

    def expect_launch(label: str, *cmd: str) -> None:
        expected = _line_count(launch_log) + 1
        count = 0
        for _attempt in (1, 2, 3):
            _fakepointer(*cmd)
            for _poll in range(8):
                count = _line_count(launch_log)
                if count >= expected:
                    break
                time.sleep(0.25)
            if count > expected:
                recipe.fail(f"{label} produced {count} launches, expected exactly {expected}")
            if count == expected:
                break
            settle_pointer()
        if _line_count(launch_log) != expected:
            recipe.fail(
                f"{label} produced {_line_count(launch_log)} launches, expected exactly {expected}"
            )
        if not wait_for_state("active"):
            recipe.fail(f"{label} did not activate the task-model window")
        pid = _tail1(pid_log)
        if not (pid and _pid_alive(int(pid))):
            recipe.fail(f"{label} launch process {pid} is not alive")
        if window_count() != 1:
            recipe.fail(f"{label} did not create exactly one fixture window")
        if active_window_title() != WINDOW_TITLE:
            recipe.fail(f"{label} did not activate the fixture window")
        print(
            f"ok: {label} reached launcher activation (pid {pid}, active window, active model row)"
        )

    def rate_process_count() -> int:
        p = Path(rate_pid_log)
        if not p.is_file():
            return 0
        count = 0
        for line in p.read_text().splitlines():
            with contextlib.suppress(ValueError):
                if _pid_alive(int(line)):
                    count += 1
        return count

    def wait_for_rate_count(expected: int) -> None:
        count = 0
        for _ in range(40):
            count = _line_count(rate_launch_log)
            if count >= expected:
                break
            time.sleep(0.25)
        if count != expected:
            recipe.fail(f"rate fixture recorded {count} launches, expected {expected}")
        if rate_process_count() != expected:
            recipe.fail(f"rate fixture process count disagrees with launch count {expected}")
        assert_pure_launcher()

    def drive_rate_to_count(expected: int, detents: int, gap: int) -> None:
        count = 0
        for _attempt in (1, 2, 3):
            _fakepointer("scroll", str(wheel_x), str(wheel_y), str(detents), str(gap))
            for _poll in range(12):
                count = _line_count(rate_launch_log)
                if count >= expected:
                    break
                time.sleep(0.25)
            if count > expected:
                recipe.fail(f"rate fixture recorded {count} launches, expected {expected}")
            if count == expected:
                break
            settle_pointer()
        wait_for_rate_count(expected)

    try:
        configure_mode(1, "false", 0)
        settle_pointer()
        expect_no_launch(
            "below-threshold +90 angleDelta", "wheel", str(wheel_x), str(wheel_y), "90"
        )
        expect_no_launch(
            "ScrollTasks negative wheel",
            "scroll",
            str(wheel_x),
            str(wheel_y),
            "-1",
            "0",
        )
        expect_launch("ScrollTasks positive wheel", "scroll", str(wheel_x), str(wheel_y), "1", "0")
        stop_fixture_app()

        configure_mode(2, "false", 0)
        settle_pointer()
        expect_no_launch(
            "ScrollToggleMinimized negative wheel",
            "scroll",
            str(wheel_x),
            str(wheel_y),
            "-1",
            "0",
        )
        expect_launch(
            "ScrollToggleMinimized positive wheel",
            "scroll",
            str(wheel_x),
            str(wheel_y),
            "1",
            "0",
        )
        stop_fixture_app()

        configure_mode(0, "false", 0)
        settle_pointer()
        expect_no_launch(
            "ScrollNone with manual scrolling disabled",
            "scroll",
            str(wheel_x),
            str(wheel_y),
            "1",
            "0",
        )

        configure_mode(0, "true", 2)
        settle_pointer()
        expect_launch(
            "ScrollNone with manual scrolling enabled on a one-item non-overflow row",
            "scroll",
            str(wheel_x),
            str(wheel_y),
            "1",
            "0",
        )
        stop_fixture_app()

        launcher_url = rate_launcher_url
        launcher_span = 0
        configure_mode(1, "false", 0)
        settle_pointer()
        drive_rate_to_count(1, 2, 50)
        print("ok: first detent launched one pure-launcher process; second at 50 ms was suppressed")
        time.sleep(0.6)
        drive_rate_to_count(2, 1, 0)
        print(
            "ok: post-cooldown detent launched a second process "
            "while the row stayed a pure launcher"
        )

        print("PASS: SC-W1 launcher wheel production path")
    finally:
        cleanup()


if __name__ == "__main__":
    recipe.run(main)
