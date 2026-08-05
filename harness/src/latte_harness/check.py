# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The harness-check gate leg: lint, format, types, unit tests, allowlist.

Each check runs as a subprocess in the harness project directory; the
first nonzero exit code is the verdict (the gate contract: exit codes,
never scraped text). Order is cheapest-first so a lint slip fails in
seconds, not after the test run.
"""

from __future__ import annotations

import functools
import os
import subprocess
import sys
from collections.abc import Sequence
from pathlib import Path

from latte_harness import bash_allowlist
from latte_harness.log import fail, info
from latte_harness.paths import RepoPaths
from latte_harness.proc import run

TOOL = "harness-check"

# A version --version probe that raises either of these "cannot run here":
# OSError is the foreign-glibc loader refusal on NixOS (FileNotFoundError for
# the missing program interpreter); TimeoutExpired is a hung or pathologically
# slow probe, no more trustworthy as the pinned checker than an outright
# refusal. Named as a tuple so the except clause reads unambiguously (not the
# Python-2 catch-and-bind form the bare comma spelling resembles).
_PROBE_CANNOT_RUN: tuple[type[Exception], ...] = (OSError, subprocess.TimeoutExpired)


@functools.cache
def _tool_argv(binary: str) -> tuple[str, ...]:
    """Resolve a checker binary: the venv's pinned copy when it runs.

    The venv wheels of ruff and basedpyright carry the exact locked
    versions but ship foreign-glibc standalone executables NixOS cannot
    exec unpatched; there the devShell provides nixpkgs builds of the
    same pinned versions. Probing the venv copy (one --version exec)
    instead of assuming keeps the version lockstep honest off-nix: a
    stray ~/.local/bin copy of a different version can never shadow the
    pinned one when the pinned one works. When the venv copy cannot run
    (the NixOS case), the first PATH copy outside the venv wins; when
    nothing runs, the leg fails loudly rather than guessing.

    Cached (functools.cache) so a tool used by two steps - ruff by both
    the lint and format checks - is probed once, not twice; the returned
    argv is an immutable tuple so a cache hit cannot be mutated by a
    caller.
    """
    venv = Path(sys.prefix).resolve()
    venv_copy = venv / "bin" / binary
    if venv_copy.is_file() and os.access(venv_copy, os.X_OK):
        try:
            probe = run([str(venv_copy), "--version"], capture=True, timeout=60)
        except _PROBE_CANNOT_RUN:
            # Fall through to the PATH copy rather than trusting or blocking on
            # a venv binary that cannot run here (see _PROBE_CANNOT_RUN).
            probe = None
        if probe is not None and probe.returncode == 0:
            return (str(venv_copy),)
    for entry in os.get_exec_path():
        candidate = Path(entry) / binary
        if candidate.resolve().is_relative_to(venv):
            continue
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return (str(candidate),)
    fail(TOOL, f"no runnable {binary}: the venv copy cannot exec here and PATH has none")


def main() -> None:
    paths = RepoPaths.discover()
    harness = paths.harness

    steps: list[tuple[str, Sequence[str]]] = [
        ("ruff lint", [*_tool_argv("ruff"), "check", "src", "tests"]),
        ("ruff format", [*_tool_argv("ruff"), "format", "--check", "src", "tests"]),
        ("basedpyright strict", _tool_argv("basedpyright")),
        ("pytest", [sys.executable, "-m", "pytest"]),
    ]
    for name, argv in steps:
        info(TOOL, name)
        result = run(argv, cwd=harness)
        if result.returncode != 0:
            fail(TOOL, f"{name} exited {result.returncode}", result.returncode)

    info(TOOL, "bash allowlist ratchet")
    violations = bash_allowlist.check(paths)
    if not violations.ok:
        for entry in violations.unlisted:
            info(TOOL, f"NOT ALLOWLISTED: {entry}")
        for entry in violations.stale:
            info(TOOL, f"STALE ENTRY: {entry}")
        fail(
            TOOL,
            f"bash allowlist: {len(violations.unlisted)} unlisted, "
            f"{len(violations.stale)} stale (see latte_harness.bash_allowlist)",
        )

    info(TOOL, "OK")


if __name__ == "__main__":
    main()
