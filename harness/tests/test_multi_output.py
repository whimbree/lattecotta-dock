# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
"""multi_output: the multi-output discover-and-pin layer's pure logic with no
dual-output vehicle - the primary/secondary discovery from ScreenPool, the
restorable KScreen projection and its refusals, the priority classification, the
no-field-dropped semantic comparison, the rectangle topology classifier, the
placement-target math, and the pin/readback assertions. The runtime drivers
(kscreen-doctor mutation, the topology-mutation safety gate, the poll loops) need
the two-output vehicle and are exercised there; here every seam is pure math or a
parsed readback, testable without a compositor.
"""

from __future__ import annotations

import json

import pytest
from pydantic import ValidationError

from latte_harness import multi_output
from latte_harness.multi_output import MultiOutputError, OutputState, Screen

# ---- readback models (the pydantic boundary) -------------------------------


def _screen(
    screen_id: int,
    name: str,
    geometry: tuple[int, int, int, int] = (0, 0, 1920, 1080),
    *,
    active: bool = True,
    primary: bool = False,
) -> Screen:
    return Screen.model_validate(
        {
            "id": screen_id,
            "name": name,
            "geometry": list(geometry),
            "isActive": active,
            "isPrimary": primary,
        }
    )


def test_screen_rejects_a_short_geometry_array() -> None:
    # Rect is a 4-tuple; a 3-element geometry is malformed and must fail at the
    # boundary, not three subsystems away.
    with pytest.raises(ValidationError):
        _ = Screen.model_validate(
            {"id": 1, "name": "DP-1", "geometry": [0, 0, 1920], "isActive": True, "isPrimary": True}
        )


# ---- discovery -------------------------------------------------------------


def test_discover_identifies_primary_and_secondary() -> None:
    screens = [
        _screen(0, "DP-1", (0, 0, 1920, 1080), primary=True),
        _screen(10, "DP-2", (1920, 0, 1080, 1920)),
    ]
    assert multi_output._discover_from_screens(screens, "DP-1") == (  # pyright: ignore[reportPrivateUsage]
        "DP-1",
        "DP-2",
        "1920,0 1080x1920",
    )


def test_discover_refuses_when_not_two_active() -> None:
    screens = [_screen(0, "DP-1", primary=True)]
    with pytest.raises(MultiOutputError, match="expected exactly 2 active outputs"):
        _ = multi_output._discover_from_screens(screens, "DP-1")  # pyright: ignore[reportPrivateUsage]


def test_discover_refuses_two_primaries() -> None:
    screens = [_screen(0, "DP-1", primary=True), _screen(1, "DP-2", primary=True)]
    with pytest.raises(MultiOutputError, match="expected exactly 1 primary"):
        _ = multi_output._discover_from_screens(screens, "DP-1")  # pyright: ignore[reportPrivateUsage]


def test_discover_refuses_when_the_onprimary_view_disagrees() -> None:
    screens = [_screen(0, "DP-1", primary=True), _screen(1, "DP-2")]
    with pytest.raises(MultiOutputError, match="discovery inconsistent"):
        _ = multi_output._discover_from_screens(screens, "DP-2")  # pyright: ignore[reportPrivateUsage]


# ---- KScreen projection ----------------------------------------------------


def _output(
    name: str,
    *,
    rotation: object = 1,
    scale: object = 1.0,
    enabled: object = True,
    x: object = 0,
    y: object = 0,
    priority: object = 1,
    extra: dict[str, object] | None = None,
) -> dict[str, object]:
    output: dict[str, object] = {
        "name": name,
        "rotation": rotation,
        "scale": scale,
        "enabled": enabled,
        "pos": {"x": x, "y": y},
        "priority": priority,
    }
    if extra is not None:
        output.update(extra)
    return output


def _kscreen(outputs: list[dict[str, object]], extra: dict[str, object] | None = None) -> str:
    payload: dict[str, object] = {"outputs": outputs}
    if extra is not None:
        payload.update(extra)
    return json.dumps(payload)


def test_project_returns_both_restorable_outputs() -> None:
    state = _kscreen(
        [_output("DP-1", priority=1), _output("DP-2", rotation=2, scale=2.0, x=1920, priority=2)]
    )
    assert multi_output._project_output_state(state, "DP-1", "DP-2") == [  # pyright: ignore[reportPrivateUsage]
        OutputState(name="DP-1", enabled=True, rotation="none", scale=1.0, x=0, y=0, priority=1),
        OutputState(name="DP-2", enabled=True, rotation="left", scale=2.0, x=1920, y=0, priority=2),
    ]


@pytest.mark.parametrize(
    ("state", "message"),
    [
        (_kscreen([_output("DP-1", priority=1)]), "expected exactly two"),
        (_kscreen([_output("A", priority=1), _output("B", priority=2)]), "absent from KScreen"),
        (
            _kscreen([_output("DP-1", rotation=99, priority=1), _output("DP-2", priority=2)]),
            "unsupported rotation",
        ),
        (
            _kscreen([_output("DP-1", scale=0, priority=1), _output("DP-2", priority=2)]),
            "invalid scale",
        ),
        (
            _kscreen([_output("DP-1", enabled="yes", priority=1), _output("DP-2", priority=2)]),
            "invalid enabled",
        ),
        (
            _kscreen([_output("DP-1", x="0", priority=1), _output("DP-2", priority=2)]),
            "invalid position",
        ),
        (_kscreen([_output("DP-1", priority=0), _output("DP-2", priority=2)]), "invalid priority"),
        (
            _kscreen([_output("DP-1", priority=1), _output("DP-2", priority=3)]),
            "continuous unique priorities",
        ),
        ("not json at all", "valid JSON"),
    ],
)
def test_project_refuses_malformed_or_unrestorable_state(state: str, message: str) -> None:
    with pytest.raises(MultiOutputError, match=message):
        _ = multi_output._project_output_state(state, "DP-1", "DP-2")  # pyright: ignore[reportPrivateUsage]


def test_project_refuses_a_whitespace_output_name() -> None:
    state = _kscreen([_output("DP 1", priority=1), _output("DP-2", priority=2)])
    with pytest.raises(MultiOutputError, match="whitespace in output name"):
        _ = multi_output._project_output_state(state, "DP 1", "DP-2")  # pyright: ignore[reportPrivateUsage]


# ---- priority classification -----------------------------------------------


def test_classify_priorities_canonical_is_zero() -> None:
    state = _kscreen([_output("DP-1", priority=1), _output("DP-2", priority=2)])
    assert multi_output._classify_output_priorities(state, "DP-1", "DP-2") == 0  # pyright: ignore[reportPrivateUsage]


def test_classify_priorities_uncanonical_needs_normalization() -> None:
    # Both active virtual outputs can start at priority 0; classify flags it for
    # normalization (return 1), not as malformed.
    state = _kscreen([_output("DP-1", priority=0), _output("DP-2", priority=0)])
    assert multi_output._classify_output_priorities(state, "DP-1", "DP-2") == 1  # pyright: ignore[reportPrivateUsage]


@pytest.mark.parametrize(
    "state",
    [
        _kscreen([_output("DP-1", enabled=False, priority=1), _output("DP-2", priority=2)]),
        _kscreen([_output("A", priority=1), _output("B", priority=2)]),
        _kscreen([_output("DP-1", priority="hi"), _output("DP-2", priority=2)]),
        "}{ not json",
    ],
)
def test_classify_priorities_malformed_is_two(state: str) -> None:
    assert multi_output._classify_output_priorities(state, "DP-1", "DP-2") == 2  # pyright: ignore[reportPrivateUsage]


# ---- semantic comparison (no field dropped) --------------------------------


def test_compare_equal_ignores_output_and_mode_order() -> None:
    captured = _kscreen(
        [
            _output("DP-1", priority=1, extra={"modes": [{"id": "m1"}, {"id": "m2"}]}),
            _output("DP-2", priority=2),
        ]
    )
    # same content, outputs and modes in a different array order
    current = _kscreen(
        [
            _output("DP-2", priority=2),
            _output("DP-1", priority=1, extra={"modes": [{"id": "m2"}, {"id": "m1"}]}),
        ]
    )
    assert multi_output._compare_output_state_semantically(captured, current) == (0, "")  # pyright: ignore[reportPrivateUsage]


def test_compare_tolerates_int_vs_float_numbers() -> None:
    captured = _kscreen([_output("DP-1", scale=1, priority=1), _output("DP-2", priority=2)])
    current = _kscreen([_output("DP-1", scale=1.0, priority=1), _output("DP-2", priority=2)])
    assert multi_output._compare_output_state_semantically(captured, current) == (0, "")  # pyright: ignore[reportPrivateUsage]


def test_compare_reports_a_changed_scalar_as_drift() -> None:
    captured = _kscreen([_output("DP-1", priority=1), _output("DP-2", priority=2)])
    current = _kscreen([_output("DP-1", priority=1), _output("DP-2", scale=3.0, priority=2)])
    status, message = multi_output._compare_output_state_semantically(captured, current)  # pyright: ignore[reportPrivateUsage]
    assert status == 1
    assert "drifted at" in message and "scale" in message


def test_compare_reports_a_removed_field_as_drift() -> None:
    captured = _kscreen(
        [_output("DP-1", priority=1, extra={"vrr": True}), _output("DP-2", priority=2)]
    )
    current = _kscreen([_output("DP-1", priority=1), _output("DP-2", priority=2)])
    status, message = multi_output._compare_output_state_semantically(captured, current)  # pyright: ignore[reportPrivateUsage]
    assert status == 1
    assert "removed" in message


def test_compare_reports_a_type_change_as_drift() -> None:
    captured = _kscreen(
        [_output("DP-1", priority=1, extra={"tag": "a"}), _output("DP-2", priority=2)]
    )
    current = _kscreen([_output("DP-1", priority=1, extra={"tag": 1}), _output("DP-2", priority=2)])
    status, message = multi_output._compare_output_state_semantically(captured, current)  # pyright: ignore[reportPrivateUsage]
    assert status == 1
    assert "type changed" in message


def test_compare_malformed_input_is_status_two() -> None:
    status, message = multi_output._compare_output_state_semantically("not json", "{}")  # pyright: ignore[reportPrivateUsage]
    assert status == 2
    assert "valid JSON" in message


# ---- rectangle topology classifier -----------------------------------------


def test_classify_rectangles_full_touching() -> None:
    assert (
        multi_output.mo_classify_rectangles((0, 0, 1920, 1080), (1920, 0, 1080, 1920))
        == "full-touching"
    )


def test_classify_rectangles_partial_touching() -> None:
    assert (
        multi_output.mo_classify_rectangles((0, 0, 1920, 1080), (1920, 540, 1080, 1920))
        == "partial-touching"
    )


def test_classify_rectangles_disconnected_by_gap() -> None:
    assert (
        multi_output.mo_classify_rectangles((0, 0, 1920, 1080), (2400, 270, 1080, 1920))
        == "disconnected"
    )


def test_classify_rectangles_touching_edge_without_overlap_is_disconnected() -> None:
    assert (
        multi_output.mo_classify_rectangles((0, 0, 100, 100), (100, 200, 100, 100))
        == "disconnected"
    )


def test_classify_rectangles_refuses_overlap() -> None:
    # the recipe's controlled negative: positive-area overlap is outside the contract
    with pytest.raises(MultiOutputError, match="overlap"):
        _ = multi_output.mo_classify_rectangles((0, 0, 1600, 1000), (1500, 100, 1000, 1600))


def test_classify_rectangles_refuses_nonpositive_size() -> None:
    with pytest.raises(MultiOutputError, match="sizes must be positive"):
        _ = multi_output.mo_classify_rectangles((0, 0, 0, 100), (100, 0, 100, 100))


# ---- rectangle read --------------------------------------------------------


def test_read_rectangles_returns_primary_then_secondary() -> None:
    screens = [
        _screen(0, "DP-1", (0, 0, 1920, 1080), primary=True),
        _screen(10, "DP-2", (1920, 0, 1080, 1920)),
    ]
    assert multi_output._read_rectangles_from(screens, "DP-1", "DP-2") == (  # pyright: ignore[reportPrivateUsage]
        (0, 0, 1920, 1080),
        (1920, 0, 1080, 1920),
    )


def test_read_rectangles_refuses_an_inactive_output() -> None:
    screens = [
        _screen(0, "DP-1", primary=True),
        _screen(10, "DP-2", active=False),
    ]
    with pytest.raises(MultiOutputError, match="both discovered outputs must be active"):
        _ = multi_output._read_rectangles_from(screens, "DP-1", "DP-2")  # pyright: ignore[reportPrivateUsage]


def test_read_rectangles_refuses_a_primary_identity_change() -> None:
    screens = [_screen(0, "DP-1"), _screen(10, "DP-2", primary=True)]
    with pytest.raises(MultiOutputError, match="primary identity changed"):
        _ = multi_output._read_rectangles_from(screens, "DP-1", "DP-2")  # pyright: ignore[reportPrivateUsage]


# ---- placement target math -------------------------------------------------


def test_placement_target_full_touching() -> None:
    assert multi_output._placement_target("full-touching", (0, 0, 1920, 1080), 1080, 1920) == (  # pyright: ignore[reportPrivateUsage]
        1920,
        0,
    )


def test_placement_target_partial_touching() -> None:
    assert multi_output._placement_target("partial-touching", (0, 0, 1920, 1080), 1080, 1920) == (  # pyright: ignore[reportPrivateUsage]
        1920,
        540,
    )


def test_placement_target_disconnected() -> None:
    assert multi_output._placement_target("disconnected", (0, 0, 1920, 1080), 1080, 1920) == (  # pyright: ignore[reportPrivateUsage]
        2400,
        270,
    )


def test_placement_target_partial_impossible_refuses() -> None:
    with pytest.raises(MultiOutputError, match="cannot form a partial contact"):
        _ = multi_output._placement_target("partial-touching", (0, 0, 10, 1), 10, 1)  # pyright: ignore[reportPrivateUsage]


# ---- pin resolution --------------------------------------------------------


def test_assert_pin_resolved_accepts_the_secondary() -> None:
    screens = [
        _screen(0, "DP-1", primary=True),
        _screen(10, "DP-2"),
    ]
    multi_output._assert_pin_resolved(screens, 10, "DP-2")  # pyright: ignore[reportPrivateUsage]  # no raise


@pytest.mark.parametrize(
    ("screens", "message"),
    [
        ([], "not in the ScreenPool mapping"),
        (
            [
                Screen.model_validate(
                    {
                        "id": 10,
                        "name": "DP-2",
                        "geometry": [0, 0, 1, 1],
                        "isActive": False,
                        "isPrimary": False,
                    }
                )
            ],
            "is not active",
        ),
        (
            [
                Screen.model_validate(
                    {
                        "id": 10,
                        "name": "DP-2",
                        "geometry": [0, 0, 1, 1],
                        "isActive": True,
                        "isPrimary": True,
                    }
                )
            ],
            "resolved to the PRIMARY",
        ),
        (
            [
                Screen.model_validate(
                    {
                        "id": 10,
                        "name": "OTHER",
                        "geometry": [0, 0, 1, 1],
                        "isActive": True,
                        "isPrimary": False,
                    }
                )
            ],
            "expected the secondary DP-2",
        ),
    ],
)
def test_assert_pin_resolved_refusals(screens: list[Screen], message: str) -> None:
    with pytest.raises(MultiOutputError, match=message):
        multi_output._assert_pin_resolved(screens, 10, "DP-2")  # pyright: ignore[reportPrivateUsage]
