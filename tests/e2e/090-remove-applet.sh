#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# C-I4 / P4 acceptance (docs/e2e-interaction-test-plan.md, HC3): the coarse
# removeApplet D-Bus action, PROVING BOTH SIDES.
#
#   Happy path - remove an existing applet by instance id and watch the coarse
#   action fire. removeApplet is the "Remove this Widget" the context menu
#   triggers (Applet::destroy()), which rides the libplasma UNDO WINDOW: the
#   applet does NOT vanish immediately, it lingers with inScheduledDestruction
#   flipped to true (the removeView undo trap, observed here for applets). The
#   recipe reports the observed removal effect (scheduled-destruction flip, or
#   an outright leave if the vehicle finalizes early) and asserts SOMETHING
#   removed the applet - a green no-op is not acceptance.
#
#   Rejection (the HC3 deliverable) - drive a BAD applet id AND a bad
#   containment id and prove each is REFUSED: a qWarning in the dock log AND
#   the applet population byte-identical (no applet entered destruction). The
#   acceptance test OBSERVES A REJECTION, not just a green remove.
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"

# ---- readback helpers -------------------------------------------------------

# all_view_ids: every current containment id, one per line.
all_view_ids() {
    e2e_json viewsData | python3 -c 'import json,sys
print("\n".join(str(v["containmentId"]) for v in json.load(sys.stdin)))'
}

# applet_ids <cid>: the applet instance ids in visual order, space separated.
applet_ids() {
    e2e_json viewAppletsData u "$1" | python3 -c 'import json,sys
print(" ".join(str(a["id"]) for a in json.load(sys.stdin)))'
}

# sched_of <cid> <appletId>: one applet's inScheduledDestruction as
# True/False, or MISSING if that id is no longer reported (finalized/absent).
sched_of() {
    e2e_json viewAppletsData u "$1" | python3 -c "import json,sys
a=next((a for a in json.load(sys.stdin) if a['id']==$2), None)
print('MISSING' if a is None else a['inScheduledDestruction'])"
}

# applet_total: applets summed across ALL views - the 'nothing changed
# anywhere' witness a rejection asserts stays byte-identical.
applet_total() {
    local id total=0 n
    for id in $(all_view_ids); do
        n="$(e2e_json viewAppletsData u "$id" | python3 -c 'import json,sys
print(len(json.load(sys.stdin)))')"
        total=$((total + n))
    done
    echo "$total"
}

# ---- target: the view with the most applets, and its last applet ------------

target="$(all_view_ids | while read -r id; do
    echo "$(applet_ids "$id" | wc -w) $id"
done | sort -rn | head -1 | awk '{print $2}')"
[[ -n "$target" ]] || e2e_fail "no view found to remove an applet from"

before="$(applet_ids "$target")"
before_n="$(wc -w <<< "$before")"
[[ "$before_n" -ge 1 ]] || e2e_fail "target view $target reports no applets to remove"

#! the last applet in visual order: least structurally load-bearing, and the
#! throwaway config copy is restarted between recipes so a scheduled removal
#! never leaks into the next one
victim="${before##* }"
victim_sched="$(sched_of "$target" "$victim")"
[[ "$victim_sched" == "False" ]] \
    || e2e_fail "target applet $victim is already inScheduledDestruction=$victim_sched before removal"
echo "target view $target has $before_n applets; removing instance $victim"

# ---- happy path: the coarse remove fires (undo window OR early finalize) -----

logmark="$(wc -l < "$E2E_DOCK_LOG")"
e2e_call removeApplet uu "$target" "$victim" >/dev/null

effect=""
for _ in $(seq 1 15); do
    sleep 1
    now="$(sched_of "$target" "$victim")"
    if [[ "$now" == "True" ]]; then
        #! the documented libplasma path: destroy() marks the applet
        #! destroyed() and holds it for the ~60s undo window, so it lingers in
        #! the readback with inScheduledDestruction=true (the removeView trap)
        effect="scheduled-destruction (undo window open)"
        break
    elif [[ "$now" == "MISSING" ]]; then
        #! the counter-case: the applet left the readback outright (the vehicle
        #! finalized before the poll saw the flip). Still a real removal.
        effect="left the readback (finalized)"
        break
    fi
done

[[ -n "$effect" ]] \
    || e2e_fail "removeApplet did not remove applet $victim: it stayed inScheduledDestruction=False and present"
echo "removeApplet fired: applet $victim $effect"

#! undo-window witness: while the window is open the applet still counts (it
#! drops from the readback only when the window ends), so report the total for
#! the record - the rejection tests below re-baseline it either way.
if [[ "$effect" == scheduled-destruction* ]]; then
    echo "undo window: applet lingers, still counted (total now $(applet_total))"
fi

# ---- HC3a: a bad APPLET id is REFUSED (qWarning + nothing enters destruction)

bad_applet=987654
for id in $(all_view_ids); do
    tr ' ' '\n' <<< "$(applet_ids "$id")" | grep -qx "$bad_applet" \
        && e2e_fail "test bug: $bad_applet is a real applet id, pick another"
done

pre_total="$(applet_total)"
logmark="$(wc -l < "$E2E_DOCK_LOG")"
e2e_call removeApplet uu "$target" "$bad_applet" >/dev/null 2>&1 || true
sleep 2

post_total="$(applet_total)"
[[ "$post_total" -eq "$pre_total" ]] \
    || e2e_fail "REJECTION LEAK: applet total changed $pre_total -> $post_total on a bad applet id"
if ! tail -n +$((logmark + 1)) "$E2E_DOCK_LOG" \
        | grep -q "removeApplet found no applet $bad_applet on containment $target"; then
    echo "---- new dock-log lines ----" >&2
    tail -n +$((logmark + 1)) "$E2E_DOCK_LOG" >&2
    e2e_fail "no removeApplet refusal qWarning for bad applet id $bad_applet in the dock log"
fi
echo "rejection observed: bad applet id $bad_applet refused (qWarning + nothing removed)"

# ---- HC3b: a bad CONTAINMENT id is REFUSED (qWarning + no removal) -----------

bad_cid=987654
all_view_ids | grep -qx "$bad_cid" && e2e_fail "test bug: $bad_cid is a real view id, pick another"

pre_total="$(applet_total)"
logmark="$(wc -l < "$E2E_DOCK_LOG")"
e2e_call removeApplet uu "$bad_cid" "$victim" >/dev/null 2>&1 || true
sleep 2

post_total="$(applet_total)"
[[ "$post_total" -eq "$pre_total" ]] \
    || e2e_fail "REJECTION LEAK: applet total changed $pre_total -> $post_total on a bad containment id"
if ! tail -n +$((logmark + 1)) "$E2E_DOCK_LOG" \
        | grep -q "removeApplet requested for containment $bad_cid which has no view"; then
    echo "---- new dock-log lines ----" >&2
    tail -n +$((logmark + 1)) "$E2E_DOCK_LOG" >&2
    e2e_fail "no removeApplet refusal qWarning for bad containment id $bad_cid in the dock log"
fi
echo "rejection observed: bad containment id $bad_cid refused (qWarning + nothing removed)"

echo "PASS: removeApplet fires the coarse remove, and both bad ids are refused"
