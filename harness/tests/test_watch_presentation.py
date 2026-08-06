# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The presentation watcher's pure cores: argument validation with
byte-identical refusal messages, the visible-view filter, and the
transition/first-seen bookkeeping. The live wiring (D-Bus queries, sleep,
artifact writes) is driven in the nested vehicle; the composition oracle it
reuses is proven in test_recipe.py.
"""

import pytest

from latte_harness import watch_presentation as wp
from latte_harness.recipe import DockSystemData, DockView


def _snapshot(*views: tuple[int, bool]) -> DockSystemData:
    """A dockSystemData snapshot of (persistentDockId, isHidden) views."""
    return DockSystemData(
        views=[
            DockView.model_validate(
                {
                    "persistentDockId": dock_id,
                    "isHidden": hidden,
                    "orientation": "horizontal",
                    "effectsRect": [0, 0, 1600, 100],
                    "canvasGeometry": [0, 0, 1600, 100],
                }
            )
            for dock_id, hidden in views
        ]
    )


# ---- parse_args: defaults and byte-identical refusals ----------------------


def test_defaults_when_no_args() -> None:
    args = wp.parse_args([])
    assert (args.duration, args.duration_text, args.interval, args.target) == (30, "30", 0.05, "")


def test_empty_positionals_fall_back_like_the_bash_defaults() -> None:
    # ${1:-30} / ${2:-0.05} / ${3:-}: an empty positional uses the default.
    args = wp.parse_args(["", "", ""])
    assert (args.duration, args.interval, args.target) == (30, 0.05, "")


def test_explicit_values_are_parsed() -> None:
    args = wp.parse_args(["5", "0.2", "7"])
    assert (args.duration, args.duration_text, args.interval, args.target) == (5, "5", 0.2, "7")


@pytest.mark.parametrize(
    ("argv", "message"),
    [
        (["abc"], "duration must be a positive integer, got 'abc'"),
        (["-5"], "duration must be a positive integer, got '-5'"),
        (["1.5"], "duration must be a positive integer, got '1.5'"),
        (["0"], "duration must be greater than zero"),
        (["5", "abc"], "sample interval must be a positive number, got 'abc'"),
        (["5", "-1"], "sample interval must be a positive number, got '-1'"),
        (["5", ".5"], "sample interval must be a positive number, got '.5'"),
        (["5", "5."], "sample interval must be a positive number, got '5.'"),
        (["5", "0"], "sample interval must be a positive number, got '0'"),
        (["5", "0.0"], "sample interval must be a positive number, got '0.0'"),
        (["5", "0.05", "abc"], "dock id must be an unsigned integer, got 'abc'"),
        (["5", "0.05", "-1"], "dock id must be an unsigned integer, got '-1'"),
        (["5", "0.05", "5.0"], "dock id must be an unsigned integer, got '5.0'"),
        (["5", "0.05", "0"], "dock id must be greater than zero"),
    ],
)
def test_bad_arguments_refuse_with_the_bash_message(argv: list[str], message: str) -> None:
    with pytest.raises(wp.ArgError) as excinfo:
        wp.parse_args(argv)
    assert str(excinfo.value) == message


def test_valid_edge_intervals_are_accepted() -> None:
    assert wp.parse_args(["5", "0.05"]).interval == 0.05
    assert wp.parse_args(["5", "10"]).interval == 10.0


# ---- select_visible_views: hidden filter and target selection --------------


def test_visible_views_excludes_hidden() -> None:
    snapshot = _snapshot((11, False), (22, True), (33, False))
    assert wp.select_visible_views(snapshot, "") == [11, 33]


def test_target_selects_one_visible_view_by_string_id() -> None:
    snapshot = _snapshot((11, False), (33, False))
    assert wp.select_visible_views(snapshot, "33") == [33]


def test_target_that_is_hidden_yields_nothing() -> None:
    snapshot = _snapshot((11, False), (22, True))
    assert wp.select_visible_views(snapshot, "22") == []


def test_view_order_is_preserved() -> None:
    snapshot = _snapshot((33, False), (11, False), (22, False))
    assert wp.select_visible_views(snapshot, "") == [33, 11, 22]


# ---- TransitionTracker: transitions counted only on a change ---------------


def test_first_observation_is_new_but_no_transition() -> None:
    tracker = wp.TransitionTracker()
    assert tracker.observe(16, "state-a") is True
    assert tracker.transitions == 0


def test_repeated_state_is_not_new() -> None:
    tracker = wp.TransitionTracker()
    tracker.observe(16, "state-a")
    assert tracker.observe(16, "state-a") is False
    assert tracker.transitions == 0


def test_changed_state_counts_a_transition() -> None:
    tracker = wp.TransitionTracker()
    tracker.observe(16, "state-a")
    assert tracker.observe(16, "state-b") is True
    assert tracker.transitions == 1


def test_transitions_are_counted_per_view_independently() -> None:
    tracker = wp.TransitionTracker()
    tracker.observe(16, "a")  # first-seen 16
    tracker.observe(17, "a")  # first-seen 17
    tracker.observe(16, "b")  # transition on 16
    tracker.observe(17, "c")  # transition on 17
    assert tracker.transitions == 2
