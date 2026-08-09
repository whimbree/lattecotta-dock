#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""EX-15 live check 4 (docs/agent-logs/EX-15.md): in edit mode, one wheel
detent over the max-length ruler moves maxLength by exactly 6 points per
direction (the RulerMouseArea cutover through LatteCore.WheelStepper,
VerticalOnly pick, threshold 96; the +-6 step feeds EX-18's shared clamp).
Asserted on the layout file, which the containment flushes within ~1s of the
write (measured in the vehicle), so every detent is verified individually and
a double-landing cannot masquerade as a single step.

The ruler lives in the CANVAS window (mapped only in edit mode, full screen
width, thin), at its outermost ~13px rows; located from the window dump
instead of hardcoding the overlay's font-dependent thickness. Each detent is a
single-invocation fakepointer scroll (motion -> 100ms -> axis, the shape that
wins the vehicle's enter race - measured 6/6 deliveries in calibration) with a
retry loop for the occasional loss.
"""

import os
import subprocess
import time

from latte_harness import recipe


def _fakepointer(*args: str) -> None:
    _ = recipe.fakepointer(*args)


def _kread(*args: str) -> str:
    result = subprocess.run(
        ["kreadconfig6", "--file", os.environ["E2E_LAYOUT"], *args],
        capture_output=True,
        text=True,
        check=False,
    )
    return result.stdout.rstrip("\n")


def _kwrite(*args: str) -> None:
    _ = recipe.kwriteconfig("--file", os.environ["E2E_LAYOUT"], *args)


def _latte_lines() -> list[str]:
    """The latte-dock window dump lines, sorted - the comm inputs' shape."""
    return sorted(line for line in recipe.dumpwins().splitlines() if "|latte-dock|" in line)


def main() -> None:
    try:
        view = recipe.tasks_view()
    except recipe.RecipeError:
        recipe.fail("no tasks view")

    general = ("--group", "Containments", "--group", str(view), "--group", "General")

    #! an ABSENT key IS a value here: writing the default (100) back makes
    #! KConfig delete the entry, so the readback must normalize absent -> 100
    #! or the up-detent's landing reads as "no change" (cost a full failing
    #! afternoon arc before the write path was instrumented and found healthy)
    def cfg() -> int:
        v = _kread(*general, "--key", "maxLength")
        if "." in v:
            v = v.rsplit(".", 1)[0]
        return int(v) if v else 100

    orig = _kread(*general, "--key", "maxLength")

    def restore_config() -> None:
        with recipe.muted_stderr():
            recipe.dock_stop()
        if orig:
            _kwrite(*general, "--key", "maxLength", orig)
        else:
            _kwrite(*general, "--key", "maxLength", "--delete")

    try:
        start = cfg()
        if not (start - 6 >= 50):
            recipe.fail(f"maxLength {start} leaves no headroom for exact-step assertions")

        strip = recipe.view(view)
        sx, _sy, sw, _sh = strip.absolute_geometry
        scw = strip.screen_geometry[2]

        windows_before = set(_latte_lines())
        #! park the pointer mid-screen before the canvas maps (part of the one
        #! delivery rhythm that proved reliable - 6/6 alternating detents in
        #! calibration; deviations from it lost up-detents 0/5)
        _fakepointer("move", "800", "400")
        time.sleep(0.5)
        recipe.call("setViewEditMode", "ub", str(view), "true")
        time.sleep(3)

        #! the canvas: the latte window edit mode just mapped, screen-wide and thin
        new_lines = [line for line in _latte_lines() if line not in windows_before]
        canvas = next(
            (
                w
                for w in recipe.parse_dumpwins("\n".join(new_lines))
                if w.width == scw and w.height < 300
            ),
            None,
        )
        if canvas is None:
            recipe.fail("no canvas window mapped for edit mode")
        cy = canvas.y

        rx = sx + sw // 2
        #! bottom dock: the ruler occupies the canvas' outermost rows (its top)
        ry = cy + 7

        last = start

        def wheel_step(detent: int, expect: int) -> None:
            nonlocal last
            for attempt in (1, 2, 3, 4, 5):
                _fakepointer("scroll", str(rx), str(ry), str(detent), "100")
                _fakepointer("move", str(rx), "650")
                time.sleep(1.2)
                for _ in range(6):
                    v = cfg()
                    if v != last:
                        if v != expect:
                            recipe.fail(
                                f"detent {detent} moved maxLength {last} -> {v} "
                                f"(expected {expect}: exactly 6 per detent)"
                            )
                        last = v
                        return
                    time.sleep(1)
                print(f"  (ruler detent {detent} not delivered on attempt {attempt}, retrying)")
            recipe.fail(f"ruler detent {detent} never delivered after 5 attempts")

        wheel_step(-1, start - 6)
        print(f"down-detent: maxLength {start} -> {last} (exactly -6)")
        wheel_step(1, start)
        print(f"up-detent: maxLength back to {last} (exactly +6)")

        recipe.call("setViewEditMode", "ub", str(view), "false")
        time.sleep(1.5)

        #! the clean stop must persist the same value (no shutdown rewrite)
        if not recipe.dock_stop():
            recipe.fail("no clean stop to confirm persistence")
        final = cfg()
        if final != start:
            recipe.fail(f"maxLength changed across shutdown: {last} -> {final}")

        print("ruler wheel steps maxLength by 6 per detent, both directions, persisted")
    finally:
        restore_config()


if __name__ == "__main__":
    recipe.run(main)
