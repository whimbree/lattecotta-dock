#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
#
# HC3 acceptance test for the MULTI-OUTPUT nested vehicle (C-I2 / P1,
# docs/tracking/e2e-interaction-test-plan.md, open question O7). Run through
# scripts/run-multi-output-e2e.sh, which brings up a TWO-output vehicle
# (E2E_OUTPUT_COUNT=2).
#
# A placement check that only passes when the view happens to land right cannot
# be trusted for the cross-screen F5/A4 scenarios. So this test, like the
# matrix-harness and sceneprobe self-tests before it, is a TRIPWIRE on its own
# driver: it does not just prove a view CAN be placed on the secondary output,
# it proves the check would CATCH a view on the WRONG output and a request for a
# non-existent output. It demonstrably OBSERVES rejections, not only successes.
#
# It proves, end to end in the dual-output vehicle, that:
#   1. the screen<->output mapping is pull-queryable (screensData) and the
#      secondary is DISCOVERED, never hardcoded (O7);
#   2. runtime KScreen mutation realizes exact full-touching, partial-touching,
#      and disconnected portrait-secondary geometry, catches a wrong topology
#      assertion and overlapping rectangles, and restores the captured state;
#   3. [HC3 place-and-assert] a 2out fixture lands its view on the SECONDARY
#      output, asserted by readback (viewsData.screen), with the pin resolved
#      as declared in ScreenPool (screensData id/name/primary);
#   4. [HC3 catch-a-misplacement] the placement check GOES RED when the same
#      view is checked against the primary (it is NOT on primary), and when a
#      view that landed on the primary (a 1out cell) is checked against the
#      secondary - proving the check distinguishes outputs, not just passes;
#   5. [HC3 no-such-output] a 2out cell whose secondary is NOT available is
#      REFUSED (the view never comes up), not silently placed on the primary.
set -uo pipefail
repo="${E2E_REPO:?run through scripts/run-multi-output-e2e.sh}"
source "$repo/tests/e2e/lib.sh"
source "$repo/tests/e2e/matrix/matrix-lib.sh"
source "$repo/tests/e2e/matrix/multi-output-lib.sh"

# this recipe is meaningless in a single-output vehicle; refuse loudly rather
# than silently "passing" against one output
if [[ "${E2E_OUTPUT_COUNT:-1}" != 2 ]]; then
    e2e_fail "multi-output-selftest needs E2E_OUTPUT_COUNT=2 (got ${E2E_OUTPUT_COUNT:-1}); run via scripts/run-multi-output-e2e.sh"
fi

# staging mutates the shared E2E_CONFIG_HOME in place; restore the pristine seed
# for the teardown shutdown check and any later recipe
original_output_topology=""
restore_base() {
    [[ -d "$MATRIX_PRISTINE" ]] || return 0
    local dock_pid=""
    if dock_pid="$(e2e_dock_pid 2>/dev/null)" \
        && [[ -n "$dock_pid" ]] && kill -0 "$dock_pid" 2>/dev/null; then
        e2e_dock_stop >/dev/null 2>&1 || return 1
    fi
    rm -rf "${E2E_CONFIG_HOME:?}"
    cp -r "$MATRIX_PRISTINE" "$E2E_CONFIG_HOME"
}
restore_vehicle() {
    local -r recipe_status=$?
    local cleanup_status=0
    if [[ -n "$original_output_topology" ]]; then
        if ! mo_restore_output_topology "$original_output_topology"; then
            echo "multi-output-selftest: failed to restore the captured output topology" >&2
            cleanup_status=1
        fi
    fi
    if ! restore_base; then
        echo "multi-output-selftest: failed to restore the pristine dock config" >&2
        cleanup_status=1
    fi
    trap - EXIT
    if (( recipe_status != 0 )); then
        exit "$recipe_status"
    fi
    exit "$cleanup_status"
}
trap restore_vehicle EXIT

matrix_init || e2e_fail "matrix_init failed to snapshot the pristine seed"

pass=0 fail=0
ok()   { echo "  ok   [$1]"; pass=$((pass + 1)); }
bad()  { echo "  FAIL [$1]: ${2:-}" >&2; fail=$((fail + 1)); }
# check_rc <wanted-rc> <label> <command...>: run a harness call, compare its
# exit code to the verdict this test expects (mirrors matrix-harness-selftest).
check_rc() {
    local want="$1" label="$2"; shift 2
    local got=0
    "$@" >/dev/null 2>&1 || got=$?
    if [[ "$got" == "$want" ]]; then ok "$label"; else bad "$label" "returned $got, expected $want"; fi
}

echo "== 1. discover the screen<->output mapping over D-Bus (O7, not log-scraped) =="
if mo_discover_outputs; then
    ok "discover-two-outputs"
else
    e2e_fail "could not discover a dual-output topology (screensData); the vehicle is not 2-output"
fi
# the two connectors must be distinct real outputs
[[ -n "$E2E_MO_PRIMARY" && -n "$E2E_MO_SECONDARY" && "$E2E_MO_PRIMARY" != "$E2E_MO_SECONDARY" ]] \
    && ok "primary != secondary" || bad "primary != secondary" "primary='$E2E_MO_PRIMARY' secondary='$E2E_MO_SECONDARY'"

original_output_topology="$(mo_capture_output_topology)" \
    || e2e_fail "could not capture the nested KScreen topology before mutation"

echo "== 2. classify exact full, partial, and disconnected output topology =="
for requested_topology in full-touching partial-touching disconnected; do
    if accepted_geometry="$(mo_place_secondary_for_topology "$requested_topology")"; then
        ok "$requested_topology topology accepted at $accepted_geometry"
    else
        e2e_fail "could not realize exact $requested_topology output geometry"
    fi
done

# Controlled negative proof: the actual final geometry is disconnected, so the
# same classifier assertion must reject a full-touching expectation.
if mo_assert_output_topology full-touching >/dev/null 2>&1; then
    bad "wrong-topology-caught" "disconnected outputs were accepted as full-touching"
else
    ok "wrong-topology-caught (disconnected is NOT full-touching)"
fi

# A pair with positive area overlap is outside the three accepted topology
# classes. The pure classifier must refuse it rather than label it disconnected.
if mo_classify_rectangles 0 0 1600 1000 1500 100 1000 1600 >/dev/null 2>&1; then
    bad "overlap-refused" "overlapping output rectangles received an accepted classification"
else
    ok "overlap-refused (overlapping rectangles are outside the contract)"
fi

mo_restore_output_topology "$original_output_topology" \
    || e2e_fail "could not restore the original output topology after classifier proof"
mo_discover_outputs >/dev/null \
    || e2e_fail "could not rediscover outputs after topology restoration"

echo "== 3. HC3(a): a 2out fixture lands its view on the SECONDARY output =="
if matrix_stage dock-bottom-center-2out; then
    ok "stage-2out-cell"
    view="$(matrix_view_id)" || e2e_fail "no view under test after staging the 2out cell"

    # the placement readback: viewsData.screen == the discovered secondary
    check_rc 0 "view-on-secondary (readback)" mo_assert_view_on "$view" "$E2E_MO_SECONDARY"
    # the pin is queryable and resolved as declared (ScreenPool id/name/primary)
    check_rc 0 "pin-resolved (screensData id $E2E_MO_SECONDARY_ID -> secondary)" mo_assert_pin_resolved
    # and the COMPOSITOR draws it where viewsData claims (state-vs-render guard)
    check_rc 0 "render agrees with reported geometry" e2e_assert_geometry_agrees

    echo "== 4. HC3(catch-a-misplacement): the SAME check goes RED for the wrong output =="
    # the view is on the secondary, so asking "is it on the PRIMARY?" MUST fail:
    # this is the tripwire proving the check catches a view on the wrong output
    if mo_assert_view_on "$view" "$E2E_MO_PRIMARY" >/dev/null 2>&1; then
        bad "wrong-output-caught" "a view on the secondary was reported as being on the primary"
    else
        ok "wrong-output-caught (secondary view is NOT reported on primary)"
    fi
else
    bad "stage-2out-cell" "the 2out view did not land on the secondary"
fi

echo "== 5. HC3(catch-a-misplacement): a 1out view (on primary) is NOT reported on the secondary =="
if matrix_stage dock-bottom-center-1out; then
    view1="$(matrix_view_id)" || e2e_fail "no view under test after staging the 1out cell"
    check_rc 0 "1out-view-on-primary (readback)" mo_assert_view_on "$view1" "$E2E_MO_PRIMARY"
    if mo_assert_view_on "$view1" "$E2E_MO_SECONDARY" >/dev/null 2>&1; then
        bad "primary-view-not-on-secondary" "a view on the primary was reported as being on the secondary"
    else
        ok "primary-view-not-on-secondary (the check distinguishes outputs)"
    fi
else
    bad "stage-1out-cell" "the baseline 1out cell did not settle"
fi

echo "== 6. HC3(no-such-output): a 2out cell whose secondary is unavailable is REFUSED =="
# clear the discovered secondary so matrix_gen falls back to the fixture's
# sentinel id (no [ScreenConnectors] mapping): the dock must REJECT the view
# ("Rejected because Screen is not available"), never place it on the primary.
# A short timeout: a rejected view never appears, so we need not wait the full
# stage budget to be sure.
( unset E2E_MO_SECONDARY
  MATRIX_STAGE_TIMEOUT=20 matrix_stage dock-bottom-center-2out )
rc=$?
if [[ "$rc" == 2 ]]; then
    ok "no-such-output refused (matrix_stage returned 2, view never came up)"
else
    bad "no-such-output refused" "matrix_stage returned $rc, expected 2 (the view should have been rejected)"
fi

echo "multi-output-selftest: $pass ok, $fail failed"
[[ "$fail" == 0 ]] || e2e_fail "the multi-output vehicle did not observe exact topology, placement, and rejection reliably"
echo "PASS: multi-output-selftest (exact topology and per-screen placement proven; wrong topology, overlap, misplacement, and no-such-output caught)"
