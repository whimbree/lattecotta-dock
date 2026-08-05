# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The interaction-gate contract: the argv and staging-decision logic.

The argv contract is load-bearing: the qmlcontracts ctest entry passes
tests/contracts as the sole argument, and the bare qmlinteraction entry passes
none, so both branches are pinned here.
"""

from pathlib import Path

from latte_harness.qml_interaction_tests import needs_staging, resolve_input_dir


def test_input_dir_defaults_to_tests_qml() -> None:
    repo = Path("/repo")
    assert resolve_input_dir([], repo) == repo / "tests" / "qml"


def test_input_dir_uses_the_argument_when_present() -> None:
    # The qmlcontracts entry's contract: the argument selects the directory.
    repo = Path("/repo")
    assert resolve_input_dir(["/repo/tests/contracts"], repo) == Path("/repo/tests/contracts")


def test_input_dir_ignores_extra_arguments() -> None:
    repo = Path("/repo")
    assert resolve_input_dir(["/first", "/second"], repo) == Path("/first")


def test_needs_staging_when_tree_absent(tmp_path: Path) -> None:
    # Nothing staged yet -> the guard says stage.
    assert needs_staging(tmp_path / "stage", "lib/qml")


def test_no_staging_when_tree_present(tmp_path: Path) -> None:
    stage = tmp_path / "stage"
    (stage / "lib/qml" / "org" / "kde" / "latte").mkdir(parents=True)
    assert not needs_staging(stage, "lib/qml")


def test_needs_staging_respects_the_distro_qmldir(tmp_path: Path) -> None:
    # The Latte tree staged under lib/qml must NOT satisfy an Arch-style
    # lib/qt6/qml probe: a hardcoded lib/qml would always miss off-nix.
    stage = tmp_path / "stage"
    (stage / "lib/qml" / "org" / "kde" / "latte").mkdir(parents=True)
    assert needs_staging(stage, "lib/qt6/qml")
