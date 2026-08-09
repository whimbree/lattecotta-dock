# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""audit: the harness contract with no live dock in it - the snapshot
serialization (byte-identical to the bash json.dumps formula), the changed-keys
diff classification (the safe present-on-one-side direction), the assert family's
verdict codes and byte-identical FAIL/REFUSED wording with negative controls, the
tasks-applet resolution refusal, the settings-window selection heuristic, and the
fractional pointer math.

The live pieces (real readbacks, the dock lifecycle, the settings-window drive)
are exercised by the nested-vehicle pilot tests/e2e/audit-harness-selftest.py;
here every seam is a parsed payload, a monkeypatched transport/probe, or a temp
snapshot file, so the whole detector is testable without a compositor.
"""

from __future__ import annotations

import json
from collections.abc import Callable
from pathlib import Path

import pytest

from latte_harness import audit, matrix, recipe
from latte_harness.audit import AuditError


# Typed stand-ins for monkeypatch.setattr (a bare lambda is untyped under strict,
# the same pattern test_matrix.py uses).
def _noop_action(*_args: str) -> None:
    return None


def _probe_returns(value: str) -> Callable[[int], str]:
    def fake(_view: int) -> str:
        return value

    return fake


def _wait_settled_ok(_timeout: int = 60) -> bool:
    return True


def _noop_sleep(_seconds: float) -> None:
    return None


# ---- snapshot serialization (byte-identical to the bash formula) -----------

_RAW_CONFIG: dict[str, object] = {
    "maxLength": 90,
    "ratio": 90.0,
    "enabled": True,
    "name": "x",
    "absent": None,
    "order": [3, 1, 2],
    "nested": {"b": 2, "a": 1},
}


def test_snapshot_lines_is_byte_identical_to_the_bash_field_dump() -> None:
    # The bash _audit_snapshot_object did ``for key in sorted(obj): print("%s\t%s"
    # % (key, json.dumps(obj[key], sort_keys=True)))`` into a redirect (trailing
    # newline per line). A byte difference here would false-diff a python snapshot
    # against a bash-captured one in the live parity check.
    expected = "".join(
        f"{key}\t{json.dumps(_RAW_CONFIG[key], sort_keys=True)}\n" for key in sorted(_RAW_CONFIG)
    )
    assert audit._snapshot_lines(_RAW_CONFIG) == expected  # pyright: ignore[reportPrivateUsage, reportArgumentType]


def test_snapshot_lines_sorts_keys_and_terminates_each_line() -> None:
    text = audit._snapshot_lines({"z": 1, "a": 2})  # pyright: ignore[reportPrivateUsage]
    assert text == "a\t2\nz\t1\n"


def test_snapshot_lines_of_an_empty_map_is_the_empty_string() -> None:
    # bash printed nothing for an empty object, so the redirect made a 0-byte file;
    # a stray "\n" here would break byte-parity on an empty snapshot.
    assert audit._snapshot_lines({}) == ""  # pyright: ignore[reportPrivateUsage]


def test_snapshot_lines_preserves_int_vs_float() -> None:
    text = audit._snapshot_lines({"i": 90, "f": 90.0})  # pyright: ignore[reportPrivateUsage]
    assert text == "f\t90.0\ni\t90\n"


def test_snapshot_lines_preserve_int_vs_float_through_the_pydantic_boundary() -> None:
    # End-to-end: a raw JSON string through the readback model into the
    # snapshot formula. Locks that pydantic JsonValue keeps 90 and 90.0
    # distinct (an == assert alone conflates them), so the on-disk snapshot
    # stays byte-identical to the bash json.load path.
    data = audit._ViewConfigData.model_validate_json(  # pyright: ignore[reportPrivateUsage]
        '{"config":{"i":90,"f":90.0},"view":{}}'
    )
    text = audit._snapshot_lines(data.config)  # pyright: ignore[reportPrivateUsage]
    assert text == "f\t90.0\ni\t90\n"


# ---- the pydantic readback boundary ----------------------------------------


def test_view_config_data_validates_config_and_view_maps() -> None:
    data = audit._ViewConfigData.model_validate_json(  # pyright: ignore[reportPrivateUsage]
        '{"config":{"maxLength":90},"view":{"byPassWM":true}}'
    )
    assert data.config == {"maxLength": 90}
    assert data.view == {"byPassWM": True}


def test_view_config_data_tolerates_a_dock_side_field_addition() -> None:
    data = audit._ViewConfigData.model_validate_json(  # pyright: ignore[reportPrivateUsage]
        '{"config":{},"view":{},"futureField":42}'
    )
    assert not hasattr(data, "futureField")


def test_view_config_data_rejects_a_payload_missing_the_view_object() -> None:
    from pydantic import ValidationError

    # The bash refused "snapshot payload has no 'view' object"; pydantic surfaces
    # the same absence as a loud ValidationError naming the field, at the boundary.
    with pytest.raises(ValidationError):
        _ = audit._ViewConfigData.model_validate_json('{"config":{}}')  # pyright: ignore[reportPrivateUsage]


def test_applet_config_data_validates_the_config_map() -> None:
    data = audit._AppletConfigData.model_validate_json('{"config":{"launchers":[]}}')  # pyright: ignore[reportPrivateUsage]
    assert data.config == {"launchers": []}


# ---- snapshot parsing (the bash load) --------------------------------------


def test_parse_snapshot_text_round_trips_key_tab_value_lines() -> None:
    parsed = audit._parse_snapshot_text('maxLength\t90\nname\t"x"\n')  # pyright: ignore[reportPrivateUsage]
    assert parsed == {"maxLength": "90", "name": '"x"'}


def test_parse_snapshot_text_skips_blank_lines() -> None:
    parsed = audit._parse_snapshot_text("a\t1\n\nb\t2\n")  # pyright: ignore[reportPrivateUsage]
    assert parsed == {"a": "1", "b": "2"}


# ---- changed-keys diff classification (the safe direction) -----------------


def test_diff_changed_keys_reports_a_changed_value() -> None:
    changed = audit._diff_changed_keys({"a": "1", "b": "2"}, {"a": "9", "b": "2"})  # pyright: ignore[reportPrivateUsage]
    assert changed == ["a"]


def test_diff_changed_keys_reports_nothing_when_equal() -> None:
    assert audit._diff_changed_keys({"a": "1"}, {"a": "1"}) == []  # pyright: ignore[reportPrivateUsage]


def test_diff_changed_keys_counts_a_key_present_on_only_one_side() -> None:
    # The KConfig default-deletion trap: a key present-then-absent (a return to
    # default) is a LOUD change, never a silent false-PASS. Both directions.
    assert audit._diff_changed_keys({"a": "1"}, {}) == ["a"]  # pyright: ignore[reportPrivateUsage]
    assert audit._diff_changed_keys({}, {"a": "1"}) == ["a"]  # pyright: ignore[reportPrivateUsage]


def test_diff_changed_keys_is_sorted() -> None:
    changed = audit._diff_changed_keys({"z": "1", "a": "1"}, {"z": "2", "a": "2"})  # pyright: ignore[reportPrivateUsage]
    assert changed == ["a", "z"]


# ---- changed_keys file boundary --------------------------------------------


def _snap(path: Path, *lines: str) -> Path:
    """The bash ``snap``: write ``key<TAB>value`` lines, newline-terminated."""
    _ = path.write_text("".join(f"{line}\n" for line in lines))
    return path


def test_changed_keys_reads_two_files(tmp_path: Path) -> None:
    before = _snap(tmp_path / "before", "maxLength\t100", "offset\t0")
    after = _snap(tmp_path / "after", "maxLength\t90", "offset\t0")
    assert audit.changed_keys(before, after) == ["maxLength"]


def test_changed_keys_refuses_a_missing_snapshot_file(tmp_path: Path) -> None:
    before = _snap(tmp_path / "before", "a\t1")
    missing = tmp_path / "nope"
    with pytest.raises(audit._Stop) as excinfo:  # pyright: ignore[reportPrivateUsage]
        _ = audit.changed_keys(before, missing)
    assert excinfo.value.code == 2
    assert (
        excinfo.value.line
        == f"audit: changed_keys: a snapshot file is missing ({before} / {missing})"
    )


# ---- assert_applies (P1) ----------------------------------------------------


def test_assert_applies_passes_when_the_key_changed(tmp_path: Path) -> None:
    before = _snap(tmp_path / "b", "maxLength\t100")
    after = _snap(tmp_path / "a", "maxLength\t90")
    assert audit.assert_applies(before, after, "maxLength") == 0


def test_assert_applies_fails_a_no_change_with_the_exact_wording(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    before = _snap(tmp_path / "b", "maxLength\t100")
    assert audit.assert_applies(before, before, "maxLength") == 1
    assert capsys.readouterr().err == (
        "audit: FAIL: P1 applies: key 'maxLength' did not change "
        "(a control that writes nothing readable)\n"
    )


def test_assert_applies_refuses_a_missing_key_with_the_exact_wording(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    before = _snap(tmp_path / "b", "maxLength\t100")
    assert audit.assert_applies(before, before, "") == 2
    assert capsys.readouterr().err == "audit: REFUSED: audit_assert_applies needs a key\n"


def test_assert_applies_refuses_a_missing_snapshot(tmp_path: Path) -> None:
    before = _snap(tmp_path / "b", "maxLength\t100")
    assert audit.assert_applies(before, tmp_path / "nope", "maxLength") == 2


# ---- assert_only_keys (P2, the decisive right-key check) --------------------


def test_assert_only_keys_passes_the_exact_set(tmp_path: Path) -> None:
    before = _snap(tmp_path / "b", "maxLength\t100", "minLength\t100")
    after = _snap(tmp_path / "a", "maxLength\t90", "minLength\t100")
    assert audit.assert_only_keys(before, after, "maxLength") == 0


def test_assert_only_keys_fails_a_stray_coupled_key(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    # The D15 coupling shape: a maxLength drive also moved minLength.
    before = _snap(tmp_path / "b", "maxLength\t100", "minLength\t100")
    after = _snap(tmp_path / "a", "maxLength\t90", "minLength\t90")
    assert audit.assert_only_keys(before, after, "maxLength") == 1
    assert "audit: FAIL: P2 right-key-only: changed key set != expected" in capsys.readouterr().err


def test_assert_only_keys_fails_a_missing_expected_key(tmp_path: Path) -> None:
    # under-write: expected {maxLength, offset} but only maxLength moved.
    before = _snap(tmp_path / "b", "maxLength\t100", "offset\t0")
    after = _snap(tmp_path / "a", "maxLength\t90", "offset\t0")
    assert audit.assert_only_keys(before, after, "maxLength", "offset") == 1


def test_assert_only_keys_dedups_and_drops_empty_expected(tmp_path: Path) -> None:
    before = _snap(tmp_path / "b", "maxLength\t100")
    after = _snap(tmp_path / "a", "maxLength\t90")
    assert audit.assert_only_keys(before, after, "maxLength", "maxLength", "") == 0


def test_assert_only_keys_refuses_a_missing_snapshot(tmp_path: Path) -> None:
    before = _snap(tmp_path / "b", "maxLength\t100")
    assert audit.assert_only_keys(before, tmp_path / "nope", "maxLength") == 2


# ---- assert_reflects (P3) ---------------------------------------------------


def test_assert_reflects_passes_a_matching_value(tmp_path: Path) -> None:
    snap = _snap(tmp_path / "s", "maxLength\t100")
    assert audit.assert_reflects(snap, "maxLength", "100") == 0


def test_assert_reflects_fails_a_wrong_value_with_the_exact_wording(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    snap = _snap(tmp_path / "s", "maxLength\t100")
    assert audit.assert_reflects(snap, "maxLength", "999") == 1
    assert (
        capsys.readouterr().err
        == "audit: FAIL: P3 reflects: key 'maxLength' is '100', expected '999'\n"
    )


def test_assert_reflects_fails_an_absent_key_with_the_exact_wording(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    snap = _snap(tmp_path / "s", "maxLength\t100")
    assert audit.assert_reflects(snap, "iconSize", "48") == 1
    assert (
        capsys.readouterr().err == "audit: FAIL: P3 reflects: key 'iconSize' absent from snapshot\n"
    )


def test_assert_reflects_refuses_a_missing_snapshot(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    missing = tmp_path / "nope"
    assert audit.assert_reflects(missing, "maxLength", "100") == 2
    assert (
        capsys.readouterr().err
        == f"audit: REFUSED: audit_assert_reflects: snapshot missing: {missing}\n"
    )


# ---- assert_agrees (P4) -----------------------------------------------------


def test_assert_agrees_passes_two_surfaces_holding_one_value(tmp_path: Path) -> None:
    a = _snap(tmp_path / "a", "maxLength\t90")
    b = _snap(tmp_path / "b", "sliderMax\t90")
    assert audit.assert_agrees(a, "maxLength", b, "sliderMax") == 0


def test_assert_agrees_fails_disagreeing_surfaces_with_the_exact_wording(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    a = _snap(tmp_path / "a", "maxLength\t90")
    b = _snap(tmp_path / "b", "sliderMax\t80")
    assert audit.assert_agrees(a, "maxLength", b, "sliderMax") == 1
    assert capsys.readouterr().err == (
        "audit: FAIL: P4 agrees: 'maxLength'='90' but 'sliderMax'='80' "
        "- two views of one value disagree\n"
    )


def test_assert_agrees_fails_an_absent_first_key(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    a = _snap(tmp_path / "a", "other\t1")
    b = _snap(tmp_path / "b", "sliderMax\t90")
    assert audit.assert_agrees(a, "maxLength", b, "sliderMax") == 1
    assert (
        capsys.readouterr().err
        == "audit: FAIL: P4 agrees: key 'maxLength' absent from first snapshot\n"
    )


def test_assert_agrees_fails_an_absent_second_key(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    a = _snap(tmp_path / "a", "maxLength\t90")
    b = _snap(tmp_path / "b", "other\t1")
    assert audit.assert_agrees(a, "maxLength", b, "sliderMax") == 1
    assert (
        capsys.readouterr().err
        == "audit: FAIL: P4 agrees: key 'sliderMax' absent from second snapshot\n"
    )


def test_assert_agrees_refuses_a_missing_snapshot(
    tmp_path: Path, capsys: pytest.CaptureFixture[str]
) -> None:
    a = _snap(tmp_path / "a", "maxLength\t90")
    assert audit.assert_agrees(a, "maxLength", tmp_path / "nope", "sliderMax") == 2
    assert capsys.readouterr().err == "audit: REFUSED: audit_assert_agrees: a snapshot is missing\n"


# ---- the verdict boundary (prints once, codes propagate) -------------------


def test_to_status_maps_a_refuse_to_2_and_prints_once(capsys: pytest.CaptureFixture[str]) -> None:
    def body() -> None:
        audit._refuse("bad input")  # pyright: ignore[reportPrivateUsage]

    assert audit._to_status(body) == 2  # pyright: ignore[reportPrivateUsage]
    assert capsys.readouterr().err == "audit: REFUSED: bad input\n"


def test_to_status_maps_a_fail_to_1_and_prints_once(capsys: pytest.CaptureFixture[str]) -> None:
    def body() -> None:
        audit._fail("residue")  # pyright: ignore[reportPrivateUsage]

    assert audit._to_status(body) == 1  # pyright: ignore[reportPrivateUsage]
    assert capsys.readouterr().err == "audit: FAIL: residue\n"


def test_to_status_returns_0_on_a_clean_body() -> None:
    assert audit._to_status(lambda: None) == 0  # pyright: ignore[reportPrivateUsage]


def test_to_status_reprints_nothing_for_an_already_reported_stop(
    capsys: pytest.CaptureFixture[str],
) -> None:
    # assert_only_keys prints its own two-line block then raises a silent _Stop(1);
    # the boundary must not re-emit a line.
    def body() -> None:
        raise audit._Stop(1)  # pyright: ignore[reportPrivateUsage]

    assert audit._to_status(body) == 1  # pyright: ignore[reportPrivateUsage]
    assert capsys.readouterr().err == ""


# ---- tasks_applet_id resolution (recipe.view_applets reuse) ----------------


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


def _returns_applets(applets: list[recipe.Applet]) -> Callable[..., list[recipe.Applet]]:
    def fake(*_args: object) -> list[recipe.Applet]:
        return applets

    return fake


def test_tasks_applet_id_returns_the_single_plasmoid_id(monkeypatch: pytest.MonkeyPatch) -> None:
    applets = [_applet(4, "org.kde.latte.plasmoid"), _applet(7, "org.kde.plasma.marginsseparator")]
    monkeypatch.setattr(recipe, "view_applets", _returns_applets(applets))
    assert audit.tasks_applet_id(16) == 4


def test_tasks_applet_id_refuses_when_no_plasmoid(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(recipe, "view_applets", _returns_applets([]))
    with pytest.raises(
        AuditError, match="expected exactly one tasks plasmoid under view 16, saw 0"
    ):
        _ = audit.tasks_applet_id(16)


def test_tasks_applet_id_refuses_two_plasmoids(monkeypatch: pytest.MonkeyPatch) -> None:
    applets = [_applet(4, "org.kde.latte.plasmoid"), _applet(5, "org.kde.latte.plasmoid")]
    monkeypatch.setattr(recipe, "view_applets", _returns_applets(applets))
    with pytest.raises(AuditError, match="saw 2"):
        _ = audit.tasks_applet_id(16)


# ---- the settings-window selection heuristic (recipe.windows reuse) --------


def _window(resource_class: str, x: int, y: int, w: int, h: int) -> recipe.Window:
    return recipe.Window(
        resource_class=resource_class,
        caption="c",
        geometry_field=f"{x},{y} {w}x{h}",
        x=x,
        y=y,
        width=w,
        height=h,
        output="Virtual-0",
        layer=3,
    )


def _returns_windows(windows: list[recipe.Window]) -> Callable[..., list[recipe.Window]]:
    def fake(*_args: object) -> list[recipe.Window]:
        return windows

    return fake


def test_settings_window_rect_picks_the_tall_wide_latte_window(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    windows = [
        _window("latte-dock", 0, 900, 1600, 100),  # the dock strip: too short
        _window("firefox", 100, 100, 800, 600),  # tall+wide but not latte-dock
        _window("latte-dock", 200, 150, 900, 700),  # the config window
    ]
    monkeypatch.setattr(recipe, "windows", _returns_windows(windows))
    assert audit.settings_window_rect() == (200, 150, 900, 700)


def test_settings_window_rect_returns_none_when_no_config_window(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.setattr(
        recipe, "windows", _returns_windows([_window("latte-dock", 0, 900, 1600, 100)])
    )
    assert audit.settings_window_rect() is None
    assert capsys.readouterr().err == "audit: no settings config window mapped\n"


def test_settings_window_rect_excludes_a_too_wide_window(monkeypatch: pytest.MonkeyPatch) -> None:
    # width must be < 2000 (a full-screen-width dock, not a config window).
    monkeypatch.setattr(
        recipe, "windows", _returns_windows([_window("latte-dock", 0, 0, 2400, 800)])
    )
    assert audit.settings_window_rect() is None


# ---- the fractional pointer math -------------------------------------------


def test_fractional_point_truncates_toward_zero() -> None:
    # int($x + $xf * $w) semantics: (100 + 0.5*900, 150 + 0.25*700) = (550, 325).
    assert audit._fractional_point((100, 150, 900, 700), 0.5, 0.25) == (550, 325)  # pyright: ignore[reportPrivateUsage]


def test_fractional_point_at_the_origin_and_far_corner() -> None:
    assert audit._fractional_point((10, 20, 100, 200), 0.0, 0.0) == (10, 20)  # pyright: ignore[reportPrivateUsage]
    assert audit._fractional_point((10, 20, 100, 200), 1.0, 1.0) == (110, 220)  # pyright: ignore[reportPrivateUsage]


# ---- the editmode drive/probe reuse (never-swallow the D-Bus failure) ------


def test_drive_editmode_returns_false_on_a_dbus_failure(monkeypatch: pytest.MonkeyPatch) -> None:
    def boom(*_args: str) -> None:
        raise matrix.MatrixDriveError("setViewEditMode failed")

    monkeypatch.setattr(matrix, "drive_action", boom)
    assert audit._drive_editmode(16, on=True) is False  # pyright: ignore[reportPrivateUsage]


def test_drive_editmode_returns_true_on_success(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(matrix, "drive_action", _noop_action)
    assert audit._drive_editmode(16, on=False) is True  # pyright: ignore[reportPrivateUsage]


def test_editmode_is_treats_a_gone_view_as_not_the_target(monkeypatch: pytest.MonkeyPatch) -> None:
    def gone(_view: int) -> str:
        raise matrix.MatrixProbeError

    monkeypatch.setattr(matrix, "verb_editmode_probe", gone)
    assert audit._editmode_is(16, "true") is False  # pyright: ignore[reportPrivateUsage]


def test_enter_editmode_fails_loudly_when_it_never_turns_on(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.setattr(matrix, "drive_action", _noop_action)
    monkeypatch.setattr(matrix, "verb_editmode_probe", _probe_returns("false"))
    monkeypatch.setattr(recipe, "wait_settled", _wait_settled_ok)
    monkeypatch.setattr("time.sleep", _noop_sleep)  # skip the 30x0.2s poll wait
    assert audit.enter_editmode(16) is False
    assert capsys.readouterr().err == "audit: FAIL: edit mode never turned on for view 16\n"


def test_enter_editmode_returns_true_when_edit_mode_reads_on(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(matrix, "drive_action", _noop_action)
    monkeypatch.setattr(matrix, "verb_editmode_probe", _probe_returns("true"))
    monkeypatch.setattr(recipe, "wait_settled", _wait_settled_ok)
    assert audit.enter_editmode(16) is True


# ---- the fakepointer env guard ---------------------------------------------


def test_fakepointer_refuses_a_missing_binary_env(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.delenv("E2E_FAKEPOINTER", raising=False)
    with pytest.raises(AuditError, match="E2E_FAKEPOINTER is unset"):
        audit._fakepointer("click", "1", "2")  # pyright: ignore[reportPrivateUsage]
