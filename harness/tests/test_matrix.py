# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
"""matrix: the harness contract that has no live dock in it - the cell-parse
refusal classification (byte-identical messages), the residue-view snapshot
(byte-identical to the bash formula), the KConfig-snapshot residue detection over
injected states, the applets-order probe validation, the residue-surface diff and
its naming, and the verb dispatch refusals.

The live pieces (staging, the dock lifecycle, real D-Bus) are exercised by the
nested-vehicle pilot tests/e2e/matrix-harness-selftest.py; here every seam is
driven through a parsed payload, a monkeypatched transport, or a temp KConfig
file, so the whole detector is testable without a compositor.
"""

from __future__ import annotations

import json
import os
import signal
import subprocess
from collections.abc import Callable
from contextlib import suppress
from pathlib import Path

import pytest

from latte_harness import matrix, matrix_fixture, recipe
from latte_harness.matrix import (
    Baseline,
    MatrixDriveError,
    MatrixError,
    MatrixProbeError,
    MatrixView,
)


# Typed stand-ins for monkeypatch.setattr (a bare lambda is untyped under strict).
def _returns_status(code: int, stdout: str) -> Callable[..., tuple[int, str]]:
    # **kwargs so the fake stands in for recipe.call_status(*args, quiet=...).
    def fake(*_args: object, **_kwargs: object) -> tuple[int, str]:
        return code, stdout

    return fake


def _returns_str(value: str) -> Callable[..., str]:
    def fake(*_args: object) -> str:
        return value

    return fake


def _returns_applets(applets: list[recipe.Applet]) -> Callable[..., list[recipe.Applet]]:
    def fake(*_args: object) -> list[recipe.Applet]:
        return applets

    return fake


def _returns_pid(pid: int | None) -> Callable[..., int | None]:
    def fake(*_args: object) -> int | None:
        return pid

    return fake


# ---- cell-parse refusal classification (byte-identical messages) -----------


def test_parse_cell_accepts_a_valid_dock_cell() -> None:
    parsed = matrix.parse_cell("dock-top-left-1out")
    assert (parsed.view_type, parsed.edge, parsed.alignment, parsed.display) == (
        "dock",
        "top",
        "left",
        "1out",
    )


def test_parse_cell_accepts_a_panel_justify_cell() -> None:
    parsed = matrix.parse_cell("panel-bottom-justify-2out")
    assert parsed.display == "2out"
    assert parsed.alignment == "justify"


_CELL_REFUSALS = [
    (
        "dock-diagonal-left-1out",
        "matrix: REFUSED: cell 'dock-diagonal-left-1out': bad edge 'diagonal'",
    ),
    ("slab-top-left-1out", "matrix: REFUSED: cell 'slab-top-left-1out': bad viewType 'slab'"),
    ("dock-top-skew-1out", "matrix: REFUSED: cell 'dock-top-skew-1out': bad alignment 'skew'"),
    ("dock-top-left-3out", "matrix: REFUSED: cell 'dock-top-left-3out': bad display '3out'"),
    (
        "dock-top-left",
        "matrix: REFUSED: cell 'dock-top-left' is not <viewType>-<edge>-<alignment>-<display>",
    ),
    (
        "dock-top-left-1out-extra",
        "matrix: REFUSED: cell 'dock-top-left-1out-extra' is not "
        "<viewType>-<edge>-<alignment>-<display>",
    ),
]


@pytest.mark.parametrize(("cell", "line"), _CELL_REFUSALS)
def test_parse_cell_refuses_with_the_exact_bash_message(cell: str, line: str) -> None:
    with pytest.raises(matrix._Stop) as excinfo:  # pyright: ignore[reportPrivateUsage]
        _ = matrix.parse_cell(cell)
    assert excinfo.value.code == 2
    assert excinfo.value.line == line


# ---- the verdict boundary (matrix_refuse 2 / matrix_fail 1) ----------------


def test_to_status_maps_a_refuse_to_2_and_prints_once(capsys: pytest.CaptureFixture[str]) -> None:
    def body() -> None:
        matrix._refuse("bad cell")  # pyright: ignore[reportPrivateUsage]

    assert matrix._to_status(body) == 2  # pyright: ignore[reportPrivateUsage]
    assert capsys.readouterr().err == "matrix: REFUSED: bad cell\n"


def test_to_status_maps_a_fail_to_1_and_prints_once(capsys: pytest.CaptureFixture[str]) -> None:
    def body() -> None:
        matrix._fail("residue")  # pyright: ignore[reportPrivateUsage]

    assert matrix._to_status(body) == 1  # pyright: ignore[reportPrivateUsage]
    assert capsys.readouterr().err == "matrix: FAIL: residue\n"


def test_to_status_returns_0_on_a_clean_body() -> None:
    assert matrix._to_status(lambda: None) == 0  # pyright: ignore[reportPrivateUsage]


def test_to_status_reprints_nothing_for_an_already_reported_stop(
    capsys: pytest.CaptureFixture[str],
) -> None:
    # The ``view="$(...)" || return 2`` shape: view_id already printed, so the
    # silent _Stop yields 2 with no second line.
    def body() -> None:
        raise matrix._Stop(2)  # pyright: ignore[reportPrivateUsage]

    assert matrix._to_status(body) == 2  # pyright: ignore[reportPrivateUsage]
    assert capsys.readouterr().err == ""


# ---- the residue-view snapshot (byte-identical to the bash formula) --------

# A COMPLETE viewsData record: the residue-relevant fields the snapshot serializes
# PLUS the rest of the shared recipe.View surface MatrixView now inherits (W3), so a
# dropped key exercises a real malformed reply, not a lazy fixture.
_RAW_VIEW: dict[str, object] = {
    "containmentId": 16,
    "isCloned": False,
    "isClonedFrom": -1,
    "type": "dock",
    "edge": "bottom",
    "alignment": "center",
    "screen": "Virtual-0",
    "onPrimary": True,
    "visibilityMode": "alwaysVisible",
    "editMode": False,
    "inConfigureAppletsMode": False,
    "keyboardNavigation": False,
    "containmentAcceptsInput": True,
    "ownsPanelFocusSession": False,
    "isHidden": False,
    "inStartup": False,
    "isOffScreen": False,
    "strutsThickness": 100,
    "publishedStruts": [0, 0, 0, 100],
    "maskRect": [0, 900, 1600, 100],
    "inputRegionRects": [[0, 900, 1600, 100]],
    "appliedInputRegionRects": [[0, 900, 1600, 100]],
    "absoluteGeometry": [0, 900, 1600, 100],
    "localGeometry": [0, 0, 1600, 100],
    "screenGeometry": [0, 0, 1600, 1000],
}


def test_residue_snapshot_is_byte_identical_to_the_bash_field_dump() -> None:
    # The bash matrix_probe_view did ``json.dumps({k: v.get(k) for k in fields},
    # sort_keys=True)``; a byte difference here is a residue detector that would
    # false-diff against a bash-captured baseline in the parity check.
    expected = json.dumps(
        {k: _RAW_VIEW[k] for k in matrix._VIEW_RESIDUE_KEYS},  # pyright: ignore[reportPrivateUsage]
        sort_keys=True,
    )
    assert MatrixView.model_validate(_RAW_VIEW).residue_snapshot() == expected


def test_residue_snapshot_omits_identity_and_clone_fields() -> None:
    snapshot = MatrixView.model_validate(_RAW_VIEW).residue_snapshot()
    assert "containmentId" not in snapshot
    assert "isCloned" not in snapshot


def test_residue_snapshot_changes_when_edit_mode_flips() -> None:
    # The abort backbone's crux: a leaked edit-mode session shows as a view-surface
    # diff (editMode false -> true).
    clean = MatrixView.model_validate(_RAW_VIEW).residue_snapshot()
    leaked = MatrixView.model_validate({**_RAW_VIEW, "editMode": True}).residue_snapshot()
    assert clean != leaked


def test_matrix_view_tolerates_a_dock_side_field_addition() -> None:
    parsed = MatrixView.model_validate({**_RAW_VIEW, "futureField": 42})
    assert parsed.containment_id == 16
    assert not hasattr(parsed, "futureField")


def test_matrix_view_rejects_a_malformed_geometry_length() -> None:
    from pydantic import ValidationError

    with pytest.raises(ValidationError):
        _ = MatrixView.model_validate({**_RAW_VIEW, "absoluteGeometry": [0, 900, 1600]})


def test_matrix_view_builds_on_the_shared_recipe_view() -> None:
    # W3 fold: MatrixView extends recipe.View instead of re-declaring the viewsData
    # surface, so it IS a View and inherits the shared fields (edit_mode, is_hidden,
    # ...) while adding its residue-only geometry (struts/mask/input region).
    parsed = MatrixView.model_validate(_RAW_VIEW)
    assert isinstance(parsed, recipe.View)
    assert parsed.edit_mode is False
    assert parsed.struts_thickness == 100


def test_matrix_view_rejects_a_missing_inherited_field() -> None:
    from pydantic import ValidationError

    # A shared field now inherited from recipe.View is required: dropping it is a
    # malformed reply that must fail at the boundary, not silently default.
    incomplete = {k: v for k, v in _RAW_VIEW.items() if k != "editMode"}
    with pytest.raises(ValidationError):
        _ = MatrixView.model_validate(incomplete)


# ---- the KConfig snapshot (residue detection over injected states) ---------


def _write(path: Path, text: str) -> Path:
    _ = path.write_text(text)
    return path


def test_kconfig_snapshot_sorts_groups_and_keys(tmp_path: Path) -> None:
    layout = _write(
        tmp_path / "layout",
        "[Containments][1][General]\nzoomLevel=16\nalignment=0\n\n[Containments][1]\nlocation=4\n",
    )
    snapshot = matrix._kconfig_snapshot(layout)  # pyright: ignore[reportPrivateUsage]
    assert (
        snapshot
        == "[Containments][1]\nlocation=4\n[Containments][1][General]\nalignment=0\nzoomLevel=16"
    )


def test_kconfig_snapshot_catches_an_injected_marker(tmp_path: Path) -> None:
    # The selftest's inject_universal shape: a foreign marker key in a group must
    # surface as a new snapshot line = residue.
    clean = _write(tmp_path / "clean", "[UniversalSettings]\nmemoryUsage=0\n")
    dirty = _write(
        tmp_path / "dirty", "[UniversalSettings]\nmemoryUsage=0\nmatrixResidueMarker=1\n"
    )
    before = matrix._kconfig_snapshot(clean, "[UniversalSettings]")  # pyright: ignore[reportPrivateUsage]
    after = matrix._kconfig_snapshot(dirty, "[UniversalSettings]")  # pyright: ignore[reportPrivateUsage]
    assert before != after
    assert "matrixResidueMarker=1" in after


def test_kconfig_snapshot_strips_base_volatile_keys(tmp_path: Path) -> None:
    layout = _write(tmp_path / "layout", "[General]\ntimerShow=250\nalignment=0\n")
    snapshot = matrix._kconfig_snapshot(layout)  # pyright: ignore[reportPrivateUsage]
    assert "timerShow" not in snapshot
    assert "alignment=0" in snapshot


def test_kconfig_snapshot_strips_scenario_volatile_extra(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    monkeypatch.setenv("MATRIX_VOLATILE_EXTRA", "breathingKey otherKey")
    layout = _write(tmp_path / "layout", "[General]\nbreathingKey=9\nalignment=0\n")
    snapshot = matrix._kconfig_snapshot(layout)  # pyright: ignore[reportPrivateUsage]
    assert "breathingKey" not in snapshot
    assert "alignment=0" in snapshot


def test_kconfig_snapshot_scopes_to_the_prefix(tmp_path: Path) -> None:
    # ``[Foo]`` and ``[Foo][Bar]`` are in scope; ``[Foobar]`` is not.
    layout = _write(
        tmp_path / "layout",
        "[Foo]\na=1\n\n[Foo][Bar]\nb=2\n\n[Foobar]\nc=3\n\n[Other]\nd=4\n",
    )
    snapshot = matrix._kconfig_snapshot(layout, "[Foo]")  # pyright: ignore[reportPrivateUsage]
    assert "[Foo]" in snapshot
    assert "b=2" in snapshot
    assert "c=3" not in snapshot
    assert "d=4" not in snapshot


def test_kconfig_snapshot_refuses_a_missing_file(tmp_path: Path) -> None:
    with pytest.raises(MatrixProbeError):
        _ = matrix._kconfig_snapshot(tmp_path / "nope")  # pyright: ignore[reportPrivateUsage]


# ---- the applets-order probe (never-swallow validation) --------------------


def test_probe_applets_order_returns_a_validated_as_array(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(
        recipe,
        "call_status",
        _returns_status(0, 'as 2 "org.kde.latte.plasmoid" "org.kde.plasma.marginsseparator"\n'),
    )
    assert (
        matrix.probe_applets_order(16)
        == 'as 2 "org.kde.latte.plasmoid" "org.kde.plasma.marginsseparator"'
    )


def test_probe_applets_order_refuses_a_dbus_call_failure(monkeypatch: pytest.MonkeyPatch) -> None:
    # A call error must NOT read as the same empty order on both sides (false PASS).
    monkeypatch.setattr(recipe, "call_status", _returns_status(1, ""))
    with pytest.raises(MatrixProbeError):
        _ = matrix.probe_applets_order(16)


def test_probe_applets_order_refuses_a_non_array_reply(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(recipe, "call_status", _returns_status(0, "b true\n"))
    with pytest.raises(MatrixProbeError):
        _ = matrix.probe_applets_order(16)


# ---- surface list and the residue diff (naming the surface) ----------------


def test_surface_list_is_the_fixed_core_without_applet_groups(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.delenv("MATRIX_APPLET_CONFIG_GROUPS", raising=False)
    assert matrix.surface_list() == [
        "view",
        "applets_order",
        "config",
        "universal",
        "screenpool",
        "verb",
    ]


def test_surface_list_appends_one_appletcfg_per_group(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("MATRIX_APPLET_CONFIG_GROUPS", "[Containments][12][Applets][4]")
    assert matrix.surface_list()[-1] == "appletcfg:[Containments][12][Applets][4]"


def _clean_baseline(monkeypatch: pytest.MonkeyPatch) -> Baseline:
    monkeypatch.delenv("MATRIX_APPLET_CONFIG_GROUPS", raising=False)
    return Baseline(snapshots=dict.fromkeys(matrix.surface_list(), "base"), frame=None)


def test_assert_baseline_restored_reports_no_residue_when_all_surfaces_match(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    baseline = _clean_baseline(monkeypatch)
    monkeypatch.setattr(matrix, "capture_surface", _returns_str("base"))
    assert matrix.assert_baseline_restored(16, "editmode", baseline) == 0


def test_assert_baseline_restored_names_the_residue_surface(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    baseline = _clean_baseline(monkeypatch)

    def fake_capture(surface: str, _view: int, _verb: str) -> str:
        return "stranded" if surface == "universal" else "base"

    monkeypatch.setattr(matrix, "capture_surface", fake_capture)
    assert matrix.assert_baseline_restored(16, "editmode", baseline) == 1
    assert "matrix: RESIDUE in surface 'universal' after abort:" in capsys.readouterr().err


def test_assert_baseline_restored_flags_an_unassertable_reread(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    baseline = _clean_baseline(monkeypatch)

    def fake_capture(surface: str, _view: int, _verb: str) -> str:
        if surface == "config":
            raise MatrixProbeError
        return "base"

    monkeypatch.setattr(matrix, "capture_surface", fake_capture)
    assert matrix.assert_baseline_restored(16, "editmode", baseline) == 1
    assert "FAILED on re-read" in capsys.readouterr().err


def test_assert_baseline_restored_flags_a_missing_baseline_snapshot(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.delenv("MATRIX_APPLET_CONFIG_GROUPS", raising=False)
    partial = Baseline(snapshots={"view": "base"}, frame=None)
    monkeypatch.setattr(matrix, "capture_surface", _returns_str("base"))
    assert matrix.assert_baseline_restored(16, "editmode", partial) == 1
    assert "no baseline snapshot for surface 'applets_order'" in capsys.readouterr().err


# ---- verb dispatch (register, refuse the unknown, action never-swallow) -----


def test_register_verb_round_trips_a_drive_probe_pair() -> None:
    seen: list[tuple[int, str]] = []
    matrix.register_verb("selftest_noop", lambda v, o: seen.append((v, o)), lambda _v: "probed")
    try:
        assert matrix.verb_drive("selftest_noop", 7, "commit") == 0
        assert seen == [(7, "commit")]
        assert matrix.verb_probe("selftest_noop", 7) == "probed"
    finally:
        del matrix._VERBS["selftest_noop"]  # pyright: ignore[reportPrivateUsage]


def test_verb_drive_refuses_an_unknown_verb(capsys: pytest.CaptureFixture[str]) -> None:
    assert matrix.verb_drive("bogus", 1, "commit") == 2
    assert "matrix: REFUSED: unknown verb 'bogus'" in capsys.readouterr().err


def test_verb_probe_refuses_an_unknown_verb() -> None:
    with pytest.raises(matrix._Stop) as excinfo:  # pyright: ignore[reportPrivateUsage]
        _ = matrix.verb_probe("bogus", 1)
    assert excinfo.value.code == 2


def test_drive_action_accepts_a_successful_call(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(recipe, "call_status", _returns_status(0, ""))
    matrix.drive_action("setViewEditMode", "ub", "16", "true")  # no raise


def test_drive_action_refuses_to_swallow_a_dbus_failure(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(recipe, "call_status", _returns_status(1, ""))
    with pytest.raises(MatrixDriveError):
        matrix.drive_action("setViewEditMode", "ub", "16", "true")


def test_a_driver_dbus_failure_becomes_a_scenario_refusal(monkeypatch: pytest.MonkeyPatch) -> None:
    # verb_drive translates a driver's MatrixDriveError to a refusal (code 2), the
    # bash ``matrix_verb_drive ... || return 2`` shape.
    def failing_drive(_v: int, _o: str) -> None:
        raise MatrixDriveError("boom")

    matrix.register_verb("selftest_failer", failing_drive, lambda _v: "x")
    try:
        assert matrix.verb_drive("selftest_failer", 1, "commit") == 2
    finally:
        del matrix._VERBS["selftest_failer"]  # pyright: ignore[reportPrivateUsage]


# ---- applet config group resolution ----------------------------------------


def test_applet_config_group_names_the_single_matching_applet(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    applet = recipe.Applet.model_validate(
        {
            "id": 4,
            "plugin": "org.kde.latte.plasmoid",
            "geometry": [0, 0, 10, 10],
            "inScheduledDestruction": False,
            "z": 0.0,
            "colorizerActive": False,
            "colorizerReason": "",
        }
    )
    monkeypatch.setattr(recipe, "view_applets", _returns_applets([applet]))
    assert (
        matrix.applet_config_group(12, "org.kde.latte.plasmoid") == "[Containments][12][Applets][4]"
    )


def test_applet_config_group_refuses_when_not_present_exactly_once(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(recipe, "view_applets", _returns_applets([]))
    with pytest.raises(MatrixError, match="expected exactly one"):
        _ = matrix.applet_config_group(12, "org.kde.latte.plasmoid")


# ---- the 2out secondary-output pin (the P1 fixture descriptor) --------------


def test_build_descriptor_1out_uses_the_sentinel_screen(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.delenv("E2E_MO_SECONDARY", raising=False)
    parsed = matrix.parse_cell("dock-top-left-1out")
    descriptor = matrix._build_descriptor("dock-top-left-1out", parsed)  # pyright: ignore[reportPrivateUsage]
    assert descriptor.screen == ""
    assert descriptor.screen_id == matrix_fixture.SECONDARY_ABSENT_SCREEN_ID


def test_build_descriptor_2out_without_a_discovered_secondary_stays_sentinel(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    # An undiscovered secondary must fall to the sentinel so the dock refuses the
    # view, never silently places it on the primary.
    monkeypatch.delenv("E2E_MO_SECONDARY", raising=False)
    parsed = matrix.parse_cell("dock-bottom-center-2out")
    descriptor = matrix._build_descriptor("dock-bottom-center-2out", parsed)  # pyright: ignore[reportPrivateUsage]
    assert descriptor.screen_id == matrix_fixture.SECONDARY_ABSENT_SCREEN_ID


def test_build_descriptor_2out_pins_a_discovered_secondary(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setenv("E2E_MO_SECONDARY", "HDMI-A-2")
    monkeypatch.setenv("E2E_MO_SECONDARY_ID", "11")
    monkeypatch.setenv("E2E_MO_SECONDARY_GEOM", "1600,0 1600x1000")
    parsed = matrix.parse_cell("dock-bottom-center-2out")
    descriptor = matrix._build_descriptor("dock-bottom-center-2out", parsed)  # pyright: ignore[reportPrivateUsage]
    assert descriptor.screen == "HDMI-A-2"
    assert descriptor.screen_id == 11
    assert descriptor.screen_geometry == "1600,0 1600x1000"


def test_build_descriptor_2out_secondary_without_an_id_refuses(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setenv("E2E_MO_SECONDARY", "HDMI-A-2")
    monkeypatch.delenv("E2E_MO_SECONDARY_ID", raising=False)
    parsed = matrix.parse_cell("dock-bottom-center-2out")
    with pytest.raises(matrix._Stop) as excinfo:  # pyright: ignore[reportPrivateUsage]
        _ = matrix._build_descriptor("dock-bottom-center-2out", parsed)  # pyright: ignore[reportPrivateUsage]
    assert excinfo.value.code == 2


# ---- the quiet dock-stop wrapper (reaping lives in recipe.dock_stop, D275) ---


def test_stop_dock_returns_false_without_a_recorded_pid(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("E2E_MODE", "nested")
    monkeypatch.setattr(recipe, "dock_pid", _returns_pid(None))
    assert matrix.stop_dock() is False


def test_stop_dock_delegates_and_stays_quiet(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    # The wrapper's whole contract since the D275 fix moved reaping into
    # recipe.dock_stop: delegate the stop (the child gets reaped there) and
    # suppress the stop's stderr chatter (the bash matrix_stage's
    # ``e2e_dock_stop >/dev/null 2>&1 || true``). A real child stands in.
    child = subprocess.Popen(["sleep", "60"])
    try:
        monkeypatch.setenv("E2E_MODE", "nested")
        monkeypatch.setattr(recipe, "dock_pid", _returns_pid(child.pid))
        assert matrix.stop_dock(timeout=5) is True
        assert child.poll() is not None  # reaped through recipe.dock_stop
        assert capsys.readouterr().err == ""  # the quiet half of the contract
    finally:
        with suppress(ProcessLookupError, ChildProcessError):
            os.kill(child.pid, signal.SIGKILL)
            _ = os.waitpid(child.pid, 0)
        child.returncode = 0
