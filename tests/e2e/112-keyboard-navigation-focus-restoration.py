#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""Keyboard navigation gives the dock layer surface keyboard focus. Pin the
complete focus-return session with actual key delivery: explicit QML and D-Bus
exits restore the saved client, external focus loss does not steal focus back,
another dock cannot consume the session, and destroyed clients/views do not
leave stale focus state.

Ported from tests/e2e/112-keyboard-navigation-focus-restoration.sh to
latte_harness.recipe (BP-3, the bash-to-python migration's recipe batch R9).
keyboardNavigation / ownsPanelFocusSession ride the widened typed View model
(W3, widen the readback models), so viewsData is read through recipe.views(); a
reply dbusreports refuses transiently while a freshly duplicated view lacks an
accepted placement raises the pollable DbusUnavailableError, the channel the
polling callers read as a non-match just as the bash command substitution
swallowed the crashed one-liner's empty output.
The coarse setViewKeyboardNavigation / duplicateView / removeView actions stay
busctl calls that fail loudly on a D-Bus error, matching the bash
``e2e_call ... || e2e_fail``.
"""

import shutil
import subprocess
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
    title: "LATTE KBNAV " + label + " READY"
    color: "#334455"

    Item {
        anchors.fill: parent
        focus: true
        Keys.onPressed: (event) => {
            window.title = "LATTE KBNAV " + window.label + " KEY " + event.key
            event.accepted = true
        }
    }
}
"""


def _fp(*args: object) -> bool:
    """`"$E2E_FAKEPOINTER" "$@"`: drive the vehicle pointer/keyboard; True on ok."""
    return recipe.fakepointer(*args) == 0


def _views() -> list[recipe.View]:
    """viewsData as typed View records; a refused reply raises the pollable
    DbusUnavailableError.

    W3 (widen the readback models): keyboardNavigation / ownsPanelFocusSession ride
    the typed recipe.View, so this reads recipe.views(); a transient dbusreports
    refusal (a duplicated view still being placed) raises DbusUnavailableError, the
    RecipeError subclass the polling callers here catch.
    """
    return recipe.views()


def _lifecycle_running() -> bool:
    """The bash ``e2e_call lifecycleState | awk '{print $2}'`` == '"running"'."""
    fields = recipe.call("lifecycleState").split()
    return len(fields) >= 2 and fields[1] == '"running"'


def _kwin_last(js: str) -> str:
    """The bash ``e2e_kwin_js '...' 0.05 | tail -1``: the last tagged line."""
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


def _window_exists(window_id: str) -> bool:
    return (
        _kwin_last(
            "for (const window of workspace.windowList()) {\n"
            f'        if (String(window.internalId) === "{window_id}") {{\n'
            '            print("@TAG@|yes");\n'
            "        }\n"
            "    }"
        )
        == "yes"
    )


def _activate_window(window_id: str) -> None:
    recipe.kwin_js(
        "for (const window of workspace.windowList()) {\n"
        f'        if (String(window.internalId) === "{window_id}") {{\n'
        "            workspace.activeWindow = window;\n"
        "        }\n"
        "    }",
        0.05,
    )


def _keyboard_navigation(cid: int) -> str:
    """'missing'/'true'/'false' for the view's keyboardNavigation, '' if refused."""
    try:
        views = _views()
    except recipe.RecipeError:
        return ""
    matches = [view for view in views if view.containment_id == cid]
    return "missing" if not matches else ("true" if matches[0].keyboard_navigation else "false")


def _wait_for_keyboard_navigation(cid: int, expected: str) -> bool:
    for _ in range(100):
        if _keyboard_navigation(cid) == expected:
            return True
        time.sleep(0.05)
    return False


def _panel_focus_session_owner(cid: int) -> str:
    """'missing'/'true'/'false' for ownsPanelFocusSession, '' if refused."""
    try:
        views = _views()
    except recipe.RecipeError:
        return ""
    matches = [view for view in views if view.containment_id == cid]
    return (
        "missing" if not matches else ("true" if matches[0].owns_panel_focus_session else "false")
    )


def _wait_for_panel_focus_session_owner(cid: int, expected: str) -> bool:
    for _ in range(100):
        if _panel_focus_session_owner(cid) == expected:
            return True
        time.sleep(0.05)
    return False


def _enter_keyboard_navigation(cid: int, boundary: str) -> None:
    recipe.call_or_fail(
        f"{boundary} enter call failed", "setViewKeyboardNavigation", "ub", str(cid), "true"
    )
    if not _wait_for_keyboard_navigation(cid, "true"):
        recipe.fail(f"{boundary} did not enter keyboard navigation")
    if not _wait_for_panel_focus_session_owner(cid, "true"):
        recipe.fail(f"{boundary} did not acquire the panel focus session")


def _exit_keyboard_navigation(cid: int, boundary: str) -> None:
    recipe.call_or_fail(
        f"{boundary} exit call failed", "setViewKeyboardNavigation", "ub", str(cid), "false"
    )
    if not _wait_for_keyboard_navigation(cid, "false"):
        recipe.fail(f"{boundary} did not exit keyboard navigation")
    if not _wait_for_panel_focus_session_owner(cid, "false"):
        recipe.fail(f"{boundary} left the panel focus session owned")


class _Clients:
    """The bash client_pids map plus launched_client_id: controlled key clients."""

    def __init__(self, scratch: Path) -> None:
        self._scratch = scratch
        self._pids: dict[str, subprocess.Popen[bytes] | None] = {}
        self._logs: list[IO[bytes]] = []

    def launch(self, label: str) -> str:
        expected = f"LATTE KBNAV {label} READY"
        # The child owns this fd for its whole lifetime (the bash `qml ... >log &`),
        # so a `with` block cannot bound it; it is closed in kill_all at teardown.
        log = (self._scratch / f"client-{label}.log").open("wb")
        self._logs.append(log)
        proc_handle = subprocess.Popen(
            ["qml", str(self._scratch / "key-client.qml"), "--", label],
            stdout=log,
            stderr=subprocess.STDOUT,
        )
        self._pids[label] = proc_handle
        window_id = ""
        for _ in range(100):
            window_id = _window_id_for_caption(expected)
            if window_id:
                break
            time.sleep(0.05)
        if not window_id:
            recipe.fail(f"controlled key client {label} never mapped")
        return window_id

    def stop(self, label: str, window_id: str) -> None:
        handle = self._pids[label]
        if handle is not None:
            with suppress(ProcessLookupError):
                handle.kill()
            with suppress(Exception):
                handle.wait()
        self._pids[label] = None
        for _ in range(100):
            if not _window_exists(window_id):
                return
            time.sleep(0.05)
        recipe.fail(f"controlled key client {label} remained mapped after exit")

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
        after = _window_caption(window_id)
        if after != before:
            return
        time.sleep(0.05)
    recipe.fail(f"{boundary} did not deliver {key} to client {window_id}")


def _send_key_and_require_saved_client_unchanged(client_id: str, key: str, boundary: str) -> None:
    before = _window_caption(client_id)
    if not _fp("key", key):
        recipe.fail(f"{boundary} could not inject {key}")
    time.sleep(0.3)
    after = _window_caption(client_id)
    if after != before:
        recipe.fail(f"{boundary} sent {key} to the saved client instead of the dock")


def main() -> None:
    proc.install_conventional_signal_exits()
    scratch = Path(tempfile.mkdtemp(prefix="latte-kbnav-focus.", dir="/tmp"))
    (scratch / "key-client.qml").write_text(_CLIENT_QML)
    clients = _Clients(scratch)
    duplicate_cid: int | None = None
    try:
        if not recipe.wait_settled(60):
            recipe.fail("vehicle dock never settled")
        views = _views()
        if not views:
            recipe.fail("vehicle has no dock view")
        source_cid = views[0].containment_id

        client_a = clients.launch("A")
        _activate_window(client_a)
        _send_key_and_require_delivery(client_a, "a", "baseline")

        _enter_keyboard_navigation(source_cid, "QML Escape")
        # Escape is handled by the real containment KeyboardNavigationHandler. If
        # the layer surface never received keyboard focus, the client title
        # changes and the mode remains active. Wait for the compositor to grant
        # the layer surface its keyboard focus before injecting Escape: that
        # grant is Qt-level (QWindow::active) and unobservable over D-Bus/KWin
        # scripting, so it needs wall-clock time, exactly as this recipe's own
        # focus-loss and destroyed-target legs sleep before their grant-dependent
        # key injection. The bash omitted it here and only won the race because
        # its per-iteration python3-spawn poll loops burned that time incidentally;
        # the typed port's ~3 ms busctl probes return from the enter far sooner
        # (the D279 poll-horizon lesson), so the settle is made explicit.
        time.sleep(3)
        if not _fp("key", "Escape"):
            recipe.fail("QML Escape injection failed")
        if not _wait_for_keyboard_navigation(source_cid, "false"):
            recipe.fail("real QML Escape did not exit keyboard navigation")
        _send_key_and_require_delivery(client_a, "b", "QML Escape restoration")
        print("ok: QML Escape restored actual key delivery")

        _activate_window(client_a)
        _send_key_and_require_delivery(client_a, "c", "D-Bus baseline")
        _enter_keyboard_navigation(source_cid, "D-Bus exit")
        _exit_keyboard_navigation(source_cid, "D-Bus exit")
        _send_key_and_require_delivery(client_a, "d", "D-Bus restoration")
        print("ok: D-Bus toggle-off restored actual key delivery")

        before_ids = {view.containment_id for view in _views()}
        recipe.call_or_fail(
            "second-view focus-session duplicate call failed",
            "duplicateView",
            "u",
            str(source_cid),
        )
        for _ in range(120):
            try:
                created = [
                    view.containment_id
                    for view in _views()
                    if view.containment_id not in before_ids
                ]
            except recipe.RecipeError:
                created = []
            if len(created) == 1:
                duplicate_cid = created[0]
                break
            time.sleep(0.25)
        if duplicate_cid is None:
            recipe.fail("independent second dock never appeared")

        _activate_window(client_a)
        _enter_keyboard_navigation(source_cid, "first focus-session owner")
        recipe.call_or_fail(
            "competing-owner call failed at D-Bus",
            "setViewKeyboardNavigation",
            "ub",
            str(duplicate_cid),
            "true",
        )
        time.sleep(0.3)
        if _keyboard_navigation(source_cid) != "true":
            recipe.fail("competing dock consumed the first dock's focus session")
        if _keyboard_navigation(duplicate_cid) != "false":
            recipe.fail("two docks entered keyboard navigation simultaneously")
        if not (
            _panel_focus_session_owner(source_cid) == "true"
            and _panel_focus_session_owner(duplicate_cid) == "false"
        ):
            recipe.fail("two docks reported ownership of one panel focus session")
        _exit_keyboard_navigation(source_cid, "first focus-session owner")
        print("ok: a second dock cannot replace or consume the active focus session")

        _activate_window(client_a)
        _enter_keyboard_navigation(duplicate_cid, "destroyed focus-session owner")
        recipe.call_or_fail(
            "could not remove the dock that owned keyboard focus",
            "removeView",
            "u",
            str(duplicate_cid),
        )
        for _ in range(120):
            if _keyboard_navigation(duplicate_cid) == "missing":
                break
            time.sleep(0.25)
        if _keyboard_navigation(duplicate_cid) != "missing":
            recipe.fail("removed focus-owning dock remained in viewsData")
        duplicate_cid = None
        _activate_window(client_a)
        _enter_keyboard_navigation(source_cid, "post-owner-destruction session")
        _exit_keyboard_navigation(source_cid, "post-owner-destruction session")
        _send_key_and_require_delivery(client_a, "e", "owner-destruction cleanup")
        print("ok: destroying the owning dock ends its focus session")

        _activate_window(client_a)
        _send_key_and_require_delivery(client_a, "f", "focus-loss baseline")
        _enter_keyboard_navigation(source_cid, "focus-loss discard")
        # KWin does not expose layer surfaces through workspace.activeWindow.
        # Exercise one real navigation key after the compositor grant instead of
        # treating that application-window property as a focus oracle.
        time.sleep(3)
        _send_key_and_require_saved_client_unchanged(client_a, "Right", "focus-loss grant")
        client_b = clients.launch("B")
        _activate_window(client_b)
        if not _wait_for_keyboard_navigation(source_cid, "false"):
            recipe.fail("external client focus did not discard keyboard navigation")
        _send_key_and_require_delivery(client_b, "g", "focus-loss winner")
        recipe.call_or_fail(
            "non-stealing idempotent exit call failed",
            "setViewKeyboardNavigation",
            "ub",
            str(source_cid),
            "false",
        )
        _send_key_and_require_delivery(client_b, "h", "non-stealing idempotent exit")
        print("ok: external focus loss ends the session without stealing focus back")
        clients.stop("B", client_b)

        _activate_window(client_a)
        _send_key_and_require_delivery(client_a, "i", "destroyed-target baseline")
        _enter_keyboard_navigation(source_cid, "destroyed saved target")
        time.sleep(3)
        _send_key_and_require_saved_client_unchanged(client_a, "Left", "destroyed-target grant")
        clients.stop("A", client_a)
        if _keyboard_navigation(source_cid) != "true":
            recipe.fail("destroying the inactive saved target unexpectedly exited the dock mode")
        _exit_keyboard_navigation(source_cid, "destroyed saved target")
        if not _lifecycle_running():
            recipe.fail("dock died while clearing a destroyed saved target")
        client_c = clients.launch("C")
        _activate_window(client_c)
        _send_key_and_require_delivery(client_c, "j", "post-target-destruction focus")
        print("ok: destroying the saved target clears safely")

        print("PASS: keyboard-navigation-focus-restoration")
    finally:
        clients.kill_all()
        if duplicate_cid is not None:
            _ = recipe.call_status(
                "setViewKeyboardNavigation", "ub", str(duplicate_cid), "false", quiet=True
            )
            _ = recipe.call_status("removeView", "u", str(duplicate_cid), quiet=True)
        shutil.rmtree(scratch, ignore_errors=True)


if __name__ == "__main__":
    recipe.run(main)
