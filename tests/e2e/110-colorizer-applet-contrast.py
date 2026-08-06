#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""D21 guard (docs/tracking/known-defects.md): stock-applet contrast in a Light-colors
panel. On a dark Plasma theme with themeColors=LightThemeColors the colorizer
engages (mustBeShown=true) and resolves a LIGHT scheme, so applet foregrounds
must be DARK against the light panel. The bug: Latte's only recolor path was a
layer-FBO ColorOverlay that never captured the digital clock's
Text.NativeRendering label (it sampled empty -> blank clock) and skipped
overlay-exempt applets like show-desktop (native Breeze-dark icon -> white).
The fix (approach B) pushes the decided scheme into each applet's own
Kirigami.Theme colour group and retires the overlay, so native content renders
with the right contrast directly.

This recipe seeds a Light-colors panel (digital clock + systray + show-desktop)
on a hermetic dark Plasma theme, then asserts the fix TWO ways:
  STATE  - colorizerData resolves a dark foreground against a light background,
           and every applet reports colorizerActive=true / reason="applied"
           (the new observability the fix ships).
  RENDER - the panel actually draws contrasting content (dark glyphs/icons on
           the light panel), not a uniform blank strip.

OBSERVES THE FAILURE, not just the pass: the RENDER assertion is exactly what
fails without the push. Proven by revert-and-watch during development (nested
vehicle, 2026-07-18): with the _wrapper Kirigami.Theme push disabled
(inherit forced true) the same panel rendered UNIFORM light - clock and
show-desktop crops mean 0.994, std 0.000, min 0.988 (invisible native text) -
while with the push it rendered dark glyphs (std 0.126, min 0.125). So a
regression that breaks the push collapses the panel back to uniform and this
recipe goes red on the std/dark-pixel check below.

Ported from tests/e2e/110-colorizer-applet-contrast.sh to latte_harness.recipe
(BP-3, the bash-to-python migration's focus-restoration recipe wave R9). The
colorizerData decision facts and the per-applet colorizerActive/colorizerReason
are not in the typed models, so they are read as raw JSON, the same boundary the
bash python one-liners used; the magick pixel statistics keep the exact crop
spec and fx thresholds.
"""

import configparser
import json
import os
import subprocess
import sys
from pathlib import Path

from latte_harness import proc, recipe


def _fail_raw(message: str) -> None:
    """The bash applet-check python block: print the message to stderr with NO
    ``FAIL:`` prefix (it carries its own ``D21 FAIL:``) and exit 1."""
    print(message, file=sys.stderr, flush=True)
    raise SystemExit(1)


def _view_applets_raw(cid: int) -> list[dict[str, object]]:
    return json.loads(recipe.json_payload("viewAppletsData", "u", str(cid)))


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


def main() -> None:
    proc.install_conventional_signal_exits()
    repo = Path(os.environ["E2E_REPO"])
    config_home = Path(os.environ["E2E_CONFIG_HOME"])
    artifacts = Path(os.environ["E2E_ARTIFACTS"])
    fixture = repo / "tests" / "e2e" / "fixtures" / "d21"
    if not ((fixture / "D21.layout.latte").is_file() and (fixture / "kdeglobals").is_file()):
        recipe.fail(f"D21 fixture missing under {fixture}")

    # ---- seed the Light-colors panel on a dark Plasma theme -----------------
    # self-seeding so the recipe reproduces its own state in any run-e2e
    # invocation: the dark kdeglobals makes LightThemeColors a real (non-plasma)
    # scheme, which is what engages the colorizer; the layout carries the three
    # stock applets.
    recipe.dock_stop()
    for stale in (config_home / "latte").glob("*.layout.latte"):
        stale.unlink()
    subprocess.run(
        ["cp", str(fixture / "D21.layout.latte"), str(config_home / "latte" / "D21.layout.latte")],
        check=True,
    )
    subprocess.run(["cp", str(fixture / "kdeglobals"), str(config_home / "kdeglobals")], check=True)
    _seed_layout(config_home)

    if not recipe.dock_start(90):
        recipe.fail("dock never settled with the D21 fixture")

    cid = next((v.containment_id for v in recipe.views() if v.edge in ("top", "bottom")), None)
    if cid is None:
        recipe.fail("no horizontal view came up from the D21 fixture")
    assert cid is not None
    print(f"D21: horizontal view is containment {cid}")

    # ---- STATE: colorizer engaged, resolved foreground contrasts background --
    colorizer_raw = recipe.json_payload("colorizerData", "u", str(cid))
    print(f"D21 colorizerData: {colorizer_raw}")
    colorizer = json.loads(colorizer_raw)
    must_be_shown = str(colorizer["mustBeShown"]).lower()
    fg_b = colorizer.get("applyColorBrightness", -1)
    bg_b = colorizer.get("backgroundColorBrightness", -1)
    assert isinstance(fg_b, (int, float))
    assert isinstance(bg_b, (int, float))
    if must_be_shown != "true":
        recipe.fail(
            f"colorizer not engaged (mustBeShown={must_be_shown}); "
            "Light mode should engage on a dark theme"
        )
    # LightThemeColors on a dark theme -> dark foreground on a light background
    contrast = abs(bg_b - fg_b)
    if not (bg_b - fg_b) > 100:
        recipe.fail(
            f"resolved foreground/background do not contrast (fg={fg_b} bg={bg_b}, "
            f"diff={contrast}); expected a dark fg on a light bg"
        )
    print(f"D21 STATE ok: foreground brightness {fg_b} vs background {bg_b} (contrast {contrast})")

    # ---- STATE: every stock applet gets the scheme pushed into its colour group
    applets = _view_applets_raw(cid)
    want = {
        "org.kde.plasma.digitalclock",
        "org.kde.plasma.showdesktop",
        "org.kde.plasma.systemtray",
    }
    seen: dict[str, tuple[object, object]] = {}
    bad: list[tuple[str, object, object]] = []
    for applet in applets:
        plugin = applet["plugin"]
        assert isinstance(plugin, str)
        seen[plugin] = (applet.get("colorizerActive"), applet.get("colorizerReason"))
        if plugin in want and not (
            applet.get("colorizerActive") is True and applet.get("colorizerReason") == "applied"
        ):
            bad.append((plugin, applet.get("colorizerActive"), applet.get("colorizerReason")))
    missing = [p for p in want if p not in seen]
    if missing:
        _fail_raw(f"D21 FAIL: fixture applets missing from the view: {missing}")
    if bad:
        _fail_raw(f"D21 FAIL: applets not colorized as 'applied': {bad}")
    print("D21 STATE ok: clock, systray, show-desktop all colorizerActive=true reason=applied")

    # ---- RENDER: the panel actually draws contrasting content, not a blank strip
    shot = artifacts / "d21-contrast.png"
    try:
        recipe.screenshot(str(shot), "include-cursor", "b", "false")
    except recipe.RecipeError:
        recipe.fail("screenshot failed")
    # crop the whole panel strip (view absoluteGeometry), robust to the per-applet
    # justify offset: whatever renders lands inside this rect
    view = next(v for v in recipe.views() if v.containment_id == cid)
    px, py, pw, ph = view.absolute_geometry
    stats = subprocess.run(
        [
            "magick",
            str(shot),
            "-crop",
            f"{pw}x{ph}+{px}+{py}",
            "+repage",
            "-format",
            "%[fx:minima] %[fx:maxima] %[fx:standard_deviation]",
            "info:",
        ],
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    mn, mx, sd = stats.split()
    print(f"D21 RENDER panel crop {pw}x{ph}+{px}+{py}: min={mn} max={mx} std={sd}")
    # uniform blank (the pre-fix failure) has std~0 and no dark pixels; a correctly
    # coloured panel has a light background (max high) AND dark foreground (min low)
    if not (float(mx) > 0.85 and float(mn) < 0.45 and float(sd) > 0.02):
        recipe.fail(
            f"panel renders without contrast (min={mn} max={mx} std={sd}); the light-scheme "
            "applet foregrounds are not visible - the D21 blank-panel failure"
        )
    print("D21 RENDER ok: panel has a light background and dark foreground content (not blank)")

    print("PASS: D21 Light-colors applet contrast (state + render)")


if __name__ == "__main__":
    recipe.run(main)
