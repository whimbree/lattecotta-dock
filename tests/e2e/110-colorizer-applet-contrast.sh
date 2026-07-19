#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# D21 guard (docs/tracking/known-defects.md): stock-applet contrast in a Light-colors
# panel. On a dark Plasma theme with themeColors=LightThemeColors the colorizer
# engages (mustBeShown=true) and resolves a LIGHT scheme, so applet foregrounds
# must be DARK against the light panel. The bug: Latte's only recolor path was a
# layer-FBO ColorOverlay that never captured the digital clock's
# Text.NativeRendering label (it sampled empty -> blank clock) and skipped
# overlay-exempt applets like show-desktop (native Breeze-dark icon -> white).
# The fix (approach B) pushes the decided scheme into each applet's own
# Kirigami.Theme colour group and retires the overlay, so native content renders
# with the right contrast directly.
#
# This recipe seeds a Light-colors panel (digital clock + systray + show-desktop)
# on a hermetic dark Plasma theme, then asserts the fix TWO ways:
#   STATE  - colorizerData resolves a dark foreground against a light background,
#            and every applet reports colorizerActive=true / reason="applied"
#            (the new observability the fix ships).
#   RENDER - the panel actually draws contrasting content (dark glyphs/icons on
#            the light panel), not a uniform blank strip.
#
# OBSERVES THE FAILURE, not just the pass: the RENDER assertion is exactly what
# fails without the push. Proven by revert-and-watch during development (nested
# vehicle, 2026-07-18): with the _wrapper Kirigami.Theme push disabled
# (inherit forced true) the same panel rendered UNIFORM light - clock and
# show-desktop crops mean 0.994, std 0.000, min 0.988 (invisible native text) -
# while with the push it rendered dark glyphs (std 0.126, min 0.125). So a
# regression that breaks the push collapses the panel back to uniform and this
# recipe goes red on the std/dark-pixel check below.
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"

fixture="$E2E_REPO/tests/e2e/fixtures/d21"
[[ -f "$fixture/D21.layout.latte" && -f "$fixture/kdeglobals" ]] \
    || e2e_fail "D21 fixture missing under $fixture"

# ---- seed the Light-colors panel on a dark Plasma theme ---------------------
# self-seeding so the recipe reproduces its own state in any run-e2e invocation:
# the dark kdeglobals makes LightThemeColors a real (non-plasma) scheme, which is
# what engages the colorizer; the layout carries the three stock applets.
e2e_dock_stop || true
rm -f "$E2E_CONFIG_HOME"/latte/*.layout.latte
cp "$fixture/D21.layout.latte" "$E2E_CONFIG_HOME/latte/D21.layout.latte"
cp "$fixture/kdeglobals" "$E2E_CONFIG_HOME/kdeglobals"
# single-layout mode pointed at the fixture
python3 - "$E2E_CONFIG_HOME/lattedockrc" <<'PY'
import sys, configparser
p = sys.argv[1]
c = configparser.RawConfigParser()
c.optionxform = str
c.read(p)
if not c.has_section("UniversalSettings"):
    c.add_section("UniversalSettings")
c.set("UniversalSettings", "singleModeLayoutName", "D21")
c.set("UniversalSettings", "memoryUsage", "0")
with open(p, "w") as f:
    c.write(f, space_around_delimiters=False)
PY

e2e_dock_start 90 || e2e_fail "dock never settled with the D21 fixture"

cid="$(e2e_json viewsData | python3 -c 'import json,sys
vs=[v for v in json.load(sys.stdin) if v["edge"] in ("top","bottom")]
print(vs[0]["containmentId"] if vs else "")')"
[[ -n "$cid" ]] || e2e_fail "no horizontal view came up from the D21 fixture"
echo "D21: horizontal view is containment $cid"

# ---- STATE: colorizer engaged, resolved foreground contrasts background ------
colorizer="$(e2e_json colorizerData u "$cid")"
echo "D21 colorizerData: $colorizer"
read -r mustBeShown fgB bgB <<<"$(python3 -c "
import json,sys
d=json.loads('''$colorizer''')
print(str(d['mustBeShown']).lower(), d.get('applyColorBrightness',-1), d.get('backgroundColorBrightness',-1))
")"
[[ "$mustBeShown" == "true" ]] || e2e_fail "colorizer not engaged (mustBeShown=$mustBeShown); Light mode should engage on a dark theme"
# LightThemeColors on a dark theme -> dark foreground on a light background
contrast="$(python3 -c "print(abs($bgB - $fgB))")"
python3 -c "import sys; sys.exit(0 if ($bgB - $fgB) > 100 else 1)" \
    || e2e_fail "resolved foreground/background do not contrast (fg=$fgB bg=$bgB, diff=$contrast); expected a dark fg on a light bg"
echo "D21 STATE ok: foreground brightness $fgB vs background $bgB (contrast $contrast)"

# ---- STATE: every stock applet gets the scheme pushed into its colour group --
applets="$(e2e_json viewAppletsData u "$cid")"
python3 - "$applets" <<'PY' || exit 1
import json, sys
applets = json.loads(sys.argv[1])
want = {"org.kde.plasma.digitalclock", "org.kde.plasma.showdesktop", "org.kde.plasma.systemtray"}
seen = {}
bad = []
for a in applets:
    seen[a["plugin"]] = (a.get("colorizerActive"), a.get("colorizerReason"))
    if a["plugin"] in want and not (a.get("colorizerActive") is True and a.get("colorizerReason") == "applied"):
        bad.append((a["plugin"], a.get("colorizerActive"), a.get("colorizerReason")))
missing = [p for p in want if p not in seen]
if missing:
    print("D21 FAIL: fixture applets missing from the view:", missing, file=sys.stderr); sys.exit(1)
if bad:
    print("D21 FAIL: applets not colorized as 'applied':", bad, file=sys.stderr); sys.exit(1)
print("D21 STATE ok: clock, systray, show-desktop all colorizerActive=true reason=applied")
PY

# ---- RENDER: the panel actually draws contrasting content, not a blank strip -
shot="$E2E_ARTIFACTS/d21-contrast.png"
e2e_screenshot "$shot" include-cursor b false || e2e_fail "screenshot failed"
# crop the whole panel strip (view absoluteGeometry), robust to the per-applet
# justify offset: whatever renders lands inside this rect
read -r px py pw ph <<<"$(e2e_json viewsData | python3 -c "import json,sys
v=[x for x in json.load(sys.stdin) if x['containmentId']==$cid][0]
print(*v['absoluteGeometry'])")"
read -r mn mx sd <<<"$(magick "$shot" -crop "${pw}x${ph}+${px}+${py}" +repage \
    -format "%[fx:minima] %[fx:maxima] %[fx:standard_deviation]" info:)"
echo "D21 RENDER panel crop ${pw}x${ph}+${px}+${py}: min=$mn max=$mx std=$sd"
# uniform blank (the pre-fix failure) has std~0 and no dark pixels; a correctly
# coloured panel has a light background (max high) AND dark foreground (min low)
python3 -c "import sys; sys.exit(0 if ($mx > 0.85 and $mn < 0.45 and $sd > 0.02) else 1)" \
    || e2e_fail "panel renders without contrast (min=$mn max=$mx std=$sd); the light-scheme applet foregrounds are not visible - the D21 blank-panel failure"
echo "D21 RENDER ok: panel has a light background and dark foreground content (not blank)"

echo "PASS: D21 Light-colors applet contrast (state + render)"
