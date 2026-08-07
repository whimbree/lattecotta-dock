# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The config-home snapshot/restore contract: byte-verified put-back of the
surfaces a recipe mutates, present-or-absent, with restore() True only when
every surface went back and a False return the caller uses to worsen a
would-be success (the 022/034 cleanup-status contract, generalized)."""

import shutil
from pathlib import Path

from latte_harness.config_restore import ConfigHomeSnapshot


def test_restores_mutated_file_bytes(tmp_path: Path) -> None:
    kdeglobals = tmp_path / "kdeglobals"
    kdeglobals.write_bytes(b"[General]\nColorScheme=Original\n")
    snapshot = ConfigHomeSnapshot()
    snapshot.snapshot_file(kdeglobals)

    kdeglobals.write_bytes(b"[General]\nColorScheme=Mutated\n")
    assert snapshot.restore() is True
    assert kdeglobals.read_bytes() == b"[General]\nColorScheme=Original\n"


def test_recreates_file_deleted_by_the_recipe(tmp_path: Path) -> None:
    lattedockrc = tmp_path / "lattedockrc"
    lattedockrc.write_bytes(b"[UniversalSettings]\nsingleModeLayoutName=My Layout\n")
    snapshot = ConfigHomeSnapshot()
    snapshot.snapshot_file(lattedockrc)

    lattedockrc.unlink()  # the recipe deleted a file that existed before
    assert snapshot.restore() is True
    assert lattedockrc.read_bytes() == b"[UniversalSettings]\nsingleModeLayoutName=My Layout\n"


def test_deletes_file_that_was_absent_before(tmp_path: Path) -> None:
    kdeglobals = tmp_path / "kdeglobals"
    snapshot = ConfigHomeSnapshot()
    snapshot.snapshot_file(kdeglobals)  # absent at snapshot time

    kdeglobals.write_bytes(b"created by the recipe")
    assert snapshot.restore() is True
    assert not kdeglobals.exists()


def test_restores_directory_file_set_and_bytes(tmp_path: Path) -> None:
    latte = tmp_path / "latte"
    latte.mkdir()
    (latte / "My Layout.layout.latte").write_bytes(b"original layout")
    (latte / "Other.layout.latte").write_bytes(b"second layout")
    snapshot = ConfigHomeSnapshot()
    snapshot.snapshot_dir(latte)

    # the recipe's mutation: wipe the layout set and install one fixture layout
    for stale in latte.glob("*.layout.latte"):
        stale.unlink()
    (latte / "D21.layout.latte").write_bytes(b"fixture layout")

    assert snapshot.restore() is True
    assert {p.name for p in latte.iterdir()} == {
        "My Layout.layout.latte",
        "Other.layout.latte",
    }
    assert (latte / "My Layout.layout.latte").read_bytes() == b"original layout"
    assert (latte / "Other.layout.latte").read_bytes() == b"second layout"


def test_restores_nested_subdirectory_tree(tmp_path: Path) -> None:
    # The real config home's latte/ carries a templates/ subdir; the whole-tree
    # restore must bring nested files back and drop nested additions.
    latte = tmp_path / "latte"
    (latte / "templates").mkdir(parents=True)
    (latte / "My Layout.layout.latte").write_bytes(b"original layout")
    (latte / "templates" / "Default.layout.latte").write_bytes(b"original template")
    snapshot = ConfigHomeSnapshot()
    snapshot.snapshot_dir(latte)

    shutil.rmtree(latte / "templates")
    (latte / "My Layout.layout.latte").write_bytes(b"mutated layout")
    (latte / "extra").mkdir()
    (latte / "extra" / "leak").write_bytes(b"nested addition")

    assert snapshot.restore() is True
    assert (latte / "My Layout.layout.latte").read_bytes() == b"original layout"
    assert (latte / "templates" / "Default.layout.latte").read_bytes() == b"original template"
    assert not (latte / "extra").exists()


def test_removes_directory_that_was_absent_before(tmp_path: Path) -> None:
    data_home = tmp_path / "d28-data"
    snapshot = ConfigHomeSnapshot()
    snapshot.snapshot_dir(data_home)  # absent at snapshot time

    (data_home / "plasma" / "plasmoids").mkdir(parents=True)
    (data_home / "plasma" / "plasmoids" / "marker").write_bytes(b"test package")
    assert snapshot.restore() is True
    assert not data_home.exists()


def test_reports_failure_when_restore_target_is_unwritable(tmp_path: Path) -> None:
    lattedockrc = tmp_path / "lattedockrc"
    lattedockrc.write_bytes(b"[UniversalSettings]\n")
    snapshot = ConfigHomeSnapshot()
    snapshot.snapshot_file(lattedockrc)

    # the live path becomes a directory, so copyfile-back raises OSError: the
    # restore cannot proceed and must report failure loudly (worsen success).
    lattedockrc.unlink()
    lattedockrc.mkdir()
    assert snapshot.restore() is False


def test_restores_every_surface_before_reporting_failure(tmp_path: Path) -> None:
    good = tmp_path / "kdeglobals"
    good.write_bytes(b"good original")
    bad = tmp_path / "lattedockrc"
    bad.write_bytes(b"bad original")
    snapshot = ConfigHomeSnapshot()
    snapshot.snapshot_file(good)
    snapshot.snapshot_file(bad)

    good.write_bytes(b"good mutated")
    bad.unlink()
    bad.mkdir()  # forces this surface's restore to fail

    # the failing surface does not short-circuit the good surface's restore
    assert snapshot.restore() is False
    assert good.read_bytes() == b"good original"
