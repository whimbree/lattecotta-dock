# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Typed process control: the exit-code and process-group contract.

The bash harness's hard-won semantics carry over exactly:

- an interrupted run exits 130 (SIGINT) or 143 (SIGTERM), the shell
  convention the gate scripts and the e2e classifier rely on;
- anything the harness starts runs in its own session (the setsid
  discipline), so teardown can kill the whole group without orphaning a
  nested compositor or a fixture window;
- teardown escalates TERM to KILL after a bounded grace and never
  hangs.
"""

from __future__ import annotations

import os
import signal
import subprocess
from collections.abc import Generator, Mapping, Sequence
from contextlib import contextmanager, suppress
from dataclasses import dataclass
from pathlib import Path
from types import FrameType
from typing import IO

EXIT_INT = 130  # 128 + SIGINT, the shell convention for an interrupt
EXIT_TERM = 143  # 128 + SIGTERM


@dataclass(frozen=True, slots=True)
class RunResult:
    argv: tuple[str, ...]
    returncode: int
    stdout: str
    stderr: str


class CommandFailedError(RuntimeError):
    """A checked run() exited nonzero; carries the full result."""

    def __init__(self, result: RunResult) -> None:
        super().__init__(f"command exited {result.returncode}: {' '.join(result.argv)}")
        self.result = result


class SessionProcess:
    """A child in its own session, with bounded whole-group teardown.

    start_new_session makes the child a session leader, so its pid is
    the process-group id and killpg reaches every descendant. This is
    the trap-EXIT + setsid + kill -- -PGID idiom from the bash harness
    as one typed object.
    """

    def __init__(self, popen: subprocess.Popen[str]) -> None:
        self._popen = popen

    @classmethod
    def spawn(
        cls,
        argv: Sequence[str],
        *,
        cwd: Path | None = None,
        env: Mapping[str, str] | None = None,
        stdout: int | IO[str] | None = None,
        stderr: int | IO[str] | None = None,
    ) -> SessionProcess:
        popen = subprocess.Popen(
            list(argv),
            cwd=cwd,
            env=dict(env) if env is not None else None,
            stdout=stdout,
            stderr=stderr,
            text=True,
            start_new_session=True,
        )
        return cls(popen)

    @property
    def pid(self) -> int:
        return self._popen.pid

    @property
    def returncode(self) -> int | None:
        return self._popen.returncode

    @property
    def stdout(self) -> IO[str] | None:
        return self._popen.stdout

    def poll(self) -> int | None:
        return self._popen.poll()

    def wait(self, timeout: float | None = None) -> int:
        return self._popen.wait(timeout=timeout)

    def communicate(self, timeout: float | None = None) -> tuple[str | None, str | None]:
        return self._popen.communicate(timeout=timeout)

    def _signal_group(self, sig: signal.Signals) -> None:
        # A vanished group is fine: teardown is idempotent by design
        # (cleanup paths run from finally-blocks and signal handlers).
        with suppress(ProcessLookupError):
            os.killpg(self.pid, sig)

    def terminate_group(self, grace: float = 5.0) -> int:
        """SIGTERM the whole group, escalate to SIGKILL after ``grace``.

        Returns the leader's exit status. A leader unkillable even by
        SIGKILL (kernel-stuck) raises TimeoutExpired loudly rather than
        hanging or pretending success.
        """
        self._signal_group(signal.SIGTERM)
        try:
            return self._popen.wait(timeout=grace)
        except subprocess.TimeoutExpired:
            self._signal_group(signal.SIGKILL)
            return self._popen.wait(timeout=grace)


@contextmanager
def terminating(proc: SessionProcess, grace: float = 5.0) -> Generator[SessionProcess]:
    """Guarantee group teardown on any exit path (the trap-EXIT idiom)."""
    try:
        yield proc
    finally:
        if proc.poll() is None:
            proc.terminate_group(grace)


def run(
    argv: Sequence[str],
    *,
    cwd: Path | None = None,
    env: Mapping[str, str] | None = None,
    timeout: float | None = None,
    check: bool = False,
    capture: bool = False,
) -> RunResult:
    """Run ``argv`` to completion in its own session.

    The child gets its own session so a timeout kills the whole group; a
    plain subprocess.run timeout kills only the direct child and leaks
    grandchildren, the exact leak the bash harness's setsid discipline
    exists to prevent.
    """
    pipe = subprocess.PIPE if capture else None
    proc = SessionProcess.spawn(argv, cwd=cwd, env=env, stdout=pipe, stderr=pipe)
    try:
        stdout, stderr = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        proc.terminate_group()
        proc.communicate()  # drain pipes now that the group is dead
        raise
    returncode = proc.returncode
    assert returncode is not None  # communicate() reaped the child
    result = RunResult(tuple(argv), returncode, stdout or "", stderr or "")
    if check and result.returncode != 0:
        raise CommandFailedError(result)
    return result


def install_conventional_signal_exits() -> None:
    """Exit 130/143 on SIGINT/SIGTERM via SystemExit.

    SystemExit unwinds the stack, so terminating() blocks and other
    finally-paths tear their process groups down before the interpreter
    exits with the conventional code - the behavior every bash script's
    ``trap 'cleanup; exit 130' INT`` provided, as one call.
    """

    def _raise_exit(signum: int, _frame: FrameType | None) -> None:
        raise SystemExit(128 + signum)

    signal.signal(signal.SIGINT, _raise_exit)
    signal.signal(signal.SIGTERM, _raise_exit)
