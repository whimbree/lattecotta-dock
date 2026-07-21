#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
# e2e-expect: status 57
#
# SC-CW1 (the D57 ConfigOverlay wheel-threshold reproduction): drive a real
# Latte-style applet through ConfigOverlay.onWheel and observe the independent
# applet geometry readback. The production handler divides angleDelta.y by 8,
# increases above +12, and should decrease below -12. A standard 120-unit
# detent is therefore 15 degrees; the exact strict boundary is 96 units.
#
# The current inherited decrease comparison is `angle < 12`, so delivered
# -96, +/-90, and horizontal events are expected to expose D57 by shrinking
# the applet. The recipe states the correct threshold expectations and remains
# XFAIL only when all of those effects occur. The separate axisstop control
# asks KWin for wl_pointer.axis_stop; Qt emits no QWheelEvent in this isolated
# sequence, so it does not claim ConfigOverlay accepts a delivered (0,0).
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"
source "$E2E_REPO/tests/e2e/matrix/applet-reorder-driver.sh"

fixture="$E2E_REPO/tests/e2e/fixtures/sc-cw1"
plugin="org.kde.latte.separator"
fixture_data="$E2E_RT/sc-cw1-data"
backup="$(mktemp -d)" || e2e_fail "could not allocate the SC-CW1 config backup"
backup_ready=false
recipe_finalized=false

cleanup() {
    local body_status=$? cleanup_failed=0 pid=""
    trap - EXIT
    if [[ "$recipe_finalized" != true && "$backup_ready" == true ]]; then
        pid="$(e2e_dock_pid 2>/dev/null)"
        if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
            if ! e2e_dock_stop >/dev/null 2>&1; then
                echo "FAIL: cleanup could not stop dock pid $pid" >&2
                cleanup_failed=1
            fi
        fi
        if ! rm -rf "$E2E_CONFIG_HOME" \
            || ! mkdir -p "$E2E_CONFIG_HOME" \
            || ! cp -a "$backup/config/." "$E2E_CONFIG_HOME/" \
            || ! diff -qr "$backup/config" "$E2E_CONFIG_HOME" >/dev/null; then
            echo "FAIL: cleanup could not restore the original config byte-for-byte" >&2
            cleanup_failed=1
        fi
        if ! rm -rf "$fixture_data" || [[ -e "$fixture_data" ]]; then
            echo "FAIL: cleanup could not remove fixture data $fixture_data" >&2
            cleanup_failed=1
        fi
    fi
    if ! rm -rf "$backup" || [[ -e "$backup" ]]; then
        echo "FAIL: cleanup could not remove backup $backup" >&2
        cleanup_failed=1
    fi
    if (( cleanup_failed != 0 )); then
        echo "FAIL: SC-CW1 recipe cleanup left residue" >&2
        body_status=1
    fi
    exit "$body_status"
}
trap cleanup EXIT

[[ -f "$fixture/SC-CW1.layout.latte" \
    && -f "$fixture/plasmoids/$plugin/metadata.json" \
    && -f "$fixture/plasmoids/$plugin/contents/ui/main.qml" ]] \
    || e2e_fail "SC-CW1 ConfigOverlay fixture is incomplete"

mkdir -p "$backup/config" || e2e_fail "could not create the SC-CW1 config backup directory"
cp -a "$E2E_CONFIG_HOME/." "$backup/config/" || e2e_fail "could not back up the original config"
backup_ready=true

expect_invalid_wheel() {
    local value="$1" output status
    output="$("$E2E_FAKEPOINTER" wheel 0 0 "$value" 2>&1)"
    status=$?
    (( status == 2 )) \
        || e2e_fail "fakepointer accepted invalid wheel delta '$value' or returned wrong status $status: $output"
    [[ "$output" == *"invalid angle-delta"* ]] \
        || e2e_fail "fakepointer rejected '$value' without the angle-delta diagnostic: $output"
}

for invalid_delta in 0 1.5 nan 100663297 2147483648; do
    expect_invalid_wheel "$invalid_delta"
done

e2e_dock_stop || e2e_fail "could not stop the vehicle dock before staging SC-CW1"
export XDG_DATA_HOME="$fixture_data"
mkdir -p "$XDG_DATA_HOME/plasma/plasmoids" || e2e_fail "could not create the fixture data tree"
cp -a "$fixture/plasmoids/." "$XDG_DATA_HOME/plasma/plasmoids/" \
    || e2e_fail "could not stage the SC-CW1 applet fixture"

python3 - "$E2E_CONFIG_HOME/lattedockrc" <<'PY' \
    || e2e_fail "could not select the SC-CW1 fixture layout"
import configparser
import sys

path = sys.argv[1]
config = configparser.RawConfigParser()
config.optionxform = str
config.read(path)
if not config.has_section("UniversalSettings"):
    config.add_section("UniversalSettings")
config.set("UniversalSettings", "singleModeLayoutName", "SC-CW1")
config.set("UniversalSettings", "memoryUsage", "0")
with open(path, "w") as output:
    config.write(output, space_around_delimiters=False)
PY

spec_failures=0
unexpected_effects=0
target_x=0
target_y=0
park_x=0
park_y=0

stage_axis() {
    local axis="$1" destination="$E2E_CONFIG_HOME/latte/SC-CW1.layout.latte" formfactor location
    rm -f "$E2E_CONFIG_HOME"/latte/*.layout.latte || e2e_fail "$axis: could not clear the prior layout"
    cp "$fixture/SC-CW1.layout.latte" "$destination" || e2e_fail "$axis: could not stage the fixture layout"
    if [[ "$axis" == horizontal ]]; then
        formfactor=2; location=3
    else
        formfactor=3; location=5
    fi
    kwriteconfig6 --file "$destination" --group Containments --group 1 --key formfactor "$formfactor" \
        || e2e_fail "$axis: could not set the fixture form factor"
    kwriteconfig6 --file "$destination" --group Containments --group 1 --key location "$location" \
        || e2e_fail "$axis: could not set the fixture edge"
}

fixture_view() {
    local axis="$1"
    e2e_json viewsData | python3 -c "
import json, sys
edges = ('top', 'bottom') if '$axis' == 'horizontal' else ('left', 'right')
views = [v for v in json.load(sys.stdin) if v['edge'] in edges]
assert len(views) == 1, 'expected one $axis fixture view, got %r' % views
print(views[0]['containmentId'])
"
}

applet_length() {
    local view="$1" axis="$2"
    e2e_json viewAppletsData u "$view" | python3 -c "
import json, sys
applets = [a for a in json.load(sys.stdin) if a['plugin'] == '$plugin']
assert len(applets) == 1, 'expected one $plugin applet, got %r' % applets
print(applets[0]['geometry'][2 if '$axis' == 'horizontal' else 3])
"
}

resolve_points() {
    local view="$1" axis="$2" views applets points
    views="$(e2e_json viewsData)" || e2e_fail "$axis: viewsData query failed"
    applets="$(e2e_json viewAppletsData u "$view")" \
        || e2e_fail "$axis: viewAppletsData query failed"
    points="$({
        echo "$view $axis"
        echo "$views"
        echo "$applets"
    } | python3 -c "
import json, sys
view_id, axis = sys.stdin.readline().split()
views = json.loads(sys.stdin.readline())
applets = json.loads(sys.stdin.readline())
view = next(v for v in views if v['containmentId'] == int(view_id))
applet = next(a for a in applets if a['plugin'] == '$plugin')
origin_x = view['absoluteGeometry'][0] - view['localGeometry'][0]
origin_y = view['absoluteGeometry'][1] - view['localGeometry'][1]
x, y, width, height = applet['geometry']
target_x = round(origin_x + x + width / 2)
target_y = round(origin_y + y + height / 2)
if axis == 'horizontal':
    park_x, park_y = target_x, view['screenGeometry'][1] + view['screenGeometry'][3] // 2
else:
    park_x, park_y = view['screenGeometry'][0] + view['screenGeometry'][2] // 2, target_y
print(target_x, target_y, park_x, park_y)
")" || e2e_fail "$axis: could not resolve the live ConfigOverlay target"
    read -r target_x target_y park_x park_y <<< "$points" \
        || e2e_fail "$axis: could not parse the live ConfigOverlay target"
}

arm_target() {
    local view="$1" axis="$2"
    resolve_points "$view" "$axis"
    "$E2E_FAKEPOINTER" move "$park_x" "$park_y" || e2e_fail "$axis: pointer park move failed"
    sleep 0.2
    "$E2E_FAKEPOINTER" glide "$park_x" "$park_y" "$target_x" "$target_y" || e2e_fail "$axis: pointer target glide failed"
    sleep 0.4
}

record_case() {
    local view="$1" view_axis="$2" label="$3" delta="$4" wheel_axis="$5"
    local spec_delta="$6" inherited_delta="$7" before after observed attempt poll
    before="$(applet_length "$view" "$view_axis")" \
        || e2e_fail "$view_axis $label: initial applet-length query failed"
    observed=0
    for attempt in 1 2 3; do
        arm_target "$view" "$view_axis"
        case "$wheel_axis" in
            horizontal) "$E2E_FAKEPOINTER" wheel "$target_x" "$target_y" "$delta" horizontal \
                || e2e_fail "$view_axis $label: horizontal wheel injection failed";;
            axisstop) "$E2E_FAKEPOINTER" axisstop "$target_x" "$target_y" \
                || e2e_fail "$view_axis $label: axis-stop injection failed";;
            *) "$E2E_FAKEPOINTER" wheel "$target_x" "$target_y" "$delta" \
                || e2e_fail "$view_axis $label: vertical wheel injection failed";;
        esac
        for poll in 1 2 3 4 5 6; do
            sleep 0.2
            after="$(applet_length "$view" "$view_axis")" \
                || e2e_fail "$view_axis $label: applet-length poll failed"
            observed=$(( after - before ))
            (( observed != 0 )) && break
        done
        (( observed != 0 || inherited_delta == 0 )) && break
        echo "  ($view_axis $label was not delivered on attempt $attempt, retrying)"
    done
    after="$(applet_length "$view" "$view_axis")" \
        || e2e_fail "$view_axis $label: final applet-length query failed"
    observed=$(( after - before ))
    printf 'OBS|view=%s|event=%s|angleDelta=%s|length=%d->%d|delta=%+d|spec=%+d|inherited=%+d\n' \
        "$view_axis" "$label" "$delta" "$before" "$after" "$observed" "$spec_delta" "$inherited_delta"
    if (( observed != inherited_delta )); then
        echo "UNEXPECTED: $view_axis $label produced $observed, expected inherited-path effect $inherited_delta" >&2
        unexpected_effects=$((unexpected_effects + 1))
    fi
    if (( observed != spec_delta )); then
        spec_failures=$((spec_failures + 1))
    fi
}

run_axis() {
    local axis="$1" view before after
    stage_axis "$axis"
    e2e_dock_start 90 || e2e_fail "$axis: dock did not settle with the SC-CW1 fixture"
    view="$(fixture_view "$axis")" || e2e_fail "$axis: fixture view discovery failed"

    # Negative control: without rearrange mode ConfigOverlay is not visible, so
    # the fixture's own no-handler surface must leave its geometry unchanged.
    before="$(applet_length "$view" "$axis")" \
        || e2e_fail "$axis: normal-mode initial applet-length query failed"
    arm_target "$view" "$axis"
    "$E2E_FAKEPOINTER" wheel "$target_x" "$target_y" 120 \
        || e2e_fail "$axis: normal-mode wheel injection failed"
    sleep 0.8
    after="$(applet_length "$view" "$axis")" \
        || e2e_fail "$axis: normal-mode final applet-length query failed"
    printf 'CONTROL|view=%s|mode=normal|angleDelta=(0,120)|length=%d->%d\n' "$axis" "$before" "$after"
    [[ "$after" == "$before" ]] || e2e_fail "$axis: normal-mode wheel changed fixture length before ConfigOverlay was active"

    applet_reorder_enter "$view" || e2e_fail "$axis: could not enter the ConfigOverlay rearrange path"

    record_case "$view" "$axis" 'vertical-positive' 120 vertical 8 8
    record_case "$view" "$axis" 'vertical-negative' -120 vertical -8 -8
    record_case "$view" "$axis" 'vertical-positive-boundary' 96 vertical 0 0
    record_case "$view" "$axis" 'vertical-negative-boundary' -96 vertical 0 -8
    record_case "$view" "$axis" 'vertical-positive-subthreshold' 90 vertical 0 -8
    record_case "$view" "$axis" 'vertical-negative-subthreshold' -90 vertical 0 -8
    record_case "$view" "$axis" 'horizontal-positive' 120 horizontal 0 -8
    record_case "$view" "$axis" 'horizontal-negative' -120 horizontal 0 -8
    record_case "$view" "$axis" 'vertical-axis-stop' stop axisstop 0 0
    record_case "$view" "$axis" 'post-zero-positive-control' 120 vertical 8 8

    applet_reorder_exit "$view" || e2e_fail "$axis: ConfigOverlay did not cleanly exit"
    e2e_dock_stop || e2e_fail "$axis: dock did not stop cleanly after the matrix"
}

finalize_recipe() {
    local pid
    pid="$(e2e_dock_pid)" || e2e_fail "finalization could not read the dock pid"
    [[ -n "$pid" ]] || e2e_fail "finalization found no recorded dock pid"
    ! kill -0 "$pid" 2>/dev/null || e2e_fail "finalization found dock pid $pid still running"
    rm -rf "$E2E_CONFIG_HOME" || e2e_fail "finalization could not clear the fixture config"
    mkdir -p "$E2E_CONFIG_HOME" || e2e_fail "finalization could not recreate the config directory"
    cp -a "$backup/config/." "$E2E_CONFIG_HOME/" || e2e_fail "finalization could not restore the original config"
    diff -qr "$backup/config" "$E2E_CONFIG_HOME" >/dev/null \
        || e2e_fail "finalization restored different config bytes"
    rm -rf "$fixture_data" || e2e_fail "finalization could not remove fixture data"
    [[ ! -e "$fixture_data" ]] || e2e_fail "finalization left fixture data at $fixture_data"
    recipe_finalized=true
}

run_axis horizontal
run_axis vertical

if (( unexpected_effects == 0 && spec_failures == 10 )); then
    finalize_recipe
    echo "D57 reproduced: -96, +/-90, and horizontal wheel events decreased the Latte-style applet on both view axes"
    exit 57
fi
if (( spec_failures == 0 )); then
    finalize_recipe
    echo "D57 corrected signature observed on both view axes; promote SC-CW1 to a regression guard"
    exit 0
fi
e2e_fail "SC-CW1 observed a partial or unrelated signature (inherited mismatches=$unexpected_effects, spec violations=$spec_failures)"
