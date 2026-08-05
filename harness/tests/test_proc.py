# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The process-control contract: exit codes, group teardown, escalation.

These are driven tests against real child processes, not mocks; the
contract under test is exactly the bash harness's setsid/trap/kill
discipline the vehicle port (BP-2a) will ride on.
"""

import signal
import subprocess
import sys
import time
from pathlib import Path

import pytest

from latte_harness.proc import (
    EXIT_TERM,
    CommandFailedError,
    SessionProcess,
    run,
    terminating,
)


def _py(code: str) -> list[str]:
    return [sys.executable, "-c", code]


def test_run_reports_exit_code_and_output() -> None:
    result = run(
        _py("import sys; print('out'); print('err', file=sys.stderr); sys.exit(3)"),
        capture=True,
    )
    assert result.returncode == 3
    assert result.stdout == "out\n"
    assert "err" in result.stderr


def test_run_check_raises_with_result() -> None:
    with pytest.raises(CommandFailedError) as excinfo:
        run(_py("raise SystemExit(7)"), check=True)
    assert excinfo.value.result.returncode == 7


def test_run_timeout_raises_and_reaps() -> None:
    started = time.monotonic()
    with pytest.raises(subprocess.TimeoutExpired):
        run(_py("import time; time.sleep(60)"), timeout=1, capture=True)
    assert time.monotonic() - started < 30


def _spawn_ready(code: str) -> SessionProcess:
    """Spawn a child that prints 'ready' once its setup is complete."""
    proc = SessionProcess.spawn(_py(code), stdout=subprocess.PIPE)
    stdout = proc.stdout
    assert stdout is not None
    assert stdout.readline().strip() == "ready"
    return proc


def test_terminate_group_kills_grandchildren(tmp_path: Path) -> None:
    marker = tmp_path / "heartbeat"
    grandchild = (
        "import pathlib, time\n"
        f"p = pathlib.Path({str(marker)!r})\n"
        "for _ in range(400):\n"
        "    p.touch()\n"
        "    time.sleep(0.05)\n"
    )
    child = (
        "import subprocess, sys, time\n"
        f"subprocess.Popen([sys.executable, '-c', {grandchild!r}])\n"
        "print('ready', flush=True)\n"
        "time.sleep(60)\n"
    )
    proc = _spawn_ready(child)
    deadline = time.monotonic() + 10
    while not marker.exists():
        assert time.monotonic() < deadline, "grandchild never heartbeated"
        time.sleep(0.02)

    assert proc.terminate_group() == -signal.SIGTERM

    # The whole group is dead: the heartbeat must stop advancing.
    time.sleep(0.3)
    mtime = marker.stat().st_mtime_ns
    time.sleep(0.4)
    assert marker.stat().st_mtime_ns == mtime, "grandchild survived the group kill"


def test_terminate_group_escalates_to_kill() -> None:
    proc = _spawn_ready(
        "import signal, time\n"
        "signal.signal(signal.SIGTERM, signal.SIG_IGN)\n"
        "print('ready', flush=True)\n"
        "time.sleep(60)\n"
    )
    started = time.monotonic()
    assert proc.terminate_group(grace=0.5) == -signal.SIGKILL
    assert time.monotonic() - started < 10


def test_terminating_cleans_up_on_exception() -> None:
    proc = _spawn_ready("print('ready', flush=True)\nimport time; time.sleep(60)\n")
    with pytest.raises(RuntimeError, match="boom"), terminating(proc):
        raise RuntimeError("boom")
    assert proc.poll() is not None


def test_conventional_signal_exit_runs_finally_blocks(tmp_path: Path) -> None:
    marker = tmp_path / "cleanup-ran"
    code = (
        "from latte_harness.proc import install_conventional_signal_exits\n"
        "import os, pathlib, signal, sys\n"
        "install_conventional_signal_exits()\n"
        "try:\n"
        "    os.kill(os.getpid(), signal.SIGTERM)\n"
        "finally:\n"
        "    pathlib.Path(sys.argv[1]).touch()\n"
    )
    result = run([sys.executable, "-c", code, str(marker)], capture=True)
    assert result.returncode == EXIT_TERM
    assert marker.exists(), "finally-block did not run on the way out"
