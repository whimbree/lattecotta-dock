# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The seeder's pure logic: layout detection, lifecycle-token parsing, and the
staged dock's environment assembly, each with a negative control.

The full seed round-trip (compositor bring-up, staged dock, teardown) is driven
by the run-matrix default self-test, not here; these pin the decision points a
unit test can hold without a running compositor.
"""

from pathlib import Path

import pytest

from latte_harness.seed import (
    RUNNING_STATE,
    build_dock_env,
    has_default_layout,
    parse_lifecycle_state,
)

# ---- has_default_layout ----------------------------------------------------


def test_has_default_layout_true_when_layout_present(tmp_path: Path) -> None:
    (tmp_path / "latte").mkdir()
    (tmp_path / "latte" / "My Layout.layout.latte").write_text("[Containments]\n")
    assert has_default_layout(tmp_path) is True


def test_has_default_layout_false_on_empty_config(tmp_path: Path) -> None:
    (tmp_path / "latte").mkdir()
    assert has_default_layout(tmp_path) is False


def test_has_default_layout_false_when_latte_dir_absent(tmp_path: Path) -> None:
    # the seed tree starts empty; a missing latte/ subdir must read as no layout,
    # not raise (the loop drives this every second until the dock writes one)
    assert has_default_layout(tmp_path) is False


def test_has_default_layout_ignores_non_layout_files(tmp_path: Path) -> None:
    (tmp_path / "latte").mkdir()
    (tmp_path / "latte" / "lattedockrc").write_text("x")
    (tmp_path / "latte" / "notes.txt").write_text("x")
    assert has_default_layout(tmp_path) is False


# ---- parse_lifecycle_state -------------------------------------------------


def test_parse_lifecycle_state_keeps_quotes_like_awk() -> None:
    # busctl renders `s "running"`; awk '{print $2}' kept the quotes, so the
    # parsed token must equal the quoted constant the loop compares against
    assert parse_lifecycle_state('s "running"') == RUNNING_STATE


def test_parse_lifecycle_state_reads_other_states() -> None:
    assert parse_lifecycle_state('s "startup"') == '"startup"'


@pytest.mark.parametrize("blank", ["", "s", "   "])
def test_parse_lifecycle_state_empty_on_short_or_blank(blank: str) -> None:
    assert parse_lifecycle_state(blank) == ""


# ---- build_dock_env --------------------------------------------------------


def test_build_dock_env_points_at_nested_bus_and_seed() -> None:
    base = {"DISPLAY": ":0", "XAUTHORITY": "/x", "PATH": "/bin"}
    env = build_dock_env(
        base,
        runtime_dir=Path("/run/nested"),
        socket="wl-seed",
        bus="unix:path=/tmp/bus,guid=abc",
        seeddir=Path("/seed"),
        build=Path("/build"),
    )
    assert env["XDG_RUNTIME_DIR"] == "/run/nested"
    assert env["WAYLAND_DISPLAY"] == "wl-seed"
    assert env["DBUS_SESSION_BUS_ADDRESS"] == "unix:path=/tmp/bus,guid=abc"
    assert env["LATTE_CONFIG_HOME"] == "/seed"
    assert env["BUILD"] == "/build"
    assert env["PATH"] == "/bin"
    assert "DISPLAY" not in env
    assert "XAUTHORITY" not in env
