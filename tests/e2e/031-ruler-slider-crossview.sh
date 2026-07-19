#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# AU-1c (docs/edit-mode-settings-audit-plan.md, cluster CL-1): the LIVE
# cross-view confirmation of the D16 fix - the Maximum settings slider re-tracks
# the on-canvas ruler after the slider's handle binding has been clobbered.
#
# The whole point of D16 is that the config value ALWAYS agrees (the ruler and
# the slider share one containment config map), so a config readback cannot tell
# the fixed dock from the broken one - only the RENDERED slider handle can. So
# this recipe:
#   1. opens the Appearance page and DRAGS the Maximum slider hard left, which
#      lowers maxLength AND (the crux) destroys the handle's config binding the
#      way any drag does - the exact clobber D16 is about;
#   2. drives the on-canvas Maximum-Length ruler UP to a deterministic value
#      (78%), reading viewConfigData after each detent so the target is exact;
#   3. asserts the config re-tracked (the ruler wrote the shared map), then
#      golden-compares a crop of the Maximum slider ROW.
# With the proxy re-sync fix the handle and its "78 %" label follow the ruler;
# reverting the fix leaves the handle stuck at the clobbered 1% and the crop
# mismatches. The value label binds to the handle (maxLengthSlider.value), not
# to config, so a stale handle shows a stale number - the golden catches both.
#
# Tooltip/pointer determinism: the pointer is parked far from the settings
# window and the ruler tooltip lives on the bottom canvas, outside the crop.
# HOST-RENDERED golden (fonts from the system profile): on a new machine verify
# once by eye (handle near 78%, label reads "78 %") and re-bless with E2E_BLESS=1.
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"
source "$E2E_REPO/tests/e2e/matrix/matrix-lib.sh"
source "$E2E_REPO/tests/e2e/audit/audit-lib.sh"

golden="$E2E_REPO/tests/e2e/goldens/ruler-slider-crossview.png"
imgdiff="$E2E_BUILD/bin/latte-imgdiff"
[[ -x "$imgdiff" ]] || e2e_fail "no latte-imgdiff at $imgdiff (build first)"

view="$(e2e_tasks_view)" || e2e_fail "no tasks view"

# maxLength read from the in-process config map (never the on-disk file, which
# the KConfig default-deletion trap would corrupt)
cfg_max() { audit_config_snapshot "$view" | awk -F'\t' '$1=="maxLength"{print $2}'; }

orig="$(cfg_max)"
restore() {
    e2e_dock_stop >/dev/null 2>&1 || true
    kwriteconfig6 --file "$E2E_LAYOUT" --group Containments --group "$view" --group General \
        --key maxLength "${orig:-100}"
}
trap restore EXIT

audit_enter_editmode "$view" || e2e_fail "edit mode never turned on"

#! the Appearance tab carries the Maximum slider (the Behavior tab is default)
audit_settings_click 0.376 0.154
sleep 1

#! drag the Maximum handle hard left: lowers maxLength to its floor AND clobbers
#! the handle's declarative binding (the drag is the imperative assignment D16
#! is about). Pinning below the 30 ruler rail makes the first up-detent snap to
#! 30, so the ruler sequence up is a deterministic 30, 36, ... .
audit_settings_drag 0.70 0.442 -360 0
sleep 1
clobbered="$(cfg_max)"
[[ -n "$clobbered" ]] || e2e_fail "could not read maxLength after the slider drag"
(( clobbered <= 30 )) || e2e_fail "slider drag did not pin maxLength below the 30 rail (got $clobbered)"
echo "clobbered the Maximum slider: maxLength $orig -> $clobbered"

#! the ruler lives on the bottom canvas (screen-wide, thin), driven at its top
#! rows exactly as 030-wheel-ruler-maxlength calibrated
canvas="$(e2e_dumpwins | awk -F'|' '
    { split($4,g," "); split(g[1],p,","); split(g[2],s,"x");
      if (s[1]==1600 && s[2]<300){printf "%d %d\n",p[1],p[2];exit} }')"
[[ -n "$canvas" ]] || e2e_fail "no canvas ruler window mapped"
read -r cx cy <<< "$canvas"
rx=800; ry=$(( cy + 7 ))

target=78
for attempt in $(seq 1 40); do
    cur="$(cfg_max)"
    [[ "$cur" == "$target" ]] && break
    (( cur > target )) && e2e_fail "ruler overshot the target: maxLength $cur > $target"
    "$E2E_FAKEPOINTER" scroll "$rx" "$ry" 1 100
    "$E2E_FAKEPOINTER" move "$rx" 650
    sleep 1
done
final="$(cfg_max)"
[[ "$final" == "$target" ]] || e2e_fail "ruler never reached maxLength $target (stuck at $final)"
echo "ruler drove maxLength $clobbered -> $final through the shared config"

#! park the pointer far from the settings window and let the ruler tooltip fade
"$E2E_FAKEPOINTER" move 200 300
sleep 1.5

read -r wx wy ww wh <<< "$(audit_settings_window_rect)" || e2e_fail "no settings window to crop"
#! crop the Maximum slider ROW: nearly the window width, a thin band at the
#! Length/Maximum row, capturing the handle position and the "%1 %" value label
cropx=$(( wx + ww / 100 ))
cropy=$(python3 -c "print(int($wy + 0.427 * $wh))")
cropw=$(( ww * 98 / 100 ))
croph=$(python3 -c "print(int(0.05 * $wh))")

shot="$(mktemp --suffix=.png)"
crop="$E2E_ARTIFACTS/ruler-slider-crossview.actual.png"
e2e_screenshot "$shot" include-cursor b false || e2e_fail "screenshot failed"
magick "$shot" -crop "${cropw}x${croph}+${cropx}+${cropy}" "$crop"
rm -f "$shot"

if [[ "${E2E_BLESS:-0}" == 1 ]]; then
    mkdir -p "$(dirname "$golden")"
    cp "$crop" "$golden"
    echo "BLESSED $golden - verify by eye before committing:"
    echo "  the Maximum handle sits near 78% and the value label reads '78 %'"
    exit 0
fi

[[ -f "$golden" ]] || e2e_fail "no golden at $golden (run once with E2E_BLESS=1, verify by eye, commit)"

#! tolerant tier: same host renders bit-close, the value label is AA text; the
#! handle is a solid disc, so delta 8 / 2% budget separates a stuck handle from
#! AA noise
if "$imgdiff" "$crop" "$golden" --delta 8 --budget 0.02 --out "$E2E_ARTIFACTS/ruler-slider-crossview.diff.png"; then
    echo "the Maximum slider re-tracked the ruler to 78% (D16 cross-view sync holds)"
else
    e2e_fail "the Maximum slider did not match the verified golden (D16 regression? see $crop and the diff)"
fi
