# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Recipe helper library for the e2e suite. Sourced by tests/e2e/*.sh AND by
# scripts/run-e2e.sh itself (single implementation of the dock lifecycle).
#
# Contract: the driver exports the E2E_* variables and, in nested mode, has
# already switched the AMBIENT environment onto the nested session (private
# XDG_RUNTIME_DIR, WAYLAND_DISPLAY, DBUS_SESSION_BUS_ADDRESS, DISPLAY unset),
# so plain busctl/fakepointer in recipes talk to the vehicle, never the desk.
# Everything here works in both modes unless marked nested-only; nested-only
# helpers refuse loudly instead of silently touching the live session.

e2e_call() {
    busctl --user call org.kde.lattedock /Latte org.kde.LatteDock "$@"
}

# e2e_json <method> [signature args...]: a read surface's payload as plain
# JSON on stdout (busctl prints one escaped 's' value).
e2e_json() {
    e2e_call "$@" | sed 's/^s "//; s/"$//; s/\\"/"/g'
}

e2e_fail() { echo "FAIL: $*" >&2; exit 1; }

# e2e_wait_running [timeout-s]: poll lifecycleState (never sleep blindly).
e2e_wait_running() {
    local timeout="${1:-60}" i state
    for ((i = 0; i < timeout; i++)); do
        state="$(e2e_call lifecycleState 2>/dev/null | awk '{print $2}')"
        [[ "$state" == '"running"' ]] && return 0
        sleep 1
    done
    echo "dock never reached lifecycleState running in ${timeout}s (last: ${state:-no reply})" >&2
    return 1
}

# e2e_wait_settled [timeout-s]: views EXIST and are all out of inStartup.
# The existence check is load-bearing: lifecycleState flips to "running" at
# layout-switch time, BEFORE any view is created (views come one per event
# loop pass, ~1s+ each), so an empty viewsData is "still starting", not
# "settled" - the vacuous version of this helper let recipes race the whole
# view-creation window (caught by duplicate-view failing against a view id
# that did not exist yet).
e2e_wait_settled() {
    local timeout="${1:-60}" i payload previous=""
    for ((i = 0; i < timeout; i++)); do
        payload="$(e2e_call viewsData 2>/dev/null)"
        if [[ -n "$payload" && "$payload" != 's "[]"' ]] && ! grep -q 'inStartup\\":true' <<<"$payload"; then
            #! geometry must also STOP MOVING: the startup zoom animation keeps
            #! resizing strips and applets for seconds after inStartup clears,
            #! and coordinates sampled mid-animation put recipe pointer math
            #! off by up to ~90px (caught as an applet rect below the screen)
            if [[ "$payload" == "$previous" ]]; then
                return 0
            fi
            previous="$payload"
        fi
        sleep 1
    done
    echo "views still absent, inStartup, or animating after ${timeout}s" >&2
    return 1
}

_e2e_require_nested() {
    [[ "${E2E_MODE:-}" == "nested" ]] && return 0
    echo "e2e: $1 is nested-only (it manages the vehicle dock / nested kwin); refusing in mode '${E2E_MODE:-unset}'" >&2
    return 2
}

e2e_dock_pid() { cat "${E2E_DOCK_PIDFILE:?}" 2>/dev/null; }

# e2e_dock_start [timeout-s] (nested-only): launch the staged dock into the
# vehicle, detached, and wait for running + settled. run-staged.sh execs the
# binary, so the launcher pid IS the dock pid.
e2e_dock_start() {
    _e2e_require_nested e2e_dock_start || return 2
    local timeout="${1:-60}"
    #! QT_FORCE_STDERR_LOGGING so the dock's qCDebug/qWarning land in
    #! E2E_DOCK_LOG: NixOS Qt otherwise routes to journald whenever stderr is
    #! not a tty (the same reason the vehicle kwin sets it), leaving the log
    #! empty - which both blinds the failure-artifact capture and hides the
    #! action-refusal warnings a rejection recipe asserts on
    setsid env LATTE_CONFIG_HOME="$E2E_CONFIG_HOME" BUILD="$E2E_BUILD" \
        LATTE_DEBUG_DBUS=1 \
        QT_FORCE_STDERR_LOGGING=1 \
        "$E2E_REPO/scripts/run-staged.sh" -d >>"$E2E_DOCK_LOG" 2>&1 &
    echo $! > "$E2E_DOCK_PIDFILE"
    e2e_wait_running "$timeout" && e2e_wait_settled "$timeout"
}

# e2e_dock_stop [timeout-s] (nested-only): SIGTERM and wait for a CLEAN exit.
# Deliberately no SIGKILL escalation - a dock that survives SIGTERM is a
# shutdown defect the caller must see, not a nuisance to sweep away.
e2e_dock_stop() {
    _e2e_require_nested e2e_dock_stop || return 2
    local timeout="${1:-25}" pid i
    pid="$(e2e_dock_pid)"
    [[ -n "$pid" ]] || { echo "e2e_dock_stop: no dock pid recorded" >&2; return 1; }
    kill -0 "$pid" 2>/dev/null || { echo "e2e_dock_stop: dock (pid $pid) already gone" >&2; return 1; }
    kill -TERM "$pid"
    for ((i = 0; i < timeout * 5; i++)); do
        kill -0 "$pid" 2>/dev/null || return 0
        sleep 0.2
    done
    echo "dock (pid $pid) survived SIGTERM for ${timeout}s" >&2
    return 1
}

# e2e_kwin_js <script-body>: run a transient KWin script on the session's
# compositor and print what it print()ed. Use the literal token @TAG@ as the
# line prefix in the script; it is replaced with a unique run tag so
# concurrent/previous runs cannot bleed into the result.
# Nested: reads the vehicle kwin's captured log (the vehicle sets
# QT_FORCE_STDERR_LOGGING=1 - NixOS Qt otherwise logs straight to journald
# when stderr is not a tty and the file stays empty). Live: reads the
# session kwin's journal, like scripts/tools/dumpwins.sh.
e2e_kwin_js() {
    local body="$1" tag js num mark
    tag="E2EJS-$$-$(date +%s%N)"
    js="$(mktemp --suffix=.js)"
    printf '%s\n' "${body//@TAG@/$tag}" > "$js"
    mark="$(date +%s.%N)"
    num="$(busctl --user call org.kde.KWin /Scripting org.kde.kwin.Scripting loadScript ss "$js" "$tag" | awk '{print $2}')"
    if [[ -z "$num" ]]; then
        rm -f "$js"
        echo "e2e_kwin_js: loadScript failed" >&2
        return 1
    fi
    busctl --user call "org.kde.KWin" "/Scripting/Script$num" org.kde.kwin.Script run >/dev/null
    sleep 0.5
    busctl --user call "org.kde.KWin" "/Scripting/Script$num" org.kde.kwin.Script stop >/dev/null 2>&1 || true
    busctl --user call org.kde.KWin /Scripting org.kde.kwin.Scripting unloadScript s "$tag" >/dev/null 2>&1 || true
    rm -f "$js"
    if [[ "${E2E_MODE:-}" == "nested" ]]; then
        grep -a "$tag|" "${E2E_KWIN_LOG:?}" | sed "s/.*$tag|//"
    else
        journalctl --user -u plasma-kwin_wayland --since "@$mark" --no-pager -o cat | grep -a "$tag|" | sed "s/.*$tag|//"
    fi
}

# e2e_dumpwins: all windows as DUMPWIN|class|caption|x,y WxH|output|layer=N
# (same shape as scripts/tools/dumpwins.sh, mode-agnostic).
e2e_dumpwins() {
    e2e_kwin_js 'for (const w of workspace.windowList()) {
        print("@TAG@|DUMPWIN|" + w.resourceClass + "|" + w.caption + "|" + w.frameGeometry.x + "," + w.frameGeometry.y + " " + w.frameGeometry.width + "x" + w.frameGeometry.height + "|" + (w.output ? w.output.name : "?") + "|layer=" + w.layer);
    }'
}

# e2e_screenshot <out.png> (nested-only): capture the vehicle workspace via
# KWin ScreenShot2. The image arrives RAW over a pipe fd (the reply vardict
# carries width/height/stride/format); the vehicle kwin runs with
# KWIN_SCREENSHOT_NO_PERMISSION_CHECKS=1 so busctl needs no desktop-file
# authorization. Live sessions keep using spectacle (the screenshot D-Bus
# call would be refused there - by design, do not work around it).
e2e_screenshot() {
    _e2e_require_nested e2e_screenshot || return 2
    local out="$1"; shift
    #! extra a{sv} option triples (key type value ...) are forwarded verbatim
    #! into CaptureWorkspace's option dict; native-resolution stays always-on.
    #! The golden bridge passes 'include-cursor b false' so a stray pointer can
    #! never land in a golden shot (the biggest live-shot nondeterminism after
    #! animation). No extra args = the original single-option behavior.
    local -a opts=(native-resolution b true "$@")
    if (( ${#opts[@]} % 3 != 0 )); then
        echo "e2e_screenshot: option args must be (key type value) triples, got: $*" >&2
        return 2
    fi
    local count=$(( ${#opts[@]} / 3 )) raw reply w h stride format
    raw="$(mktemp)"
    reply="$(busctl --user call org.kde.KWin /org/kde/KWin/ScreenShot2 \
        org.kde.KWin.ScreenShot2 CaptureWorkspace "a{sv}h" "$count" "${opts[@]}" 3 3>"$raw")" \
        || { rm -f "$raw"; echo "e2e_screenshot: CaptureWorkspace failed" >&2; return 1; }
    w="$(grep -oE '"width" u [0-9]+' <<<"$reply" | awk '{print $3}')"
    h="$(grep -oE '"height" u [0-9]+' <<<"$reply" | awk '{print $3}')"
    stride="$(grep -oE '"stride" u [0-9]+' <<<"$reply" | awk '{print $3}')"
    format="$(grep -oE '"format" u [0-9]+' <<<"$reply" | awk '{print $3}')"
    # QImage formats 5/6 are (A)RGB32: BGRA byte order on little-endian.
    # Anything else (or a padded stride) needs new handling, not guessing.
    if [[ "$format" != 5 && "$format" != 6 ]] || [[ "$stride" != $((w * 4)) ]]; then
        rm -f "$raw"
        echo "e2e_screenshot: unexpected raw layout (format=$format stride=$stride width=$w) - extend the converter" >&2
        return 1
    fi
    magick -size "${w}x${h}" -depth 8 "bgra:$raw" "$out"
    local rc=$?
    rm -f "$raw"
    return "$rc"
}

# e2e_tasks_view: the containment id of the widest horizontal view that
# carries a tasks applet - the canonical target for task-interaction
# recipes, independent of any particular config's ids.
e2e_tasks_view() {
    local id
    for id in $(e2e_json viewsData | python3 -c '
import json, sys
# hidden views (sidebars, auto-hidden docks) are excluded: they have no
# input region to drive and their reported width breathes with slide
# animations, which once flipped the pick onto an on-demand sidebar
views = [v for v in json.load(sys.stdin)
         if v["edge"] in ("bottom", "top") and not v["isHidden"]]
# deterministic: widest first, bottom beats top on ties (my config carries
# same-width top and bottom docks; every by-hand calibration used bottom)
views.sort(key=lambda v: (-v["absoluteGeometry"][2], v["edge"] != "bottom"))
for v in views:
    print(v["containmentId"])
'); do
        if e2e_json viewAppletsData u "$id" | grep -q '"org.kde.latte.plasmoid"'; then
            echo "$id"
            return 0
        fi
    done
    echo "e2e_tasks_view: no horizontal view carries a tasks applet" >&2
    return 1
}

# e2e_view_window_x <containment-id>: the TRUE screen x of a horizontal
# view's window, from the compositor's window dump. viewsData's
# absolute/local pair implies the window origin; since the Phase 8 masked-dock
# surface drift was fixed (app/wm/waylandlayershell.cpp anchorsFor) the two
# agree, and 060-geometry-agreement.sh guards that they stay in agreement.
# This helper stays as the ground-truth window position for pointer math: it
# reads what the compositor actually shows rather than trusting the reported
# origin, so a future divergence cannot silently misaim a recipe.
e2e_view_window_x() {
    local id="$1" edge screenw screenh
    read -r edge screenw screenh <<< "$(e2e_view_field "$id" '"%s %d %d" % (v["edge"], v["screenGeometry"][2], v["screenGeometry"][3])')"
    e2e_dumpwins | awk -F'|' -v edge="$edge" -v sw="$screenw" -v sh="$screenh" '
        $2 ~ /latte-dock/ && $6 == "layer=3" {
            split($4, g, " "); split(g[1], pos, ","); split(g[2], size, "x");
            if (size[1] != sw) next
            if (edge == "bottom" && pos[2] + size[2] == sh) { print pos[1]; exit }
            if (edge == "top" && pos[2] == 0) { print pos[1]; exit }
        }'
}

# e2e_task_center <containment-id> <appId>: the SCREEN center of a task
# icon, computed arithmetically (tasks applet geometry is view-local;
# viewsData's absolute/local pair gives the window origin; icons split the
# applet evenly at rest). Published task geometries are not usable here:
# they stay at the window rect until the tasks applet publishes, and the
# parabolic zoom distorts everything once the pointer is inside the dock -
# so callers must approach the returned point from OUTSIDE the dock.
e2e_task_center() {
    local id="$1" app="$2" winx
    winx="$(e2e_view_window_x "$id")"
    { e2e_json viewsData; e2e_json viewAppletsData u "$id"; e2e_json viewTasksData u "$id"; } | python3 -c "
import json, sys
views, applets, tasks = (json.loads(line) for line in sys.stdin)
view = next(v for v in views if v['containmentId'] == $id)
ax, ay, aw, ah = view['absoluteGeometry']
lx, ly = view['localGeometry'][:2]
# x from the compositor's true window position (see e2e_view_window_x);
# y from the abs/local pair, which stays consistent for anchored edges
ox = ${winx:-ax - lx}
oy = ay - ly
applet = next(a for a in applets if a['plugin'] == 'org.kde.latte.plasmoid')
px, py, pw, ph = applet['geometry']
idx = next(i for i, t in enumerate(tasks) if t['appId'] == '$app')
n = len(tasks)
horizontal = view['edge'] in ('bottom', 'top')
if horizontal:
    cx = ox + px + (idx + 0.5) * pw / n
    cy = oy + py + ph / 2
else:
    cx = (ax - lx) + px + pw / 2
    cy = oy + py + (idx + 0.5) * ph / n
print(int(cx), int(cy))
"
}

# e2e_view_field <containment-id> <python-expr over view dict v>: one field
# of one view, e.g. e2e_view_field 16 'v["absoluteGeometry"]'.
e2e_view_field() {
    local id="$1" expr="$2"
    e2e_json viewsData | python3 -c "
import json, sys
views = json.load(sys.stdin)
match = [v for v in views if v['containmentId'] == $id]
if not match:
    sys.exit('no view with containmentId $id')
v = match[0]
print($expr)
"
}

# e2e_geometry_drift <containment-id>: pixels the view's rendered surface sits
# off its REPORTED origin, rendered_x - (absoluteGeometry.x - localGeometry.x).
# Zero means the compositor draws the dock exactly where viewsData claims it
# is; a nonzero value is the state/render divergence that every D-Bus assertion
# in this suite is blind to by construction (viewsData is self-consistent even
# when the surface renders elsewhere - the Phase 8 bottom-dock drift is the
# known instance). Prints the signed x drift, or returns non-zero (no output)
# for a view whose window this helper cannot locate - a non-horizontal or
# non-screen-width dock, a clone, or a view still settling.
e2e_geometry_drift() {
    local id="$1" winx reportedx
    winx="$(e2e_view_window_x "$id")"
    [[ -n "$winx" ]] || return 1
    reportedx="$(e2e_view_field "$id" 'v["absoluteGeometry"][0] - v["localGeometry"][0]')" || return 1
    [[ -n "$reportedx" ]] || return 1
    echo $(( winx - reportedx ))
}

# e2e_assert_geometry_agrees [tolerance-px]: every LOCATABLE horizontal view's
# rendered surface must sit within <tolerance> (default 2, for sub-pixel
# rounding) of the origin viewsData reports. Fails loud, naming each divergent
# view. This is the standing guard for the one bug class the D-Bus assertions
# cannot catch: the dock believing the right geometry while the compositor
# draws it somewhere else. Views the drift helper cannot measure are skipped
# (not silently passed) - a run where NOTHING was measurable also fails, so a
# broken dump can never look like agreement.
e2e_assert_geometry_agrees() {
    local tol="${1:-2}" id drift bad=0 measured=0
    for id in $(e2e_json viewsData | python3 -c 'import json,sys; print("\n".join(str(v["containmentId"]) for v in json.load(sys.stdin)))'); do
        drift="$(e2e_geometry_drift "$id")" || continue
        measured=$((measured + 1))
        if (( drift < -tol || drift > tol )); then
            echo "e2e_assert_geometry_agrees: view $id renders ${drift}px off its reported origin (tolerance ${tol}px)" >&2
            bad=1
        fi
    done
    if (( measured == 0 )); then
        echo "e2e_assert_geometry_agrees: no view geometry was measurable - refusing to report agreement" >&2
        return 1
    fi
    (( bad == 0 ))
}
