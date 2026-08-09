#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# E2E: keyboard focus mode lifecycle over D-Bus in the nested vehicle -
# enter/exit readback (viewsData keyboardNavigation), unknown-id refusal,
# and the bulletproof focus-loss exit: with the mode on, a client window
# mapping in the compositor takes the keyboard focus and the mode must
# fall back to off ON ITS OWN (a dock stuck focusable breaks every
# fullscreen application - that is the defect class this pins).
#
# Uses the driver's managed vehicle dock like every sibling recipe. The
# first version launched its OWN dock instead (written before the driver
# existed); under the driver's shared bus that second launch died on the
# KDBusService unique name AND its forwarded activation popped the
# Settings window on the driver dock - which then held keyboard focus,
# so enterKeyboardNavigation's requestActivate never landed and the
# focus-loss leg failed while ALSO poisoning whichever recipe ran next
# (caught 2026-07-17 promoting the suite; the window dump in the ledger
# is the evidence). Recipes must never launch a second dock.
#
# The focus-taker is a minimal QML window, not konsole: konsole's cold
# start inside the nested session exceeded every reasonable wait and the
# focus-loss leg timed out on it (caught 2026-07-17 while landing the
# mode; the qml window maps in about a second).
#
# Ported from tests/e2e/keyboard-navigation-mode.sh to latte_harness.recipe
# (BP-3, the bash-to-python migration's focus-restoration recipe wave R9).
# keyboardNavigation is not in the typed View model, so viewsData is read as
# raw JSON (the same boundary the bash jq pipeline used).

import shutil
import subprocess
import tempfile
import time
from contextlib import suppress
from pathlib import Path

from latte_harness import proc, recipe

_FOCUS_TAKER_QML = (
    "import QtQuick\n"
    'Window { visible: true; width: 300; height: 200; title: "kbnav-focus-taker" }\n'
)


def _call_quiet(*args: str) -> None:
    """`call ... >/dev/null 2>&1`: run a lattedock method, discarding all output."""
    subprocess.run(
        ["busctl", "--user", "call", "org.kde.lattedock", "/Latte", "org.kde.LatteDock", *args],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )


def _views() -> list[recipe.View]:
    """viewsData as typed View records (W3, widen the readback models):
    containmentId / keyboardNavigation ride the typed recipe.View."""
    return recipe.views()


def _lifecycle_running() -> bool:
    """The bash ``call lifecycleState | awk '{print $2}'`` == '"running"'."""
    fields = recipe.call("lifecycleState").split()
    return len(fields) >= 2 and fields[1] == '"running"'


def main() -> None:
    proc.install_conventional_signal_exits()
    scratch = Path(tempfile.mkdtemp(prefix="kbnav-e2e.", dir="/tmp"))
    (scratch / "focus-taker.qml").write_text(_FOCUS_TAKER_QML)
    taker: subprocess.Popen[bytes] | None = None

    def fail(message: str) -> None:
        print(f"FAIL: {message}")
        raise SystemExit(1)

    def passed(message: str) -> None:
        print(f"ok: {message}")

    def kbnav(cid: int) -> str:
        for view in _views():
            if view.containment_id == cid:
                return "true" if view.keyboard_navigation else "false"
        return ""

    try:
        if not recipe.wait_settled(45):
            fail("vehicle dock never settled")
        passed("driver dock running and settled")

        views = _views()
        cid = views[0].containment_id if views else None
        if cid is None:
            fail("no containment id in viewsData")
        assert cid is not None
        passed(f"containment id {cid}")

        if kbnav(cid) != "false":
            fail("baseline keyboardNavigation is not false")
        passed("baseline false (mode off is the default)")

        #! unknown id is refused loudly, dock stays alive, real view untouched
        _call_quiet("setViewKeyboardNavigation", "ub", "999999", "true")
        time.sleep(1)
        if not _lifecycle_running():
            fail("dock died on unknown-id refusal")
        if kbnav(cid) != "false":
            fail("unknown-id call changed the real view's state")
        passed("unknown containment id refused, dock alive")

        _call_quiet("setViewKeyboardNavigation", "ub", str(cid), "true")
        got = ""
        for _ in range(10):
            got = kbnav(cid)
            if got == "true":
                break
            time.sleep(0.5)
        if got != "true":
            fail("enter did not read back keyboardNavigation true")
        passed("enter over D-Bus: keyboardNavigation true")

        _call_quiet("setViewKeyboardNavigation", "ub", str(cid), "false")
        got = ""
        for _ in range(10):
            got = kbnav(cid)
            if got == "false":
                break
            time.sleep(0.5)
        if got != "false":
            fail("exit did not read back keyboardNavigation false")
        passed("exit over D-Bus: keyboardNavigation false")

        #! the focus-loss exit
        _call_quiet("setViewKeyboardNavigation", "ub", str(cid), "true")
        for _ in range(10):
            if kbnav(cid) == "true":
                break
            time.sleep(0.5)
        if kbnav(cid) != "true":
            fail("re-enter before the focus-loss leg failed")

        #! Wait for the compositor to actually grant the layer-shell dock its
        #! OnDemand keyboard focus before mapping the taker. This state is
        #! Qt-level (QWindow::active on the layer surface) and NOT observable
        #! over D-Bus or KWin scripting - KWin's workspace.activeWindow never
        #! reports layer surfaces. If the taker maps before the grant lands the
        #! dock was never active, so there is no active->inactive transition for
        #! the exit watcher to catch and the leg races false-negative (proven
        #! 2026-07-17: the leg passed deterministically once ~1.5s of probe
        #! overhead sat here, failed without it). The settle is for an
        #! inherently-unobservable compositor grant, not a value clamp; the
        #! denial half of this (grant refused, not merely slow) is the filed
        #! keyboard-item follow-up.
        time.sleep(3)

        taker = subprocess.Popen(
            ["qml", str(scratch / "focus-taker.qml")],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.STDOUT,
        )
        got = ""
        for _ in range(60):
            got = kbnav(cid)
            if got == "false":
                break
            time.sleep(0.5)
        with suppress(ProcessLookupError):
            taker.kill()
        with suppress(Exception):
            taker.wait()
        taker = None
        if got != "false":
            fail("focus loss did not exit keyboard navigation (dock stuck focusable)")
        passed("focus loss exits the mode on its own")

        #! exit is idempotent
        _call_quiet("setViewKeyboardNavigation", "ub", str(cid), "false")
        time.sleep(1)
        if not _lifecycle_running():
            fail("dock died on idempotent exit")
        passed("idempotent exit, dock alive")

        print("PASS: keyboard-navigation-mode")
    finally:
        if taker is not None:
            with suppress(ProcessLookupError):
                taker.kill()
            with suppress(Exception):
                taker.wait()
        shutil.rmtree(scratch, ignore_errors=True)


if __name__ == "__main__":
    recipe.run(main)
