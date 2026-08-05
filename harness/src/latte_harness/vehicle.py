# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Nested kwin_wayland compositor lifecycle: the out-of-session vehicle.

The typed port of scripts/lib-nested-kwin.sh (BP-2a). The bash version was
sourced into a caller's shell and kept its lifecycle in shell variables that
persisted across the whole run; teardown was the caller's own
``trap nested_kwin_cleanup EXIT INT TERM``. This module instead exposes the
lifecycle as state-file-driven subcommands, because the bridge (still bash)
must hand the same variables back to the consumers that read them:

- ``prepare`` mktemps a private ``XDG_RUNTIME_DIR`` (mode 0700), writes the
  initial state file under it, and emits the eval-able shell the bridge sources
  (``NESTED_RT`` and ``NESTED_KWIN_LOG``).
- ``start`` brings up ``kwin_wayland --virtual`` inside its own
  ``dbus-run-session`` and its own session/process group, waits for the wayland
  socket, publishes the private bus address, records pid/pgid/socket/bus in the
  state file, and emits ``NESTED_SOCK`` / ``NESTED_KWIN_PID`` / ``NESTED_BUS``.
  It exits 2 (after printing kwin's log) if the socket never appears. On any
  interruption or failure BEFORE it reports the pid, it tears the compositor it
  spawned back down: the bash caller could clean up on a mid-start failure only
  because ``NESTED_KWIN_PID=$!`` was set in its own shell before the socket
  wait; the bridge cannot see that pid until start emits it, so start owns the
  no-leak guarantee for its own start window.
- ``stop`` is the exact nested_kwin_cleanup teardown: whole-group TERM then a
  bounded escalation to KILL, FUSE unmount of ``RT/doc``, runtime-dir removal,
  and the recreation contract check. Idempotent: a dead group or a missing dir
  succeed quietly (the trap idiom).
- ``stop-group`` is the zombie-aware, bounded process-group transaction that
  the seed reuses for its throwaway dock. It mirrors the bash
  latte_package_gate_stop_process_group (which the seed reused via
  scripts/lib-installed-package-gate.sh); that bash helper is ported in BP-4,
  and its output prefix is preserved verbatim here so the seed's teardown reads
  identically until then.
- ``status`` reads the state file and reports the recorded fields plus group
  liveness (the observability surface; not consumed by the bridge).

Why the port owns the process group and not the launching shell: bash launched
the compositor with ``setsid ... &`` in the caller's shell, so it stayed a child
of that shell and ``wait`` reaped it. Here ``start`` launches it in its own
session (SessionProcess / start_new_session) and then EXITS, so the compositor
reparents to init. Every teardown therefore works by process GROUP id
(``killpg`` / ``pgrep -g``), never by a parent-child ``wait`` that no longer
holds. The observable is identical (the group is gone), and killing by pgid is
independent of who the parent is, which is exactly what the installed-package
gate relies on when it stops the compositor by ``NESTED_KWIN_PID`` itself.
"""

from __future__ import annotations

import argparse
import os
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from collections.abc import Generator, Mapping, Sequence
from contextlib import contextmanager, suppress
from dataclasses import dataclass
from pathlib import Path

from pydantic import BaseModel

from latte_harness.proc import SessionProcess, install_conventional_signal_exits

TOOL = "nested-kwin"

# start's socket wait: 150 tries at 0.1s (bash `for _i in $(seq 1 150)`), up to
# 15s for the virtual compositor to bind its wayland socket.
SOCKET_WAIT_ATTEMPTS = 150
SOCKET_WAIT_DELAY = 0.1

# stop's compositor group escalation: after SIGTERM, poll the group (members
# included, zombies and all, exactly like the bash `pgrep -g`) for up to 5s,
# then SIGKILL and let it settle. Best-effort: bash never re-verified after the
# KILL, it went straight on to removing the runtime dir.
CLEANUP_GROUP_POLL_ATTEMPTS = 50
CLEANUP_GROUP_POLL_DELAY = 0.1
CLEANUP_KILL_SETTLE = 0.2

# after removing the runtime dir, wait this long and re-check: kwin/kded can
# flush config on SIGTERM and recreate a dir inside RT after the rm (a leftover
# holding a fresh kwinoutputconfig.json was caught on the e2e driver's first
# day). If it reappears, name the survivor loudly, then remove again.
CLEANUP_RECREATE_SETTLE = 0.3

# stop-group defaults match latte_package_gate_stop_process_group: 25 tries at
# 0.2s for each of the TERM and KILL waits (5s apiece).
STOP_GROUP_DEFAULT_ATTEMPTS = 25
STOP_GROUP_DEFAULT_DELAY = 0.2

# stop-group is the typed twin of latte_package_gate_stop_process_group, which
# lived in scripts/lib-installed-package-gate.sh. Its diagnostics kept the
# "installed-package-gate:" tool prefix; preserve it verbatim so the seed's
# teardown reads identically until BP-4 ports and unifies that helper.
_PKG_GATE_TOOL = "installed-package-gate"

EXIT_START_FAILED = 2  # socket never appeared (bash nested_kwin_start `return 2`)
EXIT_STOP_GROUP_FAILED = 2  # the group outlived a bounded SIGKILL, or a poll error

STATE_FILENAME = "vehicle-state.json"

# The kwin launch wrapper, byte-identical to lib-nested-kwin.sh's `sh -c`. It
# runs under dbus-run-session so $DBUS_SESSION_BUS_ADDRESS is set, publishes
# that address to $1/bus-address (how both this module and any further client
# discover the one private bus), then execs kwin. $1=runtime dir, $2=width,
# $3=height, $4=socket, $5=output-count. Only >1 outputs adds --output-count,
# so a single-output request stays the historical command line.
_KWIN_LAUNCH_WRAPPER = (
    'printf %s "$DBUS_SESSION_BUS_ADDRESS" >"$1/bus-address";\n'
    'if [ "$5" -gt 1 ]; then\n'
    '    exec kwin_wayland --virtual --output-count "$5" --width "$2" --height "$3"'
    ' --no-lockscreen --socket "$4";\n'
    "else\n"
    '    exec kwin_wayland --virtual --width "$2" --height "$3" --no-lockscreen --socket "$4";\n'
    "fi"
)


class VehicleState(BaseModel):
    """The durable record start writes under the runtime dir.

    pydantic validates it on read (the boundary contract): a corrupt or
    truncated state file fails loudly at ``status`` rather than yielding a bogus
    pid. All lifecycle fields past ``phase == "prepared"`` are optional because
    prepare records the dir before the compositor exists.
    """

    runtime_dir: str
    log: str
    phase: str
    socket: str | None = None
    pgid: int | None = None
    leader_starttime: str | None = None
    bus: str | None = None
    width: int | None = None
    height: int | None = None
    outputs: int | None = None


@dataclass(frozen=True, slots=True)
class VehicleSession:
    """A running nested compositor, as the library (the seed) sees it."""

    runtime_dir: Path
    socket: str
    pgid: int
    bus: str
    log: Path
    leader_starttime: str | None = None


class CompositorStartError(RuntimeError):
    """kwin never brought up its wayland socket within the bounded wait.

    The kwin log's TEXT is captured eagerly at raise time: running_compositor
    tears the runtime dir (and the log file in it) down in its finally BEFORE
    the exception reaches any caller, so a caller reading the ``log`` path
    gets nothing - the PR #173 review found exactly that dead diagnostic.
    Callers print ``log_text``; the path stays for identification only.
    """

    def __init__(self, runtime_dir: Path, socket: str, log: Path) -> None:
        super().__init__(
            f"nested kwin_wayland never brought up socket {socket} under {runtime_dir}"
        )
        self.runtime_dir = runtime_dir
        self.socket = socket
        self.log = log
        try:
            self.log_text: str = log.read_text()
        except OSError:
            self.log_text = ""


class MalformedEnvError(ValueError):
    """An extra-env token was not VAR=VALUE (a bug in the caller, refused loud)."""


# ---- pure logic (unit-tested directly) -------------------------------------


def build_session_env(
    base_env: Mapping[str, str],
    runtime_dir: Path,
    extra_env: Sequence[str],
) -> dict[str, str]:
    """The compositor's environment, mirroring the bash ``env`` prefix exactly.

    Start from the caller's environment, STRIP DISPLAY and XAUTHORITY (nothing
    in the nested session needs the real X server; leaving them inherited let
    every dbus-activated service open connections to the desk Xwayland that
    never closed and once saturated the X client limit), force the private
    ``XDG_RUNTIME_DIR`` and ``KWIN_WAYLAND_NO_PERMISSION_CHECKS=1``, then apply
    each ``VAR=VALUE`` the caller appended to ``nested_kwin_env``.

    A token without ``=`` is a caller bug and is refused loudly rather than
    silently dropped (the failures-and-root-cause contract).
    """
    env = dict(base_env)
    env.pop("DISPLAY", None)
    env.pop("XAUTHORITY", None)
    env["XDG_RUNTIME_DIR"] = str(runtime_dir)
    env["KWIN_WAYLAND_NO_PERMISSION_CHECKS"] = "1"
    for token in extra_env:
        key, sep, value = token.partition("=")
        if not sep or not key:
            raise MalformedEnvError(f"extra env token is not VAR=VALUE: {token!r}")
        env[key] = value
    return env


def build_kwin_argv(
    runtime_dir: Path, width: int, height: int, socket: str, outputs: int
) -> list[str]:
    """The full argv: dbus-run-session wrapping the kwin launch wrapper.

    The positional tail (``sh RT W H SOCK OUTPUTS``) is what ``sh -c`` reads as
    ``$0..$5``; ``$0`` is the conventional ``sh`` name so ``$1`` is the runtime
    dir, matching the bash exactly.
    """
    return [
        "dbus-run-session",
        "--",
        "sh",
        "-c",
        _KWIN_LAUNCH_WRAPPER,
        "sh",
        str(runtime_dir),
        str(width),
        str(height),
        socket,
        str(outputs),
    ]


def parse_proc_state(stat_contents: str) -> str | None:
    """The single-char process state from ``/proc/<pid>/stat`` contents.

    The comm field (field 2) is wrapped in parens and can itself contain spaces
    and parens, so the state is the first token AFTER the last ``") "`` - the
    same ``${stat_line##*) }`` / ``${stat_tail%% *}`` the bash used. Returns None
    when the contents cannot be parsed that way.
    """
    _, sep, tail = stat_contents.rpartition(") ")
    if not sep:
        return None
    tokens = tail.split()
    return tokens[0] if tokens else None


def parse_proc_starttime(stat_contents: str) -> str | None:
    """Field 22 (starttime, clock ticks since boot) from ``/proc/<pid>/stat``.

    A (pid, starttime) pair uniquely names one process incarnation: a recycled
    pid never reproduces the original's starttime. Parsed after the last
    ``") "`` like parse_proc_state (the comm field can hold spaces and parens);
    starttime is the 20th token of the tail (field 22 overall).
    """
    _, sep, tail = stat_contents.rpartition(") ")
    if not sep:
        return None
    tokens = tail.split()
    return tokens[19] if len(tokens) >= 20 else None


def leader_starttime(pid: int) -> str | None:
    """The live starttime of ``pid``, or None when it has no /proc entry."""
    try:
        return parse_proc_starttime(Path(f"/proc/{pid}/stat").read_text())
    except OSError:
        return None


# ---- runtime-dir lifecycle -------------------------------------------------


def prepare_runtime_dir() -> Path:
    """mktemp the private runtime dir (bash ``mktemp -d`` + ``chmod 700``).

    mkdtemp already creates the dir with mode 0700, so the explicit chmod the
    bash did for belt-and-braces is unnecessary here.
    """
    return Path(tempfile.mkdtemp(prefix="nested-kwin-xdg.", dir="/tmp"))


def log_path(runtime_dir: Path) -> Path:
    return runtime_dir / "kwin.log"


def _state_path(runtime_dir: Path) -> Path:
    return runtime_dir / STATE_FILENAME


def _write_state(state: VehicleState) -> None:
    _state_path(Path(state.runtime_dir)).write_text(state.model_dump_json())


def read_state(runtime_dir: Path) -> VehicleState:
    return VehicleState.model_validate_json(_state_path(runtime_dir).read_text())


# ---- compositor bring-up ---------------------------------------------------


def _spawn_compositor(
    runtime_dir: Path,
    width: int,
    height: int,
    socket: str,
    outputs: int,
    extra_env: Sequence[str],
    base_env: Mapping[str, str],
) -> tuple[int, str | None]:
    """Launch the compositor session group; return (pgid, leader starttime).

    The child is its own session leader (SessionProcess / start_new_session),
    so the returned pid is the process-group id every teardown targets. Its
    stdout+stderr are redirected to the log file, so the process holds no copy
    of any command-substitution pipe the bridge captured: the bridge's
    ``$(... start)`` sees EOF the instant this ``start`` process exits, even
    though the compositor keeps running.

    The starttime is read here, while the leader is still this process's
    child (even freshly dead it would be an unreaped zombie with a readable
    /proc entry), so the identity the teardown gate compares against is
    captured race-free.
    """
    argv = build_kwin_argv(runtime_dir, width, height, socket, outputs)
    session_env = build_session_env(base_env, runtime_dir, extra_env)
    log = log_path(runtime_dir)
    with log.open("w") as handle:
        proc = SessionProcess.spawn(argv, env=session_env, stdout=handle, stderr=subprocess.STDOUT)
    starttime = leader_starttime(proc.pid)
    if starttime is None:
        # Cannot happen while the leader is this process's unreaped child; a
        # None here means /proc itself misbehaved. Warn loudly instead of
        # silently running the whole session without the teardown identity
        # gate (degenerate values are symptoms, not things to swallow).
        print(
            f"{TOOL}: WARNING: no starttime readable for leader {proc.pid}; "
            "the teardown identity gate is OFF for this session",
            file=sys.stderr,
            flush=True,
        )
    return proc.pid, starttime


def _await_socket(runtime_dir: Path, socket: str, pgid: int) -> None:
    """Wait for the wayland socket, or raise CompositorStartError.

    Breaks early if the compositor group dies before binding (bash
    ``kill -0 "$NESTED_KWIN_PID" || break``); a dead group can never bind, so
    there is no point waiting the full 15s.
    """
    socket_path = runtime_dir / socket
    for _ in range(SOCKET_WAIT_ATTEMPTS):
        if socket_path.is_socket():
            return
        if not _group_has_members(pgid):
            break
        time.sleep(SOCKET_WAIT_DELAY)
    if not socket_path.is_socket():
        raise CompositorStartError(runtime_dir, socket, log_path(runtime_dir))


def read_bus_address(runtime_dir: Path) -> str:
    return (runtime_dir / "bus-address").read_text()


@contextmanager
def running_compositor(
    runtime_dir: Path,
    width: int,
    height: int,
    socket: str,
    outputs: int,
    extra_env: Sequence[str],
    base_env: Mapping[str, str],
) -> Generator[VehicleSession]:
    """Bring the compositor up and guarantee teardown on every exit path.

    The library entry the seed uses: unlike the ``start`` subcommand (which must
    LEAVE the compositor running for the bridge), this tears it down in the
    finally, so a socket timeout, an assertion failure, or a signal all reclaim
    the process group and the runtime dir. This is the bash seed's
    ``trap nested_kwin_cleanup EXIT`` expressed as a context manager.
    """
    pgid, starttime = _spawn_compositor(
        runtime_dir, width, height, socket, outputs, extra_env, base_env
    )
    try:
        _await_socket(runtime_dir, socket, pgid)
        bus = read_bus_address(runtime_dir)
        session = VehicleSession(runtime_dir, socket, pgid, bus, log_path(runtime_dir), starttime)
        _write_state(_running_state(session, width, height, outputs))
        yield session
    finally:
        stop_compositor(runtime_dir, pgid, starttime)


def _running_state(session: VehicleSession, width: int, height: int, outputs: int) -> VehicleState:
    return VehicleState(
        runtime_dir=str(session.runtime_dir),
        log=str(session.log),
        phase="running",
        socket=session.socket,
        pgid=session.pgid,
        leader_starttime=session.leader_starttime,
        bus=session.bus,
        width=width,
        height=height,
        outputs=outputs,
    )


# ---- process-group teardown primitives -------------------------------------


def _killpg(pgid: int, sig: signal.Signals) -> bool:
    with suppress(ProcessLookupError, PermissionError):
        os.killpg(pgid, sig)
        return True
    return False


def _kill_leader(pid: int, sig: signal.Signals) -> bool:
    with suppress(ProcessLookupError, PermissionError):
        os.kill(pid, sig)
        return True
    return False


def _signal_group_or_leader(pgid: int, sig: signal.Signals) -> None:
    """Signal the whole group, falling back to the leader alone.

    Mirrors bash ``kill -- -PID || kill PID`` (both error paths swallowed): the
    fallback matters when the group has already collapsed to just the leader.
    """
    if not _killpg(pgid, sig):
        _kill_leader(pgid, sig)


def _leader_identity_intact(pgid: int, expected_starttime: str | None) -> bool:
    """True when signalling ``pgid`` can only reach the group that was launched.

    The kernel reserves a pid for reuse only while some process still
    references it as pid, pgid, or sid. Cases:

    - leader present with the recorded starttime: the launched group.
    - leader present with a DIFFERENT starttime: the leader died, the group
      emptied (freeing the id), and the kernel recycled the pid into an
      unrelated process; signalling now could hit an innocent group. Refuse.
    - leader absent: either the leaderless group still holds members (the
      pgid stays reserved for exactly them, so killpg reaches only them) or
      the whole group is gone (killpg is an ESRCH no-op). Safe for the
      single-recycle case; the honest residual is the double-death corner
      (the recycled leader ALSO dies and is reaped while its children keep
      the group alive), which a single leader starttime cannot discriminate.
      Closing that fully needs per-member identity; the corner is accepted
      as astronomically rare and recorded here rather than papered over.

    Bash never faced the recycled case: the compositor stayed the consumer
    shell's unreaped child, so its pid was held (zombie) until cleanup's
    wait. The reparent-to-init design gives that hold away - init reaps the
    dead leader promptly - and this check is the replacement guarantee. A
    None ``expected_starttime`` (no recorded identity: a pre-gate state or
    a bash-owned group whose caller still holds the zombie) skips the check,
    keeping the caller's original semantics.
    """
    if expected_starttime is None:
        return True
    current = leader_starttime(pgid)
    return current is None or current == expected_starttime


def _refuse_recycled_group(pgid: int, expected_starttime: str | None, tool: str) -> None:
    print(
        f"{tool}: cleanup: refusing to signal process group {pgid}: its leader's "
        f"starttime {leader_starttime(pgid)!r} does not match the recorded "
        f"{expected_starttime!r} (the launched group is gone and the pid was "
        "recycled); skipping the kill, continuing the non-kill teardown",
        file=sys.stderr,
        flush=True,
    )


def _group_has_members(pgid: int) -> bool:
    """Any process still in group ``pgid`` (bash ``pgrep -g``, zombies counted).

    Deliberately zombie-inclusive to match nested_kwin_cleanup, which used the
    plain ``pgrep -g``; the compositor's members reparent to init and are reaped
    there, so a zombie is a transient the poll simply waits out.
    """
    result = subprocess.run(["pgrep", "-g", str(pgid)], capture_output=True, text=True, check=False)
    return result.returncode == 0


def _read_proc_state(pid: int) -> str | None:
    try:
        return parse_proc_state(Path(f"/proc/{pid}/stat").read_text())
    except OSError:
        return None


def group_live_status(pgid: int) -> str:
    """'live' | 'gone' | 'error', the zombie-AWARE poll for stop-group.

    Mirrors latte_package_gate_process_group_has_live_members exactly: a member
    in state Z (zombie) or X (dead) does not count as live, any other uppercase
    state does, and a member that vanishes between the pgrep and the procfs read
    is skipped. Anything the bash would have FAILed on (a pgrep transport error,
    an unreadable/unparseable live member) is reported as 'error', so a genuine
    fault is loud instead of masquerading as 'gone'.
    """
    result = subprocess.run(["pgrep", "-g", str(pgid)], capture_output=True, text=True, check=False)
    if result.returncode == 1:
        return "gone"
    if result.returncode != 0:
        detail = f": {result.stderr.strip()}" if result.stderr.strip() else ""
        print(
            f"{_PKG_GATE_TOOL}: FAIL: pgrep failed while polling process group "
            f"{pgid} with status {result.returncode}{detail}",
            file=sys.stderr,
            flush=True,
        )
        return "error"
    if not result.stdout.strip():
        print(
            f"{_PKG_GATE_TOOL}: FAIL: pgrep returned success without members "
            f"for process group {pgid}",
            file=sys.stderr,
            flush=True,
        )
        return "error"
    for line in result.stdout.splitlines():
        pid_text = line.strip()
        if not pid_text.isdigit():
            print(
                f"{_PKG_GATE_TOOL}: FAIL: pgrep returned an invalid pid while "
                f"polling process group {pgid}: {pid_text}",
                file=sys.stderr,
                flush=True,
            )
            return "error"
        pid = int(pid_text)
        state = _read_proc_state(pid)
        if state is None:
            if not Path(f"/proc/{pid}").is_dir():
                continue  # member disappeared between pgrep and the procfs read
            print(
                f"{_PKG_GATE_TOOL}: FAIL: cannot read state for process-group member {pid}",
                file=sys.stderr,
                flush=True,
            )
            return "error"
        if state in ("Z", "X"):
            continue
        if len(state) == 1 and "A" <= state <= "Z":
            return "live"
        print(
            f"{_PKG_GATE_TOOL}: FAIL: cannot parse state for process-group member {pid}",
            file=sys.stderr,
            flush=True,
        )
        return "error"
    return "gone"


def _wait_group_exits(pgid: int, attempts: int, delay: float) -> str:
    """Poll group_live_status until 'gone'/'error' or the bounded tries run out.

    Mirrors latte_package_gate_wait_until_process_group_exits: it checks, sleeps
    only while 'live', and returns the final status (which may still be 'live').
    """
    for _ in range(attempts):
        status = group_live_status(pgid)
        if status != "live":
            return status
        time.sleep(delay)
    return group_live_status(pgid)


def stop_process_group(
    pgid: int,
    label: str,
    term_attempts: int = STOP_GROUP_DEFAULT_ATTEMPTS,
    term_delay: float = STOP_GROUP_DEFAULT_DELAY,
    kill_attempts: int = STOP_GROUP_DEFAULT_ATTEMPTS,
    kill_delay: float = STOP_GROUP_DEFAULT_DELAY,
    expected_starttime: str | None = None,
) -> int:
    """Zombie-aware bounded group teardown; 0 on success, 2 on failure/error.

    The typed twin of latte_package_gate_stop_process_group, which the seed
    reused for its throwaway dock (a KCrash can leave the leader STOPPED, so a
    leader-only SIGTERM+wait can never finish; the group transaction escalates
    to SIGKILL on a bound). Messages keep the bash prefix verbatim so the
    teardown reads identically until BP-4 unifies the helper.

    A recycled-leader refusal returns 0: the recycle proves the launched
    group already emptied, so the teardown goal is met and only the near-miss
    is reported.
    """
    if not _leader_identity_intact(pgid, expected_starttime):
        _refuse_recycled_group(pgid, expected_starttime, _PKG_GATE_TOOL)
        return 0
    initial = group_live_status(pgid)
    if initial == "gone":
        return 0
    if initial == "error":
        return EXIT_STOP_GROUP_FAILED

    _signal_group_or_leader(pgid, signal.SIGTERM)
    after_term = _wait_group_exits(pgid, term_attempts, term_delay)
    if after_term == "gone":
        return 0
    if after_term == "error":
        return EXIT_STOP_GROUP_FAILED

    print(
        f"{_PKG_GATE_TOOL}: cleanup: {label} survived SIGTERM; sending SIGKILL",
        file=sys.stderr,
        flush=True,
    )
    _signal_group_or_leader(pgid, signal.SIGKILL)
    after_kill = _wait_group_exits(pgid, kill_attempts, kill_delay)
    if after_kill == "gone":
        return 0
    if after_kill == "live":
        print(
            f"{_PKG_GATE_TOOL}: cleanup: {label} still exists after bounded SIGKILL wait",
            file=sys.stderr,
            flush=True,
        )
    return EXIT_STOP_GROUP_FAILED


# ---- compositor teardown (nested_kwin_cleanup) -----------------------------


def _terminate_compositor_group(pgid: int, expected_starttime: str | None = None) -> None:
    """SIGTERM the group, poll up to 5s, then SIGKILL - best-effort, always
    proceeds to the runtime-dir removal (the bash never re-verified after KILL).

    Killing the WHOLE group, not just the leader, is load-bearing: killing the
    dbus-run-session leader alone orphaned the kwin child often enough that a
    day of runs once left hundreds of virtual compositors alive.
    """
    if not _leader_identity_intact(pgid, expected_starttime):
        _refuse_recycled_group(pgid, expected_starttime, TOOL)
        return
    _signal_group_or_leader(pgid, signal.SIGTERM)
    for _ in range(CLEANUP_GROUP_POLL_ATTEMPTS):
        if not _group_has_members(pgid):
            break
        time.sleep(CLEANUP_GROUP_POLL_DELAY)
    if _group_has_members(pgid):
        _killpg(pgid, signal.SIGKILL)  # KILL has no leader fallback in the bash
        time.sleep(CLEANUP_KILL_SETTLE)


def _fuse_unmount(doc: Path) -> None:
    """Unmount the xdg-desktop-portal FUSE mount at ``RT/doc`` before removal.

    The nested bus activates xdg-desktop-portal, which FUSE-mounts ``RT/doc``;
    removing the runtime dir without unmounting first leaves the mountpoint
    behind. Try fusermount3 then fusermount, ignoring an absent tool or a
    nothing-mounted error (bash ``... || ... || true``).
    """
    for tool in ("fusermount3", "fusermount"):
        try:
            result = subprocess.run([tool, "-u", str(doc)], capture_output=True, check=False)
        except FileNotFoundError:
            continue
        if result.returncode == 0:
            return


def _report_runtime_dir_survivors(runtime_dir: Path) -> None:
    """Name whatever recreated the runtime dir after removal, loudly, on stderr.

    A silent survivor holds a live config path and pollutes the next run's
    isolation, so the writer is named rather than quietly cleaned - either by
    cmdline (pgrep -af) or, if only an env reference, said so.
    """
    result = subprocess.run(
        ["pgrep", "-af", str(runtime_dir)], capture_output=True, text=True, check=False
    )
    if result.returncode == 0 and result.stdout.strip():
        sys.stderr.write(result.stdout)
    else:
        print("  (none found by cmdline; an env-only reference)", file=sys.stderr)
    for path in sorted(runtime_dir.rglob("*")):
        if path.is_file():
            print(str(path), file=sys.stderr)
    sys.stderr.flush()


def _remove_runtime_dir(runtime_dir: Path) -> None:
    _fuse_unmount(runtime_dir / "doc")
    shutil.rmtree(runtime_dir, ignore_errors=True)
    time.sleep(CLEANUP_RECREATE_SETTLE)
    if runtime_dir.is_dir():
        print(
            f"{TOOL} cleanup: {runtime_dir} was recreated after removal; survivors referencing it:",
            file=sys.stderr,
            flush=True,
        )
        _report_runtime_dir_survivors(runtime_dir)
        shutil.rmtree(runtime_dir, ignore_errors=True)


def stop_compositor(
    runtime_dir: Path | None, pgid: int | None, expected_starttime: str | None = None
) -> None:
    """The nested_kwin_cleanup teardown, driven by the two governing inputs.

    The two halves are independent, exactly as the bash guarded them with
    ``[ -n "${NESTED_KWIN_PID:-}" ]`` and ``[ -n "${NESTED_RT:-}" ]``. A caller
    that has already stopped the group itself (the installed-package gate does)
    passes ``pgid=None`` to skip the kill and still get the FUSE unmount and the
    runtime-dir removal. Both halves are idempotent: a dead group and a missing
    dir succeed quietly.
    """
    if pgid is not None:
        _terminate_compositor_group(pgid, expected_starttime)
    if runtime_dir is not None:
        _remove_runtime_dir(runtime_dir)


# ---- shell emission for the bridge -----------------------------------------


def _emit_prepare_shell(runtime_dir: Path) -> None:
    print(f"NESTED_RT={shlex.quote(str(runtime_dir))}")
    print(f"NESTED_KWIN_LOG={shlex.quote(str(log_path(runtime_dir)))}")


def _emit_start_shell(socket: str, pgid: int, bus: str, starttime: str | None) -> None:
    print(f"NESTED_SOCK={shlex.quote(socket)}")
    print(f"NESTED_KWIN_PID={pgid}")
    # The teardown identity gate's input; the bridge owns it (no consumer
    # reads it) and hands it back to stop as --starttime.
    print(f"NESTED_KWIN_STARTTIME={shlex.quote(starttime or '')}")
    print(f"NESTED_BUS={shlex.quote(bus)}")


# ---- subcommands -----------------------------------------------------------


def _cmd_prepare() -> None:
    runtime_dir = prepare_runtime_dir()
    _write_state(
        VehicleState(runtime_dir=str(runtime_dir), log=str(log_path(runtime_dir)), phase="prepared")
    )
    _emit_prepare_shell(runtime_dir)


def _cmd_start(args: argparse.Namespace) -> None:
    install_conventional_signal_exits()
    runtime_dir = Path(args.runtime_dir)
    socket: str = args.socket
    extra_env: list[str] = list(args.env)
    pgid, starttime = _spawn_compositor(
        runtime_dir, args.width, args.height, socket, args.outputs, extra_env, os.environ
    )
    try:
        _await_socket(runtime_dir, socket, pgid)
        bus = read_bus_address(runtime_dir)
    except CompositorStartError as err:
        # Tear down the compositor this start spawned before reporting: the
        # bridge has no pid to clean up with until start emits it (see module
        # docstring), so the no-leak guarantee for the start window lives here.
        print(
            f"{TOOL}: nested kwin_wayland never brought up its socket; its log:",
            file=sys.stderr,
            flush=True,
        )
        sys.stderr.write(err.log_text)
        sys.stderr.flush()
        stop_compositor(runtime_dir, pgid, starttime)
        raise SystemExit(EXIT_START_FAILED) from err
    except BaseException:
        # SIGINT/SIGTERM (SystemExit from the conventional-exit handler) or any
        # other fault mid-start: same no-leak guarantee, then propagate the
        # original exit code (130/143 stay intact).
        stop_compositor(runtime_dir, pgid, starttime)
        raise
    session = VehicleSession(runtime_dir, socket, pgid, bus, log_path(runtime_dir), starttime)
    _write_state(_running_state(session, args.width, args.height, args.outputs))
    _emit_start_shell(socket, pgid, bus, starttime)


def _optional_pgid(value: str) -> int | None:
    if not value:
        return None
    if not value.lstrip("-").isdigit():
        print(f"{TOOL}: FAIL --pgid must be an integer, got {value!r}", file=sys.stderr, flush=True)
        raise SystemExit(1)
    return int(value)


def _cmd_stop(args: argparse.Namespace) -> None:
    runtime_dir = Path(args.runtime_dir) if args.runtime_dir else None
    pgid = _optional_pgid(args.pgid)
    stop_compositor(runtime_dir, pgid, args.starttime or None)


def _cmd_stop_group(args: argparse.Namespace) -> None:
    code = stop_process_group(
        args.pgid,
        args.label,
        term_attempts=args.term_attempts,
        term_delay=args.term_delay,
        kill_attempts=args.kill_attempts,
        kill_delay=args.kill_delay,
        expected_starttime=args.starttime or None,
    )
    raise SystemExit(code)


def _cmd_status(args: argparse.Namespace) -> None:
    runtime_dir = Path(args.runtime_dir)
    try:
        state = read_state(runtime_dir)
    except (OSError, ValueError) as err:
        # No readable/valid state file: fail loudly and cleanly (the observability
        # surface refuses at its boundary rather than dumping a raw traceback).
        # OSError covers a missing dir/file; ValueError covers pydantic's
        # ValidationError on a corrupt or truncated state file.
        print(
            f"{TOOL}: FAIL no readable vehicle state under {runtime_dir}: {err}",
            file=sys.stderr,
            flush=True,
        )
        raise SystemExit(1) from err
    alive = state.pgid is not None and _group_has_members(state.pgid)
    print(state.model_dump_json())
    print(f"group_alive={'yes' if alive else 'no'}")


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="latte_harness.vehicle", description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("prepare", help="mktemp the runtime dir; emit NESTED_RT / NESTED_KWIN_LOG")

    start = sub.add_parser("start", help="bring up the compositor; emit NESTED_SOCK/PID/BUS")
    start.add_argument("--runtime-dir", required=True)
    start.add_argument("--width", type=int, required=True)
    start.add_argument("--height", type=int, required=True)
    start.add_argument("--socket", required=True)
    start.add_argument("--outputs", type=int, default=1)
    # nargs="*" so the bash `--env VAR=VAL VAR2=VAL2` array append maps straight
    # through; it must be the last flag or it would swallow later positionals.
    start.add_argument("--env", nargs="*", default=[])

    stop = sub.add_parser("stop", help="the nested_kwin_cleanup teardown (idempotent)")
    stop.add_argument("--runtime-dir", default="")
    stop.add_argument("--pgid", default="")
    stop.add_argument("--starttime", default="")

    stop_group = sub.add_parser("stop-group", help="zombie-aware bounded process-group teardown")
    stop_group.add_argument("pgid", type=int)
    stop_group.add_argument("--label", default="process group")
    stop_group.add_argument("--starttime", default="")
    stop_group.add_argument("--term-attempts", type=int, default=STOP_GROUP_DEFAULT_ATTEMPTS)
    stop_group.add_argument("--term-delay", type=float, default=STOP_GROUP_DEFAULT_DELAY)
    stop_group.add_argument("--kill-attempts", type=int, default=STOP_GROUP_DEFAULT_ATTEMPTS)
    stop_group.add_argument("--kill-delay", type=float, default=STOP_GROUP_DEFAULT_DELAY)

    status = sub.add_parser("status", help="report the state file plus group liveness")
    status.add_argument("--runtime-dir", required=True)

    return parser


def main(argv: Sequence[str] | None = None) -> None:
    args = _build_parser().parse_args(argv)
    command: str = args.command
    if command == "prepare":
        _cmd_prepare()
    elif command == "start":
        _cmd_start(args)
    elif command == "stop":
        _cmd_stop(args)
    elif command == "stop-group":
        _cmd_stop_group(args)
    else:  # "status" (subparsers required=True rejects anything else)
        _cmd_status(args)


if __name__ == "__main__":
    main()
