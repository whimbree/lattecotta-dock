#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# HC3 acceptance test for the edit-mode settings audit harness
# (docs/tracking/edit-mode-settings-audit-plan.md, cluster CL-0). It proves the
# config-snapshot-diff harness OBSERVES A REJECTION - it reports FAIL on a wrong
# outcome, never a green pass - so the ~dozen suspected-broken controls the audit
# hunts cannot slip through. Two legs:
#
#   1. CRAFTED (deterministic): feed the audit_assert_* helpers snapshots that
#      stand in for a control writing the wrong key / a stray coupled key / a
#      no-op, and assert each REJECTION path returns FAIL. Also proves the safe
#      direction of the KConfig default-deletion trap (a key present-then-absent
#      is a loud change, never a silent pass). Mirror of the C++ proofs in
#      tests/units/configsnapshotdifftest.cpp and tests/settingswiringharnesstest.cpp.
#   2. LIVE: the viewConfigData / appletConfigData readbacks answer for a real
#      view, the snapshots carry the audit's keys, and a no-change between two
#      live reads is REJECTED by P1 (proving the harness catches a no-op on live
#      snapshots too, not only crafted ones).
set -uo pipefail
source "${E2E_REPO:?run through scripts/run-e2e.sh}/tests/e2e/lib.sh"
source "$E2E_REPO/tests/e2e/matrix/matrix-lib.sh"
source "$E2E_REPO/tests/e2e/audit/audit-lib.sh"

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# expect_pass / expect_fail <desc> <cmd...>: run a harness assertion and require
# a specific verdict. A harness that returns the WRONG verdict fails the recipe.
expect_pass() {
    local desc="$1"; shift
    if "$@" >/dev/null 2>&1; then
        echo "  PASS (correctly accepted): $desc"
    else
        e2e_fail "harness REJECTED a correct outcome: $desc"
    fi
}
expect_fail() {
    local desc="$1"; shift
    if "$@" >/dev/null 2>&1; then
        e2e_fail "harness ACCEPTED a wrong outcome (HC3 breach): $desc"
    else
        echo "  PASS (correctly rejected): $desc"
    fi
}

snap() { printf '%s\n' "$@" > "$1"; }

echo "== leg 1: crafted rejection proofs (deterministic) =="

# a stable three-key baseline
snap "$work/before" $'maxLength\t100' $'minLength\t100' $'offset\t0'

# correct: only maxLength moved -> P1 applies, P2 exact
snap "$work/after_good" $'maxLength\t90' $'minLength\t100' $'offset\t0'
expect_pass "maxLength drive applies"          audit_assert_applies   "$work/before" "$work/after_good" maxLength
expect_pass "maxLength drive changed only maxLength" audit_assert_only_keys "$work/before" "$work/after_good" maxLength

# D15 shape: maxLength drive also dragged minLength -> P2 must FAIL
snap "$work/after_stray" $'maxLength\t90' $'minLength\t90' $'offset\t0'
expect_fail "stray coupled minLength write is caught" audit_assert_only_keys "$work/before" "$work/after_stray" maxLength

# D10 shape: nothing changed -> P1 must FAIL
expect_fail "no-op control is caught" audit_assert_applies "$work/before" "$work/before" maxLength

# wrong-key: offset moved when maxLength was expected -> P1 for maxLength FAILS,
# P2 for {maxLength} FAILS
snap "$work/after_wrong" $'maxLength\t100' $'minLength\t100' $'offset\t5'
expect_fail "wrong-key write fails maxLength P1"   audit_assert_applies   "$work/before" "$work/after_wrong" maxLength
expect_fail "wrong-key write fails maxLength P2"   audit_assert_only_keys "$work/before" "$work/after_wrong" maxLength

# missing expected key: expected {maxLength,offset} but only maxLength moved
expect_fail "under-write (missing expected key) is caught" \
    audit_assert_only_keys "$work/before" "$work/after_good" maxLength offset

# default-deletion safety: a key present-then-absent is a LOUD change (the safe
# false-FAIL direction), never a silent pass
snap "$work/after_deleted" $'maxLength\t100' $'offset\t0'
expect_pass "vanished key surfaces as a change (default-deletion safe)" \
    audit_assert_applies "$work/before" "$work/after_deleted" minLength

# P3 reflect-state: value present passes, wrong/absent value fails
expect_pass "reflect matches the stored value"   audit_assert_reflects "$work/before" maxLength 100
expect_fail "reflect rejects a wrong value"       audit_assert_reflects "$work/before" maxLength 999
expect_fail "reflect rejects an absent key"       audit_assert_reflects "$work/before" iconSize 48

# P4 cross-view agreement: same value agrees, different disagrees
snap "$work/surfaceA" $'maxLength\t90'
snap "$work/surfaceB" $'sliderMax\t90'
snap "$work/surfaceC" $'sliderMax\t80'
expect_pass "two surfaces holding one value agree"      audit_assert_agrees "$work/surfaceA" maxLength "$work/surfaceB" sliderMax
expect_fail "two surfaces holding different values are caught" audit_assert_agrees "$work/surfaceA" maxLength "$work/surfaceC" sliderMax

echo "== leg 2: live readback + no-op rejection =="

view="$(e2e_tasks_view)" || e2e_fail "no tasks view to exercise the live readback"

audit_config_snapshot "$view" > "$work/live_before" || e2e_fail "viewConfigData snapshot failed for view $view"
[[ -s "$work/live_before" ]] || e2e_fail "live config snapshot is empty for view $view"
grep -q $'^maxLength\t' "$work/live_before" || e2e_fail "live config snapshot has no maxLength key"
echo "  live config snapshot has $(wc -l < "$work/live_before") keys incl. maxLength"

audit_view_snapshot "$view" > "$work/live_view" || e2e_fail "viewConfigData 'view' snapshot failed for view $view"
grep -q $'^byPassWM\t' "$work/live_view" || e2e_fail "live 'view' snapshot has no byPassWM (the live C++ half)"
echo "  live 'view' snapshot carries the C++ P3 props (byPassWM, indicatorType, ...)"

# a second read with no drive between them: the harness must REJECT this as a
# no-op (P1 fails), proving the rejection path works on live snapshots too
audit_config_snapshot "$view" > "$work/live_after"
expect_fail "no-change between two live reads is caught" \
    audit_assert_applies "$work/live_before" "$work/live_after" maxLength

# and the tasks-config readback answers for CL-5's D10 question
if applet="$(audit_tasks_applet_id "$view" 2>/dev/null)"; then
    audit_applet_config_snapshot "$view" "$applet" > "$work/live_tasks" || e2e_fail "appletConfigData snapshot failed"
    [[ -s "$work/live_tasks" ]] || e2e_fail "tasks-config snapshot is empty"
    echo "  tasks-config snapshot (applet $applet) has $(wc -l < "$work/live_tasks") keys"
else
    echo "  (no tasks plasmoid under view $view; tasks-config leg skipped)"
fi

echo "audit harness self-test: rejections caught, live readbacks answer"
