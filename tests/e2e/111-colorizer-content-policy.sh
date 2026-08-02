#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# D28 (obsolete whole-applet colorfulness veto): palette propagation changes
# inherited Kirigami.Theme roles. It does not recolor fixed image, SVG, or
# Rectangle pixels, so a colorful fixed region must not veto palette response
# elsewhere in the same applet.
#
# Four deterministic applets isolate the policy:
# - responsive-only draws Kirigami.Theme.textColor;
# - fixed-only draws the literal #d62976;
# - mixed draws both controls side by side;
# - inline-mixed draws the same pair from an inline full representation.
#
# The same applets are captured first with PlasmaThemeColors disengaged, then
# with LightThemeColors applied. Per-control raw RGBA crops must show responsive
# pixels changing to the treatment palette while every fixed crop stays
# byte-identical across states. Literal-color checks remain as independent
# non-vacuity evidence. Sustained treatment sampling spans the retired probe's
# retry interval, so restoring the old asynchronous veto cannot pass during its
# initial unknown state.
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"

fixture="$E2E_REPO/tests/e2e/fixtures/d28"
theme="$E2E_REPO/tests/e2e/fixtures/d21/kdeglobals"
plugins=(
    org.kde.latte.d28-responsive
    org.kde.latte.d28-fixed
    org.kde.latte.d28-mixed
    org.kde.latte.d28-inline-mixed
)

[[ -f "$fixture/D28.layout.latte" && -f "$theme" ]] \
    || e2e_fail "D28 layout or hermetic color scheme fixture is missing"
for plugin in "${plugins[@]}"; do
    [[ -f "$fixture/plasmoids/$plugin/metadata.json" \
        && -f "$fixture/plasmoids/$plugin/contents/ui/main.qml" ]] \
        || e2e_fail "D28 applet fixture is incomplete: $plugin"
done

# Install test-only packages into the nested process's private data home.
e2e_dock_stop || e2e_fail "could not stop the vehicle dock before staging D28"
export XDG_DATA_HOME="$E2E_RT/d28-data"
rm -rf "$XDG_DATA_HOME"
mkdir -p "$XDG_DATA_HOME/plasma/plasmoids"
cp -r "$fixture/plasmoids/." "$XDG_DATA_HOME/plasma/plasmoids/"
cp "$theme" "$E2E_CONFIG_HOME/kdeglobals"

python3 - "$E2E_CONFIG_HOME/lattedockrc" <<'PY'
import configparser
import sys

path = sys.argv[1]
config = configparser.RawConfigParser()
config.optionxform = str
config.read(path)
if not config.has_section("UniversalSettings"):
    config.add_section("UniversalSettings")
config.set("UniversalSettings", "singleModeLayoutName", "D28")
config.set("UniversalSettings", "memoryUsage", "0")
with open(path, "w") as output:
    config.write(output, space_around_delimiters=False)
PY

stage_fixture_layout() {
    local palette="$1" destination="$E2E_CONFIG_HOME/latte/D28.layout.latte"
    rm -f "$E2E_CONFIG_HOME"/latte/*.layout.latte
    cp "$fixture/D28.layout.latte" "$destination"
    python3 - "$destination" "$palette" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
palette = sys.argv[2]
text = path.read_text()
source = "themeColors=LightThemeColors"
if text.count(source) != 1:
    sys.exit("D28 fixture must contain exactly one LightThemeColors source line")
path.write_text(text.replace(source, "themeColors=" + palette))
PY
}

horizontal_view_id() {
    e2e_json viewsData | python3 -c 'import json,sys
views=[view for view in json.load(sys.stdin) if view["edge"] in ("top", "bottom")]
print(views[0]["containmentId"] if views else "")'
}

resolved_palette() {
    local cid="$1" shown="$2" mode="$3" field="$4" colorizer
    colorizer="$(e2e_json colorizerData u "$cid")"
    python3 - "$colorizer" "$shown" "$mode" "$field" <<'PY'
import json
import sys

colorizer = json.loads(sys.argv[1])
expected_shown = sys.argv[2] == "true"
expected_mode = sys.argv[3]
field = sys.argv[4]
if colorizer.get("mustBeShown") is not expected_shown:
    sys.exit("D28 mustBeShown=%r, expected %r" % (colorizer.get("mustBeShown"), expected_shown))
if colorizer.get("themeColorsMode") != expected_mode:
    sys.exit("D28 themeColorsMode=%r, expected %r" % (colorizer.get("themeColorsMode"), expected_mode))
color = colorizer.get(field, "")
if len(color) != 7 or not color.startswith("#"):
    sys.exit("D28 colorizer has no resolved %s" % field)
print(color)
PY
}

assert_fixture_state() {
    local cid="$1" active="$2" reason="$3" samples="$4" sample applets
    for ((sample = 1; sample <= samples; sample++)); do
        applets="$(e2e_json viewAppletsData u "$cid")"
        python3 - "$applets" "$active" "$reason" "${plugins[@]}" <<'PY' \
            || e2e_fail "fixture applet state diverged from active=$active reason=$reason (sample $sample)"
import json
import sys

applets = {applet["plugin"]: applet for applet in json.loads(sys.argv[1])}
expected_active = sys.argv[2] == "true"
expected_reason = sys.argv[3]
expected_plugins = sys.argv[4:]
missing = [plugin for plugin in expected_plugins if plugin not in applets]
bad = [
    (plugin, applets[plugin].get("colorizerActive"), applets[plugin].get("colorizerReason"))
    for plugin in expected_plugins
    if plugin in applets
    and not (
        applets[plugin].get("colorizerActive") is expected_active
        and applets[plugin].get("colorizerReason") == expected_reason
    )
]
if missing or bad:
    print("D28 state failure: missing=%s bad=%s" % (missing, bad), file=sys.stderr)
    sys.exit(1)
PY
        (( sample < samples )) && sleep 1
    done
}

crop_path() {
    printf '%s/d28-%s-%s.png' "$E2E_ARTIFACTS" "$1" "$2"
}

raw_crop_path() {
    printf '%s/d28-%s-%s.rgba' "$E2E_ARTIFACTS" "$1" "$2"
}

capture_controls() {
    local state="$1" cid="$2" shot applets views crop_specs label rect image raw
    e2e_assert_geometry_agrees 2 \
        || e2e_fail "D28 $state control crops cannot trust view geometry"
    shot="$E2E_ARTIFACTS/d28-$state-content-policy.png"
    e2e_screenshot "$shot" include-cursor b false \
        || e2e_fail "D28 $state screenshot failed"
    applets="$(e2e_json viewAppletsData u "$cid")"
    views="$(e2e_json viewsData)"
    crop_specs="$(python3 - "$cid" "$views" "$applets" <<'PY'
import json
import sys

containment_id = int(sys.argv[1])
views = json.loads(sys.argv[2])
applets = {applet["plugin"]: applet for applet in json.loads(sys.argv[3])}
view = next(view for view in views if view["containmentId"] == containment_id)
origin_x = view["absoluteGeometry"][0] - view["localGeometry"][0]
origin_y = view["absoluteGeometry"][1] - view["localGeometry"][1]

def center(plugin, offset=0):
    x, y, width, height = applets[plugin]["geometry"]
    return origin_x + x + width // 2 + offset, origin_y + y + height // 2

def emit(label, plugin, offset=0):
    center_x, center_y = center(plugin, offset)
    print("%s 12x12+%d+%d" % (label, center_x - 6, center_y - 6))

emit("responsive", "org.kde.latte.d28-responsive")
emit("fixed", "org.kde.latte.d28-fixed")
emit("mixed-responsive", "org.kde.latte.d28-mixed", -18)
emit("mixed-fixed", "org.kde.latte.d28-mixed", 18)
emit("inline-responsive", "org.kde.latte.d28-inline-mixed", -18)
emit("inline-fixed", "org.kde.latte.d28-inline-mixed", 18)
PY
)" || e2e_fail "could not resolve D28 $state per-control crop geometry"

    while read -r label rect; do
        image="$(crop_path "$state" "$label")"
        raw="$(raw_crop_path "$state" "$label")"
        magick "$shot" -crop "$rect" +repage "$image" \
            || e2e_fail "could not crop D28 $state $label control at $rect"
        magick "$image" -depth 8 "rgba:$raw" \
            || e2e_fail "could not serialize D28 $state $label RGBA bytes"
        echo "D28 $state crop $label: $rect"
    done <<< "$crop_specs"
}

assert_solid_rgba() {
    local state="$1" label="$2" expected="$3" image pixels
    image="$(crop_path "$state" "$label")"
    pixels="$(magick "$image" -depth 8 txt:-)" \
        || e2e_fail "could not read D28 $state $label crop pixels"
    python3 - "$state-$label" "$expected" "$pixels" <<'PY'
import re
import sys

label, expected_hex = sys.argv[1:3]
expected = tuple(bytes.fromhex(expected_hex.removeprefix("#"))) + (255,)
pixels = []
for line in sys.argv[3].splitlines():
    match = re.search(r"\s#([0-9a-fA-F]{6})([0-9a-fA-F]{2})?\s", line)
    if match:
        rgba = tuple(bytes.fromhex(match.group(1))) + (
            int(match.group(2), 16) if match.group(2) else 255,
        )
        pixels.append(rgba)
if len(pixels) != 144:
    sys.exit("D28 %s crop yielded %d pixels, expected 144" % (label, len(pixels)))
mismatches = [pixel for pixel in pixels if pixel != expected]
if mismatches:
    observed = sorted(set(mismatches))[:8]
    sys.exit(
        "D28 %s pixels differ from %s: %d/144 mismatches, observed %s"
        % (label, expected, len(mismatches), observed)
    )
print("D28 RENDER ok: %s is byte-exact %s" % (label, expected_hex))
PY
}

assert_crops_equal() {
    local first_state="$1" second_state="$2" label="$3"
    cmp "$(raw_crop_path "$first_state" "$label")" "$(raw_crop_path "$second_state" "$label")" >/dev/null \
        || e2e_fail "D28 $label bytes changed between $first_state and $second_state"
    echo "D28 CROSS-STATE ok: $label bytes are identical"
}

assert_crops_differ() {
    local first_state="$1" second_state="$2" label="$3"
    if cmp "$(raw_crop_path "$first_state" "$label")" "$(raw_crop_path "$second_state" "$label")" >/dev/null; then
        e2e_fail "D28 $label bytes did not change between $first_state and $second_state"
    fi
    echo "D28 CROSS-STATE ok: $label bytes changed"
}

# CONTROL: the Plasma palette is inherited normally and the Latte colorizer is
# genuinely disengaged. These pixels are the before-image for the treatment.
stage_fixture_layout PlasmaThemeColors
e2e_dock_start 90 || e2e_fail "dock never settled with the D28 control fixture"
control_cid="$(horizontal_view_id)"
[[ -n "$control_cid" ]] || e2e_fail "no horizontal D28 control view came up"
control_color="$(resolved_palette "$control_cid" false plasma textColor)" \
    || e2e_fail "could not resolve the disengaged D28 control palette"
assert_fixture_state "$control_cid" false notEngaged 1
echo "D28 CONTROL state: fixtures are inactive with reason=notEngaged"
capture_controls control "$control_cid"
assert_solid_rgba control responsive "$control_color" \
    || e2e_fail "control responsive-only content does not match its inherited palette"
assert_solid_rgba control mixed-responsive "$control_color" \
    || e2e_fail "control mixed responsive content does not match its inherited palette"
assert_solid_rgba control fixed "#d62976" \
    || e2e_fail "control fixed-only content differs from its literal color"
assert_solid_rgba control mixed-fixed "#d62976" \
    || e2e_fail "control mixed fixed content differs from its literal color"
assert_solid_rgba control inline-responsive "$control_color" \
    || e2e_fail "control inline responsive content does not match its inherited palette"
assert_solid_rgba control inline-fixed "#d62976" \
    || e2e_fail "control inline fixed content differs from its literal color"
e2e_dock_stop || e2e_fail "could not stop the D28 control dock"

# TREATMENT: LightThemeColors engages Latte's palette push. The removed probe
# retried every two seconds, so six one-second applied samples ensure restoring
# the old veto fails after its initial unknown state.
stage_fixture_layout LightThemeColors
e2e_dock_start 90 || e2e_fail "dock never settled with the D28 treatment fixture"
treatment_cid="$(horizontal_view_id)"
[[ -n "$treatment_cid" ]] || e2e_fail "no horizontal D28 treatment view came up"
treatment_color="$(resolved_palette "$treatment_cid" true light applyColor)" \
    || e2e_fail "could not resolve the applied D28 treatment palette"
[[ "$control_color" != "$treatment_color" ]] \
    || e2e_fail "D28 control and treatment palettes are identical ($control_color)"
assert_fixture_state "$treatment_cid" true applied 6
echo "D28 TREATMENT state: fixtures stayed active with reason=applied"
capture_controls treatment "$treatment_cid"
assert_solid_rgba treatment responsive "$treatment_color" \
    || e2e_fail "treatment responsive-only content did not follow the panel palette"
assert_solid_rgba treatment mixed-responsive "$treatment_color" \
    || e2e_fail "treatment mixed responsive content did not follow the panel palette"
assert_solid_rgba treatment fixed "#d62976" \
    || e2e_fail "treatment fixed-only content differs from its literal color"
assert_solid_rgba treatment mixed-fixed "#d62976" \
    || e2e_fail "treatment mixed fixed content differs from its literal color"
assert_solid_rgba treatment inline-responsive "$treatment_color" \
    || e2e_fail "treatment inline responsive content did not follow the panel palette"
assert_solid_rgba treatment inline-fixed "#d62976" \
    || e2e_fail "treatment inline fixed content differs from its literal color"

assert_crops_differ control treatment responsive
assert_crops_equal control treatment fixed
assert_crops_differ control treatment mixed-responsive
assert_crops_equal control treatment mixed-fixed
echo "D28 MIXED ok: responsive bytes changed while fixed bytes stayed identical"
assert_crops_differ control treatment inline-responsive
assert_crops_equal control treatment inline-fixed
echo "D28 INLINE ok: full-representation roles changed while fixed bytes stayed identical"

echo "PASS: D28 control/treatment palette response and fixed-pixel stability"
