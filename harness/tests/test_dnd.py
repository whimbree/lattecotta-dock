# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
"""dnd: the widget-explorer DnD driver's pure logic with no live dock - the
delegate/centre/empty-point coordinate math, the tall-and-narrow explorer-window
selection, the busctl int-reply parse, and the addwidget verb's registration and
never-swallow refusal. The live drag (fakepointer choreography, the marker poll)
is exercised by the nested-vehicle parity flow against 093; here every seam is a
parsed value or a monkeypatched readback, so the whole detector is testable
without a compositor.
"""

from __future__ import annotations

from collections.abc import Callable

import pytest

from latte_harness import dnd, matrix, recipe
from latte_harness.dnd import DndError
from latte_harness.matrix import MatrixDriveError


def _window(resource_class: str, geometry_field: str, width: int, height: int) -> recipe.Window:
    """A recipe.Window with only the fields the explorer selection reads."""
    return recipe.Window(
        resource_class=resource_class,
        caption="",
        geometry_field=geometry_field,
        x=0,
        y=0,
        width=width,
        height=height,
        output="Virtual-1",
        layer=3,
    )


# ---- the delegate press point (coordinate math) ----------------------------


def test_delegate_point_is_column0_body() -> None:
    # "x,y WxH": +width/6 (column-0 centre of a 3-wide grid), +300 into the body.
    assert dnd._delegate_point("0,0 300x900") == (50, 300)  # pyright: ignore[reportPrivateUsage]


def test_delegate_point_carries_the_window_origin() -> None:
    assert dnd._delegate_point("120,40 600x800") == (220, 340)  # pyright: ignore[reportPrivateUsage]


def test_delegate_point_refuses_an_empty_rect() -> None:
    # The bash ``call dnd_open_explorer first`` guard: an unset rect is a caller
    # error, never a press at (0,0).
    with pytest.raises(DndError, match="call dnd_open_explorer first"):
        _ = dnd.delegate_point("")


# ---- the view centre (commit target) ---------------------------------------


def test_center_of_uses_integer_halves() -> None:
    assert dnd._center_of((0, 900, 1601, 101)) == (800, 950)  # pyright: ignore[reportPrivateUsage]


# ---- the tall-and-narrow explorer-window selection -------------------------


def test_select_explorer_rect_picks_the_tall_narrow_latte_window() -> None:
    windows = [
        _window("latte-dock", "0,900 1600x100", 1600, 100),  # the wide-short edge dock
        _window("org.kde.plasma.dock", "0,0 300x900", 300, 900),  # tall-narrow but foreign
        _window("latte-dock", "0,0 300x900", 300, 900),  # the explorer
    ]
    assert dnd._select_explorer_rect(windows, 1600, 1000) == "0,0 300x900"  # pyright: ignore[reportPrivateUsage]


def test_select_explorer_rect_returns_none_when_only_edge_docks_are_open() -> None:
    windows = [_window("latte-dock", "0,900 1600x100", 1600, 100)]
    assert dnd._select_explorer_rect(windows, 1600, 1000) is None  # pyright: ignore[reportPrivateUsage]


def test_select_explorer_rect_ignores_the_tiny_shadow_helper() -> None:
    windows = [_window("latte-dock", "0,0 8x8", 8, 8)]
    assert dnd._select_explorer_rect(windows, 1600, 1000) is None  # pyright: ignore[reportPrivateUsage]


# ---- the empty (off-dock) abort point --------------------------------------


def test_empty_point_returns_the_first_uncovered_candidate() -> None:
    assert dnd._empty_point_from([], 1600, 1000) == (800, 400)  # pyright: ignore[reportPrivateUsage]


def test_empty_point_skips_a_covered_candidate() -> None:
    # A band across the first candidate (800,400) but clear of the second (800,600).
    covering = [(0, 300, 1600, 200)]
    assert dnd._empty_point_from(covering, 1600, 1000) == (800, 600)  # pyright: ignore[reportPrivateUsage]


def test_empty_point_is_none_when_every_candidate_is_covered() -> None:
    fullscreen = [(0, 0, 1600, 1000)]
    assert dnd._empty_point_from(fullscreen, 1600, 1000) is None  # pyright: ignore[reportPrivateUsage]


def test_empty_point_refuses_loudly_when_all_covered(monkeypatch: pytest.MonkeyPatch) -> None:
    # The live wrapper turns the all-covered None into the exact bash refusal,
    # never a point silently on top of a view.
    monkeypatch.setenv("E2E_SCREEN_W", "1600")
    monkeypatch.setenv("E2E_SCREEN_H", "1000")

    def full_screen_views() -> list[recipe.View]:
        return [
            recipe.View.model_validate(
                {
                    "containmentId": 1,
                    "edge": "bottom",
                    "isHidden": False,
                    "inStartup": False,
                    "absoluteGeometry": [0, 0, 1600, 1000],
                    "localGeometry": [0, 0, 1600, 1000],
                    "screenGeometry": [0, 0, 1600, 1000],
                }
            )
        ]

    monkeypatch.setattr(recipe, "views", full_screen_views)
    with pytest.raises(DndError, match="every candidate is inside a view"):
        _ = dnd.empty_point()


# ---- the busctl int-reply parse (marker readback) --------------------------


@pytest.mark.parametrize(
    ("reply", "field"),
    [("i -1\n", "-1"), ("i 3\n", "3"), ("x 0", "0"), ("", ""), ("   ", "")],
)
def test_last_field_takes_the_busctl_value(reply: str, field: str) -> None:
    assert dnd._last_field(reply) == field  # pyright: ignore[reportPrivateUsage]


@pytest.mark.parametrize(
    ("text", "ok"),
    [("-1", True), ("3", True), ("+0", False), ("", False), ("as", False), ("1.5", False)],
)
def test_is_int_matches_the_bash_regex(text: str, ok: bool) -> None:
    assert dnd._is_int(text) is ok  # pyright: ignore[reportPrivateUsage]


# ---- the addwidget verb (registration + never-swallow) ---------------------


def test_addwidget_verb_is_registered_at_import() -> None:
    assert "addwidget" in matrix._VERBS  # pyright: ignore[reportPrivateUsage]


def test_addwidget_probe_reports_the_applet_count(monkeypatch: pytest.MonkeyPatch) -> None:
    def three_applets(_cid: int) -> list[recipe.Applet]:
        return [
            recipe.Applet.model_validate(
                {"id": i, "plugin": "p", "geometry": [0, 0, 1, 1], "inScheduledDestruction": False}
            )
            for i in range(3)
        ]

    monkeypatch.setattr(recipe, "view_applets", three_applets)
    assert dnd.verb_addwidget_probe(16) == "3"


def test_addwidget_drive_translates_a_step_failure_to_a_drive_error(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    # A failed open (DndError) must surface as MatrixDriveError so the matrix
    # backbone refuses the scenario - never a silently swallowed drag.
    def failing_open(_cid: int) -> str:
        raise DndError("dnd_open_explorer: no widget-explorer window appeared")

    monkeypatch.setattr(dnd, "open_explorer", failing_open)
    with pytest.raises(MatrixDriveError, match="no widget-explorer window appeared"):
        dnd.verb_addwidget_drive(16, "commit")


def _drag_recorder(sink: list[tuple[int, ...]]) -> Callable[..., None]:
    def fake(_rect: str, *coords: int) -> None:
        sink.append(coords)

    return fake


def _returns_rect(_cid: int) -> str:
    return "0,0 300x900"


def _returns_center(_cid: int) -> tuple[int, int]:
    return 800, 950


def _returns_empty() -> tuple[int, int]:
    return 400, 500


def test_addwidget_drive_commit_drops_on_the_view_centre(monkeypatch: pytest.MonkeyPatch) -> None:
    calls: list[tuple[int, ...]] = []
    monkeypatch.setattr(dnd, "open_explorer", _returns_rect)
    monkeypatch.setattr(dnd, "view_center", _returns_center)
    monkeypatch.setattr(dnd, "drag_widget", _drag_recorder(calls))
    dnd.verb_addwidget_drive(16, "commit")
    # commit hovers then releases on the centre: (cx cy cx cy), no empty point.
    assert calls == [(800, 950, 800, 950)]


def test_addwidget_drive_abort_releases_over_an_empty_point(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    calls: list[tuple[int, ...]] = []
    monkeypatch.setattr(dnd, "open_explorer", _returns_rect)
    monkeypatch.setattr(dnd, "view_center", _returns_center)
    monkeypatch.setattr(dnd, "empty_point", _returns_empty)
    monkeypatch.setattr(dnd, "drag_widget", _drag_recorder(calls))
    dnd.verb_addwidget_drive(16, "abort")
    # abort hovers the view (spacer opens) THEN releases over nothing.
    assert calls == [(800, 950, 800, 950, 400, 500, 400, 500)]
