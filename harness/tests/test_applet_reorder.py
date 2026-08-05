# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
"""applet_reorder: the applet-reorder driver's pure logic with no live dock - the
whole coordinate model (_compute_points on both axes and its round-half-to-even
rounding), the viewAppletsOrder item parse, the z / flag readbacks at the pydantic
boundary, the per-mode glide choreography, the attempt classification
(commit/refused/error), and the appletreorder verb's registration and refusal
translation. The live drag (fakepointer choreography, the rearrange flip) is
exercised by the nested-vehicle parity flow against 100; here every seam is pure
math or a monkeypatched readback, testable without a compositor.
"""

from __future__ import annotations

import json
from collections.abc import Callable, Iterator

import pytest

from latte_harness import applet_reorder, matrix, recipe
from latte_harness.applet_reorder import AppletReorderError, ReorderPoints
from latte_harness.matrix import MatrixDriveError, MatrixProbeError

# ---- typed stand-ins for monkeypatch.setattr (a bare lambda is untyped under strict) ----


def _returns_status(result: tuple[int, str]) -> Callable[..., tuple[int, str]]:
    def fake(*_args: object) -> tuple[int, str]:
        return result

    return fake


def _returns_str(value: str) -> Callable[..., str]:
    def fake(*_args: object) -> str:
        return value

    return fake


def _returns_points(points: ReorderPoints) -> Callable[..., ReorderPoints]:
    def fake(*_args: object) -> ReorderPoints:
        return points

    return fake


def _returns_int(value: int) -> Callable[..., int]:
    def fake(*_args: object) -> int:
        return value

    return fake


def _noop(*_args: object) -> None:
    return None


@pytest.fixture(autouse=True)
def _no_sleep(  # pyright: ignore[reportUnusedFunction]  # pytest collects the autouse fixture
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    """The choreography sleeps between pointer steps; drop them so the pure-logic
    tests never wait on real time."""
    monkeypatch.setattr(applet_reorder.time, "sleep", _noop)


# ---- the viewAppletsOrder item parse ---------------------------------------


@pytest.mark.parametrize(
    ("reply", "order"),
    [
        ('as 3 "10" "11" "12"', '"10" "11" "12"'),
        ('as 1 "7"', '"7"'),
        ("as 0", ""),
    ],
)
def test_parse_applets_order_drops_the_signature_and_count(reply: str, order: str) -> None:
    assert applet_reorder._parse_applets_order(reply) == order  # pyright: ignore[reportPrivateUsage]


def test_applet_reorder_order_parses_a_live_reply(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(applet_reorder, "_call_status", _returns_status((0, 'as 2 "10" "11"\n')))
    assert applet_reorder.applet_reorder_order(16) == '"10" "11"'


def test_applet_reorder_order_refuses_a_failed_call(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(applet_reorder, "_call_status", _returns_status((1, "")))
    with pytest.raises(AppletReorderError, match="viewAppletsOrder call FAILED"):
        _ = applet_reorder.applet_reorder_order(16)


def test_applet_reorder_order_refuses_a_non_array_reply(monkeypatch: pytest.MonkeyPatch) -> None:
    # A D-Bus error reply is not an 'as' array; a plausible-but-empty order that
    # read "unchanged" on both sides of an abort would be a false PASS.
    monkeypatch.setattr(applet_reorder, "_call_status", _returns_status((0, "s something\n")))
    with pytest.raises(AppletReorderError, match="not an 'as' array"):
        _ = applet_reorder.applet_reorder_order(16)


# ---- the coordinate model (the pure heart of the driver) -------------------


def test_compute_points_horizontal_matches_the_bash_model() -> None:
    pts = applet_reorder._compute_points(  # pyright: ignore[reportPrivateUsage]
        "bottom", (100, 900, 800, 100), (0, 0, 800, 100), (0, 0, 40, 40), 11, (100, 0, 40, 40), 12
    )
    assert pts == ReorderPoints(
        axis="h",
        s=(120, 920),
        ap=(120, 880),
        m=(170, 920),
        cross=(234, 920),
        ctr=(220, 920),
        over=(256, 920),
        o=(120, 920),
        ret=(108, 920),
        n=(130, 920),
        from_id=11,
        to_id=12,
    )


def test_compute_points_vertical_drags_on_the_y_axis() -> None:
    pts = applet_reorder._compute_points(  # pyright: ignore[reportPrivateUsage]
        "left", (0, 100, 100, 800), (0, 0, 100, 800), (0, 0, 40, 40), 21, (0, 100, 40, 40), 22
    )
    assert pts == ReorderPoints(
        axis="v",
        s=(20, 120),
        ap=(-20, 120),
        m=(20, 170),
        cross=(20, 234),
        ctr=(20, 220),
        over=(20, 256),
        o=(20, 120),
        ret=(20, 108),
        n=(20, 130),
        from_id=21,
        to_id=22,
    )


def test_compute_points_reverse_span_flips_the_sign() -> None:
    # dst LEFT of src: span negative, so cross/over overshoot toward smaller x.
    pts = applet_reorder._compute_points(  # pyright: ignore[reportPrivateUsage]
        "bottom", (0, 900, 800, 100), (0, 0, 800, 100), (100, 0, 40, 40), 1, (0, 0, 40, 40), 2
    )
    # from centre x=120, to centre x=20: cross = 20 - 0.35*40 = 6, over = 20 - 36 = -16.
    assert pts.s == (120, 920)
    assert pts.cross == (6, 920)
    assert pts.over == (-16, 920)
    # nudge = -1 * min(|span=-100|*0.30=30, 40*0.25=10) = -10, so n = 120-10.
    assert pts.n == (110, 920)


def test_compute_points_rounds_a_half_pixel_centre_to_even() -> None:
    # An odd applet width puts the centre on x.5; round() is round-half-to-even, the
    # SAME rounding the bash-invoked python3 int(round(...)) applied, so the port
    # stays byte-identical (round(120.5) == 120).
    pts = applet_reorder._compute_points(  # pyright: ignore[reportPrivateUsage]
        "bottom", (100, 900, 800, 100), (0, 0, 800, 100), (0, 0, 41, 40), 1, (100, 0, 41, 40), 2
    )
    assert pts.s == (120, 920)


# ---- the z and flag readbacks (pydantic boundary) --------------------------


def _applets_payload(entries: list[dict[str, object]]) -> str:
    return json.dumps(entries)


def test_applet_reorder_z_reads_the_stacking_z(monkeypatch: pytest.MonkeyPatch) -> None:
    payload = _applets_payload([{"id": 10, "z": 0.0}, {"id": 11, "z": 900.0}])
    monkeypatch.setattr(recipe, "json_payload", _returns_str(payload))
    assert applet_reorder.applet_reorder_z(16, 11) == 900.0


def test_applet_reorder_z_refuses_an_absent_applet(monkeypatch: pytest.MonkeyPatch) -> None:
    payload = _applets_payload([{"id": 10, "z": 0.0}])
    monkeypatch.setattr(recipe, "json_payload", _returns_str(payload))
    with pytest.raises(AppletReorderError, match="applet 99 not present"):
        _ = applet_reorder.applet_reorder_z(16, 99)


def test_reorder_flags_read_edit_and_configure(monkeypatch: pytest.MonkeyPatch) -> None:
    payload = json.dumps([{"containmentId": 16, "editMode": True, "inConfigureAppletsMode": False}])
    monkeypatch.setattr(recipe, "json_payload", _returns_str(payload))
    assert applet_reorder.applet_reorder_edit_mode(16) is True
    assert applet_reorder.applet_reorder_configuring(16) is False


def test_reorder_flags_refuse_a_gone_view(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(recipe, "json_payload", _returns_str("[]"))
    with pytest.raises(AppletReorderError, match="view 16 gone"):
        _ = applet_reorder.applet_reorder_edit_mode(16)


# ---- the per-mode glide choreography ---------------------------------------

_POINTS = ReorderPoints(
    axis="h",
    s=(120, 920),
    ap=(120, 880),
    m=(170, 920),
    cross=(234, 920),
    ctr=(220, 920),
    over=(256, 920),
    o=(120, 920),
    ret=(108, 920),
    n=(130, 920),
    from_id=11,
    to_id=12,
)


def _record_fakepointer(monkeypatch: pytest.MonkeyPatch) -> list[tuple[str, ...]]:
    calls: list[tuple[str, ...]] = []

    def record(*args: str) -> None:
        calls.append(args)

    monkeypatch.setattr(applet_reorder, "_applet_reorder_points", _returns_points(_POINTS))
    monkeypatch.setattr(applet_reorder, "_fakepointer", record)
    return calls


def test_glide_arms_currentapplet_before_the_drag(monkeypatch: pytest.MonkeyPatch) -> None:
    calls = _record_fakepointer(monkeypatch)
    applet_reorder.applet_reorder_glide(16, "commit", 0, 1)
    # move to the off-item point, glide onto the source centre, THEN the drag.
    assert calls[0] == ("move", "120", "880")
    assert calls[1] == ("glide", "120", "880", "120", "920")


@pytest.mark.parametrize(
    ("mode", "drag"),
    [
        ("commit", ("drag", "120", "920", "170", "920", "234", "920")),
        ("occupied", ("drag", "120", "920", "170", "920", "220", "920")),
        ("overflow", ("drag", "120", "920", "170", "920", "256", "920")),
        ("origin", ("drag", "120", "920", "170", "920", "234", "920", "170", "920", "108", "920")),
        ("noop", ("drag", "120", "920", "130", "920", "120", "920")),
        ("jitter", ("drag", "120", "920", "234", "920", "120", "920", "234", "920")),
        ("escape", ("dragkey", "Escape", "120", "920", "170", "920", "234", "920")),
    ],
)
def test_glide_mode_dispatch(
    monkeypatch: pytest.MonkeyPatch, mode: str, drag: tuple[str, ...]
) -> None:
    calls = _record_fakepointer(monkeypatch)
    applet_reorder.applet_reorder_glide(16, mode, 0, 1)
    assert calls[-1] == drag


def test_glide_refuses_an_unknown_mode(monkeypatch: pytest.MonkeyPatch) -> None:
    _ = _record_fakepointer(monkeypatch)
    with pytest.raises(AppletReorderError, match="unknown mode 'wat'"):
        applet_reorder.applet_reorder_glide(16, "wat", 0, 1)


def test_glide_to_threads_the_optional_via_waypoint(monkeypatch: pytest.MonkeyPatch) -> None:
    calls = _record_fakepointer(monkeypatch)
    applet_reorder.applet_reorder_glide_to(16, 0, 400, 500, 300, 350)
    assert calls[-1] == ("drag", "120", "920", "300", "350", "400", "500")


def test_glide_to_without_a_via_goes_straight(monkeypatch: pytest.MonkeyPatch) -> None:
    calls = _record_fakepointer(monkeypatch)
    applet_reorder.applet_reorder_glide_to(16, 0, 400, 500)
    assert calls[-1] == ("drag", "120", "920", "400", "500")


# ---- the attempt classification (0 commit / 3 refused / 1 error) -----------


def _stub_attempt(
    monkeypatch: pytest.MonkeyPatch, *, before: str, after: str, enter_ok: bool = True
) -> None:
    orders: Iterator[str] = iter([before, after])

    def order(_view: int) -> str:
        return next(orders)

    def enter(_view: int) -> None:
        if not enter_ok:
            raise AppletReorderError("enter failed")

    monkeypatch.setattr(applet_reorder, "applet_reorder_order", order)
    monkeypatch.setattr(applet_reorder, "applet_reorder_enter", enter)
    monkeypatch.setattr(applet_reorder, "applet_reorder_glide", _noop)
    monkeypatch.setattr(applet_reorder, "applet_reorder_exit", _noop)


def test_attempt_reports_a_changed_order_as_committed(monkeypatch: pytest.MonkeyPatch) -> None:
    _stub_attempt(monkeypatch, before='"10" "11"', after='"11" "10"')
    assert applet_reorder.applet_reorder_attempt(16, "commit", 0, 1) == 0


def test_attempt_reports_an_unchanged_order_as_refused(monkeypatch: pytest.MonkeyPatch) -> None:
    _stub_attempt(monkeypatch, before='"10" "11"', after='"10" "11"')
    assert applet_reorder.applet_reorder_attempt(16, "noop", 0, 1) == 3


def test_attempt_reports_a_step_failure_as_a_driver_error(monkeypatch: pytest.MonkeyPatch) -> None:
    _stub_attempt(monkeypatch, before='"10" "11"', after='"10" "11"', enter_ok=False)
    assert applet_reorder.applet_reorder_attempt(16, "commit", 0, 1) == 1


# ---- the matrix verb hookup ------------------------------------------------


def test_appletreorder_verb_is_registered_at_import() -> None:
    assert "appletreorder" in matrix._VERBS  # pyright: ignore[reportPrivateUsage]


def test_verb_drive_commit_requires_a_reorder(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(applet_reorder, "applet_reorder_attempt", _returns_int(0))
    applet_reorder.verb_appletreorder_drive(16, "commit")  # no raise


def test_verb_drive_commit_that_did_not_reorder_is_a_drive_error(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    monkeypatch.setattr(applet_reorder, "applet_reorder_attempt", _returns_int(3))
    with pytest.raises(MatrixDriveError, match="did not change the order"):
        applet_reorder.verb_appletreorder_drive(16, "commit")


@pytest.mark.parametrize("rc", [0, 3])
def test_verb_drive_abort_accepts_committed_or_refused(
    monkeypatch: pytest.MonkeyPatch, rc: int
) -> None:
    monkeypatch.setattr(applet_reorder, "applet_reorder_attempt", _returns_int(rc))
    applet_reorder.verb_appletreorder_drive(16, "abort")  # no raise


def test_verb_drive_abort_driver_error_is_a_drive_error(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setattr(applet_reorder, "applet_reorder_attempt", _returns_int(1))
    with pytest.raises(MatrixDriveError, match="abort was a driver error"):
        applet_reorder.verb_appletreorder_drive(16, "abort")


def test_verb_drive_honours_the_from_to_env(monkeypatch: pytest.MonkeyPatch) -> None:
    seen: list[tuple[int, int]] = []
    monkeypatch.setenv("APPLET_REORDER_FROM", "2")
    monkeypatch.setenv("APPLET_REORDER_TO", "5")

    def attempt(_view: int, _mode: str, frm: int, to: int) -> int:
        seen.append((frm, to))
        return 0

    monkeypatch.setattr(applet_reorder, "applet_reorder_attempt", attempt)
    applet_reorder.verb_appletreorder_drive(16, "commit")
    assert seen == [(2, 5)]


def test_verb_probe_translates_a_failed_order_to_unassertable(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    def failing(_view: int) -> str:
        raise AppletReorderError("viewAppletsOrder call FAILED")

    monkeypatch.setattr(applet_reorder, "applet_reorder_order", failing)
    with pytest.raises(MatrixProbeError):
        _ = applet_reorder.verb_appletreorder_probe(16)
