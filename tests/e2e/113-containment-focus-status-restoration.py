#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""Plasma PanelView preserves the active application while a containment holds
AcceptingInputStatus. Drive the same contract through a deterministic test
applet: external application focus ends without stealing focus back, Passive
restores, Active keeps the current focus, and the status and keyboard-
navigation reasons share one session without ending it early.

Ported from tests/e2e/113-containment-focus-status-restoration.sh to
latte_harness.recipe (BP-3, the bash-to-python migration's focus-restoration
recipe wave R9). containmentAcceptsInput / keyboardNavigation /
ownsPanelFocusSession ride the widened typed View model (W3, widen the readback
models), so viewsData is read through recipe.views(); a refused reply
(recipe.DbusUnavailableError) reads as the empty sentinel the wait loops treat
as a non-match, exactly as the bash command substitution swallowed the crashed
one-liner's empty output. The config
backup/restore and the
fixture staging keep the byte-for-byte cp -a / diff -qr contract (subprocess),
and the coarse setViewKeyboardNavigation action fails loudly on a D-Bus error.
"""

import configparser
import os
import subprocess
import sys
import tempfile
import time
from contextlib import suppress
from pathlib import Path
from typing import IO

from latte_harness import proc, recipe

_CLIENT_QML = """import QtQuick

Window {
    id: window
    readonly property string label: Qt.application.arguments[Qt.application.arguments.length - 1]
    visible: true
    width: 700
    height: 500
    title: "LATTE PANEL FOCUS " + label + " READY"
    color: "#334455"

    Item {
        anchors.fill: parent
        focus: true
        Keys.onPressed: (event) => {
            window.title = "LATTE PANEL FOCUS " + window.label + " KEY " + event.key
            event.accepted = true
        }
    }
}
"""

_PLUGIN = "org.kde.latte.panel-focus-fixture"


def _fp(*args: object) -> bool:
    return recipe.fakepointer(*args) == 0


def _views() -> list[recipe.View]:
    """viewsData as typed View records, or [] on a refused/failed reply (the bash
    crash-to-empty sentinel the wait loops read as a non-match).

    W3 (widen the readback models): containmentAcceptsInput / keyboardNavigation /
    ownsPanelFocusSession ride the typed recipe.View, so this reads recipe.views()."""
    try:
        return recipe.views()
    except recipe.DbusUnavailableError:
        return []


def _kwin_last(js: str) -> str:
    lines = [ln for ln in recipe.kwin_js(js, 0.05).splitlines() if ln]
    return lines[-1] if lines else ""


def _window_id_for_caption(expected: str) -> str:
    return _kwin_last(
        "for (const window of workspace.windowList()) {\n"
        f'        if (window.caption === "{expected}") {{\n'
        '            print("@TAG@|" + window.internalId);\n'
        "        }\n"
        "    }"
    )


def _window_caption(window_id: str) -> str:
    return _kwin_last(
        "for (const window of workspace.windowList()) {\n"
        f'        if (String(window.internalId) === "{window_id}") {{\n'
        '            print("@TAG@|" + window.caption);\n'
        "        }\n"
        "    }"
    )


def _activate_window(window_id: str) -> None:
    recipe.kwin_js(
        "for (const window of workspace.windowList()) {\n"
        f'        if (String(window.internalId) === "{window_id}") {{\n'
        "            window.minimized = false;\n"
        "            workspace.activeWindow = window;\n"
        "        }\n"
        "    }",
        0.05,
    )


def _active_window_id() -> str:
    return _kwin_last(
        "const window = workspace.activeWindow;\n"
        "        if (window) {\n"
        '            print("@TAG@|" + window.internalId);\n'
        "        }"
    )


def _wait_for_active_window(expected: str) -> bool:
    for _ in range(80):
        if _active_window_id() == expected:
            return True
        time.sleep(0.05)
    return False


def _activate_window_and_require(window_id: str, boundary: str) -> None:
    _activate_window(window_id)
    if not _wait_for_active_window(window_id):
        recipe.fail(f"{boundary} application did not become active")


def _set_window_minimized(window_id: str, minimized: str) -> None:
    recipe.kwin_js(
        "for (const window of workspace.windowList()) {\n"
        f'        if (String(window.internalId) === "{window_id}") {{\n'
        f"            window.minimized = {minimized};\n"
        "        }\n"
        "    }",
        0.05,
    )


def _window_is_minimized(window_id: str) -> str:
    return _kwin_last(
        "for (const window of workspace.windowList()) {\n"
        f'        if (String(window.internalId) === "{window_id}") {{\n'
        '            print("@TAG@|" + window.minimized);\n'
        "        }\n"
        "    }"
    )


def _keyboard_navigation(cid: int) -> str:
    for view in _views():
        if view.containment_id == cid:
            return "true" if view.keyboard_navigation else "false"
    return ""


def _wait_for_keyboard_navigation(cid: int, expected: str) -> bool:
    for _ in range(80):
        if _keyboard_navigation(cid) == expected:
            return True
        time.sleep(0.05)
    return False


def _panel_focus_state(cid: int) -> str:
    for view in _views():
        if view.containment_id == cid:
            accepts = str(view.containment_accepts_input).lower()
            keyboard = str(view.keyboard_navigation).lower()
            owns = str(view.owns_panel_focus_session).lower()
            return f"{accepts} {keyboard} {owns}"
    return ""


def _wait_for_panel_focus_state(cid: int, expected: str) -> bool:
    for _ in range(80):
        if _panel_focus_state(cid) == expected:
            return True
        time.sleep(0.05)
    return False


def _set_keyboard_navigation(cid: int, enabled: str, boundary: str) -> None:
    recipe.call_or_fail(
        f"{boundary} D-Bus call failed", "setViewKeyboardNavigation", "ub", str(cid), enabled
    )
    if not _wait_for_keyboard_navigation(cid, enabled):
        recipe.fail(f"{boundary} did not reach keyboardNavigation={enabled}")


class _Clients:
    def __init__(self, scratch: Path) -> None:
        self._scratch = scratch
        self._pids: dict[str, subprocess.Popen[bytes] | None] = {}
        self._logs: list[IO[bytes]] = []

    def launch(self, label: str) -> str:
        expected = f"LATTE PANEL FOCUS {label} READY"
        log = (self._scratch / f"client-{label}.log").open("wb")
        self._logs.append(log)
        self._pids[label] = subprocess.Popen(
            ["qml", str(self._scratch / "key-client.qml"), "--", label],
            stdout=log,
            stderr=subprocess.STDOUT,
        )
        window_id = ""
        for _ in range(100):
            window_id = _window_id_for_caption(expected)
            if window_id:
                break
            time.sleep(0.05)
        if not window_id:
            recipe.fail(f"controlled key client {label} never mapped")
        return window_id

    def kill_all(self) -> None:
        for handle in self._pids.values():
            if handle is not None:
                with suppress(ProcessLookupError):
                    handle.kill()
                with suppress(Exception):
                    handle.wait()
        for log in self._logs:
            with suppress(OSError):
                log.close()


def _send_key_and_require_delivery(window_id: str, key: str, boundary: str) -> None:
    before = _window_caption(window_id)
    if not _fp("key", key):
        recipe.fail(f"{boundary} could not inject {key}")
    for _ in range(80):
        if _window_caption(window_id) != before:
            return
        time.sleep(0.05)
    recipe.fail(f"{boundary} did not deliver {key} to client {window_id}")


def _send_key_and_require_saved_client_unchanged(window_id: str, key: str, boundary: str) -> None:
    before = _window_caption(window_id)
    if not _fp("key", key):
        recipe.fail(f"{boundary} could not inject {key}")
    time.sleep(0.3)
    if _window_caption(window_id) != before:
        recipe.fail(f"{boundary} sent {key} to the saved application")


def _select_fixture_layout(lattedockrc: Path) -> None:
    config = configparser.RawConfigParser()
    config.optionxform = str  # type: ignore[assignment,method-assign]
    config.read(lattedockrc)
    if not config.has_section("UniversalSettings"):
        config.add_section("UniversalSettings")
    config.set("UniversalSettings", "singleModeLayoutName", "PanelFocus")
    config.set("UniversalSettings", "memoryUsage", "0")
    with lattedockrc.open("w") as output:
        config.write(output, space_around_delimiters=False)


def main() -> None:
    proc.install_conventional_signal_exits()
    repo = Path(os.environ["E2E_REPO"])
    fixture = repo / "tests" / "e2e" / "fixtures" / "panel-focus"
    fixture_data = Path(os.environ["E2E_RT"]) / "panel-focus-data"
    config_home = Path(os.environ["E2E_CONFIG_HOME"])
    backup = Path(tempfile.mkdtemp(prefix="latte-panel-focus-backup.", dir="/tmp"))
    scratch = Path(tempfile.mkdtemp(prefix="latte-panel-focus-client.", dir="/tmp"))
    clients = _Clients(scratch)
    state = {"backup_ready": False, "recipe_finalized": False}

    def _cp_a(source: str, destination: str) -> int:
        return subprocess.run(["cp", "-a", source, destination], check=False).returncode

    def _restore_config() -> int:
        failed = 0
        pid = recipe.dock_pid()
        if pid is not None:
            alive = True
            try:
                os.kill(pid, 0)
            except OSError:
                alive = False
            if alive and not recipe.dock_stop():
                failed = 1
        if subprocess.run(["rm", "-rf", str(config_home)], check=False).returncode != 0:
            failed = 1
        if subprocess.run(["mkdir", "-p", str(config_home)], check=False).returncode != 0:
            failed = 1
        if _cp_a(f"{backup}/config/.", f"{config_home}/") != 0:
            failed = 1
        if (
            subprocess.run(
                ["diff", "-qr", f"{backup}/config", str(config_home)],
                stdout=subprocess.DEVNULL,
                check=False,
            ).returncode
            != 0
        ):
            failed = 1
        if subprocess.run(["rm", "-rf", str(fixture_data)], check=False).returncode != 0:
            failed = 1
        return failed

    try:
        if not (
            (fixture / "PanelFocus.layout.latte").is_file()
            and (fixture / "plasmoids" / _PLUGIN / "metadata.json").is_file()
            and (fixture / "plasmoids" / _PLUGIN / "contents" / "ui" / "main.qml").is_file()
        ):
            recipe.fail("panel-focus fixture is incomplete")

        (backup / "config").mkdir(parents=True)
        if _cp_a(f"{config_home}/.", f"{backup}/config/") != 0:
            recipe.fail("could not back up the nested config")
        state["backup_ready"] = True

        if not recipe.dock_stop():
            recipe.fail("could not stop the vehicle before staging panel focus")
        os.environ["XDG_DATA_HOME"] = str(fixture_data)
        (fixture_data / "plasma" / "plasmoids").mkdir(parents=True, exist_ok=True)
        if _cp_a(f"{fixture}/plasmoids/.", f"{fixture_data}/plasma/plasmoids/") != 0:
            recipe.fail("could not stage the panel-focus applet")
        for stale in (config_home / "latte").glob("*.layout.latte"):
            stale.unlink()
        if (
            _cp_a(
                f"{fixture}/PanelFocus.layout.latte",
                f"{config_home}/latte/PanelFocus.layout.latte",
            )
            != 0
        ):
            recipe.fail("could not stage the panel-focus layout")

        _select_fixture_layout(config_home / "lattedockrc")

        (scratch / "key-client.qml").write_text(_CLIENT_QML)

        if not recipe.dock_start(90):
            recipe.fail("dock did not settle with the panel-focus fixture")
        views = _views()
        cid = views[0].containment_id if len(views) == 1 else None
        if cid is None:
            recipe.fail("panel-focus fixture did not create exactly one view")
        assert cid is not None

        view = next(v for v in _views() if v.containment_id == cid)
        applet = next(a for a in recipe.view_applets(cid) if a.plugin == _PLUGIN)
        origin_x = view.absolute_geometry[0] - view.local_geometry[0]
        origin_y = view.absolute_geometry[1] - view.local_geometry[1]
        ax, ay, aw, ah = applet.geometry
        accepting_x = round(origin_x + ax + aw / 6)
        target_y = round(origin_y + ay + ah / 2)
        approach_x = view.screen_geometry[0] + view.screen_geometry[2] // 2
        approach_y = view.screen_geometry[1] + view.screen_geometry[3] // 2

        def click_accepting_input() -> None:
            if not _fp("move", approach_x, approach_y):
                recipe.fail("could not approach the AcceptingInput status region")
            # accepting_x/target_y address the applet at its RESTING geometry, but
            # the click's own warp onto the dock starts the parabolic hover zoom,
            # and between legs the pointer lingers inside the zoomed dock from the
            # previous click. Settle after un-hovering so the click lands while the
            # dock is at rest.
            time.sleep(0.4)
            if not _fp("click", accepting_x, target_y):
                recipe.fail("could not click the AcceptingInput status region")

        def begin_accepting_input() -> None:
            # A single pointer click on the parabolically-zooming dock intermittently
            # misses the resting AcceptingInput status region (both the first click,
            # whose warp starts the zoom, and later ones, which fight the lingering
            # hover). The bash drove one click and won that race by timing luck; the
            # blue region is an idempotent SET to AcceptingInputStatus (fixtures
            # main.qml), never a toggle, so re-clicking it until the containment
            # acquires the session is safe and only re-asserts the same status. The
            # final refusal keeps the bash message verbatim.
            deadline = time.monotonic() + 20.0
            while time.monotonic() < deadline:
                click_accepting_input()
                for _ in range(30):
                    if _panel_focus_state(cid) == "true false true":
                        return
                    time.sleep(0.05)
            recipe.fail(
                "AcceptingInput did not acquire the panel focus session; "
                f"state={_panel_focus_state(cid)}"
            )

        def set_status_from_focused_panel(status: str, boundary: str) -> None:
            key = {"passive": "F8", "active": "F9"}.get(status)
            if key is None:
                recipe.fail(f"unknown focused-panel status {status}")
            assert key is not None
            if not _fp("key", key):
                recipe.fail(f"{boundary} could not inject {key}")
            time.sleep(0.25)

        client_a = clients.launch("A")
        client_b = clients.launch("B")

        _activate_window_and_require(client_a, "external-focus baseline")
        _send_key_and_require_delivery(client_a, "a", "external-focus baseline")
        begin_accepting_input()
        _send_key_and_require_saved_client_unchanged(client_a, "Right", "external-focus grant")
        _activate_window_and_require(client_b, "external-focus winner")
        if not _wait_for_panel_focus_state(cid, "false false false"):
            recipe.fail("external application focus did not discard the containment session")
        _send_key_and_require_delivery(client_b, "b", "external-focus winner")
        recipe.call_or_fail(
            "non-stealing idempotent exit call failed",
            "setViewKeyboardNavigation",
            "ub",
            str(cid),
            "false",
        )
        if not _wait_for_panel_focus_state(cid, "false false false"):
            recipe.fail("idempotent exit changed the discarded containment session")
        _send_key_and_require_delivery(client_b, "c", "non-stealing idempotent exit")
        print("ok: external application focus ended containment input without stealing focus back")

        _activate_window_and_require(client_a, "Passive baseline")
        _send_key_and_require_delivery(client_a, "c", "Passive baseline")
        begin_accepting_input()
        _send_key_and_require_saved_client_unchanged(client_a, "Right", "AcceptingInput focus")
        set_status_from_focused_panel("passive", "Passive transition")
        if not _wait_for_panel_focus_state(cid, "false false false"):
            recipe.fail("Passive transition did not release the panel focus session")
        if not _wait_for_active_window(client_a):
            recipe.fail("Passive transition did not reactivate the saved application")
        _send_key_and_require_delivery(client_a, "d", "Passive restoration")
        print("ok: AcceptingInput to Passive restored actual key delivery")

        _activate_window_and_require(client_a, "Active baseline")
        _send_key_and_require_delivery(client_a, "e", "Active baseline")
        begin_accepting_input()
        _send_key_and_require_saved_client_unchanged(client_a, "Left", "Active focus precondition")
        _set_window_minimized(client_a, "true")
        set_status_from_focused_panel("active", "Active transition")
        if not _wait_for_panel_focus_state(cid, "false false false"):
            recipe.fail("Active transition did not discard the panel focus session")
        time.sleep(0.4)
        if _window_is_minimized(client_a) != "true":
            recipe.fail("AcceptingInput to Active reactivated the saved application")
        print("ok: AcceptingInput to Active discarded the saved application target")

        _activate_window_and_require(client_a, "Passive coexistence baseline")
        _send_key_and_require_delivery(client_a, "f", "Passive coexistence baseline")
        begin_accepting_input()
        _set_keyboard_navigation(cid, "true", "Passive coexistence enter")
        set_status_from_focused_panel("passive", "Passive coexistence transition")
        if not _wait_for_panel_focus_state(cid, "false true true"):
            recipe.fail("Passive ended the shared session before keyboard navigation")
        if _keyboard_navigation(cid) != "true":
            recipe.fail("Passive status ended the independent keyboard-navigation reason")
        _send_key_and_require_saved_client_unchanged(client_a, "Down", "Passive coexistence")
        _set_keyboard_navigation(cid, "false", "Passive coexistence final exit")
        if not _wait_for_panel_focus_state(cid, "false false false"):
            recipe.fail("final keyboard exit did not release the shared session")
        if not _wait_for_active_window(client_a):
            recipe.fail("Passive coexistence exit did not reactivate the saved application")
        _send_key_and_require_delivery(client_a, "g", "Passive coexistence restoration")
        print("ok: Passive preserved the shared target until keyboard navigation ended")

        _activate_window_and_require(client_a, "Active coexistence baseline")
        _send_key_and_require_delivery(client_a, "h", "Active coexistence baseline")
        begin_accepting_input()
        _set_keyboard_navigation(cid, "true", "Active coexistence enter")
        _set_window_minimized(client_a, "true")
        set_status_from_focused_panel("active", "Active coexistence transition")
        if not _wait_for_panel_focus_state(cid, "false true true"):
            recipe.fail("Active ended the shared session before keyboard navigation")
        if _keyboard_navigation(cid) != "true":
            recipe.fail("Active status ended the independent keyboard-navigation reason")
        _send_key_and_require_saved_client_unchanged(client_a, "Up", "Active coexistence")
        _set_keyboard_navigation(cid, "false", "Active coexistence final exit")
        if not _wait_for_panel_focus_state(cid, "false false false"):
            recipe.fail("final keyboard exit did not discard the shared session")
        time.sleep(0.4)
        if _window_is_minimized(client_a) != "true":
            recipe.fail("final keyboard exit reused the target invalidated by Active status")
        print("ok: Active invalidated the target while keyboard navigation kept focus")

        if not recipe.dock_stop():
            recipe.fail("panel-focus fixture dock did not stop cleanly")
        if subprocess.run(["rm", "-rf", str(config_home)], check=False).returncode != 0:
            recipe.fail("could not clear the fixture config")
        if subprocess.run(["mkdir", "-p", str(config_home)], check=False).returncode != 0:
            recipe.fail("could not recreate the config directory")
        if _cp_a(f"{backup}/config/.", f"{config_home}/") != 0:
            recipe.fail("could not restore the nested config")
        if (
            subprocess.run(
                ["diff", "-qr", f"{backup}/config", str(config_home)],
                stdout=subprocess.DEVNULL,
                check=False,
            ).returncode
            != 0
        ):
            recipe.fail("restored nested config differs from its backup")
        if subprocess.run(["rm", "-rf", str(fixture_data)], check=False).returncode != 0:
            recipe.fail("could not remove the fixture data")
        state["recipe_finalized"] = True

        print("PASS: containment focus status restoration and coexistence")
    finally:
        cleanup_failed = 0
        clients.kill_all()
        if not state["recipe_finalized"] and state["backup_ready"] and _restore_config() != 0:
            cleanup_failed = 1
        for temp in (backup, scratch):
            if subprocess.run(["rm", "-rf", str(temp)], check=False).returncode != 0:
                cleanup_failed = 1
        if cleanup_failed:
            print("FAIL: panel-focus fixture cleanup left residue", file=sys.stderr)
            raise SystemExit(1)


if __name__ == "__main__":
    recipe.run(main)
