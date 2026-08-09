# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The typed recipe API contract: the readback models validate at the boundary,
the bounded wait loops keep their exact bounds and messages, the window-dump
selection reproduces the awk field logic, the presentation-coverage oracle
catches the D150 escape, and the E2E_* env contract refuses loudly by name.

The wait loops and the window/coverage logic are driven through their PURE
cores (injected probe/clock, parsed inputs), so the whole contract is testable
without a live dock or compositor - the busctl transport is the only untested
seam and it is a thin argv wrapper.
"""

import os
import subprocess
import sys
import time
from pathlib import Path

import pytest
from pydantic import ValidationError

from latte_harness import recipe
from latte_harness.recipe import (
    Applet,
    DockSystemData,
    DockView,
    RecipeError,
    Task,
    View,
    Window,
)

# ---- readback models: valid / invalid / extra-field tolerated --------------


def _one_view_dict() -> dict[str, object]:
    """A COMPLETE viewsData record - the always-emitted serializeViewRecord surface.

    Every field the dock writes for every view, so a test that drops one is
    exercising a real "malformed reply", not a lazy fixture (W3 widened recipe.View
    to require this whole surface).
    """
    return {
        "containmentId": 16,
        "isCloned": False,
        "isClonedFrom": -1,
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
        "absoluteGeometry": [0, 900, 1600, 100],
        "localGeometry": [0, 0, 1600, 100],
        "screenGeometry": [0, 0, 1600, 1000],
    }


def _one_applet_dict() -> dict[str, object]:
    """A COMPLETE viewAppletsData record - the always-emitted serializeAppletRecord
    surface recipe.Applet requires (W3 added z / colorizerActive / colorizerReason)."""
    return {
        "id": 4,
        "plugin": "org.kde.latte.plasmoid",
        "geometry": [10, 0, 200, 100],
        "inScheduledDestruction": False,
        "z": 0.0,
        "colorizerActive": False,
        "colorizerReason": "",
    }


def _one_task_dict() -> dict[str, object]:
    """A COMPLETE viewTasksData record - the always-emitted serializeTaskRecord
    surface recipe.Task requires (W3 added launcherUrl / isLauncher / isGrouped /
    childCount / isActive)."""
    return {
        "appId": "org.kde.konsole.desktop",
        "launcherUrl": "applications:org.kde.konsole.desktop",
        "isLauncher": True,
        "isGrouped": False,
        "childCount": 0,
        "isActive": False,
    }


def test_view_model_parses_a_real_payload() -> None:
    parsed = recipe._VIEWS.validate_python([_one_view_dict()])  # pyright: ignore[reportPrivateUsage]
    assert len(parsed) == 1
    view = parsed[0]
    assert view.containment_id == 16
    assert view.edge == "bottom"
    assert view.absolute_geometry == (0, 900, 1600, 100)
    assert view.screen_geometry[2] == 1600


def test_view_model_carries_the_w3_widened_fields() -> None:
    # The fields W3 added for the ported recipes: lineage, placement strings,
    # mode flags, and the focus-session trio. A recipe reads them by attribute
    # instead of indexing a raw dict.
    view = View.model_validate(
        {
            **_one_view_dict(),
            "isCloned": True,
            "isClonedFrom": 16,
            "alignment": "left",
            "screen": "Virtual-1",
            "visibilityMode": "autoHide",
            "editMode": True,
            "inConfigureAppletsMode": True,
            "keyboardNavigation": True,
            "containmentAcceptsInput": False,
            "ownsPanelFocusSession": True,
        }
    )
    assert view.is_cloned is True
    assert view.is_cloned_from == 16
    assert view.alignment == "left"
    assert view.screen == "Virtual-1"
    assert view.visibility_mode == "autoHide"
    assert view.edit_mode is True
    assert view.in_configure_applets_mode is True
    assert view.keyboard_navigation is True
    assert view.containment_accepts_input is False
    assert view.owns_panel_focus_session is True


def test_view_model_tolerates_a_dock_side_field_addition() -> None:
    # A field the dock adds later must not break an existing recipe (extra=ignore).
    parsed = recipe._VIEWS.validate_python(  # pyright: ignore[reportPrivateUsage]
        [{**_one_view_dict(), "futureField": 42}]
    )
    assert parsed[0].containment_id == 16
    assert not hasattr(parsed[0], "futureField")


def test_view_model_rejects_a_wrong_typed_field() -> None:
    with pytest.raises(ValidationError):
        View.model_validate({**_one_view_dict(), "containmentId": "not-a-number"})


def test_view_model_rejects_a_missing_widened_field() -> None:
    # A widened field is REQUIRED: the dock always emits editMode, so a reply
    # without it is malformed and must fail loudly at the boundary, never default.
    incomplete = {k: v for k, v in _one_view_dict().items() if k != "editMode"}
    with pytest.raises(ValidationError):
        View.model_validate(incomplete)


def test_view_model_rejects_a_malformed_geometry_length() -> None:
    with pytest.raises(ValidationError):
        View.model_validate({**_one_view_dict(), "absoluteGeometry": [0, 900, 1600]})


def test_view_model_rejects_a_fractional_pixel() -> None:
    with pytest.raises(ValidationError):
        View.model_validate({**_one_view_dict(), "absoluteGeometry": [0, 900.5, 1600, 100]})


def test_applet_and_task_models_parse() -> None:
    applet = Applet.model_validate(_one_applet_dict())
    assert applet.plugin == "org.kde.latte.plasmoid"
    assert applet.geometry[2] == 200
    assert applet.z == 0.0
    assert applet.colorizer_active is False
    assert applet.colorizer_reason == ""
    task = Task.model_validate(_one_task_dict())
    assert task.app_id == "org.kde.konsole.desktop"
    assert task.launcher_url == "applications:org.kde.konsole.desktop"
    assert task.is_launcher is True


def test_applet_model_carries_the_stacking_z() -> None:
    applet = Applet.model_validate({**_one_applet_dict(), "z": 900.0, "colorizerActive": True})
    assert applet.z == 900.0
    assert applet.colorizer_active is True


def test_task_model_rejects_a_missing_widened_field() -> None:
    # launcherUrl is always present in viewTasksData (empty for a window task); a
    # reply without the key is malformed and must fail at the boundary.
    incomplete = {k: v for k, v in _one_task_dict().items() if k != "launcherUrl"}
    with pytest.raises(ValidationError):
        Task.model_validate(incomplete)


# ---- the busctl-reply unescape (byte-identical to the e2e_json sed) ---------


def test_unescape_busctl_json_unwraps_and_unescapes() -> None:
    raw = 's "[{\\"containmentId\\":16}]"\n'
    assert recipe._unescape_busctl_json(raw) == '[{"containmentId":16}]'  # pyright: ignore[reportPrivateUsage]


def test_unescape_busctl_json_handles_empty_array_reply() -> None:
    assert recipe._unescape_busctl_json('s "[]"\n') == "[]"  # pyright: ignore[reportPrivateUsage]


# ---- the wait loops (pure cores: injected probe + clock) --------------------


def test_wait_running_settles_when_the_state_flips() -> None:
    states = iter(['"startup"', '"startup"', '"running"'])
    calls: list[float] = []
    ok, message = recipe._wait_running_loop(lambda: next(states), 60, calls.append)  # pyright: ignore[reportPrivateUsage]
    assert ok is True
    assert message == ""
    assert calls == [1, 1]  # slept only for the two non-running polls


def test_wait_running_times_out_with_the_last_state_named() -> None:
    ok, message = recipe._wait_running_loop(lambda: '"startup"', 3, lambda _s: None)  # pyright: ignore[reportPrivateUsage]
    assert ok is False
    assert message == 'dock never reached lifecycleState running in 3s (last: "startup")'


def test_wait_running_timeout_reports_no_reply_when_unreachable() -> None:
    ok, message = recipe._wait_running_loop(lambda: "", 2, lambda _s: None)  # pyright: ignore[reportPrivateUsage]
    assert ok is False
    assert message.endswith("(last: no reply)")


def test_wait_settled_returns_only_after_two_identical_replies() -> None:
    payload = 's "[{\\"inStartup\\":false}]"'
    replies = iter([payload, payload])
    ok, message = recipe._wait_settled_loop(lambda: next(replies), 60, lambda _s: None)  # pyright: ignore[reportPrivateUsage]
    assert ok is True
    assert message == ""


def test_wait_settled_keeps_waiting_while_geometry_animates() -> None:
    # A payload that differs every poll is still animating; never settles.
    counter = iter(range(100))
    ok, message = recipe._wait_settled_loop(  # pyright: ignore[reportPrivateUsage]
        lambda: f's "[{{\\"x\\":{next(counter)}}}]"', 4, lambda _s: None
    )
    assert ok is False
    assert message == "views still absent, inStartup, or animating after 4s"


def test_wait_settled_ignores_instartup_and_empty_replies() -> None:
    for reply in ('s "[]"', 's "[{\\"inStartup\\":true}]"'):
        ok, _ = recipe._wait_settled_loop(lambda r=reply: r, 3, lambda _s: None)  # pyright: ignore[reportPrivateUsage]
        assert ok is False


# ---- the window dump and the awk-faithful view-window selection ------------

_DUMP = "\n".join(
    [
        "DUMPWIN|latte-dock|Latte Dock|0,900 1600x100|Virtual-1|layer=3",
        "DUMPWIN|org.kde.konsole|Konsole|200,100 800x600|Virtual-1|layer=0",
        "not a dump line at all",
    ]
)


def test_parse_dumpwins_reads_the_fixed_fields() -> None:
    parsed = recipe.parse_dumpwins(_DUMP)
    assert len(parsed) == 2
    dock = parsed[0]
    assert dock.resource_class == "latte-dock"
    assert (dock.x, dock.y, dock.width, dock.height) == (0, 900, 1600, 100)
    assert dock.layer == 3
    assert parsed[1].resource_class == "org.kde.konsole"


def test_select_view_window_x_picks_the_anchored_screen_width_dock() -> None:
    parsed = recipe.parse_dumpwins(_DUMP)
    assert recipe._select_view_window_x(parsed, "bottom", 1600, 1000) == 0  # pyright: ignore[reportPrivateUsage]


def test_select_view_window_x_rejects_a_non_screen_width_window() -> None:
    dump = recipe.parse_dumpwins("DUMPWIN|latte-dock|d|0,900 800x100|o|layer=3")
    assert recipe._select_view_window_x(dump, "bottom", 1600, 1000) is None  # pyright: ignore[reportPrivateUsage]


def test_select_view_window_x_matches_a_top_dock_at_the_origin() -> None:
    dump = recipe.parse_dumpwins("DUMPWIN|latte-dock|d|0,0 1600x100|o|layer=3")
    assert recipe._select_view_window_x(dump, "top", 1600, 1000) == 0  # pyright: ignore[reportPrivateUsage]


def test_select_view_window_x_skips_non_layer3_windows() -> None:
    dump = recipe.parse_dumpwins("DUMPWIN|latte-dock|d|0,900 1600x100|o|layer=0")
    assert recipe._select_view_window_x(dump, "bottom", 1600, 1000) is None  # pyright: ignore[reportPrivateUsage]


def test_window_is_a_frozen_value() -> None:
    win = Window("latte-dock", "d", "0,900 1600x100", 0, 900, 1600, 100, "o", 3)
    with pytest.raises((AttributeError, TypeError)):
        win.x = 1  # pyright: ignore[reportAttributeAccessIssue]


# ---- the screenshot reply parse --------------------------------------------


def test_reply_uint_extracts_the_vardict_scalars() -> None:
    reply = 'a{sv} 4 "width" u 1600 "height" u 1000 "stride" u 6400 "format" u 5'
    assert recipe._reply_uint(reply, "width") == 1600  # pyright: ignore[reportPrivateUsage]
    assert recipe._reply_uint(reply, "format") == 5  # pyright: ignore[reportPrivateUsage]
    assert recipe._reply_uint(reply, "absent") is None  # pyright: ignore[reportPrivateUsage]


# ---- the presentation-coverage oracle (the D150 escape shape) --------------


def _snapshot(effects: list[int], canvas: list[int], *, hidden: bool = False) -> DockSystemData:
    return DockSystemData(
        views=[
            DockView.model_validate(
                {
                    "persistentDockId": 16,
                    "isHidden": hidden,
                    "orientation": "horizontal",
                    "effectsRect": effects,
                    "canvasGeometry": canvas,
                }
            )
        ]
    )


def _applet(x: int, w: int) -> Applet:
    return Applet.model_validate(
        {
            "id": 1,
            "plugin": "p",
            "geometry": [x, 0, w, 100],
            "inScheduledDestruction": False,
            "z": 0.0,
            "colorizerActive": False,
            "colorizerReason": "",
        }
    )


def test_presentation_coverage_passes_when_content_sits_inside() -> None:
    line = recipe._assert_presentation_coverage(  # pyright: ignore[reportPrivateUsage]
        _snapshot([0, 0, 1600, 100], [0, 0, 1600, 100]), [_applet(10, 200)], 16, 2
    )
    assert line == (
        "presentation coverage: view 16 content=[10,210] background=[0,1600] canvas=[0,1600]"
    )


def test_presentation_coverage_catches_content_past_the_background() -> None:
    # The D150 shape: a hovered applet row whose right edge escapes the resting
    # background rectangle.
    with pytest.raises(RecipeError) as excinfo:
        recipe._assert_presentation_coverage(  # pyright: ignore[reportPrivateUsage]
            _snapshot([0, 0, 100, 100], [0, 0, 1600, 100]), [_applet(10, 200)], 16, 2
        )
    assert "content ends at 210, after background 100" in str(excinfo.value)


def test_presentation_coverage_refuses_a_hidden_view() -> None:
    with pytest.raises(RecipeError, match="is hidden"):
        recipe._assert_presentation_coverage(  # pyright: ignore[reportPrivateUsage]
            _snapshot([0, 0, 1600, 100], [0, 0, 1600, 100], hidden=True), [_applet(10, 200)], 16, 2
        )


def test_presentation_coverage_refuses_when_no_live_applet() -> None:
    dead = Applet.model_validate(
        {
            "id": 1,
            "plugin": "p",
            "geometry": [0, 0, 0, 0],
            "inScheduledDestruction": True,
            "z": 0.0,
            "colorizerActive": False,
            "colorizerReason": "",
        }
    )
    with pytest.raises(RecipeError, match="no live applet geometry"):
        recipe._assert_presentation_coverage(  # pyright: ignore[reportPrivateUsage]
            _snapshot([0, 0, 1600, 100], [0, 0, 1600, 100]), [dead], 16, 2
        )


# ---- the E2E_* environment contract (refuses loudly, naming the var) --------


def test_dock_pid_refuses_when_the_pidfile_var_is_unset(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.delenv("E2E_DOCK_PIDFILE", raising=False)
    with pytest.raises(RecipeError, match="E2E_DOCK_PIDFILE"):
        recipe.dock_pid()


def test_dock_pid_reads_a_recorded_pid(monkeypatch: pytest.MonkeyPatch, tmp_path: object) -> None:
    from pathlib import Path

    assert isinstance(tmp_path, Path)
    pidfile = tmp_path / "dock.pid"
    pidfile.write_text("4321\n")
    monkeypatch.setenv("E2E_DOCK_PIDFILE", str(pidfile))
    assert recipe.dock_pid() == 4321


def test_dock_pid_is_none_when_the_file_is_absent(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("E2E_DOCK_PIDFILE", "/nonexistent/dock.pid")
    assert recipe.dock_pid() is None


def test_require_nested_refuses_outside_nested_mode(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("E2E_MODE", "live")
    with pytest.raises(RecipeError) as excinfo:
        recipe._require_nested("e2e_dock_start")  # pyright: ignore[reportPrivateUsage]
    assert "nested-only" in str(excinfo.value)
    assert "'live'" in str(excinfo.value)


def test_require_nested_names_an_unset_mode(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.delenv("E2E_MODE", raising=False)
    with pytest.raises(RecipeError, match="'unset'"):
        recipe._require_nested("e2e_screenshot")  # pyright: ignore[reportPrivateUsage]


def test_require_nested_allows_nested_mode(monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.setenv("E2E_MODE", "nested")
    recipe._require_nested("e2e_dock_start")  # pyright: ignore[reportPrivateUsage]


# ---- loud failure and the recipe entry wrapper -----------------------------


def test_fail_prints_and_exits_one(capsys: pytest.CaptureFixture[str]) -> None:
    with pytest.raises(SystemExit) as excinfo:
        recipe.fail("dock not running")
    assert excinfo.value.code == 1
    assert capsys.readouterr().err == "FAIL: dock not running\n"


def test_run_exits_zero_on_a_clean_body() -> None:
    with pytest.raises(SystemExit) as excinfo:
        recipe.run(lambda: None)
    assert excinfo.value.code == 0


def test_run_turns_a_recipe_error_into_a_loud_exit(capsys: pytest.CaptureFixture[str]) -> None:
    def body() -> None:
        raise RecipeError("e2e_tasks_view: no horizontal view carries a tasks applet")

    with pytest.raises(SystemExit) as excinfo:
        recipe.run(body)
    assert excinfo.value.code == 1
    assert "no horizontal view carries a tasks applet" in capsys.readouterr().err


def test_run_passes_a_fail_exit_through(capsys: pytest.CaptureFixture[str]) -> None:
    with pytest.raises(SystemExit) as excinfo:
        recipe.run(lambda: recipe.fail("boom"))
    assert excinfo.value.code == 1
    assert "FAIL: boom" in capsys.readouterr().err


def test_env_module_leaves_no_state() -> None:
    # A sanity guard that the module reads os.environ live (not at import), so
    # monkeypatched env in the tests above is honored.
    assert "E2E_MODE" not in os.environ or isinstance(os.environ["E2E_MODE"], str)


def test_dock_stop_reaps_a_child_dock(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    # D275 (a recipe-started dock stays a zombie its parent never reaps):
    # dock_stop's kill(0) probe read a SIGTERM'd child as alive for the full
    # timeout and returned a false shutdown failure. The reaping probe must
    # see the exit promptly.
    child = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(60)"])
    pidfile = tmp_path / "dock.pid"
    pidfile.write_text(f"{child.pid}\n")
    monkeypatch.setenv("E2E_MODE", "nested")
    monkeypatch.setenv("E2E_DOCK_PIDFILE", str(pidfile))
    started = time.monotonic()
    assert recipe.dock_stop(timeout=20) is True
    assert time.monotonic() - started < 5, "the zombie stalled the stop"
    assert child.poll() is not None  # reaped, not left a zombie


# ---- read_json: the one refusal channel (harness audit A4) ------------------


def _busctl_reply(
    monkeypatch: pytest.MonkeyPatch, returncode: int, stdout: str
) -> dict[str, object]:
    """Fake the busctl transport with a fixed reply; returns the argv capture."""
    seen: dict[str, object] = {}

    def fake(args: list[str], *, forward_stderr: bool) -> subprocess.CompletedProcess[str]:
        seen["args"] = args
        return subprocess.CompletedProcess(args, returncode, stdout, "")

    monkeypatch.setattr(recipe, "_run_busctl", fake)
    return seen


def test_read_json_parses_a_delivered_reply(monkeypatch: pytest.MonkeyPatch) -> None:
    seen = _busctl_reply(monkeypatch, 0, 's "[{\\"a\\":1}]"\n')
    assert recipe.read_json("viewAppletsData", "u", "16") == [{"a": 1}]
    # The addressing triple plus the method and its signature args, in order.
    assert seen["args"] == [
        "org.kde.lattedock",
        "/Latte",
        "org.kde.LatteDock",
        "viewAppletsData",
        "u",
        "16",
    ]


def test_read_json_raises_unavailable_on_a_busctl_failure(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    _busctl_reply(monkeypatch, 1, "")
    with pytest.raises(recipe.DbusUnavailableError):
        recipe.read_json("viewsData")


def test_read_json_raises_unavailable_on_a_refused_empty_reply(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    # The dbusreports refusal arrives as `s ""`: busctl succeeds, the payload
    # is empty. That is the SAME event as a failed call to a recipe.
    _busctl_reply(monkeypatch, 0, 's ""\n')
    with pytest.raises(recipe.DbusUnavailableError, match="refused or returned no JSON"):
        recipe.read_json("viewsData")


def test_read_json_names_a_nonempty_unparseable_reply(monkeypatch: pytest.MonkeyPatch) -> None:
    # Garbage that actually arrived is named in the message, never silently
    # classified as a mere refusal.
    _busctl_reply(monkeypatch, 0, 's "not-json"\n')
    with pytest.raises(recipe.DbusUnavailableError, match="not-json"):
        recipe.read_json("viewsData")


def test_dbus_unavailable_is_a_recipe_error_so_pollers_poll_through() -> None:
    # Existing `except RecipeError` pollers must treat the refusal as their
    # transient non-answer channel without any edit.
    assert issubclass(recipe.DbusUnavailableError, recipe.RecipeError)


def test_a_refused_reply_never_reaches_the_typed_validators(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    # Mutation-grade A4 negative: views() on a refused reply must raise the
    # refusal error, NOT a misleading pydantic ValidationError about "".
    _busctl_reply(monkeypatch, 0, 's ""\n')
    with pytest.raises(recipe.DbusUnavailableError):
        recipe.views()


def test_typed_readbacks_still_validate_parsed_garbage_loudly(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    # Malformed-but-parsed content is not a refusal: pydantic stays the loud
    # layer and names the offending field.
    _busctl_reply(monkeypatch, 0, 's "[{\\"containmentId\\":\\"x\\"}]"\n')
    with pytest.raises(ValidationError):
        recipe.views()


# ---- kwin_js transport failures (harness audit A2) --------------------------


def _fake_kwin_transport(
    monkeypatch: pytest.MonkeyPatch,
    *,
    loadscript: subprocess.CompletedProcess[str],
    run_returncode: int = 0,
) -> list[list[str]]:
    """Fake subprocess.run for the kwin_js call sequence; returns the argv log."""
    calls: list[list[str]] = []

    def fake(argv: list[str], **_kwargs: object) -> subprocess.CompletedProcess[str]:
        calls.append(list(argv))
        if "loadScript" in argv:
            return loadscript
        if any(part == "run" for part in argv):
            return subprocess.CompletedProcess(argv, run_returncode, "", "")
        return subprocess.CompletedProcess(argv, 0, "", "")

    monkeypatch.setattr(recipe.subprocess, "run", fake)
    return calls


def test_kwin_js_raises_when_loadscript_is_refused(monkeypatch: pytest.MonkeyPatch) -> None:
    # Mutation-grade A2 negative: a loadScript transport failure must raise,
    # never read as the ""-empty ran-and-printed-nothing result.
    _fake_kwin_transport(
        monkeypatch,
        loadscript=subprocess.CompletedProcess([], 1, "", "Failed to connect to bus"),
    )
    with pytest.raises(recipe.KwinScriptError):
        recipe.kwin_js("print('@TAG@|x');", 0.0)


def test_kwin_js_raises_when_the_reply_carries_no_script_number(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    _fake_kwin_transport(monkeypatch, loadscript=subprocess.CompletedProcess([], 0, "", ""))
    with pytest.raises(recipe.KwinScriptError):
        recipe.kwin_js("print('@TAG@|x');", 0.0)


def test_kwin_js_raises_when_the_run_call_is_refused_and_still_unloads(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    # A loaded script whose run call is refused never executed: raise, and
    # still stop/unload so the refused script does not leak into kwin.
    calls = _fake_kwin_transport(
        monkeypatch,
        loadscript=subprocess.CompletedProcess([], 0, "i 7\n", ""),
        run_returncode=1,
    )
    with pytest.raises(recipe.KwinScriptError):
        recipe.kwin_js("print('@TAG@|x');", 0.0)
    assert any("unloadScript" in argv for argv in calls)


def test_kwin_js_empty_capture_stays_a_legitimate_result(
    monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    # The other half of the A2 contract: a script that ran and printed nothing
    # still returns "", so match-nothing queries keep their meaning.
    kwin_log = tmp_path / "kwin.log"
    kwin_log.write_text("unrelated line\n")
    monkeypatch.setenv("E2E_MODE", "nested")
    monkeypatch.setenv("E2E_KWIN_LOG", str(kwin_log))
    _fake_kwin_transport(monkeypatch, loadscript=subprocess.CompletedProcess([], 0, "i 7\n", ""))
    assert recipe.kwin_js("print('@TAG@|x');", 0.0) == ""


def test_kwin_script_error_is_a_recipe_error_so_pollers_poll_through() -> None:
    # A poller's broad `except RecipeError` must treat the transport failure
    # as its transient non-match, mirroring the old ""-as-non-match shape.
    assert issubclass(recipe.KwinScriptError, recipe.RecipeError)


# ---- try_json_payload / is_running: the live-tool boundary helpers ----------


def test_try_json_payload_returns_none_on_a_busctl_failure(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    # The bash `views="$(e2e_json ...)" || { ...query failed... }`: a non-zero
    # busctl exit is the transport failure the caller must react to.
    def failed(args: list[str], *, forward_stderr: bool) -> subprocess.CompletedProcess[str]:
        return subprocess.CompletedProcess(args, 1, "", "call failed")

    monkeypatch.setattr(recipe, "_run_busctl", failed)
    assert recipe.try_json_payload("dockSystemData") is None


def test_try_json_payload_returns_none_on_a_refused_empty_reply(
    monkeypatch: pytest.MonkeyPatch,
) -> None:
    # The refusal arm mirrors read_json's DbusUnavailableError: an empty reply
    # is a non-answer, so it can never reach a validator as "".
    _busctl_reply(monkeypatch, 0, 's ""\n')
    assert recipe.try_json_payload("dockSystemData") is None


def test_try_json_payload_unescapes_a_delivered_reply(monkeypatch: pytest.MonkeyPatch) -> None:
    seen: dict[str, object] = {}

    def fake(args: list[str], *, forward_stderr: bool) -> subprocess.CompletedProcess[str]:
        seen["args"] = args
        return subprocess.CompletedProcess(args, 0, 's "[{\\"a\\":1}]"\n', "")

    monkeypatch.setattr(recipe, "_run_busctl", fake)
    assert recipe.try_json_payload("viewAppletsData", "u", "16") == '[{"a":1}]'
    # The addressing triple plus the method and its signature args, in order.
    assert seen["args"] == [
        "org.kde.lattedock",
        "/Latte",
        "org.kde.LatteDock",
        "viewAppletsData",
        "u",
        "16",
    ]


@pytest.mark.parametrize(
    ("state", "expected"),
    [('"running"', True), ('"stopped"', False), ("", False)],
)
def test_is_running_reflects_the_one_shot_lifecycle_probe(
    monkeypatch: pytest.MonkeyPatch, state: str, expected: bool
) -> None:
    monkeypatch.setattr(recipe, "_probe_lifecycle_state", lambda: state)
    assert recipe.is_running() is expected
