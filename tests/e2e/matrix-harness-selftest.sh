#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# HC3 acceptance test for the matrix harness (P0 / C-I1). A harness that only
# proves the happy path is untrustworthy: it could not be relied on to assert
# "the abort left no residue", because it would report success regardless. So
# this test is a TRIPWIRE on the harness itself (the sceneprobe gate's
# good/bad/blank precedent, and run-e2e's XPASS discipline, applied to the
# driver): it asserts the harness's OWN verdicts, and demonstrably OBSERVES
# rejections, not only successes.
#
# It proves, end to end in the nested vehicle, that the harness:
#   1. stages a parametrized DOCK cell and reports a correct commit as PASS,
#   2. stages a PANEL cell and confirms the derived viewType by readback,
#   3. runs the abort backbone on a clean no-op and reports NO residue,
#   4. reports a WRONG expected-readback as FAIL (not a green pass)  [HC3 (a)],
#   5. reports abort RESIDUE (a leaked edit-mode session) as FAIL - the crux
#      of the whole abort column: the backbone actually SEES residue,
#   6. REFUSES a malformed cell (bad token; the generator rejects it)  [HC3 (b)],
#   7. REFUSES a fixture that did not realize as declared (the dock-side guard
#      every scenario leans on).
set -uo pipefail
repo="${E2E_REPO:?run through scripts/run-e2e.sh}"
source "$repo/tests/e2e/lib.sh"
source "$repo/tests/e2e/matrix/matrix-lib.sh"

# restore the untouched base for any later recipe (staging mutates the shared
# E2E_CONFIG_HOME in place)
restore_base() {
    [[ -d "$MATRIX_PRISTINE" ]] || return 0
    e2e_dock_stop >/dev/null 2>&1 || true
    rm -rf "${E2E_CONFIG_HOME:?}"
    cp -r "$MATRIX_PRISTINE" "$E2E_CONFIG_HOME"
}
trap restore_base EXIT

matrix_init || e2e_fail "matrix_init failed to snapshot the pristine seed"

# a leaked-abort verb: enters edit mode and NEVER exits, so an abort scenario
# run against it MUST see residue. This is the negative control that proves the
# abort backbone is not vacuous (it defines its own verb, exactly as a scenario
# chunk would).
matrix_verb_editmode_leaky_drive() {
    local view="$1"
    e2e_call setViewEditMode ub "$view" true >/dev/null || return 1
    local i
    for ((i = 0; i < 30; i++)); do
        [[ "$(matrix_verb_editmode_probe "$view")" == true ]] && break
        sleep 0.2
    done
    return 0   # deliberately no exit: the residue is the point
}
matrix_verb_editmode_leaky_probe() { matrix_verb_editmode_probe "$1"; }

pass=0 fail=0
# check_rc <wanted-rc> <label> <command...>: run the harness call, compare its
# exit code to the verdict this test expects of the harness.
check_rc() {
    local want="$1" label="$2"; shift 2
    local got=0
    "$@" >/dev/null 2>&1 || got=$?
    if [[ "$got" == "$want" ]]; then
        echo "  ok   [$label] harness returned $got as expected"
        pass=$((pass + 1))
    else
        echo "  FAIL [$label] harness returned $got, expected $want" >&2
        fail=$((fail + 1))
    fi
}

echo "== 1. happy commit: a dock cell entering edit mode reports PASS =="
check_rc 0 "commit-correct" matrix_scenario_commit dock-top-left-1out editmode true

echo "== 2. panel cell: the derived viewType is confirmed by readback =="
# matrix_stage asserts the realized type==panel; a refusal (2) here would mean
# the panel derivation did not hold, which the harness would (correctly) refuse.
check_rc 0 "panel-realizes" matrix_stage panel-bottom-justify-1out

echo "== 3. abort backbone on a clean no-op reports NO residue (PASS) =="
check_rc 0 "abort-clean" matrix_scenario_abort dock-bottom-center-1out editmode

echo "== 4. HC3(a): a WRONG expected-readback is reported as FAIL, not a green pass =="
check_rc 1 "commit-wrong-expected" matrix_scenario_commit dock-top-left-1out editmode false

echo "== 5. abort RESIDUE (a leaked edit session) is reported as FAIL =="
check_rc 1 "abort-residue" matrix_scenario_abort dock-bottom-center-1out editmode_leaky

echo "== 6. HC3(b): a malformed cell is REFUSED (return 2), not a green pass =="
check_rc 2 "malformed-cell" matrix_scenario_commit dock-diagonal-left-1out editmode true

echo "== 7. a fixture that did not realize as declared is REFUSED =="
# stage a real top cell, then ask the realization guard to reconcile it against
# a LYING manifest (claims left edge): the dock-side guard every scenario leans
# on must refuse a view that does not match its declaration.
matrix_stage dock-top-center-1out >/dev/null 2>&1 || e2e_fail "could not stage the realization-guard fixture"
lying_manifest='{"expect":{"type":"dock","edge":"left","alignment":"center"}}'
check_rc 2 "realization-mismatch" matrix_assert_realized dock-top-center-1out "$lying_manifest"

echo "matrix-harness-selftest: $pass ok, $fail failed"
[[ "$fail" == 0 ]] || e2e_fail "the harness did not behave as a trustworthy driver (see failures above)"
echo "PASS: matrix-harness-selftest (the harness observes rejections, not just successes)"
