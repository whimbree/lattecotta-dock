# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
"""matrix_golden: tier selection, the crop-rect math, and the view crop rect over
a parsed viewsData payload - the pure pieces of the render-golden bridge, testable
without a compositor or latte-imgdiff (the subprocess seams stay thin wrappers).
"""

from __future__ import annotations

from collections.abc import Callable

import pytest

from latte_harness import matrix_golden, recipe
from latte_harness.matrix_golden import MatrixGoldenError

# ---- the compare tier (pure over the environment) --------------------------


def test_golden_tier_defaults_to_tolerance(monkeypatch: pytest.MonkeyPatch) -> None:
    # The vehicle dock is host-rendered, so interaction goldens gate at Tolerance,
    # not the sceneprobe BitExact (open question O3).
    monkeypatch.delenv("SCENEPROBE_TIER", raising=False)
    assert matrix_golden.golden_tier() == "tolerance"


def test_golden_tier_honors_an_explicit_override(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("SCENEPROBE_TIER", "bitexact")
    assert matrix_golden.golden_tier() == "bitexact"


def test_golden_tier_treats_an_empty_override_as_the_default(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    # ``${SCENEPROBE_TIER:-tolerance}``: an empty value takes the fallback.
    monkeypatch.setenv("SCENEPROBE_TIER", "")
    assert matrix_golden.golden_tier() == "tolerance"


# ---- the crop-rect math (WxH+X+Y from absoluteGeometry) --------------------


def test_crop_rect_of_formats_width_height_then_offset() -> None:
    # The bash ``"%dx%d+%d+%d" % (w, h, x, y)`` order: size, then origin.
    assert matrix_golden.crop_rect_of((0, 900, 1600, 100)) == "1600x100+0+900"


def test_crop_rect_of_keeps_a_nonzero_origin() -> None:
    assert matrix_golden.crop_rect_of((1600, 0, 800, 60)) == "800x60+1600+0"


@pytest.mark.parametrize("geometry", [(0, 0, 0, 100), (0, 0, 1600, 0), (0, 0, -1, -1)])
def test_crop_rect_of_refuses_a_degenerate_rect(geometry: tuple[int, int, int, int]) -> None:
    # A zero/negative rect is a symptom to surface, never cropped into a
    # plausible-but-wrong image.
    with pytest.raises(MatrixGoldenError, match="degenerate view rect"):
        _ = matrix_golden.crop_rect_of(geometry)


# ---- view_crop_rect over the typed recipe.views() surface ------------------
#
# W3 fold: view_crop_rect reads recipe.views() (the shared typed viewsData
# reader) instead of a re-declared _GoldenView twin, so the tests inject typed
# recipe.View records - the boundary parse itself is tested in test_recipe.py.


def _view(cid: int, *, cloned: bool, geometry: tuple[int, int, int, int]) -> recipe.View:
    """A complete recipe.View with only the crop-relevant fields varied; the rest
    take neutral always-emitted values so the record is a valid viewsData reply."""
    return recipe.View.model_validate(
        {
            "containmentId": cid,
            "isCloned": cloned,
            "isClonedFrom": cid if cloned else -1,
            "edge": "bottom",
            "alignment": "center",
            "screen": "Virtual-0",
            "visibilityMode": "alwaysVisible",
            "isHidden": False,
            "inStartup": False,
            "editMode": False,
            "inConfigureAppletsMode": False,
            "keyboardNavigation": False,
            "containmentAcceptsInput": True,
            "ownsPanelFocusSession": False,
            "absoluteGeometry": list(geometry),
            "localGeometry": [0, 0, geometry[2], geometry[3]],
            "screenGeometry": [0, 0, 1600, 1000],
            "type": "dock",
            "inputRegionRects": [[0, 0, geometry[2], geometry[3]]],
            "appliedInputRegionRects": [[0, 0, geometry[2], geometry[3]]],
        }
    )


def _returns_views(views: list[recipe.View]) -> Callable[..., list[recipe.View]]:
    def fake(*_args: object) -> list[recipe.View]:
        return views

    return fake


_TWO_VIEWS = [
    _view(16, cloned=False, geometry=(0, 900, 1600, 100)),
    _view(17, cloned=True, geometry=(0, 0, 1600, 100)),
]


def test_view_crop_rect_picks_the_single_non_cloned_view(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(recipe, "views", _returns_views(_TWO_VIEWS))
    assert matrix_golden.view_crop_rect() == "1600x100+0+900"


def test_view_crop_rect_selects_a_named_view(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(recipe, "views", _returns_views(_TWO_VIEWS))
    assert matrix_golden.view_crop_rect(17) == "1600x100+0+0"


def test_view_crop_rect_refuses_an_unknown_named_view(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(recipe, "views", _returns_views(_TWO_VIEWS))
    with pytest.raises(MatrixGoldenError, match="no view 99"):
        _ = matrix_golden.view_crop_rect(99)


def test_view_crop_rect_refuses_when_the_non_cloned_count_is_not_one(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    two_non_cloned = [
        _view(16, cloned=False, geometry=(0, 900, 1600, 100)),
        _view(17, cloned=False, geometry=(0, 0, 1600, 100)),
    ]
    monkeypatch.setattr(recipe, "views", _returns_views(two_non_cloned))
    with pytest.raises(MatrixGoldenError, match="expected exactly one non-cloned view, saw 2"):
        _ = matrix_golden.view_crop_rect()
