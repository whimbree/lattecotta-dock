#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
"""E2E: the view settings window maps FULLY on-screen on a cold session
(the 1b932ed9 regression: upstream's self-origin exclusion made the
chrome map 99px above the screen top on cold starts). Consumes the
EX-08 ScreenGeometryCalculator path end to end. Triggered through
kglobalaccel ("show view settings") - inside the vehicle KWin itself
provides org.kde.kglobalaccel on the private bus, so the shortcut
registration path is exercised in both modes.

Ported from tests/e2e/settings-window-onscreen.sh to latte_harness.recipe
(BP-3, the bash-to-python migration's recipe batch).
"""

import os
import subprocess
import time

from latte_harness import recipe


def _invoke_settings() -> None:
    subprocess.run(
        [
            "busctl",
            "--user",
            "call",
            "org.kde.kglobalaccel",
            "/component/lattedock",
            "org.kde.kglobalaccel.Component",
            "invokeShortcut",
            "s",
            "show view settings",
        ],
        stdout=subprocess.DEVNULL,
        check=False,
    )


def _config_window() -> recipe.Window | None:
    """The last latte-dock window whose size matches the settings chrome
    (height > 400, 300 < width < 2000), or None (the awk's last-match capture)."""
    found: recipe.Window | None = None
    for w in recipe.windows():
        if "latte-dock" in w.resource_class and w.height > 400 and 300 < w.width < 2000:
            found = w
    return found


def main() -> None:
    _invoke_settings()
    time.sleep(3)
    #! first invoke can race kglobalaccel registration
    if _config_window() is None:
        _invoke_settings()
        time.sleep(2.5)

    #! screen bounds come from the dock's own report (viewsData), not from a
    #! plasmashell window - the vehicle has no plasmashell
    views = recipe.views()
    if not views:
        recipe.fail("no views")
    sx, sy, sw, sh = views[0].screen_geometry

    config = _config_window()
    if config is None:
        result = "NOCONFIG"
    else:
        cx, cy, cw, ch = config.x, config.y, config.width, config.height
        if cx >= sx and cy >= sy and cx + cw <= sx + sw and cy + ch <= sy + sh:
            result = "ONSCREEN"
        else:
            result = (
                f"OFFSCREEN config={cx},{cy} {cw}x{ch} screen={sx},{sy} {sw}x{sh}"
            )

    #! close the settings again: focus-loss click at screen center first (the
    #! Qt5-faithful path), then the deterministic D-Bus close for every view -
    #! in the vehicle there is no other focusable window, so the click alone
    #! cannot be relied on to dismiss the chrome
    subprocess.run(
        [os.environ["E2E_FAKEPOINTER"], "click", str(sx + sw // 2), str(sy + sh // 2)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    time.sleep(1)
    for view in recipe.views():
        recipe.call("setViewEditMode", "ub", str(view.containment_id), "false")
    time.sleep(1)

    if result == "ONSCREEN":
        print("settings window fully on-screen")
        return
    if result == "NOCONFIG":
        recipe.fail("no settings window mapped after two invokes")
    recipe.fail(result)


if __name__ == "__main__":
    recipe.run(main)
