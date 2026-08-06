#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""HC3 acceptance test for the edit-mode settings audit harness, on the typed API.

(docs/tracking/edit-mode-settings-audit-plan.md, cluster CL-0.) It proves the
config-snapshot-diff harness OBSERVES A REJECTION - it reports FAIL on a wrong
outcome, never a green pass - so the ~dozen suspected-broken controls the audit
hunts cannot slip through. Two legs:

  1. CRAFTED (deterministic): feed the assert_* helpers snapshots that stand in
     for a control writing the wrong key / a stray coupled key / a no-op, and
     assert each REJECTION path returns FAIL. Also proves the safe direction of
     the KConfig default-deletion trap (a key present-then-absent is a loud
     change, never a silent pass). Mirror of the C++ proofs in
     tests/units/configsnapshotdifftest.cpp and tests/settingswiringharnesstest.cpp.
  2. LIVE: the viewConfigData / appletConfigData readbacks answer for a real
     view, the snapshots carry the audit's keys, and a no-change between two live
     reads is REJECTED by P1 (proving the harness catches a no-op on live
     snapshots too, not only crafted ones).

This is the BP-3b pilot: the port of audit-harness-selftest.sh over
latte_harness.audit, exactly as matrix-harness-selftest.py piloted the matrix
API. Every numbered control is preserved, negative controls included.
"""

from __future__ import annotations

import io
import sys
import tempfile
from collections.abc import Callable
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

from latte_harness import audit, recipe


def _snap(path: Path, *lines: str) -> Path:
    """The bash ``snap``: write ``key<TAB>value`` lines, one per line."""
    _ = path.write_text("".join(f"{line}\n" for line in lines))
    return path


def _snap_text(path: Path, text: str) -> Path:
    """Write a snapshot string to a file verbatim (the bash ``> file`` redirect)."""
    _ = path.write_text(text)
    return path


def _keys(snapshot: str) -> list[str]:
    """The keys of a ``key<TAB>value`` snapshot (the bash ``grep``/``wc -l`` reads)."""
    return [line.split("\t", 1)[0] for line in snapshot.splitlines()]


def _verdict(call: Callable[[], int]) -> int:
    """Run a harness assertion with its chatter suppressed (the bash `>/dev/null
    2>&1`) and return its verdict code. A rejection's own FAIL/REFUSED line is the
    thing being tested for, not recipe output, so it stays captured.
    """
    with redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO()):
        return call()


def expect_pass(desc: str, call: Callable[[], int]) -> None:
    """Require the harness to ACCEPT a correct outcome (verdict 0)."""
    if _verdict(call) == 0:
        print(f"  PASS (correctly accepted): {desc}")
    else:
        recipe.fail(f"harness REJECTED a correct outcome: {desc}")


def expect_fail(desc: str, call: Callable[[], int]) -> None:
    """Require the harness to REJECT a wrong outcome (verdict nonzero); a green
    pass here is the HC3 breach the whole self-test exists to catch.
    """
    if _verdict(call) == 0:
        recipe.fail(f"harness ACCEPTED a wrong outcome (HC3 breach): {desc}")
    else:
        print(f"  PASS (correctly rejected): {desc}")


def _leg_crafted(work: Path) -> None:
    print("== leg 1: crafted rejection proofs (deterministic) ==")

    # a stable three-key baseline
    before = _snap(work / "before", "maxLength\t100", "minLength\t100", "offset\t0")

    # correct: only maxLength moved -> P1 applies, P2 exact
    after_good = _snap(work / "after_good", "maxLength\t90", "minLength\t100", "offset\t0")
    expect_pass(
        "maxLength drive applies", lambda: audit.assert_applies(before, after_good, "maxLength")
    )
    expect_pass(
        "maxLength drive changed only maxLength",
        lambda: audit.assert_only_keys(before, after_good, "maxLength"),
    )

    # D15 shape: maxLength drive also dragged minLength -> P2 must FAIL
    after_stray = _snap(work / "after_stray", "maxLength\t90", "minLength\t90", "offset\t0")
    expect_fail(
        "stray coupled minLength write is caught",
        lambda: audit.assert_only_keys(before, after_stray, "maxLength"),
    )

    # D10 shape: nothing changed -> P1 must FAIL
    expect_fail(
        "no-op control is caught", lambda: audit.assert_applies(before, before, "maxLength")
    )

    # wrong-key: offset moved when maxLength was expected -> P1 for maxLength FAILS,
    # P2 for {maxLength} FAILS
    after_wrong = _snap(work / "after_wrong", "maxLength\t100", "minLength\t100", "offset\t5")
    expect_fail(
        "wrong-key write fails maxLength P1",
        lambda: audit.assert_applies(before, after_wrong, "maxLength"),
    )
    expect_fail(
        "wrong-key write fails maxLength P2",
        lambda: audit.assert_only_keys(before, after_wrong, "maxLength"),
    )

    # missing expected key: expected {maxLength,offset} but only maxLength moved
    expect_fail(
        "under-write (missing expected key) is caught",
        lambda: audit.assert_only_keys(before, after_good, "maxLength", "offset"),
    )

    # default-deletion safety: a key present-then-absent is a LOUD change (the safe
    # false-FAIL direction), never a silent pass
    after_deleted = _snap(work / "after_deleted", "maxLength\t100", "offset\t0")
    expect_pass(
        "vanished key surfaces as a change (default-deletion safe)",
        lambda: audit.assert_applies(before, after_deleted, "minLength"),
    )

    # P3 reflect-state: value present passes, wrong/absent value fails
    expect_pass(
        "reflect matches the stored value",
        lambda: audit.assert_reflects(before, "maxLength", "100"),
    )
    expect_fail(
        "reflect rejects a wrong value", lambda: audit.assert_reflects(before, "maxLength", "999")
    )
    expect_fail(
        "reflect rejects an absent key", lambda: audit.assert_reflects(before, "iconSize", "48")
    )

    # P4 cross-view agreement: same value agrees, different disagrees
    surface_a = _snap(work / "surfaceA", "maxLength\t90")
    surface_b = _snap(work / "surfaceB", "sliderMax\t90")
    surface_c = _snap(work / "surfaceC", "sliderMax\t80")
    expect_pass(
        "two surfaces holding one value agree",
        lambda: audit.assert_agrees(surface_a, "maxLength", surface_b, "sliderMax"),
    )
    expect_fail(
        "two surfaces holding different values are caught",
        lambda: audit.assert_agrees(surface_a, "maxLength", surface_c, "sliderMax"),
    )


def _leg_live(work: Path) -> None:
    print("== leg 2: live readback + no-op rejection ==")

    try:
        view = recipe.tasks_view()
    except recipe.RecipeError:
        recipe.fail("no tasks view to exercise the live readback")

    config_text = audit.config_snapshot(view)
    live_before = _snap_text(work / "live_before", config_text)
    if not config_text:
        recipe.fail(f"live config snapshot is empty for view {view}")
    if "maxLength" not in _keys(config_text):
        recipe.fail("live config snapshot has no maxLength key")
    print(f"  live config snapshot has {len(_keys(config_text))} keys incl. maxLength")

    view_text = audit.view_snapshot(view)
    _ = _snap_text(work / "live_view", view_text)
    if "byPassWM" not in _keys(view_text):
        recipe.fail("live 'view' snapshot has no byPassWM (the live C++ half)")
    print("  live 'view' snapshot carries the C++ P3 props (byPassWM, indicatorType, ...)")

    # a second read with no drive between them: the harness must REJECT this as a
    # no-op (P1 fails), proving the rejection path works on live snapshots too
    live_after = _snap_text(work / "live_after", audit.config_snapshot(view))
    expect_fail(
        "no-change between two live reads is caught",
        lambda: audit.assert_applies(live_before, live_after, "maxLength"),
    )

    # and the tasks-config readback answers for CL-5's D10 question
    try:
        applet = audit.tasks_applet_id(view)
    except audit.AuditError:
        print(f"  (no tasks plasmoid under view {view}; tasks-config leg skipped)")
        return
    tasks_text = audit.applet_config_snapshot(view, applet)
    if not tasks_text:
        recipe.fail("tasks-config snapshot is empty")
    _ = _snap_text(work / "live_tasks", tasks_text)
    print(f"  tasks-config snapshot (applet {applet}) has {len(_keys(tasks_text))} keys")


def main() -> int:
    with tempfile.TemporaryDirectory() as work_str:
        work = Path(work_str)
        _leg_crafted(work)
        _leg_live(work)
    print("audit harness self-test: rejections caught, live readbacks answer")
    return 0


if __name__ == "__main__":
    sys.exit(main())
