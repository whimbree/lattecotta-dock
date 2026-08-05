# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Headless QML interaction harness (BP-1f).

The typed port of scripts/qml-interaction-tests.sh (porting plan Phase 5,
docs/reference/TESTING.md). It drives real Latte QML components offscreen
through qmltestrunner. Tests live in tests/qml/tst_*.qml and resolve Latte's
modules through the staged install, so module registration and type resolution
are part of what every test exercises.

An optional first argument selects a different test directory (default
tests/qml). ctest runs this gate twice: bare as the ``qmlinteraction`` entry,
and with ``tests/contracts`` as the ``qmlcontracts`` entry. The staging,
import list and offscreen env are shared with the compile gate
(latte_harness.qml_compile_gate); this gate reuses the tree that gate stages
when it already ran this build, and re-stages only when it did not.
"""

from __future__ import annotations

import os
import sys
from collections.abc import Sequence
from pathlib import Path

from latte_harness.paths import RepoPaths
from latte_harness.proc import install_conventional_signal_exits
from latte_harness.qml_compile_gate import (
    refuse_without_devshell,
    resolve_qml_env,
    run_qmltestrunner,
)
from latte_harness.qmlenv import MissingModulePathError, stage_qml_modules

TOOL = "qmlinteraction"


def resolve_input_dir(args: Sequence[str], repo: Path) -> Path:
    """The test directory to drive: the first argument, or tests/qml.

    Preserves the bash ``inputdir="${1:-$repo/tests/qml}"`` argv contract that
    the qmlcontracts ctest entry relies on (it passes tests/contracts).
    """
    return Path(args[0]) if args else repo / "tests" / "qml"


def needs_staging(stage: Path, qmldir: str) -> bool:
    """True when the staged Latte tree is absent and must be (re)staged.

    Mirrors the bash ``[[ ! -d "$stage/$qmldir/org/kde/latte" ]]`` probe, using
    this distro's install spelling of qmldir (lib/qml on nixpkgs, lib/qt6/qml
    elsewhere) rather than a hardcoded path, so the guard does not always miss
    off-nix and re-stage.
    """
    return not (stage / qmldir / "org" / "kde" / "latte").is_dir()


def main(argv: Sequence[str] | None = None) -> None:
    install_conventional_signal_exits()
    args = list(sys.argv[1:] if argv is None else argv)
    paths = RepoPaths.discover()
    try:
        env = resolve_qml_env(paths.root, os.environ)
    except MissingModulePathError:
        refuse_without_devshell()

    # Reuse the compile gate's staging when it already ran this build; stage
    # otherwise. The compile gate stages unconditionally, so in the ctest
    # ordering (DEPENDS qmlcompilegate) this branch is normally skipped.
    if needs_staging(env.stage, env.qmldir):
        stage_qml_modules(env.build, env.stage)

    inputdir = resolve_input_dir(args, paths.root)
    raise SystemExit(run_qmltestrunner(env.imports, inputdir, env.child_env))


if __name__ == "__main__":
    main()
