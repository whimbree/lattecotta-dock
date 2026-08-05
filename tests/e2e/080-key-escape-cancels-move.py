#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""P9 / C-I10 acceptance (HC3): prove the fakepointer `key` verb's Escape
actually CANCELS an in-flight drag, observed as the drag's effect being
ABORTED - not merely that a keysym was delivered.

WHY THIS DRAG. A standalone `key` invocation cannot inject into a
pointer-button-held drag: a fake_input client holds its button only while
connected, and KWin drops held input the instant the tool exits, so the
button would release before a separate `key` process could run. The
in-flight drag a STANDALONE key verb can genuinely cancel is a PERSISTENT
compositor mode: KWin's keyboard interactive window-move ("Window Move").
It stays in-mode across input-client boundaries; arrow keys nudge the window
live (Window::keyPressEvent, delta 8px, no modifiers); Return commits at the
nudged spot; Escape cancels and RESTORES the pre-move geometry
(finishInteractiveMoveResize(cancel=true) -> moveResize(initialGeometry)).
That restore is the observable cancel effect this recipe asserts. The real
A2/A3 pointer-drag Escape sub-paths (P6/P7) will interleave the key WITHIN
one drag client; this recipe proves the key verb and its cancel effect on
the simplest in-flight drag constructible from the primitives that exist
today, per the C-I10 brief.

TRIPWIRE (HC3 observes-a-rejection). The recipe drives BOTH outcomes and
proves it tells them apart: a Return trial COMMITS (window ends moved) and an
Escape trial CANCELS (window returns to its pre-move spot). A key verb that
silently did nothing leaves the Return trial unmoved (the recipe fails); an
Escape that did not cancel leaves the window moved like a commit (the restore
assertion fails). So a green run requires the cancel to have actually taken.

Ported from tests/e2e/080-key-escape-cancels-move.sh to latte_harness.recipe
(BP-3, the bash-to-python migration's recipe batch). The konsole fixture rides
proc.SessionProcess/terminating (the setsid + trap-EXIT lifecycle as one typed
object, no leaks).
"""

import os
import subprocess
import time

from latte_harness import proc, recipe

#! id|x|y|w|h of the konsole window from the compositor's own truth (KWin
#! updates frameGeometry live during an interactive move, so this reads the
#! in-flight position too). recipe.kwin_js has already stripped the run tag.
_KONSOLE_GEO_JS = (
    'for (const w of workspace.windowList()) {\n'
    '    if (w.resourceClass == "org.kde.konsole") {\n'
    '        print("@TAG@|" + w.internalId + "|" + Math.round(w.frameGeometry.x) + "|" '
    '+ Math.round(w.frameGeometry.y) + "|" + Math.round(w.frameGeometry.width) + "|" '
    '+ Math.round(w.frameGeometry.height));\n'
    '    }\n'
    '}'
)
_ACTIVE_ID_JS = (
    'print("@TAG@|" + (workspace.activeWindow ? workspace.activeWindow.internalId : "none"));'
)

NUDGES = 5
STEP = 8
EXPECT_DY = NUDGES * STEP
TOL = 2  #! rounding only; the move math is integer


def _fp(*args: object) -> None:
    """`"$E2E_FAKEPOINTER" "$@"`: drive the vehicle pointer/keyboard."""
    subprocess.run([os.environ["E2E_FAKEPOINTER"], *(str(a) for a in args)], check=False)


def _konsole_geo() -> list[str]:
    """The last DUMPWIN-style konsole record split into [id, x, y, w, h]."""
    lines = [ln for ln in recipe.kwin_js(_KONSOLE_GEO_JS).splitlines() if ln]
    return lines[-1].split("|") if lines else []


def _active_id() -> str:
    """The active window's internalId, or 'none' (the bash active_id | tail -1)."""
    lines = [ln for ln in recipe.kwin_js(_ACTIVE_ID_JS).splitlines() if ln]
    return lines[-1] if lines else ""


def _konsole_count() -> int:
    """The number of mapped konsole windows (the bash grep -c '|org.kde.konsole|')."""
    return sum(1 for w in recipe.windows() if w.resource_class == "org.kde.konsole")


def _invoke_window_move() -> None:
    """Start KWin's keyboard interactive move on the active window. Component
    "kwin", shortcut unique name "Window Move" (useractions.cpp initShortcut);
    invokeShortcut fires it whether or not a key is bound."""
    subprocess.run(
        [
            "busctl",
            "--user",
            "call",
            "org.kde.kglobalaccel",
            "/component/kwin",
            "org.kde.kglobalaccel.Component",
            "invokeShortcut",
            "s",
            "Window Move",
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )


def _nudge_up() -> None:
    for _ in range(NUDGES):
        _fp("key", "Up")
        time.sleep(0.15)


def main() -> None:
    proc.install_conventional_signal_exits()

    # ---- a normal toplevel to move (the dock is a layer-shell surface, not
    #      interactively movable). konsole is the vehicle's proven client (020).
    if _konsole_count() != 0:
        recipe.fail("konsole already present; this recipe owns its client")
    konsole = proc.SessionProcess.spawn(
        ["konsole"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
    )
    with proc.terminating(konsole):
        for _ in range(20):
            if _konsole_count() >= 1:
                break
            time.sleep(1)
        if _konsole_count() < 1:
            recipe.fail("konsole never mapped in the vehicle")
        time.sleep(2)  #! let it settle to a stable geometry

        geo = _konsole_geo()
        if len(geo) < 5 or not (geo[0] and geo[1] and geo[2]):
            recipe.fail("could not read konsole geometry")
        kid, kx0, ky0, kw, kh = geo[0], int(geo[1]), int(geo[2]), int(geo[3]), int(geo[4])
        print(f"konsole {kid} at {kx0},{ky0} {kw}x{kh}")

        #! slotWindowMove targets the ACTIVE window; a click activates konsole
        _fp("click", kx0 + kw // 2, ky0 + kh // 2)
        time.sleep(0.6)
        if _active_id() != kid:
            recipe.fail(f"konsole did not become the active window (got {_active_id()})")

        # ---- trial 1: COMMIT control (Return). Proves keys reach the move mode
        #      and a commit is observable. Without a working key verb this fails.
        base_y = ky0
        _invoke_window_move()
        time.sleep(0.5)
        _nudge_up()
        geo = _konsole_geo()
        my = int(geo[2])
        moved_dy = base_y - my
        print(f"commit trial: nudged {base_y} -> {my} (dy={moved_dy}, expect ~{EXPECT_DY})")
        if abs(moved_dy - EXPECT_DY) > TOL:
            recipe.fail(
                f"keyboard nudge did not move the window as expected (dy={moved_dy}, "
                f"expected {EXPECT_DY}); the move mode did not start or Up keys were not delivered"
            )
        _fp("key", "Return")
        time.sleep(0.5)
        geo = _konsole_geo()
        cx, cy = int(geo[1]), int(geo[2])
        print(f"commit trial: after Return at {cx},{cy}")
        if abs(cy - my) > TOL:
            recipe.fail(
                f"Return did not commit the move (window at {cy}, expected it to stay near {my})"
            )
        print("COMMIT observed: Return left the window at the nudged position")

        # ---- trial 2: CANCEL (Escape). The window is moved in-flight, then
        #      Escape must restore it to THIS trial's pre-move baseline.
        base_x, base_y = cx, cy
        _invoke_window_move()
        time.sleep(0.5)
        _nudge_up()
        geo = _konsole_geo()
        ix, iy = int(geo[1]), int(geo[2])
        inflight_dy = base_y - iy
        print(f"cancel trial: in-flight nudged {base_y} -> {iy} (dy={inflight_dy})")
        if abs(inflight_dy - EXPECT_DY) > TOL:
            recipe.fail(
                f"cancel trial: the in-flight nudge did not move the window "
                f"(dy={inflight_dy}); nothing to cancel"
            )
        _fp("key", "Escape")
        time.sleep(0.5)
        geo = _konsole_geo()
        ex, ey = int(geo[1]), int(geo[2])
        print(f"cancel trial: after Escape at {ex},{ey} (pre-move baseline was {base_x},{base_y})")
        if abs(ey - base_y) > TOL or abs(ex - base_x) > TOL:
            recipe.fail(
                f"Escape did NOT cancel: window at {ex},{ey}, expected restore to "
                f"{base_x},{base_y} (in-flight moved to {ix},{iy})"
            )

        print(f"CANCEL observed: Escape aborted the in-flight move and restored {base_x},{base_y}")
        print(
            "fakepointer key Escape cancels an in-flight drag "
            "(move committed on Return, aborted+restored on Escape)"
        )


if __name__ == "__main__":
    recipe.run(main)
