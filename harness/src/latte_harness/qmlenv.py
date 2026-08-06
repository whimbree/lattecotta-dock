# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""QML environment assembly for the headless QML checks and staged runs.

The typed port of scripts/lib-qml-env.sh (BP-1a). The bash bridge that eval-ed
this module's setup/stage subcommands is gone (BW-1): every consumer is a Python
gate now (qml_compile_gate, qmllint_gate, qml_interaction_tests, sceneprobe_gate,
staged_run) that calls the two library entries directly. What remains:

- ``build_setup_script`` assembles the ``-import`` list (the pinned QML module
  search path) and the env mutations (unset the profile's QML2_IMPORT_PATH /
  QML_IMPORT_PATH, re-export the nixpkgs Qt6 seed vars with only the packaged
  latte-dock store leaf stripped). The compile and qmllint gates lock their own
  env recompute against it byte-for-byte so the two cannot drift.
- ``stage_qml_modules`` installs the built modules into ``build/_qmlstage`` with
  the install-manifest preserved across the throwaway install, restored on every
  exit path including SIGINT/SIGTERM (the bash trap-EXIT idiom as a context
  manager).
- the ``seed-env`` subcommand emits just the filtered seed-var exports for the
  two bash gates that still eval them (scripts/build-check.sh and
  ci/build-and-gate.sh) so their ctest legs mask the packaged dock the same way.

The import-path doctrine is Qt5-faithful and deliberate. The user profile's
QML2_IMPORT_PATH carries Qt 5 and differently-pinned Qt 6 builds whose plugins
fail to load in this runtime (private-API symbol versioning); the same applies
to the engine's ambient defaults, so every needed module is passed explicitly
with ``-import`` (a later ``-import`` outranks earlier ones and the ambient
defaults). The nixpkgs Qt6 runtime wrapper reads the seed vars from the ENV
independently of QML2_IMPORT_PATH, so the D8/D271 doctrine strips ONLY the
packaged latte-dock store leaf from them: those paths also carry KDE framework
modules the QML gates import, so unsetting the vars wholesale would drop a
module the contracts need. The staged latte-dock lives under a $HOME path, not
a /nix/store/*-latte-dock-* one, so it is never matched.
"""

from __future__ import annotations

import argparse
import os
import re
import shlex
import shutil
import subprocess
from collections.abc import Generator, Mapping, Sequence
from contextlib import contextmanager
from pathlib import Path

from latte_harness.proc import (
    SessionProcess,
    run,
    terminating,
)

TOOL = "qmlenv"

# D8/D271 doctrine: the packaged latte-dock lives at a /nix/store/<hash>-
# latte-dock-<version> leaf (the store name is one path component, so the hash
# sits between /nix/store/ and -latte-dock- with no slash). Deny only that leaf
# from the seed vars, never the whole var (which also carries KDE framework
# modules the gates need). Mirrors the bash ``grep -vE`` exactly.
_PACKAGED_LATTE_DOCK = re.compile(r"/nix/store/[^/]*-latte-dock-")

# One /nix/store/<pkg> prefix per ldd line (``libfoo.so => /nix/store/hash-
# name/lib/...``); mirrors the perl ``m{=> (/nix/store/[^/]+)/}``.
_LDD_NIX_STORE = re.compile(r"=> (/nix/store/[^/]+)/")

# The binaries whose linked store paths pin the QML providers to the exact
# packages the code dlopens (libplasma, plasma-workspace via the tasks plugin).
_LINKED_BINARIES = ("bin/latte-dock", "bin/liblattetasksplugin.so")

# The nixpkgs Qt6 runtime seed vars, read from the ambient env and re-exported
# with the packaged latte-dock leaf stripped. Public: the QML gate modules
# import this canonical list rather than mirroring it (a mirror is a drift
# class no test can fully pin; the PR #160 review proved the addition
# blindness concretely).
NIXPKGS_SEED_VARS = ("NIXPKGS_QT6_QML_IMPORT_PATH", "NIXPKGS_QML_SEARCH_PATHS")


def strip_packaged_latte_dock(value: str) -> str:
    """Drop only the packaged latte-dock store leaf from a colon-joined path.

    Empty components are preserved so the round-trip matches the bash
    ``tr ':' '\\n' | grep -v | paste -sd:`` byte-for-byte (a middle ``::``
    stays a ``::``).
    """
    kept = [part for part in value.split(":") if not _PACKAGED_LATTE_DOCK.search(part)]
    return ":".join(kept)


def seed_var_exports(env: Mapping[str, str]) -> list[str]:
    """Eval-able exports re-publishing the seed vars, packaged leaf stripped.

    The D8 doctrine (strip only the packaged latte-dock store leaf, keep the
    KDE framework modules the vars also carry) for every consumer that must
    not resolve org.kde.latte.* from the system-installed package. D277
    extends it to the ctest legs: with the packaged dock in the system
    profile, a QML-engine test's in-process org.kde.latte.* registration
    collides with the package's on-disk qmldir (themeawareicontest's
    namespace refusal), so ctest evals these same exports before running.
    """
    lines: list[str] = []
    for var in NIXPKGS_SEED_VARS:
        current = env.get(var)
        if current:
            lines.append(f"export {var}={shlex.quote(strip_packaged_latte_dock(current))}")
    return lines


def parse_linked_store_prefixes(ldd_output: str) -> list[str]:
    """Sorted-unique /nix/store/<pkg> prefixes referenced in an ldd dump.

    Mirrors ``ldd <so> | perl -ne 'print $1 if m{=> (/nix/store/[^/]+)/}'
    | sort -u``: at most one prefix per line, deduplicated, codepoint-sorted.
    """
    prefixes: set[str] = set()
    for line in ldd_output.splitlines():
        match = _LDD_NIX_STORE.search(line)
        if match:
            prefixes.add(match.group(1))
    return sorted(prefixes)


def resolve_install_qmldir(build: Path) -> str:
    """The distro's KDE_INSTALL_QMLDIR (lib/qml on nixpkgs, lib/qt6/qml on
    Arch/Fedora/Debian), read from the build's authoritative latte-qmldir.txt.

    The build emits the configure-time value there; the stage does not exist
    yet when setup runs, so the filesystem cannot be probed for it. Defaults to
    lib/qml only if the file is absent or empty. The caller reuses the value to
    probe the staged tree, so it must stay this distro's spelling, never a
    hardcoded lib/qml.
    """
    marker = build / "latte-qmldir.txt"
    if marker.is_file():
        lines = marker.read_text().splitlines()
        if lines and lines[0].strip():
            return lines[0].strip()
    return "lib/qml"


def _module_path_imports(module_path: str) -> list[str]:
    """``-import`` flags for each existing dir in LATTE_QML_MODULE_PATH order.

    The empty-component guard mirrors bash ``[[ -d "$p" ]]``, which is false
    for the empty string: without it Path("").is_dir() resolves to "." (the
    cwd, which exists) and would emit a bogus ``-import .`` the bash never did.
    """
    flags: list[str] = []
    for path in module_path.split(":"):
        if path and Path(path).is_dir():
            flags += ["-import", path]
    return flags


def _linked_provider_imports(build: Path) -> list[str]:
    """``-import`` flags for the Qt6 qml trees the built binaries link.

    Pins QML modules to the exact packages the binaries dlopen so a foreign
    Plasma leaked into the session cannot resolve a provider from the wrong
    build (a module resolved from a foreign build fails to dlopen). Later
    ``-import`` wins, so these outrank the module-path imports above.
    """
    flags: list[str] = []
    for rel in _LINKED_BINARIES:
        binary = build / rel
        if not binary.exists():
            continue
        ldd = run(["ldd", str(binary)], capture=True)
        for prefix in parse_linked_store_prefixes(ldd.stdout):
            qml_tree = Path(prefix) / "lib" / "qt-6" / "qml"
            if qml_tree.is_dir():
                flags += ["-import", str(qml_tree)]
    return flags


def assemble_imports(module_path: str, build: Path, stage: Path, qmldir: str) -> list[str]:
    """The full ``-import`` list, in win-last order.

    Module-path dirs first, the linked-provider pins next (they outrank the
    module path), the staged Latte tree last so it wins over everything.
    """
    imports = _module_path_imports(module_path)
    imports += _linked_provider_imports(build)
    imports += ["-import", str(stage / qmldir)]
    return imports


class MissingModulePathError(RuntimeError):
    """LATTE_QML_MODULE_PATH is absent: the flake devShell is not active."""


def build_setup_script(repo: Path, env: Mapping[str, str]) -> str:
    """The eval-able shell the bash bridge sources, as one string.

    Emits, in order: the ``build``/``stage``/``qmldir`` shell vars, the QML2
    import-path unset, the filtered nixpkgs seed exports (only when the var is
    set and non-empty, matching the bash ``[[ -n ... ]] || continue``), and the
    ``imports`` array. All paths are shell-quoted, so a path with a space
    round-trips through ``eval`` intact.
    """
    module_path = env.get("LATTE_QML_MODULE_PATH")
    if not module_path:
        raise MissingModulePathError

    build_override = env.get("BUILD")
    build = Path(build_override) if build_override else repo / "build"
    stage_override = env.get("STAGE")
    stage = Path(stage_override) if stage_override else build / "_qmlstage"
    qmldir = resolve_install_qmldir(build)
    imports = assemble_imports(module_path, build, stage, qmldir)

    lines = [
        f"build={shlex.quote(str(build))}",
        f"stage={shlex.quote(str(stage))}",
        f"qmldir={shlex.quote(qmldir)}",
        "unset QML2_IMPORT_PATH QML_IMPORT_PATH",
        *seed_var_exports(env),
    ]
    tokens = " ".join(shlex.quote(token) for token in imports)
    lines.append(f"imports=({tokens})")
    return "\n".join(lines)


def _manifest_mentions_stage(manifest: Path, stage: Path) -> bool:
    """True if the manifest references the stage prefix (bash ``grep -q``)."""
    return str(stage) in manifest.read_text()


@contextmanager
def preserved_install_manifest(manifest: Path, stage: Path) -> Generator[None]:
    """Preserve build/install_manifest.txt across a staging install.

    cmake --install unconditionally rewrites this file, which ECM's
    appstreamtest reads; a staged manifest left behind changes what that test
    validates. Preserve a real one and restore it afterwards; a leaked staged
    manifest is itself never a legitimate state, so drop it (self-heal) and let
    a real install regenerate it. Restore runs on every exit path - normal
    return, exception, and (via install_conventional_signal_exits) SIGINT /
    SIGTERM - which is the bash ``trap '_qml_env_restore_manifest' EXIT INT
    TERM`` guarantee expressed as a context manager's finally.
    """
    pre_stage = Path(f"{manifest}.pre-stage")
    had_real_manifest = False
    if manifest.is_file():
        if _manifest_mentions_stage(manifest, stage):
            print("dropping leaked staged install_manifest.txt", flush=True)
            manifest.unlink()
        else:
            had_real_manifest = True
            shutil.copyfile(manifest, pre_stage)
    try:
        yield
    finally:
        if had_real_manifest:
            os.replace(pre_stage, manifest)  # mv -f: atomic overwrite
        else:
            manifest.unlink(missing_ok=True)  # rm -f the staged manifest


def _run_to_log(argv: Sequence[str], log: Path, mode: str) -> int:
    """Run argv with stdout+stderr going to ``log``; return the exit code.

    The child runs in its own session (proc.SessionProcess) and is torn down
    on any interruption, so a SIGINT/SIGTERM mid-install does not orphan cmake
    or rsync - the group-kill half of the bash trap idiom.
    """
    with log.open(mode) as handle:
        proc = SessionProcess.spawn(argv, stdout=handle, stderr=subprocess.STDOUT)
        with terminating(proc):
            return proc.wait()


def _print_log_tail(log: Path, lines: int) -> None:
    """Print the last ``lines`` lines of ``log`` (the bash ``tail -15``)."""
    if not log.is_file():
        return
    for line in log.read_text().splitlines()[-lines:]:
        print(line)


def _install_to_stage(build: Path, stage: Path) -> None:
    """Install to a throwaway prefix, then checksum-rsync into the real stage.

    The rsync is deliberately ``--checksum`` (not ``-t``): files whose content
    did not change keep their existing mtime, so the dock's QML disk cache
    (~/.cache/lattedock/qmlcache), which validates entries against the source
    file's timestamp, is not invalidated wholesale on every restart. A failed
    install or sync exits 2 (the distinguished stage-failure code).
    """
    log = Path(f"{stage}.log")
    stage_new = Path(f"{stage}.new")

    shutil.rmtree(stage_new, ignore_errors=True)
    if _run_to_log(["cmake", "--install", str(build), "--prefix", str(stage_new)], log, "w") != 0:
        print("STAGE FAILED:")
        _print_log_tail(log, 15)
        shutil.rmtree(stage_new, ignore_errors=True)
        raise SystemExit(2)

    stage.mkdir(parents=True, exist_ok=True)
    sync = ["rsync", "-rlp", "--checksum", "--delete", f"{stage_new}/", f"{stage}/"]
    if _run_to_log(sync, log, "a") != 0:
        print("STAGE SYNC FAILED:")
        _print_log_tail(log, 15)
        shutil.rmtree(stage_new, ignore_errors=True)
        raise SystemExit(2)

    shutil.rmtree(stage_new, ignore_errors=True)


def stage_qml_modules(build: Path, stage: Path) -> None:
    """Stage the built QML modules into ``stage``, manifest preserved."""
    print(f"staging {build} -> {stage} ...", flush=True)
    manifest = build / "install_manifest.txt"
    with preserved_install_manifest(manifest, stage):
        _install_to_stage(build, stage)


def main(argv: Sequence[str] | None = None) -> None:
    parser = argparse.ArgumentParser(prog="latte_harness.qmlenv", description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    # setup and stage are library calls now (build_setup_script / stage_qml_modules,
    # which the Python QML gates import); seed-env is the one remaining subcommand,
    # eval-ed by scripts/build-check.sh and ci/build-and-gate.sh for their ctest legs.
    sub.add_parser(
        "seed-env",
        help="emit eval-able seed-var exports with the packaged latte-dock leaf stripped",
    )

    parser.parse_args(argv)  # only "seed-env"; subparsers required=True rejects anything else
    exports = seed_var_exports(os.environ)
    if exports:
        print("\n".join(exports))


if __name__ == "__main__":
    main()
