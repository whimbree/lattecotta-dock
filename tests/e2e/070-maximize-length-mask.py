#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""The maximize-length repaint fix (app/view/inputmaskflush.h, Effects). On Qt6
wayland a masked dock's window mask clips each frame's submitted damage, so
when the input band shrinks along its LENGTH axis the vacated edge pixels'
clearing damage is dropped and the compositor keeps a stale frosted band. The
fix keeps the WINDOW mask at the union across a length shrink and collapses it
back to the band once the band settles (~100ms, a coalescing timer in Effects).
A hidden dock's band (reveal strip, accept-nowhere sentinel) is deliberately
NOT held - inputmaskflushtest pins that scoping by the visibility flag.

The motivating trigger is "maximize panel length in presence of maximized
windows" (maximizeWhenMaximized): a maximized client overrides the dock's
maxLength to full width, and un-maximizing drops it back, shrinking the band.
That trigger is NOT drivable in the nested vehicle - existsWindowMaximized
never flips here (this vehicle's kwin does not surface the plasma
window-management maximized state to Latte; a konsole cycled maximized <->
normal left the band unchanged, measured). So this recipe drives the IDENTICAL
band-shrink path through the exact quantity maximizeWhenMaximized overrides -
maxLength - via the edit-mode length ruler, below the applet extent so the
band actually shrinks, and asserts per-view over D-Bus that after each shrink
settles the applied window mask (appliedInputRegionRects) has COLLAPSED back
to the band (applied == input) and STAYS there.

The per-detent assertion demands a SUSTAINED settled window, not one instant
sample: a below-extent detent legitimately runs a short automatic-sizing
confirmation chain (one more animated size step ~1s after the detent, each
step holding the union briefly by design), so a single sample races those
transients. The D274 defect this recipe caught was a PERMANENT 1Hz
grow/shrink oscillation whose union-hold re-widened the applied mask every
second forever; an oscillating dock can never produce the required quiet
window (six identical settled samples spanning 1.5s, longer than the
oscillation period), so the tripwire stays real while legitimate settling
passes.

The ~100ms union-hold DURING each shrink is below D-Bus round-trip latency
(rapid sampling right after a detent never catches it, measured), so it is
not asserted here; its tripwire is the pure-core unit test inputmaskflushtest
and the live union-then-collapse is recorded in
docs/agent-logs/2026-07-18-maximize-length-repaint.md. Ported from the bash
recipe of the same name (deleted with this port) once D274 was fixed.
"""

import os
import subprocess
import time

from latte_harness import recipe


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


def _mask_widths(view_id: int) -> tuple[int, int]:
    """Per-view applied and input band widths (0 when the region is cleared)."""
    found = next((v for v in recipe.views() if v.containment_id == view_id), None)
    if found is None:
        return (0, 0)
    applied = found.applied_input_region_rects[0][2] if found.applied_input_region_rects else 0
    band = found.input_region_rects[0][2] if found.input_region_rects else 0
    return (applied, band)


def _stable_mask_width(view_id: int) -> int | None:
    """A genuine quiet window: six consecutive samples 0.3s apart, every one
    applied == input at one unchanging width. The settled width, or None when
    no such window appears within the deadline."""
    deadline = time.monotonic() + 10.0
    count = 0
    last: int | None = None
    while time.monotonic() < deadline:
        applied, band = _mask_widths(view_id)
        if applied == band and applied != 0 and (last is None or applied == last):
            count += 1
            last = applied
            if count >= 6:
                return applied
        else:
            count = 0
            last = None
        time.sleep(0.3)
    return None


def main() -> None:
    #! widest bottom masked dock (a dock realises its length through the mask; a
    #! plasma panel has none, and the ruler lives on horizontal docks)
    docks = [
        v
        for v in recipe.views()
        if v.view_type == "dock" and v.edge == "bottom" and not v.is_hidden
    ]
    docks.sort(key=lambda v: -v.absolute_geometry[2])
    if not docks:
        recipe.fail("no masked bottom dock to drive")
    target = docks[0]
    view = target.containment_id

    sx, _sy, sw, _sh = target.absolute_geometry
    scw = target.screen_geometry[2]

    rest_a, rest_i = _mask_widths(view)
    print(f"view {view} rest: applied={rest_a} input={rest_i} (screen {scw}px)")
    if rest_a <= 0:
        recipe.fail("rest applied mask is empty (no band to shrink)")
    if rest_a != rest_i:
        recipe.fail(f"rest applied ({rest_a}) != input ({rest_i}): not collapsed at rest")

    general = ("--group", "Containments", "--group", str(view), "--group", "General")

    def cur_maxl() -> int:
        #! an ABSENT key IS a value here: KConfig deletes the entry at the
        #! default (100), so the readback normalizes absent -> 100
        value = _kread(*general, "--key", "maxLength")
        if "." in value:
            value = value.rsplit(".", 1)[0]
        return int(value) if value else 100

    orig_maxl = _kread(*general, "--key", "maxLength")
    in_edit = False

    def restore() -> None:
        if in_edit:
            #! recipe.call cannot raise (busctl runs with check=False, the bash
            #! `|| true` tolerance), so a dead dock just yields empty output here
            recipe.call("setViewEditMode", "ub", str(view), "false")
            time.sleep(1)
        with recipe.muted_stderr():
            recipe.dock_stop()
        if orig_maxl:
            _kwrite(*general, "--key", "maxLength", orig_maxl)
        else:
            _kwrite(*general, "--key", "maxLength", "--delete")

    try:
        #! enter edit mode (the length ruler only exists there); the canvas is
        #! the screen-wide thin latte window that maps on entry
        windows_before = set(_latte_lines())
        _ = recipe.fakepointer("move", 800, 400)
        time.sleep(0.5)
        recipe.call("setViewEditMode", "ub", str(view), "true")
        in_edit = True
        time.sleep(3)

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
        rx = sx + sw // 2  #! ruler center, over the strip
        ry = canvas.y + 7  #! bottom dock: ruler on the canvas' top rows

        def down_detent(before: int) -> int | None:
            """One down-detent that actually lands (retries; a lost axis event
            is common in the nested compositor), leaving the pointer off the
            ruler. The new maxLength, or None when no detent ever landed."""
            for _attempt in range(5):
                _ = recipe.fakepointer("scroll", rx, ry, -1, 100)
                _ = recipe.fakepointer("move", rx, 650)
                for _poll in range(6):
                    time.sleep(0.5)
                    now = cur_maxl()
                    if now != before:
                        return now
            return None

        #! wheel maxLength down past the applet extent so the band shrinks,
        #! checking after every settled detent that the applied mask collapsed
        #! back to the band and STAYED there (a full quiet window, see the
        #! module docstring)
        prev_band = rest_a
        shrinks = 0
        last_maxl = cur_maxl()
        for step in range(1, 17):
            new_maxl = down_detent(last_maxl)
            if new_maxl is None:
                recipe.fail(f"ruler down-detent {step} never landed")
            last_maxl = new_maxl
            settled = _stable_mask_width(view)
            if settled is None:
                recipe.fail(
                    f"after shrink to maxLength {new_maxl}: applied never settled onto "
                    "the input band - settle collapse failed or the automatic size fit "
                    "keeps oscillating (D274's signature)"
                )
            if settled < prev_band:
                shrinks += 1
                print(
                    f"maxLength {new_maxl}: band {prev_band} -> {settled}, "
                    f"applied collapsed to input ({settled})"
                )
            prev_band = settled
            if settled * 100 <= rest_a * 70:
                break  #! a clear, multi-step shrink is enough

        if shrinks < 2:
            recipe.fail(
                f"the band never shrank across {shrinks} step(s) (ruler did not drive "
                "a length reduction below the applet extent)"
            )
        if prev_band * 100 > rest_a * 80:
            recipe.fail(f"band {prev_band} did not shrink meaningfully below rest {rest_a}")

        recipe.call("setViewEditMode", "ub", str(view), "false")
        in_edit = False
        time.sleep(2)

        #! back out of edit mode the mask stays consistent (applied still
        #! collapsed, through the same quiet-window contract)
        fin_a = _stable_mask_width(view)
        if fin_a is None:
            recipe.fail("after leaving edit mode: applied never settled onto the input band")

        print(
            f"maximize-length path: band shrank {rest_a} -> {prev_band} over {shrinks} "
            "steps, applied window mask collapsed to the band at every step"
        )
    finally:
        restore()


if __name__ == "__main__":
    recipe.run(main)
