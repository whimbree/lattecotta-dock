#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# C-I3 / P3 acceptance (docs/e2e-interaction-test-plan.md, HC3): the coarse
# addApplet D-Bus action, PROVING BOTH SIDES.
#
#   Happy path - append an installed plasmoid and watch the applet-id order
#   (the G1 readback) grow by exactly one NEW instance id; the two order
#   readbacks (viewAppletsData ids, viewAppletsOrder) agree.
#
#   Rejection (the HC3 deliverable) - drive a BAD containment id and prove the
#   action REFUSES it: a qWarning in the dock log AND zero applets created on
#   ANY view. The acceptance test OBSERVES A REJECTION, not just a green add.
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"

# ---- readback helpers -------------------------------------------------------

# all_view_ids: every current containment id, one per line.
all_view_ids() {
    e2e_json viewsData | python3 -c 'import json,sys
print("\n".join(str(v["containmentId"]) for v in json.load(sys.stdin)))'
}

# applet_ids <cid>: the applet instance ids in visual order, from
# viewAppletsData (JSON, splitter-free), space separated.
applet_ids() {
    e2e_json viewAppletsData u "$1" | python3 -c 'import json,sys
print(" ".join(str(a["id"]) for a in json.load(sys.stdin)))'
}

# applet_order <cid>: the same ids via the viewAppletsOrder method (busctl
# "as N \"id\" ..."); the G1 fix strips justify-splitter sentinels so this
# must equal applet_ids on every alignment.
applet_order() {
    e2e_call viewAppletsOrder u "$1" | python3 -c 'import re,sys
print(" ".join(re.findall(r"\"(-?\d+)\"", sys.stdin.read())))'
}

# plugin_of <cid> <appletId>: the plugin string of one applet.
plugin_of() {
    e2e_json viewAppletsData u "$1" | python3 -c "import json,sys
print(next(a['plugin'] for a in json.load(sys.stdin) if a['id']==$2))"
}

# applet_total: applets summed across ALL views - the 'created nothing
# anywhere' witness the rejection asserts stays byte-identical.
applet_total() {
    local id total=0 n
    for id in $(all_view_ids); do
        n="$(e2e_json viewAppletsData u "$id" | python3 -c 'import json,sys
print(len(json.load(sys.stdin)))')"
        total=$((total + n))
    done
    echo "$total"
}

# ---- target: the view with the most applets, and a safe source plugin -------

target="$(all_view_ids | while read -r id; do
    echo "$(applet_ids "$id" | wc -w) $id"
done | sort -rn | head -1 | awk '{print $2}')"
[[ -n "$target" ]] || e2e_fail "no view found to add an applet to"

before="$(applet_ids "$target")"
before_n="$(wc -w <<< "$before")"
[[ "$before_n" -ge 1 ]] || e2e_fail "target view $target reports no applets"

#! a plugin the dock can actually RESOLVE in this vehicle: addApplet accepts
#! only plugins under <datadir>/plasma/plasmoids/<id> (Importer::standardPaths
#! = GenericDataLocation), which is Latte's staged plasmoids plus the system
#! plasma set - NOT every widget the session renders (analogclock is a
#! metapackage, no package dir, and addApplet rightly refuses it). minimizeall
#! is a trivial system button; org.kde.latte.plasmoid is the staged guarantee
#! (and, since the view already carries one, adds a SECOND same-plugin instance
#! - the exact G1 disambiguation case). Try in order; a refusal is logged, so
#! it is distinguishable from a slow async create.
candidates=(org.kde.plasma.minimizeall org.kde.latte.plasmoid)

src_plugin=""; after=""; after_n="$before_n"
for cand in "${candidates[@]}"; do
    echo "target view $target has $before_n applets; trying to add '$cand'"
    logmark="$(wc -l < "$E2E_DOCK_LOG")"
    e2e_call addApplet us "$target" "$cand" >/dev/null

    for _ in $(seq 1 15); do
        sleep 1
        after="$(applet_ids "$target")"
        after_n="$(wc -w <<< "$after")"
        [[ "$after_n" -gt "$before_n" ]] && break
        tail -n +$((logmark + 1)) "$E2E_DOCK_LOG" \
            | grep -q "found no installed plasmoid named .$cand." && break
    done

    if [[ "$after_n" -gt "$before_n" ]]; then
        src_plugin="$cand"
        break
    fi
    echo "  ('$cand' not resolvable in this vehicle, trying the next candidate)"
done

[[ -n "$src_plugin" ]] || e2e_fail "no candidate plugin was addable in this vehicle: ${candidates[*]}"
[[ "$after_n" -eq $((before_n + 1)) ]] \
    || e2e_fail "addApplet did not add exactly one applet (before $before_n, after $after_n: $after)"

#! exactly one id is new, every old id survived (nothing dropped/reordered
#! into oblivion), and the new one carries the SAME plugin - the same-plugin
#! disambiguation the G1 id-order readback exists for
new_ids="$(comm -13 <(tr ' ' '\n' <<< "$before" | sort) <(tr ' ' '\n' <<< "$after" | sort))"
[[ "$(wc -w <<< "$new_ids")" -eq 1 ]] || e2e_fail "expected exactly one new applet id, got: $new_ids"
missing="$(comm -23 <(tr ' ' '\n' <<< "$before" | sort) <(tr ' ' '\n' <<< "$after" | sort))"
[[ -z "$missing" ]] || e2e_fail "add dropped pre-existing applet ids: $missing"
new_id="$(tr -d ' ' <<< "$new_ids")"
new_plugin="$(plugin_of "$target" "$new_id")"
[[ "$new_plugin" == "$src_plugin" ]] || e2e_fail "new applet $new_id is '$new_plugin', expected '$src_plugin'"
echo "added one new instance id $new_id (plugin $new_plugin), distinct from $before_n existing"

#! G1 consistency: the cheap viewAppletsOrder method must report the SAME
#! ordered ids as viewAppletsData (both splitter-free) - the fix that retires
#! the justify -10 pollution
order_method="$(applet_order "$target")"
[[ "$order_method" == "$after" ]] \
    || e2e_fail "viewAppletsOrder ('$order_method') disagrees with viewAppletsData ids ('$after')"
echo "viewAppletsOrder agrees with viewAppletsData id order (G1)"

# ---- HC3: a bad containment id is REFUSED (qWarning + zero applets) ---------

bad_cid=987654
all_view_ids | grep -qx "$bad_cid" && e2e_fail "test bug: $bad_cid is a real view id, pick another"

before_total="$(applet_total)"
logmark="$(wc -l < "$E2E_DOCK_LOG")"

e2e_call addApplet us "$bad_cid" "$src_plugin" >/dev/null 2>&1 || true
sleep 2

after_total="$(applet_total)"
[[ "$after_total" -eq "$before_total" ]] \
    || e2e_fail "REJECTION LEAK: applet total changed $before_total -> $after_total on a bad containment id"

#! the refusal must be LOUD, not a silent no-op: the qWarning naming the bad
#! id must appear in the dock log lines produced by THIS call
if ! tail -n +$((logmark + 1)) "$E2E_DOCK_LOG" \
        | grep -q "addApplet requested for containment $bad_cid which has no view"; then
    echo "---- new dock-log lines ----" >&2
    tail -n +$((logmark + 1)) "$E2E_DOCK_LOG" >&2
    e2e_fail "no addApplet refusal qWarning for bad containment id $bad_cid in the dock log"
fi
echo "rejection observed: bad containment id $bad_cid refused (qWarning + 0 applets created)"

echo "PASS: addApplet appends one, G1 order is consistent, and a bad id is refused"
