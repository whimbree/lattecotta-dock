# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Byte-verified snapshot and restore of the config-home surfaces a recipe mutates.

The nested e2e runner copies ONE throwaway config home per invocation and reuses
it (and the vehicle's dock) across every recipe (e2e_runner
_prepare_throwaway_config). A recipe that overwrites part of that config home and
does not put it back makes recipe ORDER a hidden coupling: the next recipe
inherits the mutated kdeglobals / lattedockrc / layout set. 022 and 034 solve
this per file with a whole-file backup restored in a finally; this generalizes
the contract to a SET of files (each present or absent before the recipe) and
directory trees (present or absent), so a config-mutating recipe leaves the
shared config home exactly as it found it.

``restore()`` returns True when EVERY surface was put back byte-verified, so a
call site reads truthfully (``if not snapshot.restore(): ...``); the caller
worsens a would-be success on a False return (a cleanup that leaves residue is
a failure, never a silent wrong state - 022's cleanup-status contract).
"""

from __future__ import annotations

import shutil
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path


def _warn(message: str) -> None:
    """A loud restore-failure line on stderr, in 022's ``FAIL: cleanup ...`` voice."""
    print(f"FAIL: cleanup {message}", file=sys.stderr, flush=True)


def _tree_files(root: Path) -> dict[Path, bytes]:
    """Every regular file under ``root`` as {relative path: bytes} (order-independent)."""
    return {p.relative_to(root): p.read_bytes() for p in root.rglob("*") if p.is_file()}


@dataclass(frozen=True, slots=True)
class _FileSurface:
    """A snapshotted file: its live path and a backup, or None when it was absent."""

    path: Path
    backup: Path | None


@dataclass(frozen=True, slots=True)
class _DirSurface:
    """A snapshotted directory: its live path and a whole-tree backup, or None when absent."""

    path: Path
    backup: Path | None


def _restore_file(surface: _FileSurface) -> bool:
    """Put a file surface back and byte-verify; True on a restore failure.

    A file that existed before is copied back and compared byte-for-byte; a file
    that was ABSENT before (the recipe created it) is removed again, so a
    created-from-nothing kdeglobals does not leak into the next recipe.
    """
    path, backup = surface.path, surface.backup
    if backup is not None:
        try:
            shutil.copyfile(backup, path)
        except OSError as err:
            _warn(f"could not restore {path}: {err}")
            return True
        if path.read_bytes() != backup.read_bytes():
            _warn(f"restored different bytes for {path}")
            return True
        return False
    try:
        path.unlink(missing_ok=True)
    except OSError as err:
        _warn(f"could not remove {path} (absent before the recipe): {err}")
        return True
    if path.exists():
        _warn(f"{path} is still present but was absent before the recipe")
        return True
    return False


def _restore_dir(surface: _DirSurface) -> bool:
    """Put a directory surface's whole tree back and byte-verify; True on failure.

    A directory ABSENT before the recipe (an XDG_DATA_HOME scratch tree, say) is
    removed again rather than restored, mirroring the absent-file case.
    """
    path, backup = surface.path, surface.backup
    if backup is None:
        try:
            if path.exists():
                shutil.rmtree(path)
        except OSError as err:
            _warn(f"could not remove {path} (absent before the recipe): {err}")
            return True
        if path.exists():
            _warn(f"{path} is still present but was absent before the recipe")
            return True
        return False
    try:
        if path.exists():
            shutil.rmtree(path)
        shutil.copytree(backup, path)
    except OSError as err:
        _warn(f"could not restore {path} from backup: {err}")
        return True
    if _tree_files(path) != _tree_files(backup):
        _warn(f"restored a different tree for {path}")
        return True
    return False


class ConfigHomeSnapshot:
    """A whole-surface snapshot of the config-home paths a recipe will mutate.

    Add each surface BEFORE the first mutation (``snapshot_file`` /
    ``snapshot_dir``), then call ``restore()`` from the recipe's finally. Backups
    live under a private temp dir that ``restore()`` removes, so a snapshot is
    SINGLE-USE: one restore() call per snapshot, never a second.
    """

    def __init__(self) -> None:
        self._root = Path(tempfile.mkdtemp(prefix="latte-config-snapshot-"))
        self._files: list[_FileSurface] = []
        self._dirs: list[_DirSurface] = []
        self._count = 0

    def _reserve(self, name: str) -> Path:
        """A unique backup path under the private root."""
        self._count += 1
        return self._root / f"{self._count}-{name}"

    def snapshot_file(self, path: Path) -> None:
        """Record ``path``'s current bytes, or its absence, for a later restore."""
        if path.exists():
            backup = self._reserve(path.name)
            shutil.copyfile(path, backup)
            self._files.append(_FileSurface(path, backup))
        else:
            self._files.append(_FileSurface(path, None))

    def snapshot_dir(self, path: Path) -> None:
        """Copy ``path``'s whole tree (or record its absence) for a later restore."""
        if path.exists():
            backup = self._reserve(path.name)
            shutil.copytree(path, backup)
            self._dirs.append(_DirSurface(path, backup))
        else:
            self._dirs.append(_DirSurface(path, None))

    def restore(self) -> bool:
        """Put every snapshotted surface back, byte-verified; True when ALL succeeded.

        Every surface is restored even if an earlier one fails (no short-circuit),
        so one bad surface never strands the others. The backup root is removed
        last, whatever the outcome, which is what makes the snapshot single-use.
        """
        failed = False
        for file_surface in self._files:
            failed = _restore_file(file_surface) or failed
        for dir_surface in self._dirs:
            failed = _restore_dir(dir_surface) or failed
        shutil.rmtree(self._root, ignore_errors=True)
        return not failed
