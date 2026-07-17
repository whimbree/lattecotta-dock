#!/usr/bin/env bash
# E2E: gliding the pointer onto a window-owning task maps its preview dialog
# (the EX-01/02/03 hover-preview paths end to end). Screen-agnostic: derives
# the widest bottom/top tasks dock from viewsData and glides onto the konsole
# icon with small steps.
#
# Why the glide is REPEATED until the dialog maps (root cause, traced live
# 2026-07-17): the preview trigger is the task MouseArea's onEntered, which
# the parabolic layer only emits while the dock is at REST - hoverEnabled
# gates OFF during the zoom animation (Qt5-faithful: mid-animation hover
# jitter is deliberately ignored). A single synthetic glide crosses konsole's
# boundary at a moment that races that animation, so onEntered fires on only
# ~60% of attempts, and once the warped pointer comes to rest inside the icon
# no further boundary crossing ever re-fires it - the preview then never maps.
# A real hand re-nudges when a hover does not "take"; this recipe does the
# same, re-gliding onto the icon until the dialog maps. The assertion still
# requires a genuine layer=6 dialog - the retry makes the DRIVE reliable, it
# never fakes the result. (fakepointer warps discrete positions; the animation
# race is a synthetic-injection artifact, not a dock defect - previews are
# reliable live with a real, continuously-moving mouse.)
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"

#! previews need a window-owning task; fresh vehicles carry only launchers.
#! konsole is part of the pinned environment (the vehicle proof's client).
konsole_started=0
if ! e2e_dumpwins | grep -q '|org.kde.konsole|'; then
    setsid konsole >/dev/null 2>&1 &
    konsole_started=$!
    for _ in $(seq 1 30); do
        e2e_dumpwins | grep -q '|org.kde.konsole|' && break
        sleep 1
    done
    e2e_dumpwins | grep -q '|org.kde.konsole|' || e2e_fail "konsole never mapped in the session"
fi
cleanup_konsole() {
    [[ "$konsole_started" != 0 ]] && kill "$konsole_started" 2>/dev/null
}
trap cleanup_konsole EXIT

#! let the dock settle after konsole's task appears: adding a window task
#! reflows the row and runs the zoom-in animation, and a glide driven while
#! that animation is live hits the hoverEnabled gate far more often (the
#! trigger race is worst on a not-yet-settled dock, e.g. right after the
#! suite's preceding recipe restarted it - see the header)
e2e_wait_settled 30 >/dev/null 2>&1 || true
sleep 3

#! geometry comes from viewsData, not the window dump: dock WINDOWS are
#! larger than the visible strip (shadow/free space) and several views can
#! tie on width, so window-rect picking is ambiguous - the view's
#! absoluteGeometry is the strip itself
tasks_view="$(e2e_tasks_view)" || e2e_fail "no tasks view"
dock="$(e2e_view_field "$tasks_view" '"%d %d %d %d" % tuple(v["absoluteGeometry"])')"
[[ -n "$dock" ]] || e2e_fail "no geometry for view $tasks_view"
read -r dx dy dw dh <<< "$dock"

#! the glide must END on a window-owning task or no preview can appear;
#! resolve the konsole icon's rest position before the pointer distorts
#! anything (e2e_task_center is only honest with the pointer outside)
read -r konx kony <<< "$(e2e_task_center "$tasks_view" org.kde.konsole.desktop)"
[[ -n "$konx" ]] || e2e_fail "could not locate the konsole task icon"

#! hover line: the icon centers, NOT the strip's outer rows - the last
#! few pixels at the screen edge are the edge margin (empty-area input),
#! and a hover there never reaches the task items
hovery=$kony

#! start the glide a few icon slots to the side of konsole that has the most
#! room, so it always CROSSES konsole's boundary (a glide that begins on the
#! icon never emits the onEntered the preview needs)
span=180
if (( konx - dx > dx + dw - konx )); then
    glidestart=$(( konx - span )); step=16
else
    glidestart=$(( konx + span )); step=-16
fi

preview_re='\|latte-dock\|\|[0-9.,-]+ [0-9]+x[0-9]+\|[^|]+\|layer=6'
#! ~60% of single glides land the onEntered (the animation race in the
#! header); the failure probability is 0.4^attempts, so 12 attempts is
#! ~1.7e-5 - a decisive margin without dozens of gestures
max_attempts=12
mapped=0
for _ in $(seq 1 "$max_attempts"); do
    #! reset between attempts: leave the dock and let the zoom fully restore
    #! to rest before the next glide - a glide started while the previous
    #! attempt's restore animation is still running crosses the boundary
    #! mid-animation and misses (the hoverEnabled gate); 0.8s covers the
    #! restore even on a sluggish just-restarted dock
    "$E2E_FAKEPOINTER" move "$konx" $(( hovery - 200 )); sleep 0.8
    "$E2E_FAKEPOINTER" move "$glidestart" "$hovery"; sleep 0.2
    x=$glidestart
    while (( (step > 0 && x < konx) || (step < 0 && x > konx) )); do
        "$E2E_FAKEPOINTER" move "$x" "$hovery"; x=$(( x + step ))
    done
    #! finish over the konsole task so the preview delay elapses on it
    "$E2E_FAKEPOINTER" move "$konx" "$hovery"
    sleep 1.1   #! previewsDelay (throwaway default 650ms) + build + margin
    if e2e_dumpwins | grep -qE "$preview_re"; then mapped=1; break; fi
done

#! leave the dock so zoom restores and the preview hides
"$E2E_FAKEPOINTER" move "$konx" $(( hovery - 400 )); sleep 1.2

if (( mapped == 1 )); then
    echo "parabolic glide engaged; preview dialog mapped (layer=6)"
    exit 0
fi
e2e_fail "no preview dialog mapped after gliding onto the konsole task ($max_attempts attempts)"
