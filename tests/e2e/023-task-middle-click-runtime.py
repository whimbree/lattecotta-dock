#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""SC-T5 (the permanent runtime-effect acceptance for D29, task-icon middle
click appears to execute left-click behavior): drive the production
TaskMouseArea with one real fakepointer middle click per phase. SC-T3 (the
D29 narrow middle-click dispatch readback) proves which request was selected;
KWin and viewTasksData independently prove its effect. The offered None action
must still record delivered input without changing any window or task state.

Ported from tests/e2e/023-task-middle-click-runtime.sh to latte_harness.recipe
(BP-3, the bash-to-python migration's input/wheel recipe batch R8). The SC-T5
window fixture (fixtures/sc-t5/window.cpp) is still compiled with the same
c++/Qt6Widgets pkg-config invocation and staged through XDG_DATA_HOME +
kbuildsycoca6; its process identities are read from /proc exactly as the bash
python one-liners did. The dispatch, task-model and config readbacks ride
recipe.py's typed boundary or the same sorted-keys JSON one-liners; every
assertion, poll bound, retry count, SC_T5_OBSERVATION line and failure message
is byte-identical, the SPDX header is preserved, and the exec bit stays 100755
(D273).

The bash trap-EXIT cleanup with its recipe_finalized/original_status contract
carries over as the main() -> int pattern (the 072 precedent): the body exits 0
and CLEANUP prints the final "PASS: SC-T5 ..." line only when the teardown is
clean, and a residue-leaving cleanup turns a successful body into a failure. An
unexpected exception is caught so the fixture-removing cleanup always runs (the
bash trap ran on every exit); its traceback stays loud. Structurally-unreachable
bash guards (the read-*-failed status branches over recipe.py readbacks, the
"target point is incomplete" check) are dropped as dead in Python.
"""

from __future__ import annotations

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
import traceback
from collections.abc import Iterator
from dataclasses import dataclass
from pathlib import Path

from latte_harness import recipe

FIXTURE_APP_ID = "org.kde.latte.sc-t5"
DESKTOP_ID = f"{FIXTURE_APP_ID}.desktop"
LAUNCHER_URL = f"applications:{DESKTOP_ID}"
WINDOW_TITLE = "latte-sc-t5-window"


@dataclass
class _State:
    fixture: str = ""
    fixture_data: str = ""
    fixture_binary: str = ""
    fixture_desktop: str = ""
    fixture_record_log: str = ""
    backup_prefix: str = ""
    backup: str = ""
    backup_ready: bool = False
    acceptance_completed: bool = False
    view: int = 0
    tasks_applet: int = 0
    launchers_key: str = ""
    config_group: tuple[str, ...] = ()
    target_x: int = 0
    target_y: int = 0
    pre_click_windows: str = ""
    pre_click_processes: str = ""
    pre_click_tasks: str = ""
    effect_windows: str = ""
    effect_processes: str = ""
    effect_tasks: str = ""
    no_effect_windows: str = ""
    no_effect_processes: str = ""
    no_effect_tasks: str = ""
    observed_dispatch: str = ""
    observed_sequence: int = 0


_S = _State()


@contextlib.contextmanager
def _muted_stderr() -> Iterator[None]:
    """Keep a dock stop's `>/dev/null 2>&1` diagnostics off the recipe output."""
    with contextlib.redirect_stderr(io.StringIO()):
        yield


def _pid_alive(pid: int) -> bool:
    """The bash ``kill -0``: alive iff a signal could be delivered."""
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    return True


def _find_launchers_key(layout: str, view: int, applet: int) -> str:
    """The launcher-list config KEY name in the tasks applet's General group."""
    header = f"[Containments][{view}][Applets][{applet}][Configuration][General]"
    inside = False
    key = re.compile(r"launchers[0-9]*=")
    for raw in Path(layout).read_text(encoding="utf-8").splitlines():
        if raw.startswith("["):
            inside = raw == header
        elif inside and key.match(raw):
            return raw.split("=", 1)[0]
    return ""


def _read_dock_pid() -> int | None:
    """read_dock_pid: the vehicle dock pid validated as a positive integer, or None."""
    pid = recipe.dock_pid()
    if pid is None or pid < 1:
        shown = pid if pid is not None else "missing"
        print(
            f"read_dock_pid: invalid dock pid '{shown}' (status 0)",
            file=sys.stderr,
            flush=True,
        )
        return None
    return pid


def _inject(label: str, *args: str) -> None:
    rc = subprocess.run([os.environ["E2E_FAKEPOINTER"], *args], check=False).returncode
    if rc != 0:
        recipe.fail(f"{label}: fakepointer '{' '.join(args)}' failed with status {rc}")


def _fixture_windows() -> str:
    """The fixture's KWin windows as sorted-by-id, sorted-keys JSON (fixture_windows)."""
    js = (
        "var rows = [];\n"
        "for (const w of workspace.windowList()) {\n"
        "    if (w.resourceClass === '" + FIXTURE_APP_ID + "') {\n"
        "        rows.push({id: String(w.internalId), resourceClass: String(w.resourceClass), "
        "caption: String(w.caption), active: workspace.activeWindow === w, "
        "minimized: Boolean(w.minimized)});\n"
        "    }\n"
        "}\n"
        "print('@TAG@|' + JSON.stringify(rows));"
    )
    rows = json.loads(recipe.kwin_js(js))
    assert all(row["resourceClass"] == FIXTURE_APP_ID for row in rows)
    assert all(row["caption"] == WINDOW_TITLE for row in rows)
    rows.sort(key=lambda row: row["id"])
    return json.dumps(rows, separators=(",", ":"), sort_keys=True)


def _recorded_processes() -> str:
    """recorded_processes: the fixture's appended pid|startTime|executable records."""
    p = Path(_S.fixture_record_log)
    if not p.exists():
        return "[]"
    records: list[dict[str, object]] = []
    seen: set[tuple[int, str]] = set()
    for number, line in enumerate(p.read_text(encoding="utf-8").splitlines(), 1):
        parts = line.split("|", 2)
        assert len(parts) == 3, f"malformed process record line {number}"
        pid, start_time, executable = parts
        assert pid.isdigit() and int(pid) > 1
        assert start_time.isdigit()
        assert executable == _S.fixture_binary
        identity = (int(pid), start_time)
        assert identity not in seen, f"duplicate process identity {identity}"
        seen.add(identity)
        records.append(
            {"pid": int(pid), "startTime": start_time, "executable": executable}
        )
    records.sort(key=lambda record: record["pid"])
    return json.dumps(records, separators=(",", ":"), sort_keys=True)


def _live_identity(record: dict[str, object]) -> dict[str, object] | None:
    """The recorded process still live with a matching /proc identity, else None."""
    proc = Path("/proc") / str(record["pid"])
    try:
        stat = (proc / "stat").read_text(encoding="utf-8")
    except FileNotFoundError:
        return None
    command_end = stat.rfind(")")
    assert command_end >= 0, f"malformed stat for pid {record['pid']}"
    fields = stat[command_end + 1 :].split()
    assert len(fields) > 19, f"short stat for pid {record['pid']}"
    if fields[0] == "Z":
        return None
    start_time = fields[19]
    try:
        executable = os.readlink(proc / "exe")
    except FileNotFoundError:
        return None
    assert record["startTime"] == start_time, (
        f"pid {record['pid']} start time mismatch: recorded {record['startTime']}, live {start_time}"
    )
    assert record["executable"] == _S.fixture_binary
    assert executable == _S.fixture_binary, (
        f"pid {record['pid']} executable mismatch: recorded {record['executable']}, "
        f"live {executable}"
    )
    return record


def _fixture_processes() -> str:
    """fixture_processes: the recorded identities still live in /proc, sorted-keys JSON."""
    live = [
        ident
        for record in json.loads(_recorded_processes())
        if (ident := _live_identity(record))
    ]
    return json.dumps(live, separators=(",", ":"), sort_keys=True)


def _validate_process_identity(pid: int, start_time: str, executable: str) -> str:
    """validate_process_identity: 'live' or 'absent' for a recorded fixture pid."""
    proc = Path("/proc") / str(pid)
    try:
        stat = (proc / "stat").read_text(encoding="utf-8")
    except FileNotFoundError:
        return "absent"
    command_end = stat.rfind(")")
    assert command_end >= 0
    fields = stat[command_end + 1 :].split()
    assert len(fields) > 19
    if fields[0] == "Z":
        return "absent"
    live_start = fields[19]
    try:
        live_exe = os.readlink(proc / "exe")
    except FileNotFoundError:
        return "absent"
    assert start_time == live_start, (
        f"pid {pid} start time mismatch before signal: recorded {start_time}, live {live_start}"
    )
    assert executable == _S.fixture_binary
    assert live_exe == _S.fixture_binary, (
        f"pid {pid} executable mismatch before signal: recorded {executable}, live {live_exe}"
    )
    return "live"


def _terminate_fixture_processes() -> bool:
    """terminate_fixture_processes: TERM the live fixture processes and wait for exit."""
    for record in json.loads(_fixture_processes()):
        pid, start_time, executable = (
            record["pid"],
            record["startTime"],
            record["executable"],
        )
        state = _validate_process_identity(pid, start_time, executable)
        if state == "live":
            try:
                os.kill(pid, signal.SIGTERM)
            except ProcessLookupError:
                print(
                    f"could not terminate validated fixture pid {pid}",
                    file=sys.stderr,
                    flush=True,
                )
                return False
        elif state != "absent":
            print(
                f"unexpected fixture pid {pid} validation state '{state}'",
                file=sys.stderr,
                flush=True,
            )
            return False
    processes = "[]"
    for _ in range(40):
        processes = _fixture_processes()
        if len(json.loads(processes)) == 0:
            return True
        time.sleep(0.25)
    print(
        f"fixture processes survived termination: {processes}",
        file=sys.stderr,
        flush=True,
    )
    return False


def _compile_fixture() -> None:
    compiler = shutil.which("c++")
    if not compiler:
        recipe.fail("fixture C++ compiler is unavailable")
    cflags = subprocess.run(
        ["pkg-config", "--cflags", "Qt6Widgets"],
        capture_output=True,
        text=True,
        check=False,
    )
    if cflags.returncode != 0:
        recipe.fail("fixture Qt6Widgets compiler flags are unavailable")
    libs = subprocess.run(
        ["pkg-config", "--libs", "Qt6Widgets"],
        capture_output=True,
        text=True,
        check=False,
    )
    if libs.returncode != 0:
        recipe.fail("fixture Qt6Widgets linker flags are unavailable")
    Path(_S.fixture_binary).parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        compiler,
        "-std=c++20",
        "-O2",
        *cflags.stdout.split(),
        str(Path(_S.fixture) / "window.cpp"),
        "-o",
        _S.fixture_binary,
        *libs.stdout.split(),
    ]
    if subprocess.run(cmd, check=False).returncode != 0:
        recipe.fail("could not compile the SC-T5 fixture executable")
    if not os.access(_S.fixture_binary, os.X_OK):
        recipe.fail(f"compiled fixture is not executable: {_S.fixture_binary}")


def _stage_desktop() -> None:
    Path(_S.fixture_desktop).parent.mkdir(parents=True, exist_ok=True)
    try:
        source = (Path(_S.fixture) / "applications" / DESKTOP_ID).read_text(
            encoding="utf-8"
        )
        Path(_S.fixture_desktop).write_text(
            source.replace("@BINARY@", _S.fixture_binary), encoding="utf-8"
        )
    except OSError:
        recipe.fail("could not stage the fixture desktop service")
    if (
        subprocess.run(
            ["kbuildsycoca6", "--noincremental"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
            check=False,
        ).returncode
        != 0
    ):
        recipe.fail("fixture desktop-service cache generation failed")


def _discover_task_fixture() -> None:
    try:
        _S.view = recipe.tasks_view()
    except recipe.RecipeError:
        recipe.fail("no tasks view")
    try:
        _S.tasks_applet = next(
            a.id
            for a in recipe.view_applets(_S.view)
            if a.plugin == "org.kde.latte.plasmoid"
        )
    except StopIteration:
        recipe.fail(f"could not resolve the tasks applet in view {_S.view}")
    layout = os.environ["E2E_LAYOUT"]
    _S.config_group = (
        "--file", layout,
        "--group", "Containments", "--group", str(_S.view),
        "--group", "Applets", "--group", str(_S.tasks_applet),
        "--group", "Configuration", "--group", "General",
    )  # fmt: skip
    _S.launchers_key = _find_launchers_key(layout, _S.view, _S.tasks_applet)
    if not _S.launchers_key:
        recipe.fail(f"tasks applet {_S.tasks_applet} has no launcher-list key")


def _write_task_key(key: str, value: str, label: str) -> None:
    if (
        subprocess.run(
            ["kwriteconfig6", *_S.config_group, "--key", key, "--", value], check=False
        ).returncode
        != 0
    ):
        recipe.fail(f"{label}: could not write {key}={value}")


def _read_tasks() -> str:
    payload = recipe.json_payload("viewTasksData", "u", str(_S.view))
    return json.dumps(json.loads(payload), separators=(",", ":"), sort_keys=True)


def _read_dispatch() -> str:
    payload = recipe.json_payload("taskMiddleClickDispatchData", "u", str(_S.view))
    return json.dumps(json.loads(payload), separators=(",", ":"), sort_keys=True)


def _running_config_snapshot() -> str:
    cfg = json.loads(
        recipe.json_payload(
            "appletConfigData", "uu", str(_S.view), str(_S.tasks_applet)
        )
    )["config"]
    return json.dumps(
        {
            "groupTasksByDefault": cfg["groupTasksByDefault"],
            "hoverAction": cfg["hoverAction"],
            "middleClickAction": cfg["middleClickAction"],
        },
        separators=(",", ":"),
        sort_keys=True,
    )


def _configure_action(action: int, label: str) -> None:
    pid = _read_dock_pid()
    if pid is None:
        recipe.fail(f"{label}: dock pid query failed")
    if not _pid_alive(pid):
        recipe.fail(f"{label}: dock pid {pid} is not running before configuration")
    if not recipe.dock_stop():
        recipe.fail(f"{label}: could not stop dock pid {pid} for configuration")
    if _pid_alive(pid):
        recipe.fail(f"{label}: dock pid {pid} survived configuration stop")

    _write_task_key(_S.launchers_key, LAUNCHER_URL, label)
    _write_task_key("middleClickAction", str(action), label)
    _write_task_key("hoverAction", "0", label)
    _write_task_key("animationLauncherBouncing", "false", label)
    _write_task_key("animationNewWindowSliding", "false", label)
    _write_task_key("animationWindowAddedInGroup", "false", label)
    _write_task_key("groupTasksByDefault", "true", label)
    _write_task_key("hideAllTasks", "false", label)
    _write_task_key("showOnlyCurrentScreen", "false", label)
    _write_task_key("showOnlyCurrentDesktop", "false", label)
    _write_task_key("showOnlyCurrentActivity", "false", label)
    _write_task_key("showWindowsOnlyFromLaunchers", "true", label)
    general = ["kwriteconfig6", "--file", os.environ["E2E_LAYOUT"], "--group", "Containments",
               "--group", str(_S.view), "--group", "General"]  # fmt: skip
    if (
        subprocess.run([*general, "--key", "alignment", "1"], check=False).returncode
        != 0
    ):
        recipe.fail(f"{label}: could not set center alignment")
    if (
        subprocess.run(
            [*general, "--key", "alignmentUpgraded", "true"], check=False
        ).returncode
        != 0
    ):
        recipe.fail(f"{label}: could not mark alignment upgraded")

    if not recipe.dock_start(90):
        recipe.fail(f"{label}: dock did not settle")
    pid = _read_dock_pid()
    if pid is None or not _pid_alive(pid):
        recipe.fail(f"{label}: restarted dock pid is unavailable")
    config = _running_config_snapshot()
    if json.loads(config) != {
        "groupTasksByDefault": True,
        "hoverAction": 0,
        "middleClickAction": action,
    }:
        recipe.fail(f"{label}: running config does not match the fixture: {config}")
    print(f"SC_T5_OBSERVATION|phase=config|label={label}|json={config}")


def _model_matches(payload: str, kind: str) -> bool:
    rows = json.loads(payload)
    if len(rows) != 1:
        return False
    row = rows[0]
    if not (
        row["launcherUrl"] == LAUNCHER_URL
        and row["appId"] == DESKTOP_ID
        and row["appletId"] == _S.tasks_applet
        and row["isMinimized"] is False
    ):
        return False
    expected = {
        "launcher": (True, False, 0, False),
        "single": (False, False, 0, True),
        "group": (False, True, 2, True),
    }[kind]
    is_launcher, is_grouped, child_count, is_active = expected
    return (
        row["isLauncher"] is is_launcher
        and row["isGrouped"] is is_grouped
        and row["childCount"] == child_count
        and row["isActive"] is is_active
    )


def _runtime_snapshot_matches(
    windows: str, processes: str, tasks: str, expected_windows: int, kind: str
) -> bool:
    if not _model_matches(tasks, kind):
        return False
    w = json.loads(windows)
    p = json.loads(processes)
    if not (len(w) == len({x["id"] for x in w}) == expected_windows):
        return False
    if not (
        len(p)
        == len({(x["pid"], x["startTime"], x["executable"]) for x in p})
        == expected_windows
    ):
        return False
    if sum(1 for x in w if x["active"]) != (1 if expected_windows else 0):
        return False
    if not all(x["minimized"] is False for x in w):
        return False
    return all(x["executable"] == _S.fixture_binary for x in p)


def _wait_for_effect(expected_windows: int, kind: str, label: str) -> None:
    windows = processes = tasks = ""
    for _ in range(60):
        windows = _fixture_windows()
        processes = _fixture_processes()
        tasks = _read_tasks()
        if (
            len(json.loads(windows)) == expected_windows
            and len(json.loads(processes)) == expected_windows
            and _runtime_snapshot_matches(
                windows, processes, tasks, expected_windows, kind
            )
        ):
            _S.effect_windows, _S.effect_processes, _S.effect_tasks = (
                windows,
                processes,
                tasks,
            )
            return
        time.sleep(0.25)
    all_windows = recipe.dumpwins()
    recipe.fail(
        f"{label} did not settle: windows={windows} processes={processes} tasks={tasks} "
        f"allWindows={all_windows}"
    )


def _phase_two_relation_matches(
    original_windows: str,
    original_processes: str,
    windows: str,
    processes: str,
    tasks: str,
) -> bool:
    if not _model_matches(tasks, "group"):
        return False
    ow = json.loads(original_windows)
    op = json.loads(original_processes)
    w = json.loads(windows)
    p = json.loads(processes)
    if not (len(ow) == len(op) == 1):
        return False
    if not (len(w) == len({x["id"] for x in w}) == 2):
        return False
    if not (
        len(p) == len({(x["pid"], x["startTime"], x["executable"]) for x in p}) == 2
    ):
        return False
    original_window = ow[0]
    if not (
        original_window["active"] is True and original_window["minimized"] is False
    ):
        return False
    windows_by_id = {x["id"]: x for x in w}
    if original_window["id"] not in windows_by_id:
        return False
    if windows_by_id[original_window["id"]] != {
        **original_window,
        "active": False,
        "minimized": False,
    }:
        return False
    new_windows = [x for x in w if x["id"] != original_window["id"]]
    if len(new_windows) != 1:
        return False
    if not (new_windows[0]["active"] is True and new_windows[0]["minimized"] is False):
        return False
    original_process = op[0]
    if original_process["executable"] != _S.fixture_binary:
        return False
    identities = {(x["pid"], x["startTime"], x["executable"]): x for x in p}
    original_identity = (
        original_process["pid"],
        original_process["startTime"],
        original_process["executable"],
    )
    if original_identity not in identities:
        return False
    new_processes = [
        x for x in p if (x["pid"], x["startTime"], x["executable"]) != original_identity
    ]
    if len(new_processes) != 1:
        return False
    return new_processes[0]["executable"] == _S.fixture_binary


def _wait_for_phase_two_effect(
    original_windows: str, original_processes: str, label: str
) -> None:
    windows = processes = tasks = ""
    for _ in range(60):
        windows = _fixture_windows()
        processes = _fixture_processes()
        tasks = _read_tasks()
        if _phase_two_relation_matches(
            original_windows, original_processes, windows, processes, tasks
        ):
            _S.effect_windows, _S.effect_processes, _S.effect_tasks = (
                windows,
                processes,
                tasks,
            )
            return
        time.sleep(0.25)
    all_windows = recipe.dumpwins()
    recipe.fail(
        f"{label} did not preserve one original and add exactly one new identity: "
        f"windows={windows} processes={processes} tasks={tasks} allWindows={all_windows}"
    )


def _assert_phase_two_relation_persists(
    original_windows: str, original_processes: str, label: str
) -> None:
    windows = _fixture_windows()
    processes = _fixture_processes()
    tasks = _read_tasks()
    if not _phase_two_relation_matches(
        original_windows, original_processes, windows, processes, tasks
    ):
        recipe.fail(
            f"{label}: old/new identity relation changed: "
            f"windows={windows} processes={processes} tasks={tasks}"
        )
    _S.effect_windows, _S.effect_processes, _S.effect_tasks = windows, processes, tasks


def _locate_target_point(kind: str, expected_windows: int) -> tuple[int, int] | None:
    winx = recipe.view_window_x(_S.view)
    if winx is None:
        print(
            f"could not resolve rendered x origin for view {_S.view}",
            file=sys.stderr,
            flush=True,
        )
        return None
    tasks = _read_tasks()
    windows = _fixture_windows()
    processes = _fixture_processes()
    if not _runtime_snapshot_matches(windows, processes, tasks, expected_windows, kind):
        print(
            f"target state is not exact {kind}/{expected_windows}: "
            f"windows={windows} processes={processes} tasks={tasks}",
            file=sys.stderr,
            flush=True,
        )
        return None
    target = recipe.view(_S.view)
    applet = next(a for a in recipe.view_applets(_S.view) if a.id == _S.tasks_applet)
    task_list = json.loads(recipe.json_payload("viewTasksData", "u", str(_S.view)))
    matches = [i for i, t in enumerate(task_list) if t["launcherUrl"] == LAUNCHER_URL]
    if not (len(task_list) == 1 and matches == [0]):
        return None
    ay = target.absolute_geometry[1]
    ly = target.local_geometry[1]
    px, py, pw, ph = applet.geometry
    if not (pw > 0 and ph > 0):
        return None
    return int(winx + px + pw / 2), int(ay - ly + py + ph / 2)


def _settle_target_pointer(label: str, kind: str, expected_windows: int) -> None:
    pointer_x = pointer_y = 0
    for pass_ in (1, 2):
        point = _locate_target_point(kind, expected_windows)
        if point is None:
            recipe.fail(
                f"{label}: could not locate an exact {kind}/{expected_windows} target "
                f"on settle pass {pass_}"
            )
        pointer_x, pointer_y = point
        _inject(f"{label} pointer exit pass {pass_}", "move", str(pointer_x), "500")
        time.sleep(0.5)
        _inject(
            f"{label} pointer glide pass {pass_}",
            "glide",
            str(pointer_x),
            "500",
            str(pointer_x),
            str(pointer_y),
        )
        time.sleep(1.5)
    _S.target_x, _S.target_y = pointer_x, pointer_y


def _capture_click_precondition(label: str, kind: str, expected_windows: int) -> None:
    _S.pre_click_windows = _fixture_windows()
    _S.pre_click_processes = _fixture_processes()
    _S.pre_click_tasks = _read_tasks()
    if not _runtime_snapshot_matches(
        _S.pre_click_windows,
        _S.pre_click_processes,
        _S.pre_click_tasks,
        expected_windows,
        kind,
    ):
        recipe.fail(
            f"{label}: final pre-click state is not exact {kind}/{expected_windows}: "
            f"windows={_S.pre_click_windows} processes={_S.pre_click_processes} "
            f"tasks={_S.pre_click_tasks}"
        )


def _dispatch_sequence(payload: str) -> int:
    record = json.loads(payload)
    sequence = record.get("sequence", 0)
    assert isinstance(sequence, int)
    return sequence


def _assert_dispatch(
    payload: str, row_kind: str, action: str, operation: str, expected_sequence: int
) -> bool:
    record = json.loads(payload)
    return (
        set(record)
        == {
            "configuredAction",
            "dispatchedOperation",
            "rowIdentity",
            "rowKind",
            "sequence",
        }
        and record["rowIdentity"] == LAUNCHER_URL
        and record["rowKind"] == row_kind
        and record["configuredAction"] == action
        and record["dispatchedOperation"] == operation
        and record["sequence"] == expected_sequence
    )


def _drive_one_middle_click(
    label: str,
    previous_sequence: int,
    row_kind: str,
    action: str,
    operation: str,
    target_kind: str,
    expected_windows: int,
) -> None:
    expected_sequence = previous_sequence + 1
    _settle_target_pointer(label, target_kind, expected_windows)
    _capture_click_precondition(label, target_kind, expected_windows)
    _inject(label, "middleclick", str(_S.target_x), str(_S.target_y))
    for _ in range(40):
        payload = _read_dispatch()
        sequence = _dispatch_sequence(payload)
        if sequence == previous_sequence:
            time.sleep(0.25)
            continue
        if sequence != expected_sequence:
            recipe.fail(
                f"{label}: sequence changed by more than one ({previous_sequence} -> {sequence})"
            )
        if not _assert_dispatch(
            payload, row_kind, action, operation, expected_sequence
        ):
            recipe.fail(
                f"{label}: unexpected dispatch after the one delivered click: {payload}"
            )
        _S.observed_dispatch = payload
        _S.observed_sequence = sequence
        return
    recipe.fail(f"{label} produced no dispatch after one status-0 middle click")


def _assert_dispatch_unchanged(
    expected_payload: str, expected_sequence: int, label: str
) -> None:
    payload = _read_dispatch()
    sequence = _dispatch_sequence(payload)
    if not (payload == expected_payload and sequence == expected_sequence):
        recipe.fail(f"{label}: dispatch changed after effect settlement: {payload}")


def _assert_containment_isolation() -> None:
    views = json.loads(recipe.json_payload("viewsData"))
    valid_controls = 0
    for other in (v["containmentId"] for v in views):
        if other == _S.view:
            continue
        payload = recipe.json_payload("taskMiddleClickDispatchData", "u", str(other))
        if payload != "{}":
            recipe.fail(
                f"target-view dispatch leaked into containment {other}: {payload}"
            )
        valid_controls += 1
    payload = recipe.json_payload("taskMiddleClickDispatchData", "u", "4294967295")
    if payload != "{}":
        recipe.fail(f"dispatch leaked into absent containment: {payload}")
    print(
        f"SC_T5_OBSERVATION|phase=containmentIsolation|validControls={valid_controls}"
        "|absentContainment=4294967295"
    )


def _assert_no_effect_interval(
    expected_windows: str,
    expected_processes: str,
    expected_tasks: str,
    expected_dispatch: str,
    expected_sequence: int,
    label: str,
) -> None:
    windows = processes = tasks = ""
    fields = (
        "appId", "appletId", "launcherUrl", "isLauncher",
        "isGrouped", "childCount", "isActive", "isMinimized",
    )  # fmt: skip
    for _ in range(12):
        time.sleep(0.25)
        windows = _fixture_windows()
        processes = _fixture_processes()
        tasks = _read_tasks()
        payload = _read_dispatch()
        sequence = _dispatch_sequence(payload)
        if not (payload == expected_dispatch and sequence == expected_sequence):
            recipe.fail(f"{label}: dispatch changed during no-op settlement: {payload}")
        before_tasks, after_tasks = json.loads(expected_tasks), json.loads(tasks)
        if not (
            json.loads(expected_windows) == json.loads(windows)
            and json.loads(expected_processes) == json.loads(processes)
            and len(before_tasks) == len(after_tasks) == 1
            and {f: before_tasks[0][f] for f in fields}
            == {f: after_tasks[0][f] for f in fields}
        ):
            recipe.fail(f"{label} changed KWin, process, or task state")
    _S.no_effect_windows, _S.no_effect_processes, _S.no_effect_tasks = (
        windows,
        processes,
        tasks,
    )


def _body() -> None:
    _discover_task_fixture()
    _compile_fixture()
    _stage_desktop()

    _configure_action(2, "new-instance positive path")
    initial_tasks = _read_tasks()
    if not _model_matches(initial_tasks, "launcher"):
        recipe.fail(
            f"initial viewTasksData is not one pure fixture launcher: {initial_tasks}"
        )
    initial_windows = _fixture_windows()
    initial_processes = _fixture_processes()
    if len(json.loads(initial_windows)) != 0:
        recipe.fail(f"fixture window exists before launcher input: {initial_windows}")
    if len(json.loads(initial_processes)) != 0:
        recipe.fail(
            f"fixture process exists before launcher input: {initial_processes}"
        )
    initial_dispatch = _read_dispatch()
    if initial_dispatch != "{}":
        recipe.fail(
            f"fresh dock already has a middle-click dispatch: {initial_dispatch}"
        )
    print(
        f"SC_T5_OBSERVATION|phase=initial|windows={initial_windows}"
        f"|processes={initial_processes}|tasks={initial_tasks}"
    )

    _drive_one_middle_click(
        "pure-launcher middle click",
        0,
        "launcher",
        "newInstance",
        "requestActivate",
        "launcher",
        0,
    )
    launcher_sequence = _S.observed_sequence
    launcher_dispatch = _S.observed_dispatch
    _assert_containment_isolation()
    _wait_for_effect(1, "single", "launcher zero-to-one effect")
    _assert_dispatch_unchanged(launcher_dispatch, 1, "launcher zero-to-one effect")
    print(f"SC_T5_OBSERVATION|phase=launcherDispatch|json={launcher_dispatch}")
    print(
        f"SC_T5_OBSERVATION|phase=launcherEffect|windows={_S.effect_windows}"
        f"|processes={_S.effect_processes}|tasks={_S.effect_tasks}"
    )

    _drive_one_middle_click(
        "single-window middle click",
        launcher_sequence,
        "task",
        "newInstance",
        "requestNewInstance",
        "single",
        1,
    )
    task_dispatch = _S.observed_dispatch
    phase_two_original_windows = _S.pre_click_windows
    phase_two_original_processes = _S.pre_click_processes
    phase_two_original_tasks = _S.pre_click_tasks
    _assert_containment_isolation()
    _wait_for_phase_two_effect(
        phase_two_original_windows,
        phase_two_original_processes,
        "single-window one-to-two effect",
    )
    time.sleep(1)
    _assert_dispatch_unchanged(task_dispatch, 2, "single-window one-to-two effect")
    _assert_phase_two_relation_persists(
        phase_two_original_windows,
        phase_two_original_processes,
        "single-window one-to-two effect",
    )
    print(
        f"SC_T5_OBSERVATION|phase=taskPreClick|windows={phase_two_original_windows}"
        f"|processes={phase_two_original_processes}|tasks={phase_two_original_tasks}"
    )
    print(f"SC_T5_OBSERVATION|phase=taskDispatch|json={task_dispatch}")
    print(
        f"SC_T5_OBSERVATION|phase=groupEffect|windows={_S.effect_windows}"
        f"|processes={_S.effect_processes}|tasks={_S.effect_tasks}"
    )

    _configure_action(0, "offered-none negative control")
    _wait_for_effect(2, "group", "negative-control baseline")
    negative_before_windows = _S.effect_windows
    negative_before_processes = _S.effect_processes
    negative_before_tasks = _S.effect_tasks
    initial_dispatch = _read_dispatch()
    if initial_dispatch != "{}":
        recipe.fail(
            f"restarted dock did not reset to no-event state: {initial_dispatch}"
        )
    _drive_one_middle_click(
        "offered-none middle click", 0, "task", "none", "none", "group", 2
    )
    negative_dispatch = _S.observed_dispatch
    if _S.observed_sequence != 1:
        recipe.fail(f"offered None sequence is not exactly 1: {_S.observed_sequence}")
    _assert_containment_isolation()
    _assert_no_effect_interval(
        negative_before_windows,
        negative_before_processes,
        negative_before_tasks,
        negative_dispatch,
        1,
        "offered-none no-op",
    )
    print(f"SC_T5_OBSERVATION|phase=negativeDispatch|json={negative_dispatch}")
    print(
        f"SC_T5_OBSERVATION|phase=negativeNoEffect|windows={_S.no_effect_windows}"
        f"|processes={_S.no_effect_processes}|tasks={_S.no_effect_tasks}"
    )

    _S.acceptance_completed = True


def _cleanup(original_status: int) -> int:
    cleanup_failed = False

    pid = _read_dock_pid()
    if pid is None:
        print("FAIL: cleanup could not query the dock pid", file=sys.stderr, flush=True)
        cleanup_failed = True
    elif _pid_alive(pid) and not recipe.dock_stop():
        print(
            f"FAIL: cleanup could not stop dock pid {pid}", file=sys.stderr, flush=True
        )
        cleanup_failed = True
    if pid is not None and _pid_alive(pid):
        print(f"FAIL: cleanup left dock pid {pid} running", file=sys.stderr, flush=True)
        cleanup_failed = True

    if not _terminate_fixture_processes():
        print(
            "FAIL: cleanup could not terminate validated fixture processes",
            file=sys.stderr,
            flush=True,
        )
        cleanup_failed = True

    try:
        windows = _fixture_windows()
        empty = len(json.loads(windows)) == 0
    except Exception:
        print("FAIL: cleanup KWin fixture query failed", file=sys.stderr, flush=True)
        cleanup_failed = True
    else:
        if not empty:
            print(
                f"FAIL: cleanup left fixture windows: {windows}",
                file=sys.stderr,
                flush=True,
            )
            cleanup_failed = True

    if _S.backup_ready:
        try:
            shutil.copyfile(_S.backup, os.environ["E2E_LAYOUT"])
            restored = (
                Path(_S.backup).read_bytes()
                == Path(os.environ["E2E_LAYOUT"]).read_bytes()
            )
        except OSError:
            restored = False
        if not restored:
            print(
                f"FAIL: cleanup could not byte-restore {os.environ['E2E_LAYOUT']}",
                file=sys.stderr,
                flush=True,
            )
            cleanup_failed = True

    with contextlib.suppress(OSError):
        os.unlink(_S.fixture_desktop)
    subprocess.run(
        ["kbuildsycoca6", "--noincremental"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    shutil.rmtree(_S.fixture_data, ignore_errors=True)
    for path in (
        _S.fixture_desktop,
        _S.fixture_binary,
        _S.fixture_record_log,
        _S.fixture_data,
    ):
        if os.path.exists(path):
            print(
                f"FAIL: cleanup left fixture path {path}", file=sys.stderr, flush=True
            )
            cleanup_failed = True
    for path in Path(os.path.dirname(_S.backup_prefix)).glob(
        os.path.basename(_S.backup_prefix) + "*"
    ):
        with contextlib.suppress(OSError):
            path.unlink()
    for path in Path(os.path.dirname(_S.backup_prefix)).glob(
        os.path.basename(_S.backup_prefix) + "*"
    ):
        print(f"FAIL: cleanup left backup path {path}", file=sys.stderr, flush=True)
        cleanup_failed = True

    if not _S.acceptance_completed and original_status == 0:
        print(
            "FAIL: recipe exited before completing its acceptance",
            file=sys.stderr,
            flush=True,
        )
        original_status = 1

    if cleanup_failed:
        if original_status != 0:
            print(
                f"FAIL: cleanup also failed after original recipe status {original_status}",
                file=sys.stderr,
                flush=True,
            )
            return original_status
        return 1
    if original_status != 0:
        return original_status
    print("PASS: SC-T5 middle-click dispatch and independent runtime effects")
    return 0


def _init_paths_and_env() -> None:
    rt = os.environ["E2E_RT"]
    _S.fixture = str(
        Path(os.environ["E2E_REPO"]) / "tests" / "e2e" / "fixtures" / "sc-t5"
    )
    _S.fixture_data = f"{rt}/sc-t5-data"
    _S.fixture_binary = f"{_S.fixture_data}/bin/latte-sc-t5"
    _S.fixture_desktop = f"{_S.fixture_data}/applications/{DESKTOP_ID}"
    _S.fixture_record_log = f"{_S.fixture_data}/process-records"
    _S.backup_prefix = f"{rt}/sc-t5-layout-backup."
    os.environ["XDG_DATA_HOME"] = _S.fixture_data
    os.environ["SC_T5_PROCESS_RECORDS"] = _S.fixture_record_log


def _setup_backup() -> None:
    handle, _S.backup = tempfile.mkstemp(
        prefix=os.path.basename(_S.backup_prefix), dir=os.path.dirname(_S.backup_prefix)
    )
    os.close(handle)
    layout = os.environ["E2E_LAYOUT"]
    shutil.copyfile(layout, _S.backup)
    if Path(layout).read_bytes() != Path(_S.backup).read_bytes():
        recipe.fail("layout backup differs immediately after copy")
    _S.backup_ready = True


def main() -> int:
    _init_paths_and_env()
    status = 0
    try:
        _setup_backup()
        _body()
    except SystemExit as exc:
        status = exc.code if isinstance(exc.code, int) else 1
    except recipe.RecipeError as exc:
        print(str(exc), file=sys.stderr, flush=True)
        status = 1
    except Exception:
        traceback.print_exc()
        status = 1
    return _cleanup(status)


if __name__ == "__main__":
    sys.exit(main())
