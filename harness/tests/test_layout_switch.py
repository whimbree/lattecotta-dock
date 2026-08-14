# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The layout_switch helper contract: the lattedockrc selection round-trips with
KConfig key case intact, staging refuses the states that would corrupt a run (a
live dock's config flush, an active name no fixture carries) and replaces the
layout set exactly, the active-layout wait accepts only the settled single-mode
shape and names what it last observed on timeout, and the plugin-sequence join
surfaces a disagreement between its two readbacks instead of dropping ids.

Everything is driven through injected seams (monkeypatched recipe/applet_reorder
readbacks, tmp_path config homes), so the whole contract is testable without a
live dock or compositor - the same shape test_recipe.py uses for the wait loops.
"""

from pathlib import Path

import pytest

from latte_harness import applet_reorder, layout_switch, recipe


def _fixture(tmp_path: Path, name: str) -> Path:
    fixture = tmp_path / "fixtures" / f"{name}.layout.latte"
    fixture.parent.mkdir(parents=True, exist_ok=True)
    fixture.write_text(f"[Containments][1]\nname={name} Dock\n")
    return fixture


def _no_dock(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(recipe, "dock_pid", lambda: None)


# ---- the lattedockrc selection write/read ----------------------------------


def test_selection_write_and_read_round_trip(tmp_path: Path) -> None:
    lattedockrc = tmp_path / "lattedockrc"
    layout_switch.write_single_mode_selection(lattedockrc, "SwitchA")
    assert layout_switch.read_single_mode_selection(lattedockrc) == "SwitchA"
    text = lattedockrc.read_text()
    # KConfig keys are case-sensitive; a lowercased singlemodelayoutname would
    # be a key the dock never reads (the optionxform pin).
    assert "singleModeLayoutName=SwitchA" in text
    assert "memoryUsage=0" in text


def test_selection_write_preserves_unrelated_keys(tmp_path: Path) -> None:
    lattedockrc = tmp_path / "lattedockrc"
    lattedockrc.write_text("[UniversalSettings]\nlaunchers=a,b\n\n[Other]\nsomeKey=kept\n")
    layout_switch.write_single_mode_selection(lattedockrc, "SwitchB")
    text = lattedockrc.read_text()
    assert "launchers=a,b" in text
    assert "someKey=kept" in text
    assert layout_switch.read_single_mode_selection(lattedockrc) == "SwitchB"


def test_selection_read_reports_an_absent_key_as_empty(tmp_path: Path) -> None:
    lattedockrc = tmp_path / "lattedockrc"
    lattedockrc.write_text("[Other]\nsomeKey=kept\n")
    assert layout_switch.read_single_mode_selection(lattedockrc) == ""


# ---- staging: refusals and the exact layout-set replacement ----------------


def test_stage_refuses_a_running_dock(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    def _alive(pid: int) -> bool:
        return True

    monkeypatch.setattr(recipe, "dock_pid", lambda: 4242)
    monkeypatch.setattr(recipe, "pid_alive", _alive)
    fixture = _fixture(tmp_path, "SwitchA")
    with pytest.raises(recipe.RecipeError, match="still running"):
        layout_switch.stage_single_mode_layouts(tmp_path / "config", [fixture], "SwitchA")


def test_stage_refuses_an_active_name_no_fixture_carries(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    _no_dock(monkeypatch)
    fixture = _fixture(tmp_path, "SwitchA")
    with pytest.raises(recipe.RecipeError, match="not among the staged fixtures"):
        layout_switch.stage_single_mode_layouts(tmp_path / "config", [fixture], "Elsewhere")


def test_stage_replaces_the_layout_set_exactly(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    _no_dock(monkeypatch)
    config_home = tmp_path / "config"
    layouts_dir = config_home / "latte"
    layouts_dir.mkdir(parents=True)
    (layouts_dir / "Stale.layout.latte").write_text("[Containments][9]\n")
    switch_a = _fixture(tmp_path, "SwitchA")
    switch_b = _fixture(tmp_path, "SwitchB")

    layout_switch.stage_single_mode_layouts(config_home, [switch_a, switch_b], "SwitchA")

    staged = sorted(p.name for p in layouts_dir.glob("*.layout.latte"))
    assert staged == ["SwitchA.layout.latte", "SwitchB.layout.latte"]
    assert (layouts_dir / "SwitchA.layout.latte").read_bytes() == switch_a.read_bytes()
    assert layout_switch.read_single_mode_selection(config_home / "lattedockrc") == "SwitchA"


# ---- the active-layout wait (injected layoutsData reader) ------------------


def _record(name: str, *, active: bool = True) -> recipe.LayoutRecord:
    return recipe.LayoutRecord.model_validate(
        {"name": name, "isActive": active, "activities": [], "viewsCount": 1}
    )


def _layouts(memory: str, records: list[recipe.LayoutRecord]) -> recipe.LayoutsData:
    return recipe.LayoutsData.model_validate({"memoryUsage": memory, "layouts": records})


def _single(*records: recipe.LayoutRecord) -> recipe.LayoutsData:
    return _layouts("single", list(records))


def test_wait_returns_once_the_target_is_the_single_active_layout(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(layout_switch, "_POLL_INTERVAL_SECONDS", 0.0)
    replies = iter(
        [
            _single(_record("SwitchA")),  # the old layout, still loaded
            _single(),  # the mid-switch window: nothing loaded
            _single(_record("SwitchB")),
        ]
    )
    monkeypatch.setattr(recipe, "layouts_data", lambda: next(replies))
    record = layout_switch.wait_for_single_active_layout("SwitchB", timeout=5.0)
    assert record.name == "SwitchB"
    assert record.is_active is True


def test_wait_rejects_an_inactive_or_crowded_reply(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(layout_switch, "_POLL_INTERVAL_SECONDS", 0.0)
    inactive = _single(_record("SwitchB", active=False))
    crowded = _layouts("multiple", [_record("SwitchB"), _record("SwitchA")])
    replies = iter([inactive, crowded, _single(_record("SwitchB"))])
    monkeypatch.setattr(recipe, "layouts_data", lambda: next(replies))
    # Only the third reply satisfies the settled single-mode shape; the first
    # two must be classified "not yet", never returned.
    record = layout_switch.wait_for_single_active_layout("SwitchB", timeout=5.0)
    assert record.name == "SwitchB"


def test_wait_timeout_names_the_last_observed_reply(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(layout_switch, "_POLL_INTERVAL_SECONDS", 0.0)
    monkeypatch.setattr(recipe, "layouts_data", lambda: _single(_record("SwitchA")))
    with pytest.raises(recipe.RecipeError, match=r"never became.*SwitchA") as excinfo:
        layout_switch.wait_for_single_active_layout("SwitchB", timeout=0.05)
    assert "SwitchB" in str(excinfo.value)


def test_wait_timeout_reports_an_unreachable_dock(monkeypatch: pytest.MonkeyPatch) -> None:
    def _refuse() -> recipe.LayoutsData:
        raise recipe.DbusUnavailableError("layoutsData: busctl call failed (dock unreachable?)")

    monkeypatch.setattr(layout_switch, "_POLL_INTERVAL_SECONDS", 0.0)
    monkeypatch.setattr(recipe, "layouts_data", _refuse)
    with pytest.raises(recipe.RecipeError, match="dock unreachable"):
        layout_switch.wait_for_single_active_layout("SwitchB", timeout=0.05)


# ---- the plugin-sequence join ----------------------------------------------


def _applet(applet_id: int, plugin: str) -> recipe.Applet:
    return recipe.Applet.model_validate(
        {
            "id": applet_id,
            "plugin": plugin,
            "geometry": [0, 0, 10, 10],
            "inScheduledDestruction": False,
            "z": 0.0,
            "colorizerActive": False,
            "colorizerReason": "",
        }
    )


def _order_of_five_then_four(view: int) -> str:
    # The REAL applet_reorder_order reply shape: busctl's quoted array items,
    # preserved by _parse_applets_order (an unquoted fake here once let the
    # unit tests pass while the first live 122 drive failed on the quotes).
    return '"5" "4"'


def test_plugin_sequence_follows_the_order_readback(monkeypatch: pytest.MonkeyPatch) -> None:
    applets = [_applet(4, "org.kde.latte.plasmoid"), _applet(5, "org.kde.plasma.analogclock")]

    def _applets(cid: int) -> list[recipe.Applet]:
        return applets

    monkeypatch.setattr(applet_reorder, "applet_reorder_order", _order_of_five_then_four)
    monkeypatch.setattr(recipe, "view_applets", _applets)
    assert layout_switch.applet_plugin_sequence(2) == [
        "org.kde.plasma.analogclock",
        "org.kde.latte.plasmoid",
    ]


def test_plugin_sequence_surfaces_an_id_the_applets_readback_lacks(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    def _applets(cid: int) -> list[recipe.Applet]:
        return [_applet(4, "org.kde.latte.plasmoid")]

    monkeypatch.setattr(applet_reorder, "applet_reorder_order", _order_of_five_then_four)
    monkeypatch.setattr(recipe, "view_applets", _applets)
    with pytest.raises(recipe.RecipeError, match=r"\['5'\] are missing"):
        layout_switch.applet_plugin_sequence(2)
