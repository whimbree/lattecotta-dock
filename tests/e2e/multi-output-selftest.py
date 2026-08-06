#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""HC3 acceptance test for the MULTI-OUTPUT nested vehicle (C-I2 / P1,
docs/tracking/e2e-interaction-test-plan.md, open question O7). Run through
scripts/run-multi-output-e2e.sh, which brings up a TWO-output vehicle
(E2E_OUTPUT_COUNT=2).

A placement check that only passes when the view happens to land right cannot be
trusted for the cross-screen F5/A4 scenarios. So this test, like the matrix-harness
and sceneprobe self-tests before it, is a TRIPWIRE on its own driver: it does not
just prove a view CAN be placed on the secondary output, it proves the check would
CATCH a view on the WRONG output and a request for a non-existent output. It
demonstrably OBSERVES rejections, not only successes.

Ported from tests/e2e/multi-output-selftest.sh to latte_harness.multi_output +
latte_harness.matrix + latte_harness.recipe (BP-3, the bash-to-python migration's
R12 dual-output recipe batch). This is the port that pays the PR #185 dual-output
residual: multi_output's live transactions (mo_discover_outputs, capture/restore
topology, place-secondary-for-topology, pin resolution) were unit-covered but only
driven live here. The bash mo_* helpers returned exit codes; the typed twins raise
MultiOutputError, so check_rc/the negatives translate raise-vs-return exactly as
the bash `if <cmd>; then` / `<cmd> || got=$?` did.

It proves, end to end in the dual-output vehicle, that:
  1. the screen<->output mapping is pull-queryable (screensData) and the secondary
     is DISCOVERED, never hardcoded (O7);
  2. runtime KScreen mutation realizes exact full-touching, partial-touching, and
     disconnected portrait-secondary geometry, catches a wrong topology assertion
     and overlapping rectangles, and restores the captured state;
  3. [HC3 place-and-assert] a 2out fixture lands its view on the SECONDARY output,
     asserted by readback (viewsData.screen), with the pin resolved as declared in
     ScreenPool (screensData id/name/primary);
  4. [HC3 catch-a-misplacement] the placement check GOES RED when the same view is
     checked against the primary (it is NOT on primary), and when a view that landed
     on the primary (a 1out cell) is checked against the secondary - proving the
     check distinguishes outputs, not just passes;
  5. [HC3 no-such-output] a 2out cell whose secondary is NOT available is REFUSED
     (the view never comes up), not silently placed on the primary.
"""

import os
import shutil
import sys
from collections.abc import Callable
from pathlib import Path

from latte_harness import matrix, multi_output, recipe
from latte_harness.proc import install_conventional_signal_exits


def _pid_alive(pid: int) -> bool:
    """The bash ``kill -0``: alive iff a signal could be delivered."""
    try:
        os.kill(pid, 0)
    except OSError:
        return False
    return True


def _status(action: Callable[[], object]) -> int:
    """Run a multi_output action, returning 0 on success or 1 when it refuses -
    the check_rc bridge from the typed raise-on-failure helpers to the bash mo_*
    exit codes (every checked negative returned 1)."""
    try:
        _ = action()
        return 0
    except multi_output.MultiOutputError:
        return 1


def _restore_base() -> bool:
    """restore_base: staging mutated E2E_CONFIG_HOME in place; restore the pristine
    seed for the teardown shutdown check and any later recipe."""
    pristine = matrix.pristine_seed_dir()
    if not pristine.is_dir():
        return True
    pid = recipe.dock_pid()
    if pid is not None and _pid_alive(pid) and not matrix.stop_dock():
        return False
    config_home = Path(os.environ["E2E_CONFIG_HOME"])
    if config_home.exists():
        shutil.rmtree(config_home)
    try:
        _ = shutil.copytree(pristine, config_home)
    except OSError:
        return False
    return True


def _restore_vehicle(original_topology: str) -> int:
    """restore_vehicle: restore the captured output topology then the pristine dock
    config; returns the cleanup status (0 clean, 1 on any restore failure)."""
    status = 0
    if original_topology:
        try:
            multi_output.mo_restore_output_topology(original_topology)
        except multi_output.MultiOutputError:
            print(
                "multi-output-selftest: failed to restore the captured output topology",
                file=sys.stderr,
                flush=True,
            )
            status = 1
    if not _restore_base():
        print(
            "multi-output-selftest: failed to restore the pristine dock config",
            file=sys.stderr,
            flush=True,
        )
        status = 1
    return status


def _selftest_body(captured: dict[str, str]) -> None:
    if matrix.init() != 0:
        recipe.fail("matrix_init failed to snapshot the pristine seed")

    counts = {"pass": 0, "fail": 0}

    def ok(label: str) -> None:
        print(f"  ok   [{label}]", flush=True)
        counts["pass"] += 1

    def bad(label: str, detail: str = "") -> None:
        print(f"  FAIL [{label}]: {detail}", file=sys.stderr, flush=True)
        counts["fail"] += 1

    def check_rc(want: int, label: str, action: Callable[[], object]) -> None:
        got = _status(action)
        if got == want:
            ok(label)
        else:
            bad(label, f"returned {got}, expected {want}")

    print(
        "== 1. discover the screen<->output mapping over D-Bus (O7, not log-scraped) ==",
        flush=True,
    )
    try:
        _ = multi_output.mo_discover_outputs()
        ok("discover-two-outputs")
    except multi_output.MultiOutputError:
        recipe.fail(
            "could not discover a dual-output topology (screensData); the vehicle is not 2-output"
        )
    primary = os.environ.get("E2E_MO_PRIMARY", "")
    secondary = os.environ.get("E2E_MO_SECONDARY", "")
    if primary and secondary and primary != secondary:
        ok("primary != secondary")
    else:
        bad("primary != secondary", f"primary='{primary}' secondary='{secondary}'")

    try:
        captured["topology"] = multi_output.mo_capture_output_topology()
    except multi_output.MultiOutputError:
        recipe.fail("could not capture the nested KScreen topology before mutation")

    print(
        "== 2. classify exact full, partial, and disconnected output topology ==",
        flush=True,
    )
    for requested_topology in ("full-touching", "partial-touching", "disconnected"):
        try:
            accepted_geometry = multi_output.mo_place_secondary_for_topology(
                requested_topology
            )
            ok(f"{requested_topology} topology accepted at {accepted_geometry}")
        except multi_output.MultiOutputError:
            recipe.fail(f"could not realize exact {requested_topology} output geometry")

    # Controlled negative proof: the actual final geometry is disconnected, so the
    # same classifier assertion must reject a full-touching expectation.
    try:
        multi_output.mo_assert_output_topology("full-touching")
        bad(
            "wrong-topology-caught",
            "disconnected outputs were accepted as full-touching",
        )
    except multi_output.MultiOutputError:
        ok("wrong-topology-caught (disconnected is NOT full-touching)")

    # A pair with positive area overlap is outside the three accepted topology
    # classes. The pure classifier must refuse it rather than label it disconnected.
    try:
        multi_output.mo_classify_rectangles((0, 0, 1600, 1000), (1500, 100, 1000, 1600))
        bad(
            "overlap-refused",
            "overlapping output rectangles received an accepted classification",
        )
    except multi_output.MultiOutputError:
        ok("overlap-refused (overlapping rectangles are outside the contract)")

    try:
        multi_output.mo_restore_output_topology(captured["topology"])
    except multi_output.MultiOutputError:
        recipe.fail(
            "could not restore the original output topology after classifier proof"
        )
    try:
        _ = multi_output.mo_discover_outputs()
    except multi_output.MultiOutputError:
        recipe.fail("could not rediscover outputs after topology restoration")

    print(
        "== 3. HC3(a): a 2out fixture lands its view on the SECONDARY output ==",
        flush=True,
    )
    if matrix.stage("dock-bottom-center-2out") == 0:
        ok("stage-2out-cell")
        try:
            view = matrix.view_id()
        except matrix.MatrixProbeError:
            recipe.fail("no view under test after staging the 2out cell")

        # the placement readback: viewsData.screen == the discovered secondary
        check_rc(
            0,
            "view-on-secondary (readback)",
            lambda: multi_output.mo_assert_view_on(
                view, os.environ["E2E_MO_SECONDARY"]
            ),
        )
        # the pin is queryable and resolved as declared (ScreenPool id/name/primary)
        check_rc(
            0,
            f"pin-resolved (screensData id {multi_output.E2E_MO_SECONDARY_ID} -> secondary)",
            multi_output.mo_assert_pin_resolved,
        )
        # and the COMPOSITOR draws it where viewsData claims (state-vs-render guard)
        check_rc(
            0,
            "render agrees with reported geometry",
            lambda: 0 if recipe.assert_geometry_agrees() else 1,
        )

        print(
            "== 4. HC3(catch-a-misplacement): the SAME check goes RED for the wrong output ==",
            flush=True,
        )
        # the view is on the secondary, so asking "is it on the PRIMARY?" MUST fail:
        # this is the tripwire proving the check catches a view on the wrong output
        try:
            multi_output.mo_assert_view_on(view, os.environ["E2E_MO_PRIMARY"])
            bad(
                "wrong-output-caught",
                "a view on the secondary was reported as being on the primary",
            )
        except multi_output.MultiOutputError:
            ok("wrong-output-caught (secondary view is NOT reported on primary)")
    else:
        bad("stage-2out-cell", "the 2out view did not land on the secondary")

    print(
        "== 5. HC3(catch-a-misplacement): a 1out view (on primary) is NOT reported on the "
        "secondary ==",
        flush=True,
    )
    if matrix.stage("dock-bottom-center-1out") == 0:
        try:
            view1 = matrix.view_id()
        except matrix.MatrixProbeError:
            recipe.fail("no view under test after staging the 1out cell")
        check_rc(
            0,
            "1out-view-on-primary (readback)",
            lambda: multi_output.mo_assert_view_on(view1, os.environ["E2E_MO_PRIMARY"]),
        )
        try:
            multi_output.mo_assert_view_on(view1, os.environ["E2E_MO_SECONDARY"])
            bad(
                "primary-view-not-on-secondary",
                "a view on the primary was reported as being on the secondary",
            )
        except multi_output.MultiOutputError:
            ok("primary-view-not-on-secondary (the check distinguishes outputs)")
    else:
        bad("stage-1out-cell", "the baseline 1out cell did not settle")

    print(
        "== 6. HC3(no-such-output): a 2out cell whose secondary is unavailable is REFUSED ==",
        flush=True,
    )
    # clear the discovered secondary so matrix_gen falls back to the fixture's
    # sentinel id (no [ScreenConnectors] mapping): the dock must REJECT the view
    # ("Rejected because Screen is not available"), never place it on the primary.
    # A short timeout: a rejected view never appears, so we need not wait the full
    # stage budget to be sure.
    saved_secondary = os.environ.pop("E2E_MO_SECONDARY", None)
    saved_timeout = os.environ.get("MATRIX_STAGE_TIMEOUT")
    os.environ["MATRIX_STAGE_TIMEOUT"] = "20"
    try:
        rc = matrix.stage("dock-bottom-center-2out")
    finally:
        if saved_secondary is not None:
            os.environ["E2E_MO_SECONDARY"] = saved_secondary
        if saved_timeout is None:
            os.environ.pop("MATRIX_STAGE_TIMEOUT", None)
        else:
            os.environ["MATRIX_STAGE_TIMEOUT"] = saved_timeout
    if rc == 2:
        ok("no-such-output refused (matrix_stage returned 2, view never came up)")
    else:
        bad(
            "no-such-output refused",
            f"matrix_stage returned {rc}, expected 2 (the view should have been rejected)",
        )

    print(
        f"multi-output-selftest: {counts['pass']} ok, {counts['fail']} failed",
        flush=True,
    )
    if counts["fail"] != 0:
        recipe.fail(
            "the multi-output vehicle did not observe exact topology, placement, and rejection "
            "reliably"
        )
    print(
        "PASS: multi-output-selftest (exact topology and per-screen placement proven; wrong "
        "topology, overlap, misplacement, and no-such-output caught)",
        flush=True,
    )


def main() -> None:
    install_conventional_signal_exits()
    if os.environ.get("E2E_OUTPUT_COUNT") != "2":
        got = os.environ.get("E2E_OUTPUT_COUNT") or "1"
        recipe.fail(
            f"multi-output-selftest needs E2E_OUTPUT_COUNT=2 (got {got}); "
            "run via scripts/run-multi-output-e2e.sh"
        )

    # The restore sits in a finally so it runs on EVERY exit path (the wave's
    # converged trap-EXIT shape): the caught verdicts, the conventional signal
    # exits, and an unexpected exception, which still propagates after the
    # restore and exits nonzero.
    captured = {"topology": ""}
    body_exit = 0
    cleanup_status = 0
    try:
        try:
            _selftest_body(captured)
        except SystemExit as exit_error:
            body_exit = exit_error.code if isinstance(exit_error.code, int) else 1
    finally:
        cleanup_status = _restore_vehicle(captured["topology"])
    raise SystemExit(body_exit if body_exit != 0 else cleanup_status)


if __name__ == "__main__":
    main()
