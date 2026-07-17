#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# EX-17 live checks (docs/agent-logs/EX-17.md): hover a two-window task and
# verify the preview tooltip's TEXT - the composed titles (app-name excision:
# " - Konsole" must be stripped from each entry) and the desktop-id fix
# (a window on another virtual desktop reads "On <real desktop name>", never
# "On undefined"/"On " - the Qt5 numeric-desktop assumption 1ed62d89 fixed).
#
# Tooltip text is not on any D-Bus surface (viewTasksData carries no window
# titles by design), so this is one of the few recipes where pixels ARE the
# thing under test: the preview window is cropped from a ScreenShot2 capture
# and compared with latte-imgdiff against a committed golden that was
# verified BY READING IT once at bless time (title text present and correct,
# subtext "On E2E Desk Two"). Determinism inside the vehicle: fixed konsole
# tab titles (profile property), icon-fallback thumbnails (no pipewire in
# the vehicle), fixed desktop name, fixed 1600x1000 output. The golden is
# HOST-RENDERED (fonts from the system profile): on a new machine, verify
# once by eye and re-bless with E2E_BLESS=1.
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"

golden="$E2E_REPO/tests/e2e/goldens/preview-tooltip.png"
imgdiff="$E2E_BUILD/bin/latte-imgdiff"
[[ -x "$imgdiff" ]] || e2e_fail "no latte-imgdiff at $imgdiff (build first)"

view="$(e2e_tasks_view)" || e2e_fail "no tasks view"

vdm() { busctl --user "$1" org.kde.KWin /VirtualDesktopManager org.kde.KWin.VirtualDesktopManager "${@:2}"; }

#! a second, distinctively named desktop (created if missing, removed after)
created_desktop=""
if [[ "$(vdm get-property count | awk '{print $2}')" -lt 2 ]]; then
    vdm call createDesktop us 1 "E2E Desk Two" >/dev/null
    created_desktop=1
else
    vdm call setDesktopName ss "$(vdm get-property desktops | grep -oE '"[0-9a-f-]{36}"' | sed -n 2p | tr -d '"')" "E2E Desk Two" >/dev/null
fi

pids=()
cleanup() {
    local p
    for p in "${pids[@]}"; do kill "$p" 2>/dev/null; done
    if [[ -n "$created_desktop" ]]; then
        local id
        id="$(vdm get-property desktops | grep -oE '"[0-9a-f-]{36}"' | sed -n 2p | tr -d '"')"
        [[ -n "$id" ]] && vdm call removeDesktop s "$id" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT

#! two konsole windows with DETERMINISTIC captions (profile tab format)
for title in "E2E WINDOW ALPHA" "E2E WINDOW BETA"; do
    setsid konsole -p "LocalTabTitleFormat=$title" >/dev/null 2>&1 &
    pids+=($!)
    sleep 2
done
for _ in $(seq 1 20); do
    [[ "$(e2e_dumpwins | grep -c 'E2E WINDOW')" -ge 2 ]] && break
    sleep 1
done
[[ "$(e2e_dumpwins | grep -c 'E2E WINDOW')" -ge 2 ]] || e2e_fail "titled konsole windows never mapped"

#! move BETA to the second desktop and verify it took effect
moved="$(e2e_kwin_js '
for (const w of workspace.windowList()) {
    if (w.caption.indexOf("E2E WINDOW BETA") === 0) {
        w.desktops = [workspace.desktops[1]];
        print("@TAG@|" + w.desktops.map(d => d.name).join(","));
    }
}')"
[[ "$moved" == "E2E Desk Two" ]] || e2e_fail "window move to desktop 2 did not take effect (got '$moved')"
sleep 2   #! let the tooltip's desktop facts settle

#! hover the konsole task and let the preview build. The rest-position
#! arithmetic can land one slot off once the parabolic shift engages on the
#! way in (a neighbor's title tooltip appeared instead of the group preview
#! in calibration), so the mapped window itself is the feedback: only the
#! GROUP preview is wide; a small layer=6 window is a title tooltip on the
#! wrong icon and the pointer steps one slot over and retries.
read -r kx ky <<< "$(e2e_task_center "$view" org.kde.konsole.desktop)"
[[ -n "$kx" ]] || e2e_fail "could not locate the konsole task icon"
slot="$({ e2e_json viewAppletsData u "$view"; e2e_json viewTasksData u "$view"; } | python3 -c '
import json, sys
applets, tasks = (json.loads(line) for line in sys.stdin)
width = next(a["geometry"][2] for a in applets if a["plugin"] == "org.kde.latte.plasmoid")
print(max(40, int(width / (len(tasks) or 1))))' )"

preview=""
for dx in 0 "-$slot" "$slot"; do
    hx=$(( kx + dx ))
    #! a glide, not a jump: the motion stream guarantees the enter lands
    "$E2E_FAKEPOINTER" move "$hx" 500; sleep 0.3
    "$E2E_FAKEPOINTER" glide "$hx" 500 "$hx" "$ky"
    sleep 2.2   #! previewsDelay + build + fade-in
    preview="$(e2e_dumpwins | awk -F'|' '
        $2 ~ /latte-dock/ && $6 == "layer=6" {
            split($4, g, " "); split(g[1], pos, ","); split(g[2], size, "x");
            if (size[1] > best) { best = size[1];
                x=pos[1]; y=pos[2]; w=size[1]; h=size[2]; } }
        END { if (best > 350) printf "%d %d %d %d\n", x, y, w, h }')"
    [[ -n "$preview" ]] && break
    "$E2E_FAKEPOINTER" move "$hx" 500; sleep 1.2   #! close whatever mapped
done
[[ -n "$preview" ]] || e2e_fail "no group preview mapped on the konsole task (or its neighbors)"
read -r px py pw ph <<< "$preview"

shot="$(mktemp --suffix=.png)"
crop="$E2E_ARTIFACTS/preview-tooltip.actual.png"
e2e_screenshot "$shot" || e2e_fail "screenshot failed"
magick "$shot" -crop "${pw}x${ph}+${px}+${py}" "$crop"
rm -f "$shot"

#! unhover so the preview closes before any later recipe
"$E2E_FAKEPOINTER" move "$kx" 500

if [[ "${E2E_BLESS:-0}" == 1 ]]; then
    mkdir -p "$(dirname "$golden")"
    cp "$crop" "$golden"
    echo "BLESSED $golden - verify the text by eye before committing:"
    echo "  titles must read 'E2E WINDOW ALPHA'/'E2E WINDOW BETA' (no ' - Konsole' tail)"
    echo "  BETA's subtext must read 'On E2E Desk Two'"
    exit 0
fi

[[ -f "$golden" ]] || e2e_fail "no golden at $golden (run once with E2E_BLESS=1, verify by eye, commit)"

#! tolerant tier: same host renders bit-close but the tooltip carries AA
#! text; delta 8 with a 1% budget separates a wrong string from AA noise
if "$imgdiff" "$crop" "$golden" --delta 8 --budget 0.01 --out "$E2E_ARTIFACTS/preview-tooltip.diff.png"; then
    echo "preview tooltip text matches the verified golden (titles + 'On E2E Desk Two')"
else
    e2e_fail "preview tooltip differs from the verified golden (see $crop and the diff)"
fi
