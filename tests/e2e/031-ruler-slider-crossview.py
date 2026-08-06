#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
# e2e-mode: nested-only
"""AU-1c (docs/tracking/edit-mode-settings-audit-plan.md, cluster CL-1): the LIVE
cross-view confirmation of the D16 fix - the Maximum settings slider re-tracks the
on-canvas ruler after the slider's handle binding has been clobbered.

The whole point of D16 is that the config value ALWAYS agrees (the ruler and the
slider share one containment config map), so a config readback cannot tell the
fixed dock from the broken one - only the RENDERED slider handle can. So this
recipe:
  1. opens the Appearance page and DRAGS the Maximum slider hard left, which
     lowers maxLength AND (the crux) destroys the handle's config binding the way
     any drag does - the exact clobber D16 is about;
  2. drives the on-canvas Maximum-Length ruler UP to a deterministic value (78%),
     reading viewConfigData after each detent so the target is exact;
  3. asserts the config re-tracked (the ruler wrote the shared map), then
     golden-compares a crop of the Maximum slider ROW.
With the proxy re-sync fix the handle and its "78 %" label follow the ruler;
reverting the fix leaves the handle stuck at the clobbered 1% and the crop
mismatches. The value label binds to the handle (maxLengthSlider.value), not to
config, so a stale handle shows a stale number - the golden catches both.

Tooltip/pointer determinism: the pointer is parked far from the settings window
and the ruler tooltip lives on the bottom canvas, outside the crop.
HOST-RENDERED golden (fonts from the system profile): on a new machine verify
once by eye (handle near 78%, label reads "78 %") and re-bless with E2E_BLESS=1.

Ported from tests/e2e/031-ruler-slider-crossview.sh to latte_harness.audit /
.recipe (BP-3, the bash-to-python migration's recipe batch). The maxLength poll
keeps the bash arithmetic-comparison tolerance: an empty or non-integer readback
mid-poll counts as neither the target nor an overshoot (the bash (( )) error
status), so a transiently refused snapshot polls through.
"""

from __future__ import annotations

import io
import os
import shutil
import subprocess
import sys
import tempfile
import time
from contextlib import redirect_stderr, suppress
from pathlib import Path

from latte_harness import audit, recipe
from pydantic import ValidationError


def _quiet_dock_stop() -> None:
    """e2e_dock_stop >/dev/null 2>&1 || true: best-effort, chatter suppressed."""
    with suppress(recipe.RecipeError), redirect_stderr(io.StringIO()):
        _ = recipe.dock_stop()


def _fakepointer(*args: str) -> None:
    """$E2E_FAKEPOINTER <args>: inject a pointer event, fire-and-forget."""
    subprocess.run([os.environ["E2E_FAKEPOINTER"], *args], check=False)


def _cfg_max(view: int) -> str:
    """cfg_max(): maxLength read from the in-process config map (never the on-disk
    file, which the KConfig default-deletion trap would corrupt). '' when absent or
    the readback cannot validate (the bash pipeline of a failed snapshot)."""
    try:
        text = audit.config_snapshot(view)
    except ValidationError:
        return ""
    for line in text.split("\n"):
        if not line:
            continue
        key, _, value = line.partition("\t")
        if key == "maxLength":
            return value
    return ""


def main() -> None:
    goldens = Path(os.environ["E2E_REPO"]) / "tests" / "e2e" / "goldens"
    golden = goldens / "ruler-slider-crossview.png"
    imgdiff = Path(os.environ["E2E_BUILD"]) / "bin" / "latte-imgdiff"
    if not os.access(imgdiff, os.X_OK):
        recipe.fail(f"no latte-imgdiff at {imgdiff} (build first)")

    try:
        view = recipe.tasks_view()
    except recipe.RecipeError:
        recipe.fail("no tasks view")

    orig = _cfg_max(view)
    try:
        if not audit.enter_editmode(view):
            recipe.fail("edit mode never turned on")

        # the Appearance tab carries the Maximum slider (the Behavior tab is default)
        _ = audit.settings_click(0.376, 0.154)
        time.sleep(1)

        # drag the Maximum handle hard left: lowers maxLength to its floor AND
        # clobbers the handle's declarative binding (the drag is the imperative
        # assignment D16 is about). Pinning below the 30 ruler rail makes the first
        # up-detent snap to 30, so the ruler sequence up is a deterministic 30, 36, ...
        _ = audit.settings_drag(0.70, 0.442, -360, 0)
        time.sleep(1)
        clobbered = _cfg_max(view)
        if not clobbered:
            recipe.fail("could not read maxLength after the slider drag")
        try:
            pinned = int(clobbered) <= 30
        except ValueError:
            pinned = False
        if not pinned:
            recipe.fail(
                f"slider drag did not pin maxLength below the 30 rail (got {clobbered})"
            )
        print(f"clobbered the Maximum slider: maxLength {orig} -> {clobbered}")

        # the ruler lives on the bottom canvas (screen-wide, thin), driven at its
        # top rows exactly as 030-wheel-ruler-maxlength calibrated
        canvas = next((w for w in recipe.windows() if w.width == 1600 and w.height < 300), None)
        if canvas is None:
            recipe.fail("no canvas ruler window mapped")
        rx, ry = 800, canvas.y + 7

        target = "78"
        for _ in range(40):
            cur = _cfg_max(view)
            if cur == target:
                break
            # the bash (( cur > target )): an empty or non-integer readback errors
            # the arithmetic (status 1) and polls through, never a false overshoot
            with suppress(ValueError):
                if int(cur) > int(target):
                    recipe.fail(f"ruler overshot the target: maxLength {cur} > {target}")
            _fakepointer("scroll", str(rx), str(ry), "1", "100")
            _fakepointer("move", str(rx), "650")
            time.sleep(1)
        final = _cfg_max(view)
        if final != target:
            recipe.fail(f"ruler never reached maxLength {target} (stuck at {final})")
        print(f"ruler drove maxLength {clobbered} -> {final} through the shared config")

        # park the pointer far from the settings window and let the ruler tooltip fade
        _fakepointer("move", "200", "300")
        time.sleep(1.5)

        rect = audit.settings_window_rect()
        if rect is None:
            recipe.fail("no settings window to crop")
        wx, wy, ww, wh = rect
        # crop the Maximum slider ROW: nearly the window width, a thin band at the
        # Length/Maximum row, capturing the handle position and the "%1 %" value label
        cropx = wx + ww // 100
        cropy = int(wy + 0.427 * wh)
        cropw = ww * 98 // 100
        croph = int(0.05 * wh)

        fd, shot = tempfile.mkstemp(suffix=".png")
        os.close(fd)
        crop = Path(os.environ["E2E_ARTIFACTS"]) / "ruler-slider-crossview.actual.png"
        try:
            try:
                recipe.screenshot(shot, "include-cursor", "b", "false")
            except recipe.RecipeError as err:
                print(str(err), file=sys.stderr, flush=True)
                recipe.fail("screenshot failed")
            subprocess.run(
                ["magick", shot, "-crop", f"{cropw}x{croph}+{cropx}+{cropy}", str(crop)],
                check=False,
            )
        finally:
            Path(shot).unlink(missing_ok=True)

        if os.environ.get("E2E_BLESS", "0") == "1":
            golden.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(crop, golden)
            print(f"BLESSED {golden} - verify by eye before committing:")
            print("  the Maximum handle sits near 78% and the value label reads '78 %'")
            return

        if not golden.is_file():
            recipe.fail(
                f"no golden at {golden} (run once with E2E_BLESS=1, verify by eye, commit)"
            )

        # tolerant tier: same host renders bit-close, the value label is AA text; the
        # handle is a solid disc, so delta 8 / 2% budget separates a stuck handle
        # from AA noise
        diff_out = Path(os.environ["E2E_ARTIFACTS"]) / "ruler-slider-crossview.diff.png"
        verdict = subprocess.run(
            [
                str(imgdiff), str(crop), str(golden),
                "--delta", "8", "--budget", "0.02", "--out", str(diff_out),
            ],
            check=False,
        )
        if verdict.returncode == 0:
            print("the Maximum slider re-tracked the ruler to 78% (D16 cross-view sync holds)")
        else:
            recipe.fail(
                "the Maximum slider did not match the verified golden "
                f"(D16 regression? see {crop} and the diff)"
            )
    finally:
        _quiet_dock_stop()
        subprocess.run(
            [
                "kwriteconfig6", "--file", os.environ["E2E_LAYOUT"],
                "--group", "Containments", "--group", str(view), "--group", "General",
                "--key", "maxLength", orig or "100",
            ],
            check=False,
        )


if __name__ == "__main__":
    recipe.run(main)
