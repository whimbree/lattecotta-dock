# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
"""task_reorder: the task-reorder driver's pure logic with no live dock - the
appId/launcherUrl order joins, the outside-the-bar approach point per edge, the
integer drag midpoint, and the off-dock release point per edge. The live drag
(fakepointer choreography) is exercised by the nested-vehicle parity flow against
092; here every seam is a monkeypatched readback or pure math, testable without a
compositor.
"""

from __future__ import annotations

import json
from collections.abc import Callable

import pytest

from latte_harness import recipe, task_reorder


def _tasks(entries: list[dict[str, object]]) -> str:
    return json.dumps(entries)


def _returns_payload(payload: str) -> Callable[..., str]:
    def fake(*_args: str) -> str:
        return payload

    return fake


# ---- the order readbacks (appId and launcherUrl joins) ---------------------


def test_taskdrag_order_joins_app_ids_in_model_order(monkeypatch: pytest.MonkeyPatch) -> None:
    def three(_view: int) -> list[recipe.Task]:
        return [recipe.Task.model_validate({"appId": a}) for a in ("firefox", "kate", "dolphin")]

    monkeypatch.setattr(recipe, "view_tasks", three)
    assert task_reorder.taskdrag_order(16) == "firefox kate dolphin"


def test_launcher_order_keeps_empty_window_task_slots(monkeypatch: pytest.MonkeyPatch) -> None:
    # A window task has an empty launcherUrl, kept in place so the list length still
    # matches the bar (the bash contract).
    payload = _tasks(
        [
            {"appId": "firefox", "launcherUrl": "applications:firefox.desktop"},
            {"appId": "win", "launcherUrl": ""},
            {"appId": "kate", "launcherUrl": "applications:kate.desktop"},
        ]
    )
    monkeypatch.setattr(recipe, "json_payload", _returns_payload(payload))
    assert (
        task_reorder.taskdrag_launcher_order(16)
        == "applications:firefox.desktop  applications:kate.desktop"
    )


def test_launcher_task_rejects_a_missing_launcher_url() -> None:
    from pydantic import ValidationError

    # launcherUrl is always present in viewTasksData (empty for window tasks); a
    # reply without the key is malformed and must fail at the boundary, loudly.
    with pytest.raises(ValidationError):
        _ = task_reorder._LAUNCHER_TASKS.validate_python([{"appId": "x"}])  # pyright: ignore[reportPrivateUsage]


# ---- the outside-the-bar approach point (edge -> staging) ------------------


@pytest.mark.parametrize(
    ("edge", "expected"),
    [
        ("bottom", (700, 500)),
        ("top", (700, 500)),
        ("left", (800, 300)),
        ("right", (800, 300)),
        ("floating", (800, 500)),
    ],
)
def test_approach_point_stages_off_the_bar_axis(edge: str, expected: tuple[int, int]) -> None:
    # cx=700 cy=300 on a 1600x1000 screen: horizontal stages at mid-height on x,
    # vertical at mid-width on y, an unknown edge at screen centre.
    assert task_reorder._approach_point(edge, 700, 300, 1600, 1000) == expected  # pyright: ignore[reportPrivateUsage]


# ---- the integer drag midpoint ---------------------------------------------


@pytest.mark.parametrize(("a", "b", "mid"), [(100, 300, 200), (100, 301, 200), (0, 1, 0)])
def test_midpoint_is_integer_division(a: int, b: int, mid: int) -> None:
    assert task_reorder._midpoint(a, b) == mid  # pyright: ignore[reportPrivateUsage]


# ---- the off-dock release point (edge -> clear of the band) ----------------


@pytest.mark.parametrize(
    ("edge", "expected"),
    [
        ("bottom", (700, 333)),
        ("top", (700, 666)),
        ("left", (1066, 300)),
        ("right", (533, 300)),
        ("floating", (800, 500)),
    ],
)
def test_out_of_applet_point_clears_the_band(edge: str, expected: tuple[int, int]) -> None:
    assert task_reorder._out_of_applet_point(edge, 700, 300, 1600, 1000) == expected  # pyright: ignore[reportPrivateUsage]
