# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The vehicle lifecycle contract: env assembly, argv shape, state round-trip,
proc-state parsing, and the zombie-aware group teardown escalation.

The group teardown tests drive REAL child process groups, not mocks - the exact
setsid/killpg/pgrep discipline the nested compositor rides on. Each escalation
path (already gone, dies on TERM, survives TERM into KILL, a STOPPED
TERM-ignoring leader like a KCrash) has a driven case, and the pure helpers
have negative controls (a malformed env token, an unparseable proc state, a
corrupt state file).
"""

import os
import signal
import socket as socket_mod
import stat
import subprocess
import sys
import time
from pathlib import Path

import pytest
from pydantic import ValidationError

from latte_harness import vehicle
from latte_harness.vehicle import (
    CompositorStartError,
    MalformedEnvError,
    VehicleSession,
    VehicleState,
    build_kwin_argv,
    build_session_env,
    group_live_status,
    parse_proc_state,
    prepare_runtime_dir,
    read_state,
    stop_compositor,
    stop_process_group,
)

# ---- build_session_env -----------------------------------------------------


def test_build_session_env_strips_x11_and_forces_runtime_dir() -> None:
    base = {"DISPLAY": ":0", "XAUTHORITY": "/x", "PATH": "/bin", "XDG_RUNTIME_DIR": "/old"}
    env = build_session_env(base, Path("/run/nested"), ["WAYLAND_DISPLAY=wl-1"])
    assert "DISPLAY" not in env
    assert "XAUTHORITY" not in env
    assert env["PATH"] == "/bin"
    assert env["XDG_RUNTIME_DIR"] == "/run/nested"
    assert env["KWIN_WAYLAND_NO_PERMISSION_CHECKS"] == "1"
    assert env["WAYLAND_DISPLAY"] == "wl-1"


def test_build_session_env_value_may_contain_equals() -> None:
    env = build_session_env({}, Path("/rt"), ["XDG_CONFIG_HOME=/a/b=c"])
    assert env["XDG_CONFIG_HOME"] == "/a/b=c"


@pytest.mark.parametrize("bad", ["NOEQUALS", "=value"])
def test_build_session_env_rejects_malformed_token(bad: str) -> None:
    with pytest.raises(MalformedEnvError):
        build_session_env({}, Path("/rt"), [bad])


# ---- build_kwin_argv -------------------------------------------------------


def test_build_kwin_argv_single_output_tail() -> None:
    argv = build_kwin_argv(Path("/rt"), 1600, 1000, "sock", 1)
    assert argv[:4] == ["dbus-run-session", "--", "sh", "-c"]
    assert argv[-6:] == ["sh", "/rt", "1600", "1000", "sock", "1"]


def test_build_kwin_argv_multi_output_count_in_tail() -> None:
    argv = build_kwin_argv(Path("/rt"), 800, 600, "sock", 2)
    assert argv[-1] == "2"
    # the branch lives in the runtime wrapper, so the wrapper text is invariant
    assert "--output-count" in argv[4]


# ---- parse_proc_state ------------------------------------------------------


@pytest.mark.parametrize(
    ("stat_contents", "expected"),
    [
        ("1234 (bash) S 1 1234 1234 0 -1 ...", "S"),
        ("1234 (weird )( name) R 1 1234 ...", "R"),
        ("1234 (kwin_wayland) Z 1 ...", "Z"),
        ("1234 (t) T 1 ...", "T"),
    ],
)
def test_parse_proc_state_reads_state_past_comm(stat_contents: str, expected: str) -> None:
    assert parse_proc_state(stat_contents) == expected


@pytest.mark.parametrize("garbage", ["no parens at all", "1234 (comm) ", ""])
def test_parse_proc_state_returns_none_on_unparseable(garbage: str) -> None:
    assert parse_proc_state(garbage) is None


# ---- state file round-trip -------------------------------------------------


def test_prepare_runtime_dir_is_private() -> None:
    rt = prepare_runtime_dir()
    try:
        assert rt.is_dir()
        assert rt.name.startswith("nested-kwin-xdg.")
        assert stat.S_IMODE(rt.stat().st_mode) == 0o700
    finally:
        rt.rmdir()


def test_state_round_trips_through_the_file() -> None:
    rt = prepare_runtime_dir()
    try:
        state = VehicleState(
            runtime_dir=str(rt),
            log=str(rt / "kwin.log"),
            phase="running",
            socket="sock",
            pgid=4321,
            bus="unix:path=/tmp/bus,guid=abc",
            width=1600,
            height=1000,
            outputs=1,
        )
        vehicle._write_state(state)  # pyright: ignore[reportPrivateUsage]
        assert read_state(rt) == state
    finally:
        stop_compositor(rt, None)


def test_read_state_rejects_corrupt_file() -> None:
    rt = prepare_runtime_dir()
    try:
        (rt / vehicle.STATE_FILENAME).write_text("{not valid json")
        with pytest.raises(ValidationError):
            read_state(rt)
    finally:
        stop_compositor(rt, None)


# ---- compositor teardown idempotence ---------------------------------------


def test_stop_compositor_removes_runtime_dir_without_pgid() -> None:
    rt = prepare_runtime_dir()
    (rt / "marker").write_text("x")
    stop_compositor(rt, None)
    assert not rt.exists()


def test_stop_compositor_is_idempotent_on_missing_dir_and_dead_group() -> None:
    rt = prepare_runtime_dir()
    stop_compositor(rt, None)
    # second stop on the already-removed dir and a never-existed group is quiet
    stop_compositor(rt, None)
    stop_compositor(None, _a_reaped_pgid())
    assert not rt.exists()


# ---- driven process-group teardown -----------------------------------------


def _leader(code: str) -> subprocess.Popen[str]:
    """A child in its own session (pid == pgid), printing 'ready' when set up."""
    proc = subprocess.Popen(
        [sys.executable, "-c", code], stdout=subprocess.PIPE, text=True, start_new_session=True
    )
    assert proc.stdout is not None
    assert proc.stdout.readline().strip() == "ready"
    return proc


def _a_reaped_pgid() -> int:
    """A process-group id that is provably gone: spawn, kill, reap."""
    proc = subprocess.Popen([sys.executable, "-c", "pass"], start_new_session=True)
    proc.wait()
    return proc.pid


def test_group_live_status_live_then_gone() -> None:
    proc = _leader("print('ready', flush=True)\nimport time; time.sleep(60)")
    try:
        assert group_live_status(proc.pid) == "live"
    finally:
        os.killpg(proc.pid, signal.SIGKILL)
        proc.wait()
    assert group_live_status(proc.pid) == "gone"


def test_group_live_status_treats_unreaped_zombie_as_gone() -> None:
    proc = subprocess.Popen([sys.executable, "-c", "pass"], start_new_session=True)
    os.kill(proc.pid, signal.SIGKILL)
    # spin until the kernel marks it a zombie (parent has not reaped yet)
    deadline = time.monotonic() + 5
    while parse_proc_state(Path(f"/proc/{proc.pid}/stat").read_text()) not in ("Z", "X"):
        assert time.monotonic() < deadline, "child never became a zombie"
        time.sleep(0.01)
    try:
        assert group_live_status(proc.pid) == "gone"
    finally:
        proc.wait()


def test_stop_process_group_returns_zero_on_already_gone() -> None:
    assert stop_process_group(_a_reaped_pgid(), "gone group") == 0


def test_stop_process_group_terminates_on_sigterm() -> None:
    proc = _leader("print('ready', flush=True)\nimport time; time.sleep(60)")
    code = stop_process_group(proc.pid, "term group", term_attempts=50, term_delay=0.05)
    proc.wait()
    assert code == 0
    assert group_live_status(proc.pid) == "gone"


def test_stop_process_group_escalates_to_kill_on_stopped_term_ignoring_leader() -> None:
    # The exact e2e-seed-cleanup-selftest scenario: a KCrash-style leader that
    # ignores SIGTERM and is SIGSTOPped, so only the bounded SIGKILL clears it.
    proc = _leader(
        "import os, signal, time\n"
        "signal.signal(signal.SIGTERM, signal.SIG_IGN)\n"
        "print('ready', flush=True)\n"
        "os.kill(os.getpid(), signal.SIGSTOP)\n"
        "time.sleep(60)\n"
    )
    started = time.monotonic()
    code = stop_process_group(
        proc.pid,
        "stopped group",
        term_attempts=1,
        term_delay=0.01,
        kill_attempts=100,
        kill_delay=0.01,
    )
    proc.wait()
    assert code == 0
    assert time.monotonic() - started < 10
    assert group_live_status(proc.pid) == "gone"


# ---- socket wait -----------------------------------------------------------


def test_await_socket_returns_once_socket_exists(tmp_path: Path) -> None:
    sock_name = "wl-sock"
    server = socket_mod.socket(socket_mod.AF_UNIX, socket_mod.SOCK_STREAM)
    try:
        server.bind(str(tmp_path / sock_name))
        assert (tmp_path / sock_name).is_socket()
        # a live pgid (this test process) keeps the loop from breaking early
        vehicle._await_socket(tmp_path, sock_name, os.getpid())  # pyright: ignore[reportPrivateUsage]
    finally:
        server.close()


def test_await_socket_raises_when_group_dies_before_binding(tmp_path: Path) -> None:
    with pytest.raises(CompositorStartError):
        vehicle._await_socket(tmp_path, "never", _a_reaped_pgid())  # pyright: ignore[reportPrivateUsage]


# ---- status subcommand -----------------------------------------------------


def test_status_reports_prepared_state(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    vehicle._write_state(  # pyright: ignore[reportPrivateUsage]
        VehicleState(runtime_dir=str(tmp_path), log=str(tmp_path / "kwin.log"), phase="prepared")
    )
    vehicle.main(["status", "--runtime-dir", str(tmp_path)])
    out = capsys.readouterr().out
    assert '"phase":"prepared"' in out
    assert "group_alive=no" in out  # pgid is null in a prepared state


def test_status_fails_cleanly_without_a_state_file(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    with pytest.raises(SystemExit) as excinfo:
        vehicle.main(["status", "--runtime-dir", str(tmp_path / "absent")])
    assert excinfo.value.code == 1
    assert "FAIL no readable vehicle state" in capsys.readouterr().err


# ---- VehicleSession is a plain value ---------------------------------------


def test_vehicle_session_is_frozen() -> None:
    session = VehicleSession(Path("/rt"), "sock", 1, "bus", Path("/rt/log"))
    with pytest.raises((AttributeError, TypeError)):
        session.pgid = 2  # pyright: ignore[reportAttributeAccessIssue]


# ---- the teardown identity gate (the PR #167 review's major finding) --------


def test_parse_proc_starttime_survives_hostile_comm() -> None:
    # comm (field 2) may hold spaces and parens; starttime is field 22, the
    # 20th token after the last ") ".
    tail = "R 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 424242 21 22"
    assert vehicle.parse_proc_starttime(f"123 (kwin) weird) {tail}") == "424242"
    assert vehicle.parse_proc_starttime("garbage with no comm close") is None
    assert vehicle.parse_proc_starttime("123 (x) R 1 2") is None  # tail too short


def test_leader_starttime_reads_a_live_child() -> None:
    proc = _leader("print('ready', flush=True)\nimport time; time.sleep(60)")
    try:
        started = vehicle.leader_starttime(proc.pid)
        assert started is not None and started.isdigit()
    finally:
        os.killpg(proc.pid, signal.SIGKILL)
        proc.wait(timeout=5)


def test_stop_process_group_refuses_a_recycled_leader() -> None:
    # A wrong recorded starttime models the recycled-pid case: the gate must
    # refuse WITHOUT signalling (the innocent process survives) and return 0
    # (the launched group is necessarily gone for a recycle to happen).
    proc = _leader("print('ready', flush=True)\nimport time; time.sleep(60)")
    try:
        code = vehicle.stop_process_group(proc.pid, "probe", expected_starttime="1")
        assert code == 0
        assert proc.poll() is None, "the identity gate signalled an innocent group"
    finally:
        os.killpg(proc.pid, signal.SIGKILL)
        proc.wait(timeout=5)


def test_stop_process_group_proceeds_on_matching_starttime() -> None:
    proc = _leader("print('ready', flush=True)\nimport time; time.sleep(60)")
    recorded = vehicle.leader_starttime(proc.pid)
    assert recorded is not None
    code = vehicle.stop_process_group(proc.pid, "probe", expected_starttime=recorded)
    assert code == 0
    proc.wait(timeout=5)
    assert proc.poll() is not None


def test_stop_compositor_refusal_still_removes_the_runtime_dir(tmp_path: Path) -> None:
    # The non-kill teardown halves must run even when the kill is refused:
    # a stale runtime dir would poison the next run's isolation.
    runtime_dir = tmp_path / "rt"
    runtime_dir.mkdir()
    proc = _leader("print('ready', flush=True)\nimport time; time.sleep(60)")
    try:
        vehicle.stop_compositor(runtime_dir, proc.pid, expected_starttime="1")
        assert proc.poll() is None, "the refused kill still signalled the group"
        assert not runtime_dir.is_dir()
    finally:
        os.killpg(proc.pid, signal.SIGKILL)
        proc.wait(timeout=5)


def test_compositor_start_error_captures_log_before_teardown(tmp_path: Path) -> None:
    # The PR #173 review's dead-diagnostic finding: running_compositor tears
    # the runtime dir down in its finally before the exception reaches any
    # caller, so the log PATH is unreadable there - the TEXT must have been
    # captured at raise time.
    log = tmp_path / "kwin.log"
    log.write_text("the compositor said something important\n")
    err = vehicle.CompositorStartError(tmp_path, "sock", log)
    log.unlink()  # models the finally-teardown deleting the runtime dir
    assert err.log_text == "the compositor said something important\n"
    absent = vehicle.CompositorStartError(tmp_path, "sock", tmp_path / "never-existed")
    assert absent.log_text == ""
