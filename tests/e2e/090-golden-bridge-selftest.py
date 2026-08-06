#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""HC3 acceptance test for the render-golden bridge (P2 / C-I6), on the typed API.

A golden compare that only ever passes on a match cannot catch a visual
regression - so, exactly like the sceneprobe gate's good/bad/blank self-test and
the matrix harness's own tripwire, this proves the bridge OBSERVES A REJECTION:
a deliberately-wrong golden and a real visual difference beyond Tolerance are
reported as FAIL, a missing required golden is a hard FAIL, and only a genuine
match passes.

It captures ONE settled crop of the vehicle dock and drives every verdict off
that single frame, so the proof is deterministic (no cross-shot dependence -
that determinism is the scenario chunks' concern when they bless real goldens,
gated at Tolerance per O3). The legs:
  1. screenshot_crop produces a real crop of the running dock,
  2. an IDENTICAL golden -> MATCH (the compare passes on a match),
  3. a WRONG golden (a painted-over band) -> FAIL beyond Tolerance   [HC3],
  4. a MISSING golden -> hard FAIL (mirror sceneprobe selftest-blank),
  5. the tier axis is REAL, not hardcoded: a sub-Tolerance +1 shift PASSES at
     tolerance but FAILS at bitexact - the same pair flips verdict at the delta
     boundary, proving toleranceForTier is actually consulted,
  6. assert_golden end-to-end: --bless writes a golden, a missing golden is
     refused, and a re-shot crop matches the just-blessed golden.

BP-3 R11 port of 090-golden-bridge-selftest.sh over latte_harness.matrix_golden
(the typed golden bridge landed in BP-3a): every leg, every expected verdict
code, and the byte-identical ok/FAIL and PASS wording preserved.
"""

from __future__ import annotations

import io
import os
import re
import shutil
import subprocess
import sys
import tempfile
from collections.abc import Callable
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

from latte_harness import matrix_golden, recipe

_CELL = "selftest-dock-bottom-center-1out"


class Tally:
    """The self-check score: the bridge observing rejections is the point."""

    def __init__(self) -> None:
        self.passed = 0
        self.failed = 0

    def check_rc(self, want: int, label: str, call: Callable[[], int]) -> None:
        """Run a bridge call and compare its exit code to the verdict this test
        demands of the bridge (the bash check_rc: ``"$@" >/dev/null 2>&1 ||
        got=$?``). The bridge functions return the code; latte-imgdiff's own
        subprocess output leaks past the Python-level redirect, exactly the noise
        the matrix-harness selftest tolerates.
        """
        with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
            got = call()
        if got == want:
            print(f"  ok   [{label}] bridge returned {got} as expected")
            self.passed += 1
        else:
            print(f"  FAIL [{label}] bridge returned {got}, expected {want}", file=sys.stderr)
            self.failed += 1


def _magick(*args: str) -> int:
    return subprocess.run(["magick", *args], check=False).returncode


def _compare_at_tier(tier: str, actual: str, expected: str) -> int:
    """compare_at_tier: golden_compare with an explicit SCENEPROBE_TIER, scoped to
    the call (the bash subshell export)."""
    saved = os.environ.get("SCENEPROBE_TIER")
    os.environ["SCENEPROBE_TIER"] = tier
    try:
        return matrix_golden.golden_compare(actual, expected)
    finally:
        if saved is None:
            _ = os.environ.pop("SCENEPROBE_TIER", None)
        else:
            os.environ["SCENEPROBE_TIER"] = saved


def _assert_golden_in(
    repo: str, bless: bool, work: str, cell: str, verb: str, phase: str, rect: str
) -> int:
    """assert_golden_in: assert_golden pointed at a throwaway golden repo, with
    E2E_REPO / E2E_ARTIFACTS / E2E_BLESS scoped to the call so they never leak
    into the committed tree or later legs (the bash subshell)."""
    keys = ("E2E_REPO", "E2E_ARTIFACTS", "E2E_BLESS")
    saved = {key: os.environ.get(key) for key in keys}
    os.environ["E2E_REPO"] = repo
    os.environ["E2E_ARTIFACTS"] = work
    os.environ["E2E_BLESS"] = "1" if bless else "0"
    try:
        return matrix_golden.assert_golden(cell, verb, phase, rect)
    finally:
        for key, value in saved.items():
            if value is None:
                _ = os.environ.pop(key, None)
            else:
                os.environ[key] = value


def _run(work: str) -> None:
    tally = Tally()

    if not recipe.wait_settled(90):
        recipe.fail("the vehicle dock never settled")

    # one deterministic crop of the settled dock, off which every verdict is driven
    try:
        rect = matrix_golden.view_crop_rect()
    except matrix_golden.MatrixGoldenError:
        recipe.fail("could not compute a view crop rect")
    actual = f"{work}/actual.png"
    if matrix_golden.screenshot_crop(actual, rect) != 0:
        recipe.fail(f"e2e_screenshot_crop failed for rect {rect}")
    if not (Path(actual).is_file() and Path(actual).stat().st_size > 0):
        recipe.fail("e2e_screenshot_crop produced an empty image")
    identify = subprocess.run(
        ["magick", "identify", "-format", "%w %h", actual],
        capture_output=True,
        text=True,
        check=False,
    )
    dims = identify.stdout.split()
    if len(dims) != 2 or not dims[0] or not dims[1]:
        recipe.fail("could not read crop dimensions")
    cw, ch = int(dims[0]), int(dims[1])
    print(f"golden-bridge-selftest: crop {cw}x{ch} of the settled dock at {rect}")

    # A narrow left strip of the view for the assert_golden legs (7, 8): those
    # RE-SHOOT the dock, so their crop must be static across two shots. The default
    # seed carries an analog clock (a moving hand), which sits right of the centered
    # applet block; the far-left strip is background/first-launcher and never
    # animates at rest, so a bless-then-re-shoot compare there is deterministic.
    match = re.match(r"^(\d+)x(\d+)\+(\d+)\+(\d+)$", rect)
    if match is None:
        recipe.fail(f"unparseable view rect '{rect}'")
    vw, vh, vx, vy = (int(match.group(i)) for i in range(1, 5))
    sw = vw if vw < 120 else 120
    strip = f"{sw}x{vh}+{vx}+{vy}"

    # derived goldens off the single captured frame (all deterministic)
    golden_match = f"{work}/match.png"
    shutil.copyfile(actual, golden_match)  # identical
    wrong = f"{work}/wrong.png"  # painted-over band
    if (
        _magick(actual, "-fill", "#ff00ff", "-draw", f"rectangle 0,0 {cw // 2},{ch // 2}", wrong)
        != 0
    ):
        recipe.fail("could not synthesize the wrong golden")
    tiny = f"{work}/tiny.png"  # <=Tolerance +1 shift
    if _magick(actual, "-evaluate", "Add", "0.3%", tiny) != 0:
        recipe.fail("could not synthesize the sub-tolerance golden")

    print("== 1. a MATCH (identical golden) passes the compare ==")
    tally.check_rc(
        0,
        "match-passes",
        lambda: matrix_golden.golden_compare(actual, golden_match, f"{work}/match.diff.png"),
    )

    print("== 2. HC3: a WRONG golden (painted-over band) is REJECTED as FAIL ==")
    tally.check_rc(
        1,
        "wrong-fails",
        lambda: matrix_golden.golden_compare(actual, wrong, f"{work}/wrong.diff.png"),
    )

    print("== 3. a MISSING required golden is a hard FAIL, not a silent pass ==")
    tally.check_rc(
        1,
        "missing-fails",
        lambda: matrix_golden.golden_compare(actual, f"{work}/does-not-exist.png"),
    )

    print("== 4. the tier axis is real: a +1 (<=Tolerance) shift PASSES at tolerance... ==")
    tally.check_rc(0, "tiny-tolerance-passes", lambda: _compare_at_tier("tolerance", actual, tiny))

    print("== 5. ...but the SAME pair FAILS at bitexact (toleranceForTier is consulted) ==")
    tally.check_rc(1, "tiny-bitexact-fails", lambda: _compare_at_tier("bitexact", actual, tiny))

    print("== 6. an unknown tier is REFUSED loudly (setup error 2), never a wrong rigor ==")
    tally.check_rc(
        2, "unknown-tier-refused", lambda: _compare_at_tier("lenient", actual, golden_match)
    )

    print("== 7. e2e_assert_golden: a MISSING committed golden is refused as FAIL ==")
    # a throwaway golden dir so the self-test never reads or writes the committed
    # tree; E2E_REPO points the bridge's golden path here for these two legs.
    gtmp = f"{work}/goldenrepo"
    (Path(gtmp) / "tests" / "e2e" / "goldens").mkdir(parents=True, exist_ok=True)
    tally.check_rc(
        1,
        "assert-missing-fails",
        lambda: _assert_golden_in(gtmp, False, work, _CELL, "probe", "before", strip),
    )

    print("== 8. e2e_assert_golden --bless writes the golden, then a re-shot crop MATCHES it ==")
    with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
        blessed_rc = _assert_golden_in(gtmp, True, work, _CELL, "probe", "before", strip)
    if blessed_rc != 0:
        recipe.fail("e2e_assert_golden --bless did not write a golden")
    golden_file = (
        Path(gtmp) / "tests" / "e2e" / "goldens" / f"{_CELL}.probe.before.expected.lavapipe.png"
    )
    if not golden_file.is_file():
        recipe.fail("bless did not create the expected golden filename")
    tally.check_rc(
        0,
        "assert-reshoot-matches",
        lambda: _assert_golden_in(gtmp, False, work, _CELL, "probe", "before", strip),
    )

    print(f"golden-bridge-selftest: {tally.passed} ok, {tally.failed} failed")
    if tally.failed != 0:
        recipe.fail(
            "the golden bridge did not behave as a trustworthy comparator (see failures above)"
        )
    print("PASS: 090-golden-bridge-selftest (the bridge observes rejections, not just matches)")


def main() -> None:
    work = tempfile.mkdtemp(prefix="golden-selftest.", dir=os.environ["E2E_ARTIFACTS"])
    try:
        _run(work)
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == "__main__":
    recipe.run(main)
