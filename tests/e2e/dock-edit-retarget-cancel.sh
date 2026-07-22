#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# D77 (dock duplication retains clone lineage and edit ownership) focused
# runtime acceptance. The shared edit chrome starts on A, receives an enter for
# B, then receives B's exit before the 400 ms retarget expires. B must never
# enter edit mode after that exit. A later ordinary B enter/exit proves the
# cancellation did not disable the target.
# e2e-mode: nested-only
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"

view_edit_mode() {
    local view="$1"
    { echo "$view"; e2e_json viewsData; } | python3 -c '
import json, sys
view = int(sys.stdin.readline())
record = next((v for v in json.load(sys.stdin) if v["containmentId"] == view), None)
if record is None:
    sys.exit("view %d disappeared" % view)
print("true" if record["editMode"] else "false")
'
}

wait_for_edit_mode() {
    local view="$1" expected="$2" i
    for ((i = 0; i < 50; ++i)); do
        [[ "$(view_edit_mode "$view")" == "$expected" ]] && return 0
        sleep 0.1
    done
    return 1
}

before="$(e2e_json viewsData)"
view_a="$(python3 -c '
import json, sys
views = [v for v in json.load(sys.stdin) if not v["isCloned"]]
if len(views) != 1:
    sys.exit("expected one original view, saw %d" % len(views))
print(views[0]["containmentId"])
' <<<"$before")"

e2e_call duplicateView u "$view_a" >/dev/null \
    || e2e_fail "duplicateView failed for original containment $view_a"

view_b=""
for _ in $(seq 1 100); do
    current="$(e2e_json viewsData)"
    view_b="$(python3 -c '
import json, sys
before = {v["containmentId"] for v in json.loads(sys.stdin.readline())}
after = json.loads(sys.stdin.readline())
created = [v for v in after if v["containmentId"] not in before]
if len(created) == 1 and not created[0]["isCloned"] and created[0]["isClonedFrom"] == -1:
    print(created[0]["containmentId"])
' <<<"$before
$current")"
    [[ -n "$view_b" ]] && break
    sleep 0.2
done
[[ -n "$view_b" ]] \
    || e2e_fail "independent duplicate did not reach viewsData"

e2e_call setViewEditMode ub "$view_a" true >/dev/null \
    || e2e_fail "could not enter edit mode on containment $view_a"
wait_for_edit_mode "$view_a" true \
    || e2e_fail "containment $view_a never entered edit mode"

start_ns="$(date +%s%N)"
e2e_call setViewEditMode ub "$view_b" true >/dev/null \
    || e2e_fail "could not request edit mode on containment $view_b"
e2e_call setViewEditMode ub "$view_b" false >/dev/null \
    || e2e_fail "could not cancel pending edit mode on containment $view_b"
end_ns="$(date +%s%N)"
elapsed_ms=$(( (end_ns - start_ns) / 1000000 ))
(( elapsed_ms < 400 )) \
    || e2e_fail "enter/exit driver took ${elapsed_ms}ms and missed the retarget window"

for _ in $(seq 1 20); do
    [[ "$(view_edit_mode "$view_a")" == false ]] \
        || e2e_fail "old containment $view_a re-entered edit mode after B cancellation"
    [[ "$(view_edit_mode "$view_b")" == false ]] \
        || e2e_fail "pending containment $view_b entered edit mode after its exit"
    sleep 0.1
done

e2e_call setViewEditMode ub "$view_b" true >/dev/null \
    || e2e_fail "ordinary edit enter failed for containment $view_b"
wait_for_edit_mode "$view_b" true \
    || e2e_fail "containment $view_b never entered edit mode after cancellation"
e2e_call setViewEditMode ub "$view_b" false >/dev/null \
    || e2e_fail "ordinary edit exit failed for containment $view_b"
wait_for_edit_mode "$view_b" false \
    || e2e_fail "containment $view_b stayed in edit mode after ordinary exit"

echo "dock edit retarget: B enter/exit canceled in ${elapsed_ms}ms before timeout; later B round-trip passed"
