# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Default-layout config seeder for the nested e2e harnesses.

The typed port of scripts/lib-e2e-seed.sh (BP-2a), consuming
latte_harness.vehicle as a library. Single source of truth so the container
release gate, the NixOS sanitized gate, the matrix front doors and the
multi-output leg can never drift on how a hermetic seed config is produced.

run-e2e.sh needs a pre-existing ``$base/latte`` to copy its throwaway config
from and refuses loudly without one, but a fresh dock only writes its default
layout on first run. So this brings up a throwaway nested compositor, runs the
staged dock against an EMPTY config home until it self-initializes the default
layout, tears the compositor down, and leaves the seeded tree for the vehicle
to copy. It seeds with a NORMAL (non-sanitized) dock: the seed is plain config
DATA (the "My Layout" default, written synchronously at first run), so it gains
nothing from ASan and only pays the sanitizer's startup overhead.

Failure is loud, never a silent empty seed: if the dock never self-initializes
a layout, or its process group cannot be stopped, the seeder exits 2 with the
seed dock's log tail.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import time
from collections.abc import Mapping, Sequence
from contextlib import suppress
from pathlib import Path

from latte_harness import vehicle
from latte_harness.proc import SessionProcess, install_conventional_signal_exits

TOOL = "e2e_seed_default_config"

# The throwaway compositor the seed dock runs against: a single 1600x1000
# virtual output on a private socket (byte-identical to the bash seeder).
SEED_SCREEN_W = 1600
SEED_SCREEN_H = 1000
SEED_SOCKET = "latte-seed-wl"

# Poll the dock's lifecycle up to 90 times at 1s: the default layout lands
# synchronously at first-run start, so this is generous headroom for a cold
# staged binary to reach the running state and write it.
SEED_LIFECYCLE_ATTEMPTS = 90
SEED_LIFECYCLE_DELAY = 1.0

# busctl renders a string return as `s "running"`; awk '{print $2}' in the bash
# kept the quotes, so the compared token is the 9-character quoted string.
RUNNING_STATE = '"running"'

# The bash reused latte_package_gate_stop_process_group with this exact label
# for the seed dock; the vehicle's stop_process_group keeps it verbatim.
SEED_DOCK_LABEL = "nested seed dock process group"

SEED_LOG_TAIL_LINES = 30

EXIT_SEED_FAILED = 2


# ---- pure logic (unit-tested directly) -------------------------------------


def has_default_layout(seeddir: Path) -> bool:
    """True iff a ``*.layout.latte`` exists under ``<seeddir>/latte``.

    The bash used a nullglob-safe for-loop deliberately (the nix devShell bash
    is built without ``compgen``, which exits 127 and made an already-seeded
    config read as empty for a whole debug session). A glob is unambiguous here.
    """
    return any((seeddir / "latte").glob("*.layout.latte"))


def parse_lifecycle_state(busctl_stdout: str) -> str:
    """The lifecycle token from a busctl string return, or '' if absent.

    Mirrors ``busctl ... | awk '{print $2}'``: field 2 of `s "running"` is the
    quoted value; a blank/short line yields the empty string the bash used.
    """
    fields = busctl_stdout.split()
    return fields[1] if len(fields) >= 2 else ""


def build_dock_env(
    base_env: Mapping[str, str],
    runtime_dir: Path,
    socket: str,
    bus: str,
    seeddir: Path,
    build: Path,
) -> dict[str, str]:
    """The staged seed dock's environment, mirroring the bash exports exactly.

    The compositor's private XDG_RUNTIME_DIR / WAYLAND_DISPLAY / bus are exported
    so every client (the dock, busctl) hits the one nested bus; DISPLAY and
    XAUTHORITY are stripped; LATTE_CONFIG_HOME points the dock at the seed tree
    and BUILD selects the staged binary (both read by run-staged.sh).
    """
    env = dict(base_env)
    env["XDG_RUNTIME_DIR"] = str(runtime_dir)
    env["WAYLAND_DISPLAY"] = socket
    env["DBUS_SESSION_BUS_ADDRESS"] = bus
    env.pop("DISPLAY", None)
    env.pop("XAUTHORITY", None)
    env["LATTE_CONFIG_HOME"] = str(seeddir)
    env["BUILD"] = str(build)
    return env


# ---- lifecycle readback ----------------------------------------------------


def read_lifecycle_state(env: Mapping[str, str]) -> str:
    """The dock's org.kde.LatteDock lifecycleState via busctl, or '' if unreadable.

    Runs on the compositor's private bus (DBUS_SESSION_BUS_ADDRESS in ``env``);
    a busctl failure (dock not up yet) reads as the empty string, exactly the
    bash ``2>/dev/null ... || true``.
    """
    result = subprocess.run(
        [
            "busctl",
            "--user",
            "call",
            "org.kde.lattedock",
            "/Latte",
            "org.kde.LatteDock",
            "lifecycleState",
        ],
        capture_output=True,
        text=True,
        env=dict(env),
        check=False,
    )
    if result.returncode != 0:
        return ""
    return parse_lifecycle_state(result.stdout)


def _print_log_tail(log: Path, lines: int) -> None:
    if not log.is_file():
        return
    for line in log.read_text().splitlines()[-lines:]:
        print(line, file=sys.stderr)


# ---- the seeder -----------------------------------------------------------


def _drive_seed_dock(
    repo: Path,
    build: Path,
    seeddir: Path,
    session: vehicle.VehicleSession,
) -> None:
    """Run the staged dock against the empty seed tree until the layout lands.

    Exits 2 (loudly, with the seed dock's log tail) if the dock never
    self-initializes a layout or its process group cannot be stopped - never a
    silent empty seed. The dock runs in its own session (SessionProcess), so its
    pid is the process-group id the zombie-aware teardown targets: a KCrash can
    leave the leader STOPPED, which is exactly why the seed reuses the group
    transaction rather than a leader-only wait.
    """
    dock_env = build_dock_env(
        os.environ, session.runtime_dir, session.socket, session.bus, seeddir, build
    )
    seedlog = build / "_seed-dock.log"
    with seedlog.open("w") as handle:
        dock = SessionProcess.spawn(
            [str(repo / "scripts" / "run-staged.sh"), "-d"],
            env=dock_env,
            stdout=handle,
            stderr=subprocess.STDOUT,
        )

    state = ""
    settled = False
    for _ in range(SEED_LIFECYCLE_ATTEMPTS):
        state = read_lifecycle_state(dock_env)
        if state == RUNNING_STATE and has_default_layout(seeddir):
            settled = True
            break
        if dock.poll() is not None:  # the dock exited during startup
            break
        time.sleep(SEED_LIFECYCLE_DELAY)

    stop_code = vehicle.stop_process_group(dock.pid, SEED_DOCK_LABEL)
    with suppress(subprocess.TimeoutExpired):
        dock.wait(timeout=5)  # reap our now-dead child so it never lingers as a zombie
    if stop_code != 0:
        print(
            f"{TOOL}: FAIL could not stop the nested seed dock process group",
            file=sys.stderr,
            flush=True,
        )
        raise SystemExit(EXIT_SEED_FAILED)
    if not settled:
        print(
            f"{TOOL}: FAIL the dock never self-initialized a default layout while seeding "
            f"(last state='{state or 'none'}'); seed dock log tail:",
            file=sys.stderr,
            flush=True,
        )
        _print_log_tail(seedlog, SEED_LOG_TAIL_LINES)
        raise SystemExit(EXIT_SEED_FAILED)


def seed_default_config(repo: Path, build: Path, seeddir: Path) -> None:
    """Seed a default-layout config at ``seeddir`` by driving the staged dock once."""
    shutil.rmtree(seeddir, ignore_errors=True)
    seeddir.mkdir(parents=True, exist_ok=True)

    runtime_dir = vehicle.prepare_runtime_dir()
    (runtime_dir / "kwin-config").mkdir(parents=True, exist_ok=True)
    (runtime_dir / "kwin-cache").mkdir(parents=True, exist_ok=True)
    # WAYLAND_DISPLAY is preseeded into the session env BEFORE kwin exists so
    # dbus-activated kactivitymanagerd gets a display in its activation
    # environment; without it the activities consumer never reaches Running and
    # the dock hangs in startup with zero views.
    extra_env = [
        f"WAYLAND_DISPLAY={SEED_SOCKET}",
        f"XDG_CONFIG_HOME={runtime_dir / 'kwin-config'}",
        f"XDG_CACHE_HOME={runtime_dir / 'kwin-cache'}",
        "QT_FORCE_STDERR_LOGGING=1",
    ]
    try:
        with vehicle.running_compositor(
            runtime_dir, SEED_SCREEN_W, SEED_SCREEN_H, SEED_SOCKET, 1, extra_env, os.environ
        ) as session:
            _drive_seed_dock(repo, build, seeddir, session)
    except vehicle.CompositorStartError as err:
        # running_compositor already tore the compositor down in its finally;
        # report the socket-timeout the way the bash nested_kwin_start did (its
        # log to stderr) and exit 2, mirroring the bash `nested_kwin_start || exit 2`.
        print(
            f"{vehicle.TOOL}: nested kwin_wayland never brought up its socket; its log:",
            file=sys.stderr,
            flush=True,
        )
        sys.stderr.write(err.log_text)
        sys.stderr.flush()
        raise SystemExit(EXIT_SEED_FAILED) from err


def main(argv: Sequence[str] | None = None) -> None:
    parser = argparse.ArgumentParser(prog="latte_harness.seed", description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    default_config = sub.add_parser("default-config", help="seed a default-layout config tree")
    default_config.add_argument("repo")
    default_config.add_argument("build")
    default_config.add_argument("seeddir")

    args = parser.parse_args(argv)
    install_conventional_signal_exits()
    seed_default_config(Path(args.repo), Path(args.build), Path(args.seeddir))


if __name__ == "__main__":
    main()
