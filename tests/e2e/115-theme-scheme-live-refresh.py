#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""D298 guard (docs/tracking/known-defects.md): a RUNTIME color-scheme change
must reach the dock without a restart.

On Plasma 6 kdeglobals always carries [WM] activeBackground (the auto-accent
layout), so SchemeColors::possibleSchemeFile("kdeglobals") resolves to the
CONSTANT ~/.config/kdeglobals path and a scheme change arrives as new CONTENT
at an unchanged path. The bug: Theme::setOriginalSchemeFile()'s path-equality
early return swallowed every watcher-driven refresh, so the default/reversed
scheme snapshots and isLightTheme stayed stale until restart while the
wm-tracker side kept updating - a half-updated dock after a Light<->Dark
switch. The fix routes the kdeglobals branch through the unguarded
Theme::refreshOriginalScheme().

This recipe drives the whole in-dock chain, not the C++ seam (that is
tests/themeextendedrefreshtest.cpp): a LightThemeColors panel (the D21
fixture layout) on a DARK probe kdeglobals resolves the reversed-of-dark
scheme; rewriting kdeglobals IN PLACE to a light probe scheme (keeping the
[WM] auto-accent marker, so the resolved path never changes) must flip
isLightTheme, regenerate the snapshots, and move the colorizer decision from
the reversed snapshot to the refreshed default snapshot. Probe color values
are distinct from any stock scheme, so the exact-hex assertions can only pass
if the snapshot files were actually re-read:

  phase A (dark):  scheme .../reversed.colors, background #c8d2dc (the dark
                   probe's foreground, reversed), text #0a141e
  phase B (light): scheme .../default.colors, background #f0f1f2, text
                   #191a1b - the pre-fix dock stays on phase A forever and
                   the poll below times out red.
"""

import configparser
import contextlib
import io
import json
import os
import subprocess
import sys
import time
from pathlib import Path

from latte_harness import proc, recipe
from latte_harness.config_restore import ConfigHomeSnapshot

DARK_KDEGLOBALS = """[General]
ColorScheme=LatteProbeDark

[WM]
activeBackground=10,20,30
activeForeground=200,210,220
inactiveBackground=12,22,32
inactiveForeground=150,160,170

[Colors:Window]
BackgroundNormal=10,20,30
ForegroundNormal=200,210,220
"""

LIGHT_KDEGLOBALS = """[General]
ColorScheme=LatteProbeLight

[WM]
activeBackground=240,241,242
activeForeground=25,26,27
inactiveBackground=235,236,237
inactiveForeground=90,95,100

[Colors:Window]
BackgroundNormal=240,241,242
ForegroundNormal=25,26,27
"""


def _seed_layout(config_home: Path) -> None:
    config = configparser.RawConfigParser()
    config.optionxform = str  # type: ignore[assignment,method-assign]
    lattedockrc = config_home / "lattedockrc"
    config.read(lattedockrc)
    if not config.has_section("UniversalSettings"):
        config.add_section("UniversalSettings")
    config.set("UniversalSettings", "singleModeLayoutName", "D21")
    config.set("UniversalSettings", "memoryUsage", "0")
    with lattedockrc.open("w") as output:
        config.write(output, space_around_delimiters=False)


def _colorizer(cid: int) -> dict:
    return json.loads(recipe.json_payload("colorizerData", "u", str(cid)))


def _body() -> None:
    repo = Path(os.environ["E2E_REPO"])
    config_home = Path(os.environ["E2E_CONFIG_HOME"])
    fixture = repo / "tests" / "e2e" / "fixtures" / "d21"
    if not (fixture / "D21.layout.latte").is_file():
        recipe.fail(f"D21 fixture layout missing under {fixture}")

    # ---- seed: LightThemeColors panel on the DARK probe scheme --------------
    recipe.dock_stop()
    for stale in (config_home / "latte").glob("*.layout.latte"):
        stale.unlink()
    subprocess.run(
        ["cp", str(fixture / "D21.layout.latte"), str(config_home / "latte" / "D21.layout.latte")],
        check=True,
    )
    (config_home / "kdeglobals").write_text(DARK_KDEGLOBALS)
    _seed_layout(config_home)

    if not recipe.dock_start(90):
        recipe.fail("dock never settled with the D21 fixture layout")

    cid = next((v.containment_id for v in recipe.views() if v.edge in ("top", "bottom")), None)
    if cid is None:
        recipe.fail("no horizontal view came up from the D21 fixture layout")
    assert cid is not None

    # ---- phase A: reversed-of-dark in force ---------------------------------
    phase_a = _colorizer(cid)
    print(f"D298 phase A colorizerData: {phase_a}")
    if not str(phase_a.get("scheme", "")).endswith("reversed.colors"):
        recipe.fail(f"phase A expected the reversed snapshot, got scheme={phase_a.get('scheme')}")
    if phase_a.get("backgroundColor") != "#c8d2dc" or phase_a.get("textColor") != "#0a141e":
        recipe.fail(
            "phase A colors are not the reversed dark probe "
            f"(bg={phase_a.get('backgroundColor')} fg={phase_a.get('textColor')})"
        )
    print("D298 phase A ok: reversed-of-dark probe scheme in force")

    # ---- the runtime flip: new content, unchanged path ----------------------
    # dock_start took well over a second, so this rewrite cannot land in the
    # same wall-clock second KDirWatch recorded for kdeglobals (its stat
    # verification is second-granular and drops same-second rewrites)
    (config_home / "kdeglobals").write_text(LIGHT_KDEGLOBALS)
    print("D298: rewrote kdeglobals to the light probe scheme in place")

    # ---- phase B: the refreshed default snapshot must take over -------------
    deadline = time.monotonic() + 20
    last: dict = {}
    while time.monotonic() < deadline:
        last = _colorizer(cid)
        if last.get("backgroundColor") == "#f0f1f2":
            break
        time.sleep(0.5)
    print(f"D298 phase B colorizerData: {last}")
    if last.get("backgroundColor") != "#f0f1f2" or last.get("textColor") != "#191a1b":
        recipe.fail(
            "runtime scheme change never reached the colorizer (the D298 stale-snapshot "
            f"failure): bg={last.get('backgroundColor')} fg={last.get('textColor')}"
        )
    if not str(last.get("scheme", "")).endswith("default.colors"):
        recipe.fail(
            "colors refreshed but the decision did not move to the default snapshot "
            f"(scheme={last.get('scheme')}); isLightTheme did not flip"
        )
    print("D298 phase B ok: light probe scheme in force without a restart")
    print("PASS: D298 runtime color-scheme change followed live (state readback)")


def _stop_dock_for_cleanup() -> bool:
    """Stop the reused vehicle dock before the restore; True when it is down
    (the 022/034 stop-then-restore order; dock_stop never escalates to KILL,
    and a surviving dock's config flush would overwrite the restored files)."""
    pid = recipe.dock_pid()
    if pid is None:
        return True
    try:
        os.kill(pid, 0)
    except OSError:
        return True
    with contextlib.redirect_stderr(io.StringIO()):
        stopped = recipe.dock_stop()
    if not stopped:
        print(f"FAIL: cleanup could not stop dock pid {pid}", file=sys.stderr, flush=True)
    return stopped


def main() -> None:
    # same shared-config-home contract as 110: this recipe overwrites
    # kdeglobals, edits lattedockrc and swaps the layout set, so every exit
    # path must restore them or the probe scheme strands into the next recipe
    proc.install_conventional_signal_exits()
    config_home = Path(os.environ["E2E_CONFIG_HOME"])
    snapshot = ConfigHomeSnapshot()
    snapshot.snapshot_file(config_home / "kdeglobals")
    snapshot.snapshot_file(config_home / "lattedockrc")
    snapshot.snapshot_dir(config_home / "latte")

    def cleanup(status: int) -> int:
        dock_stopped = _stop_dock_for_cleanup()
        restored = snapshot.restore()
        return recipe.worsen_status_on_cleanup_failure(status, not (dock_stopped and restored))

    recipe.run_with_cleanup(_body, cleanup, install_signal_exits=False)


if __name__ == "__main__":
    main()
