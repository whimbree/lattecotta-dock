#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""HC3 acceptance test for the matrix harness (P0 / C-I1), on the typed API.

A harness that only proves the happy path is untrustworthy: it could not be
relied on to assert "the abort left no residue", because it would report success
regardless. So this test is a TRIPWIRE on the harness itself: it asserts the
harness's OWN verdicts and demonstrably OBSERVES rejections, not only successes.

It proves, end to end in the nested vehicle, that the harness:
  1. stages a parametrized DOCK cell and reports a correct commit as PASS,
  2. stages a PANEL cell and confirms the derived viewType by readback,
  3. runs the abort backbone on a clean no-op and reports NO residue,
  4. reports a WRONG expected-readback as FAIL (not a green pass)  [HC3 (a)],
  5. reports abort RESIDUE (a leaked edit-mode session) as FAIL - the crux of the
     whole abort column: the backbone actually SEES residue,
  6. REFUSES a malformed cell (bad token; the generator rejects it)  [HC3 (b)],
  7. REFUSES a fixture that did not realize as declared (the dock-side guard),
  8-10. SEES residue in EACH new persisted surface - lattedockrc
     [UniversalSettings], lattedockrc [ScreenConnectors], and a named applet
     config group - not just the editMode dimension: for each, inject residue
     into ONLY that surface and prove assert_baseline_restored reports it, naming
     that surface. Without this a hardened detector could still be blind to a
     whole surface and false-PASS the abort it was hardened for.

This is the BP-3a pilot: the port of matrix-harness-selftest.sh over
latte_harness.matrix, exactly as 000-smoke.py piloted the recipe API. Every
numbered self-check is preserved, negative controls included.
"""

from __future__ import annotations

import atexit
import io
import os
import shutil
import subprocess
import sys
import time
from collections.abc import Callable
from contextlib import redirect_stderr, redirect_stdout, suppress
from pathlib import Path

from latte_harness import matrix, matrix_fixture, recipe


# restore the untouched base for any later recipe (staging mutates the shared
# E2E_CONFIG_HOME in place); best-effort, exactly as the bash restore_base trap.
def restore_base() -> None:
    with suppress(Exception):
        pristine = matrix.pristine_seed_dir()
        if not pristine.is_dir():
            return
        # matrix.stop_dock reaps the recipe-started dock (recipe.dock_stop cannot,
        # so it would leave a zombie that races the runner's teardown).
        _ = matrix.stop_dock()
        config_home = Path(os.environ["E2E_CONFIG_HOME"])
        if config_home.exists():
            shutil.rmtree(config_home)
        _ = shutil.copytree(pristine, config_home)


# A leaked-abort verb: enters edit mode and NEVER exits, so an abort scenario run
# against it MUST see residue. This is the negative control that proves the abort
# backbone is not vacuous (it registers its own verb, exactly as a scenario would).
def _editmode_leaky_drive(view: int, _outcome: str) -> None:
    matrix.drive_action("setViewEditMode", "ub", str(view), "true")
    for _ in range(30):
        with suppress(matrix.MatrixProbeError):
            if matrix.verb_editmode_probe(view) == "true":
                break
        time.sleep(0.2)
    # deliberately no exit from edit mode: the residue is the point.


matrix.register_verb("editmode_leaky", _editmode_leaky_drive, matrix.verb_editmode_probe)


# residue injectors: each strands state into exactly ONE persisted surface, via
# kwriteconfig6 (so the file stays well-formed and a foreign marker key survives a
# dock sync - KConfig merges rather than truncates).
def _kwrite(*args: str) -> None:
    _ = subprocess.run(["kwriteconfig6", *args], check=True)


def inject_universal(_view: int) -> None:
    _kwrite(
        "--file",
        f"{os.environ['E2E_CONFIG_HOME']}/lattedockrc",
        "--group",
        "UniversalSettings",
        "--key",
        "matrixResidueMarker",
        "1",
    )


def inject_screenpool(_view: int) -> None:
    # a phantom connector id (>= FIRSTSCREENID) that no real output owns - the
    # exact shape of the A4 "vacated but never claimed" strand.
    _kwrite(
        "--file",
        f"{os.environ['E2E_CONFIG_HOME']}/lattedockrc",
        "--group",
        "ScreenConnectors",
        "--key",
        "99",
        "PHANTOM-MATRIX:::0,0 100x100",
    )


def inject_applet_launcher(view: int, appid: str) -> None:
    _kwrite(
        "--file",
        os.environ["E2E_LAYOUT"],
        "--group",
        "Containments",
        "--group",
        str(view),
        "--group",
        "Applets",
        "--group",
        appid,
        "--group",
        "Configuration",
        "--group",
        "General",
        "--key",
        "matrixResidueMarker",
        "1",
    )


class Tally:
    """The self-check score: the harness observing rejections is the point."""

    def __init__(self) -> None:
        self.passed = 0
        self.failed = 0

    def ok(self, message: str) -> None:
        print(f"  ok   {message}")
        self.passed += 1

    def bad(self, message: str) -> None:
        print(f"  FAIL {message}", file=sys.stderr)
        self.failed += 1


def _staged(cell: str) -> int:
    """matrix.stage with the harness chatter suppressed (the bash `>/dev/null 2>&1`)."""
    with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
        return matrix.stage(cell)


def check_rc(tally: Tally, want: int, label: str, call: Callable[[], int]) -> None:
    """Run the harness call and compare its verdict code to what this test expects
    of the harness (the bash check_rc: `"$@" >/dev/null 2>&1 || got=$?`).
    """
    with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
        got = call()
    if got == want:
        tally.ok(f"[{label}] harness returned {got} as expected")
    else:
        tally.bad(f"[{label}] harness returned {got}, expected {want}")


def assert_surface_detects(
    tally: Tally,
    label: str,
    cell: str,
    expect: str,
    inject: Callable[[int], None],
    applet_group: str = "",
) -> None:
    """Stage fresh, capture the baseline (with the applet group in the surface set
    if given), strand residue into ONE surface via ``inject``, and prove
    assert_baseline_restored reports residue NAMING ``expect`` (code 1). Proves the
    detector SEES residue in that specific surface, not just that SOME surface fired.
    """
    os.environ["MATRIX_APPLET_CONFIG_GROUPS"] = applet_group
    try:
        if _staged(cell) != 0:
            tally.bad(f"[{label}] could not stage {cell}")
            return
        try:
            view = matrix.view_id()
        except matrix.MatrixProbeError:
            tally.bad(f"[{label}] no view under test")
            return
        try:
            baseline = matrix.baseline_capture(view, "editmode")
        except matrix.MatrixProbeError:
            tally.bad(f"[{label}] baseline capture failed")
            return
        inject(view)
        captured = io.StringIO()
        with redirect_stdout(io.StringIO()), redirect_stderr(captured):
            rc = matrix.assert_baseline_restored(view, "editmode", baseline)
        stderr = captured.getvalue()
        if rc == 1 and f"RESIDUE in surface '{expect}'" in stderr:
            tally.ok(f"[{label}] detector saw residue in surface '{expect}'")
        else:
            tally.bad(f"[{label}] rc={rc}, expected residue named '{expect}'. assert stderr:")
            for line in stderr.splitlines():
                print(f"    {line}", file=sys.stderr)
    finally:
        _ = os.environ.pop("MATRIX_APPLET_CONFIG_GROUPS", None)


def main() -> int:
    if matrix.init() != 0:
        recipe.fail("matrix_init failed to snapshot the pristine seed")
    tally = Tally()

    print("== 1. happy commit: a dock cell entering edit mode reports PASS ==")
    check_rc(
        tally,
        0,
        "commit-correct",
        lambda: matrix.scenario_commit("dock-top-left-1out", "editmode", "true"),
    )

    print("== 2. panel cell: the derived viewType is confirmed by readback ==")
    # matrix.stage asserts the realized type==panel; a refusal (2) here would mean
    # the panel derivation did not hold, which the harness would (correctly) refuse.
    check_rc(tally, 0, "panel-realizes", lambda: matrix.stage("panel-bottom-justify-1out"))

    print("== 3. abort backbone on a clean no-op reports NO residue (PASS) ==")
    # spans every surface (view + applet order + layout + lattedockrc universal +
    # screenpool + verb): a clean edit-mode enter/exit must leave all of them intact.
    check_rc(
        tally,
        0,
        "abort-clean",
        lambda: matrix.scenario_abort("dock-bottom-center-1out", "editmode"),
    )

    print("== 4. HC3(a): a WRONG expected-readback is reported as FAIL, not a green pass ==")
    check_rc(
        tally,
        1,
        "commit-wrong-expected",
        lambda: matrix.scenario_commit("dock-top-left-1out", "editmode", "false"),
    )

    print("== 5. abort RESIDUE (a leaked edit session) is reported as FAIL ==")
    check_rc(
        tally,
        1,
        "abort-residue",
        lambda: matrix.scenario_abort("dock-bottom-center-1out", "editmode_leaky"),
    )

    print("== 6. HC3(b): a malformed cell is REFUSED (return 2), not a green pass ==")
    check_rc(
        tally,
        2,
        "malformed-cell",
        lambda: matrix.scenario_commit("dock-diagonal-left-1out", "editmode", "true"),
    )

    print("== 7. a fixture that did not realize as declared is REFUSED ==")
    # stage a real top cell, then ask the realization guard to reconcile it against
    # a LYING expectation (claims left edge): the dock-side guard every scenario
    # leans on must refuse a view that does not match its declaration.
    if _staged("dock-top-center-1out") != 0:
        recipe.fail("could not stage the realization-guard fixture")
    lying = matrix_fixture.ExpectedRealization(type="dock", edge="left", alignment="center")
    check_rc(
        tally,
        2,
        "realization-mismatch",
        lambda: matrix.assert_realized("dock-top-center-1out", lying),
    )

    print("== 8. detector SEES residue in lattedockrc [UniversalSettings] ==")
    assert_surface_detects(
        tally, "residue-universal", "dock-bottom-center-1out", "universal", inject_universal
    )

    print("== 9. detector SEES residue in lattedockrc [ScreenConnectors] (A4 phantom connector) ==")
    assert_surface_detects(
        tally, "residue-screenpool", "dock-bottom-center-1out", "screenpool", inject_screenpool
    )

    print("== 10. detector SEES residue in a named applet config group (launcher key) ==")
    # stage, discover the tasks applet, and prove a strand in ITS config group is
    # caught on the appletcfg surface a scenario opts into via
    # MATRIX_APPLET_CONFIG_GROUPS. applet_config_group is the API a scenario uses.
    if _staged("dock-bottom-center-1out") == 0:
        view = matrix.view_id()
        try:
            tasks_group = matrix.applet_config_group(view, "org.kde.latte.plasmoid")
        except matrix.MatrixError:
            recipe.fail("could not resolve the tasks applet config group")
        # the applet id is the trailing [N] of the group prefix (fed to kwriteconfig).
        appid = tasks_group.rsplit("[Applets][", 1)[1].rstrip("]")
        assert_surface_detects(
            tally,
            "residue-appletcfg",
            "dock-bottom-center-1out",
            f"appletcfg:{tasks_group}",
            lambda v: inject_applet_launcher(v, appid),
            tasks_group,
        )
    else:
        tally.bad("[residue-appletcfg] could not stage the applet-config fixture")

    print(f"matrix-harness-selftest: {tally.passed} ok, {tally.failed} failed")
    if tally.failed != 0:
        recipe.fail("the harness did not behave as a trustworthy driver (see failures above)")
    print(
        "PASS: matrix-harness-selftest (the harness observes rejections in every residue surface)"
    )
    return 0


if __name__ == "__main__":
    atexit.register(restore_base)
    sys.exit(main())
