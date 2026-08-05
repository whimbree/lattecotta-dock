# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
"""matrix_fixture_check: the ported fixture gate stays green on the tree, and
its assertion/refusal controls are non-vacuous (they catch an introduced
violation), proving the port both ways per the BP equivalence contract.
"""

from __future__ import annotations

from pathlib import Path

from latte_harness.matrix_fixture_check import (
    TEMPLATE_REL,
    Checks,
    _assert_refused,  # pyright: ignore[reportPrivateUsage]
    run_checks,
)
from latte_harness.paths import RepoPaths


def test_check_passes_on_the_real_default_template(tmp_path: Path) -> None:
    template = RepoPaths.discover().root / TEMPLATE_REL
    assert template.is_file()
    assert run_checks(tmp_path, template) == 0


def test_key_assertion_catches_a_missing_line(tmp_path: Path) -> None:
    layout = tmp_path / "layout"
    _ = layout.write_text("location=4\n")
    checks = Checks()
    checks.key("wanted", layout, r"^location=3$")
    assert checks.fails == 1


def test_refusal_control_catches_a_fixture_that_was_not_refused(tmp_path: Path) -> None:
    # Drive a VALID descriptor through the refusal control: the generator accepts
    # it (exit 0, output written), so the control MUST flag it as a failure -
    # a refusal control that passed here would be vacuous.
    seed = tmp_path / "seed"
    (seed / "latte").mkdir(parents=True)
    template = RepoPaths.discover().root / TEMPLATE_REL
    _ = (seed / "latte" / "My Layout.layout.latte").write_text(template.read_text())
    _ = (seed / "lattedockrc").write_text(
        "[UniversalSettings]\nsingleModeLayoutName=My Layout\nmemoryUsage=0\n"
    )
    checks = Checks()
    _assert_refused(
        checks,
        "valid-cell-should-not-refuse",
        tmp_path / "out",
        seed,
        ["--view-type", "dock", "--edge", "top", "--alignment", "left", "--display", "1out"],
    )
    assert checks.fails == 1
