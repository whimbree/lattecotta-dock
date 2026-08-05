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

import os
import sys
from pathlib import Path

from latte_harness import bash_allowlist
from latte_harness.log import fail, info
from latte_harness.paths import RepoPaths
from latte_harness.proc import run

TOOL = "harness-check"


def _tool_argv(binary: str) -> list[str]:
    """Resolve a checker binary, preferring a copy outside the venv.

    Under ``uv run`` the venv's bin directory leads PATH, but the venv
    wheels of ruff and basedpyright ship foreign-glibc standalone
    executables NixOS cannot run unpatched; the devShell provides
    nixpkgs builds of the same pinned versions. So: first executable on
    PATH that is NOT inside sys.prefix wins (the devShell binary on
    NixOS), and the venv copy is the fallback (correct off-nix, where
    no system copy exists).
    """
    venv = Path(sys.prefix).resolve()
    for entry in os.get_exec_path():
        candidate = Path(entry) / binary
        if candidate.resolve().is_relative_to(venv):
            continue
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return [str(candidate)]
    return [str(venv / "bin" / binary)]


def main() -> None:
    paths = RepoPaths.discover()
    harness = paths.harness

    steps: list[tuple[str, list[str]]] = [
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
