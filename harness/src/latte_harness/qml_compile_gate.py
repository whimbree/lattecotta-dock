# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Headless compile-check for every packaged QML file (BP-1f).

The typed port of scripts/qml-compile-gate.sh (porting plan Phase 5). It
stages an install, then compiles every QML file in the shell, containment,
plasmoid and indicator packages inside a real QML engine via
Qt.createComponent - so it catches removed-type and removed-property errors in
lazy, interaction-only components (widget explorer, config pages) that would
otherwise need a click in a live session to surface. It compiles, it does not
instantiate: type resolution and property-assignment existence are checked,
runtime binding evaluation is not.

Two file classes are skipped and reported in the output, exactly as the bash
did:

- files importing ``org.kde.latte.private.app`` - that module is registered
  inside the latte-dock binary (lattecorona.cpp), it never exists for a
  standalone engine; these all load during dock startup anyway;
- superseded ``*.5.2[0-5].qml`` version-ladder variants - on Plasma 6 only the
  newest variant is ever loaded (ToolTipInstance.qml's selector); the older
  rungs target removed Plasma 5 APIs and are dead here.

This module also houses the shared QML-gate plumbing the interaction gate
reuses: ``resolve_qml_env`` (the import list and the offscreen child env, from
the qmlenv library) and ``run_qmltestrunner``. The interaction gate is ordered
after this one in ctest (``DEPENDS qmlcompilegate``) and reuses the staged tree
this gate produces, so sharing the resolution here matches that relationship.
"""

from __future__ import annotations

import os
import re
from collections.abc import Callable, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path
from typing import NoReturn

from latte_harness.log import fail
from latte_harness.paths import RepoPaths
from latte_harness.proc import (
    SessionProcess,
    install_conventional_signal_exits,
    terminating,
)
from latte_harness.qmlenv import (
    MissingModulePathError,
    assemble_imports,
    resolve_install_qmldir,
    stage_qml_modules,
    strip_packaged_latte_dock,
)

TOOL = "qmlcompilegate"

# The nixpkgs Qt6 runtime seed vars, re-exported with the packaged latte-dock
# leaf stripped. The canonical list is qmlenv's own _NIXPKGS_SEED_VARS; it is
# mirrored here because basedpyright-strict forbids importing that private
# constant, and pinned to it by test_resolve_env_matches_build_setup_script
# (the drift net: the exports build_setup_script emits must match these).
_NIXPKGS_SEED_VARS = ("NIXPKGS_QT6_QML_IMPORT_PATH", "NIXPKGS_QML_SEARCH_PATHS")

# The packaged QML trees the gate compiles, relative to the stage prefix. A
# single sorted sweep across all four (the bash ``find <roots> ... | sort``).
PACKAGE_QML_ROOTS = (
    "share/plasma/shells/org.kde.latte.shell",
    "share/plasma/plasmoids/org.kde.latte.containment",
    "share/plasma/plasmoids/org.kde.latte.plasmoid",
    "share/latte/indicators",
)

# A file importing this module depends on a type registered inside the
# latte-dock binary; it cannot compile in a standalone engine (bash ``grep -q
# 'org.kde.latte.private.app'``, matched as a literal substring).
_APP_MODULE_IMPORT = "org.kde.latte.private.app"

# The dead Plasma 5 version-ladder rungs (bash ``[[ "$f" =~ \.5\.2[0-5]\.qml$
# ]]``); the newest variant is the only one Plasma 6 loads.
_VERSION_LADDER = re.compile(r"\.5\.2[0-5]\.qml$")


# --- shared QML-gate environment (used by both gates) ----------------------


@dataclass(frozen=True, slots=True)
class QmlEnv:
    """The resolved QML-gate environment the bash bridge's ``eval`` produced.

    ``imports`` is the win-last ``-import`` list; ``child_env`` is the process
    env for the offscreen qmltestrunner (the profile QML2 import paths unset,
    the nixpkgs seed vars leaf-stripped, QT_QPA_PLATFORM=offscreen).
    """

    build: Path
    stage: Path
    qmldir: str
    imports: tuple[str, ...]
    child_env: Mapping[str, str]


def _offscreen_child_env(env: Mapping[str, str]) -> dict[str, str]:
    """The env for the offscreen qmltestrunner (the eval'd setup's mutations).

    Mirrors what ``qml_env_setup`` put in the sourcing shell: unset the
    profile's QML2_IMPORT_PATH / QML_IMPORT_PATH (Qt5 and foreign-pinned Qt6
    plugins fail to load here, so nothing ambient is trusted), re-export the
    nixpkgs Qt6 seed vars with only the packaged latte-dock leaf stripped, and
    add the offscreen platform the bash set at the qmltestrunner call site.
    """
    child = dict(env)
    child.pop("QML2_IMPORT_PATH", None)
    child.pop("QML_IMPORT_PATH", None)
    for var in _NIXPKGS_SEED_VARS:
        current = env.get(var)
        if current:
            child[var] = strip_packaged_latte_dock(current)
    child["QT_QPA_PLATFORM"] = "offscreen"
    return child


def resolve_qml_env(repo: Path, env: Mapping[str, str]) -> QmlEnv:
    """Resolve build/stage/qmldir, the import list, and the offscreen env.

    The build/stage/qmldir resolution mirrors qmlenv.build_setup_script exactly
    (BUILD/STAGE override the defaults; qmldir is this distro's install spelling
    read from the build marker), and the import list comes straight from
    qmlenv.assemble_imports. Raises MissingModulePathError when the flake
    devShell is not active, the same refusal the bridge's setup subcommand made.
    """
    module_path = env.get("LATTE_QML_MODULE_PATH")
    if not module_path:
        raise MissingModulePathError

    build = Path(env["BUILD"]) if env.get("BUILD") else repo / "build"
    stage = Path(env["STAGE"]) if env.get("STAGE") else build / "_qmlstage"
    qmldir = resolve_install_qmldir(build)
    imports = tuple(assemble_imports(module_path, build, stage, qmldir))
    return QmlEnv(build, stage, qmldir, imports, _offscreen_child_env(env))


def run_qmltestrunner(
    imports: Sequence[str], input_path: Path, child_env: Mapping[str, str]
) -> int:
    """Run qmltestrunner over ``input_path`` with the pinned imports; return its
    exit code (the gate verdict, never a scraped log line).

    The child runs in its own session and is torn down on any interruption, so
    a SIGINT/SIGTERM mid-run cannot orphan it - the group-teardown half of the
    bash foreground-child-under-``set -e`` discipline. stdout/stderr are
    inherited so qmltestrunner's own FAIL and summary lines stream through.
    """
    argv = ["qmltestrunner", *imports, "-input", str(input_path)]
    proc = SessionProcess.spawn(argv, env=child_env)
    with terminating(proc):
        return proc.wait()


def refuse_without_devshell() -> NoReturn:
    """The shared MissingModulePathError refusal both gates make (never returns)."""
    fail(
        TOOL,
        "LATTE_QML_MODULE_PATH is unset; run inside the flake devShell (nix develop provides it)",
    )


# --- file selection --------------------------------------------------------


@dataclass(frozen=True, slots=True)
class QmlSelection:
    """The compile set plus the two skip tallies the summary line reports."""

    files: tuple[Path, ...]
    skipped_app: int
    skipped_ladder: int


def is_app_module_dependent(text: str) -> bool:
    """True if the QML imports the in-binary app module (grep-parity)."""
    return _APP_MODULE_IMPORT in text


def is_dead_version_ladder(path: Path) -> bool:
    """True for a superseded ``*.5.2[0-5].qml`` version-ladder rung."""
    return _VERSION_LADDER.search(str(path)) is not None


def _read_text(path: Path) -> str:
    # errors="ignore" keeps a non-UTF-8 byte from crashing the scan; the
    # app-import marker is ASCII, so it survives the drop (grep is byte-wise).
    return path.read_text(errors="ignore")


def classify_qml_files(
    paths: Sequence[Path],
    read_text: Callable[[Path], str] = _read_text,
) -> QmlSelection:
    """Split the scanned files into the compile set and the two skip classes.

    The app-module check comes first and ``continue``s, exactly as the bash
    ordered it: a file that is both app-module-dependent and a version-ladder
    rung is counted as app-module-dependent only.
    """
    kept: list[Path] = []
    skipped_app = 0
    skipped_ladder = 0
    for path in paths:
        if is_app_module_dependent(read_text(path)):
            skipped_app += 1
            continue
        if is_dead_version_ladder(path):
            skipped_ladder += 1
            continue
        kept.append(path)
    return QmlSelection(tuple(kept), skipped_app, skipped_ladder)


def scan_package_qml(stage: Path) -> list[Path]:
    """Every ``*.qml`` under the four package roots, one sorted sweep.

    A missing root contributes nothing (the bash ``find ... 2>/dev/null``); the
    combined result is codepoint-sorted (== ``LC_ALL=C sort``), which fixes the
    compile order deterministically without affecting the pass/fail verdict.
    """
    found: list[Path] = []
    for rel in PACKAGE_QML_ROOTS:
        root = stage / rel
        if root.is_dir():
            found.extend(root.rglob("*.qml"))
    return sorted(found, key=str)


# --- generated TestCase ----------------------------------------------------

# The compile TestCase, split around the file-list. Kept byte-identical to the
# bash heredoc so qmltestrunner's output (the "FAIL <file>" lines and the
# "=== N of M ... ===" summary) is unchanged. The ``\n`` in the console.warn
# line is a literal backslash-n written into the QML, as the single-quoted bash
# ``echo`` wrote it (JS parses it as a newline in the string literal).
_TESTCASE_HEAD = (
    "import QtQuick",
    "import QtTest",
    "TestCase {",
    '    name: "QmlCompileGate"',
    "    property var files: [",
)
_TESTCASE_TAIL = (
    "    ]",
    "    function test_compileAll() {",
    "        var failed = [];",
    "        for (var i = 0; i < files.length; i++) {",
    "            var c = Qt.createComponent(files[i]);",
    "            if (c.status === Component.Error) {",
    '                console.warn("FAIL " + files[i] + "\\n      " + c.errorString().trim());',
    "                failed.push(files[i]);",
    "            }",
    "            if (c) c.destroy();",
    "        }",
    '        console.warn("=== " + failed.length + " of " + files.length'
    ' + " package QML files failed to compile ===");',
    '        verify(failed.length === 0, failed.length + " QML files failed to compile");',
    "    }",
    "}",
)


def generate_compile_testcase(files: Sequence[Path]) -> str:
    """The QmlCompileGate TestCase text, with one ``file://`` entry per file."""
    lines = [*_TESTCASE_HEAD]
    lines += [f'        "file://{path}",' for path in files]
    lines += _TESTCASE_TAIL
    return "\n".join(lines) + "\n"


# --- orchestration ---------------------------------------------------------


def main() -> None:
    install_conventional_signal_exits()
    paths = RepoPaths.discover()
    try:
        env = resolve_qml_env(paths.root, os.environ)
    except MissingModulePathError:
        refuse_without_devshell()

    # The compile gate always re-stages (the bash calls qml_env_stage
    # unconditionally): the build may have changed since the last stage.
    stage_qml_modules(env.build, env.stage)

    all_paths = scan_package_qml(env.stage)
    if not all_paths:
        # Zero files FOUND is the distinguished stage-empty failure (exit 2);
        # zero files LEFT after skipping is fine and compiles an empty set.
        print(f"no staged QML found under {env.stage}", flush=True)
        raise SystemExit(2)

    selection = classify_qml_files(all_paths)
    print(
        f"skipped {selection.skipped_app} app-module-dependent + "
        f"{selection.skipped_ladder} dead-version-ladder files",
        flush=True,
    )

    gen = env.stage / "_compile_gate.qml"
    gen.write_text(generate_compile_testcase(selection.files))

    print(f"compiling {len(selection.files)} QML files (offscreen)...", flush=True)
    raise SystemExit(run_qmltestrunner(env.imports, gen, env.child_env))


if __name__ == "__main__":
    main()
