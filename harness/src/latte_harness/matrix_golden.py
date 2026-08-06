# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
"""The typed render-golden bridge: the port of tests/e2e/matrix/golden-bridge.sh
(BP-3a).

Goldens are the last resort in the interaction suite (HC2): almost every
scenario asserts by D-Bus readback, and a golden is blessed only where a pixel
fact has no queryable readback. This module is the shared plumbing those few
scenarios and the abort backbone (latte_harness.matrix) call; it never blesses
a golden itself.

It REUSES the sceneprobe comparator: a golden compare is a ``latte-imgdiff``
run, and the compare rigor is the Phase C GoldenTier axis resolved in C++
(imagecompare.h). This bridge never re-derives the delta/budget numbers - it
hands ``latte-imgdiff --tier`` and lets the shared toleranceForTier table
decide. The interaction tier defaults to 'tolerance' (the host-rendered vehicle
dock carries GPU rasterization and text-AA variance no bit-exact gate survives),
unlike the sceneprobe render gate's 'bitexact'.

Transport reuse: screenshots go through ``latte_harness.recipe.screenshot`` (the
same KWin ScreenShot2 path lib.sh used), and the geometry readback through
``recipe.json_payload`` - one implementation of each, shared with the recipe API.
The exit-code contract is preserved verbatim from the bash:

- ``golden_compare`` returns 0 MATCH / 1 MISMATCH or MISSING expected / 2 setup.
- ``assert_golden`` returns 0 PASS (or blessed) / 1 FAIL / 2 error.

golden-bridge.sh was retired with matrix-lib.sh (its only consumer) once the 073
topology port removed the last bash matrix recipe; this module is now the sole
render-golden bridge.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
from contextlib import suppress
from pathlib import Path

from pydantic import BaseModel, ConfigDict, Field, TypeAdapter

from latte_harness import recipe
from latte_harness.recipe import Rect

# The interaction golden tier default. Sceneprobe defaults to 'bitexact'; the
# vehicle dock is host-rendered, so its live shots are gated at 'tolerance'
# (open question O3, resolved 2026-07-18). SCENEPROBE_TIER overrides it (a
# multi-distro leg or an explicit bitexact investigation sets it).
_DEFAULT_TIER = "tolerance"


class MatrixGoldenError(Exception):
    """A crop rect could not be computed (no such view, degenerate geometry).

    Raised where the bash inline python did ``sys.exit(...)``; the diagnostic is
    printed at the raise site, and ``assert_golden`` maps it to the exit-2 setup
    error the bash ``|| return 2`` produced.
    """


def _require_env(name: str) -> str:
    """The bash ``${VAR:?}``: return the value, or refuse loudly naming the var."""
    value = os.environ.get(name)
    if not value:
        raise MatrixGoldenError(f"e2e: required environment variable {name} is unset")
    return value


# ---- the compare tier ------------------------------------------------------


def golden_tier() -> str:
    """_golden_tier: SCENEPROBE_TIER when a leg sets it, else 'tolerance'.

    The 'lavapipe' in the golden FILENAME is a device-keying convention kept
    uniform with sceneprobe; it does not claim the vehicle dock rendered on
    lavapipe. Pure over the environment so tier selection is unit-testable.
    """
    return os.environ.get("SCENEPROBE_TIER") or _DEFAULT_TIER


# ---- the crop rect ---------------------------------------------------------


class _GoldenView(BaseModel):
    """The viewsData fields the crop rect needs: identity, clone flag, geometry."""

    model_config = ConfigDict(extra="ignore", frozen=True, populate_by_name=True)

    containment_id: int = Field(alias="containmentId")
    is_cloned: bool = Field(alias="isCloned")
    absolute_geometry: Rect = Field(alias="absoluteGeometry")


_GOLDEN_VIEWS = TypeAdapter(list[_GoldenView])


def crop_rect_of(geometry: Rect) -> str:
    """The ImageMagick ``WxH+X+Y`` crop string for a view's absoluteGeometry.

    Pure so the crop math is unit-testable without a compositor. A degenerate
    (zero or negative) rect is a symptom to surface loudly, never cropped into a
    plausible-but-wrong image (the failures-and-root-cause rule).
    """
    x, y, w, h = geometry
    if w <= 0 or h <= 0:
        raise MatrixGoldenError(f"matrix_view_crop_rect: degenerate view rect {w}x{h}")
    return f"{w}x{h}+{x}+{y}"


def view_crop_rect(view_id: int | None = None) -> str:
    """matrix_view_crop_rect: the screen rect of the view under test.

    A named ``view_id`` selects that view; otherwise the single non-cloned view
    (the matrix fixture seeds exactly one). The bare view rect is the plan's
    default golden crop; a scenario needing the edit-chrome union refines it.
    """
    views = _GOLDEN_VIEWS.validate_json(recipe.json_payload("viewsData"))
    if view_id is not None:
        found = next((v for v in views if v.containment_id == view_id), None)
        if found is None:
            raise MatrixGoldenError(f"matrix_view_crop_rect: no view {view_id}")
        target = found
    else:
        non_cloned = [v for v in views if not v.is_cloned]
        if len(non_cloned) != 1:
            raise MatrixGoldenError(
                f"matrix_view_crop_rect: expected exactly one non-cloned view, "
                f"saw {len(non_cloned)}"
            )
        target = non_cloned[0]
    return crop_rect_of(target.absolute_geometry)


# ---- the pixel compare (delegated to latte-imgdiff at the tier) ------------


def golden_compare(actual: str | Path, expected: str | Path, diff: str | Path | None = None) -> int:
    """e2e_golden_compare: the tier-aware pixel compare through latte-imgdiff.

    The delta/budget lives once in the C++ tier table; this passes ``--tier`` and
    the resolved SCENEPROBE_TIER. A missing expected golden is a compare FAIL (1),
    distinct from a setup error (2) so the caller tells "the dock regressed / no
    golden yet" from "the tool is not built".
      return 0 MATCH   1 MISMATCH or MISSING expected   2 setup/usage error
    """
    imgdiff = Path(_require_env("E2E_BUILD")) / "bin" / "latte-imgdiff"
    if not os.access(imgdiff, os.X_OK):
        print(
            f"e2e_golden_compare: no latte-imgdiff at {imgdiff} (build first)",
            file=sys.stderr,
            flush=True,
        )
        return 2
    if not Path(actual).is_file():
        print(f"e2e_golden_compare: no actual image {actual}", file=sys.stderr, flush=True)
        return 2
    if not Path(expected).is_file():
        print(
            f"e2e_golden_compare: MISSING expected golden {expected} "
            "(a required golden absent is a hard fail)",
            file=sys.stderr,
            flush=True,
        )
        return 1
    argv = [str(imgdiff), str(actual), str(expected), "--tier"]
    if diff is not None:
        argv += ["--out", str(diff)]
    env = {**os.environ, "SCENEPROBE_TIER": golden_tier()}
    return subprocess.run(argv, env=env, check=False).returncode


def screenshot_crop(out: str | Path, rect: str) -> int:
    """e2e_screenshot_crop: a deterministic golden shot cropped to ``rect``.

    The cursor is excluded (a stray pointer is the biggest live-shot
    nondeterminism after animation) and ``+repage`` drops the virtual-canvas
    offset. Returns 0 on success; 1 if the shot failed (the bash return 1) or the
    magick crop returned nonzero.
    """
    handle, full = tempfile.mkstemp(suffix=".png")
    os.close(handle)
    try:
        try:
            recipe.screenshot(full, "include-cursor", "b", "false")
        except recipe.RecipeError:
            return 1
        return subprocess.run(
            ["magick", full, "-crop", rect, "+repage", str(out)], check=False
        ).returncode
    finally:
        with suppress(OSError):
            os.unlink(full)


# ---- the committed-golden assertion ----------------------------------------


def assert_golden(cell: str, verb: str, phase: str, rect: str | None = None) -> int:
    """e2e_assert_golden: shoot+crop the view and compare against the committed
    golden ``tests/e2e/goldens/<cell>.<verb>.<phase>.expected.lavapipe.png``.

    E2E_BLESS=1 (re)writes the golden from the shot and returns 0; a MISSING
    required golden is a hard FAIL, never a silent skip (mirror sceneprobe
    main.cpp). Saves actual/expected/diff under E2E_ARTIFACTS on mismatch.
      return 0 PASS (or blessed)   1 FAIL (mismatch or missing)   2 error
    """
    goldendir = Path(_require_env("E2E_REPO")) / "tests" / "e2e" / "goldens"
    golden = goldendir / f"{cell}.{verb}.{phase}.expected.lavapipe.png"
    stem = Path(_require_env("E2E_ARTIFACTS")) / f"{cell}.{verb}.{phase}"
    actual = f"{stem}.actual.png"

    if rect is None:
        try:
            rect = view_crop_rect()
        except MatrixGoldenError:
            print(
                "e2e_assert_golden: could not compute a view crop rect", file=sys.stderr, flush=True
            )
            return 2
    if screenshot_crop(actual, rect) != 0:
        print(
            f"e2e_assert_golden: shot/crop failed for {cell}/{verb}/{phase}",
            file=sys.stderr,
            flush=True,
        )
        return 2

    if os.environ.get("E2E_BLESS") == "1":
        goldendir.mkdir(parents=True, exist_ok=True)
        _ = shutil.copyfile(actual, golden)
        print(f"e2e_assert_golden: BLESSED {golden} - verify the pixels by eye before committing")
        return 0
    if not golden.is_file():
        print(
            f"e2e_assert_golden: MISSING required golden {golden} "
            "(run once with E2E_BLESS=1, verify by eye, commit)",
            file=sys.stderr,
            flush=True,
        )
        with suppress(OSError):
            _ = shutil.copyfile(actual, f"{stem}.actual-no-golden.png")
        return 1
    if golden_compare(actual, golden, f"{stem}.diff.png") == 0:
        print(f"e2e_assert_golden: PASS {cell}/{verb}/{phase} matches golden")
        return 0
    with suppress(OSError):
        _ = shutil.copyfile(golden, f"{stem}.expected.png")
    print(
        f"e2e_assert_golden: FAIL {cell}/{verb}/{phase} differs from golden "
        f"(see {actual}, {stem}.expected.png, {stem}.diff.png)",
        file=sys.stderr,
        flush=True,
    )
    return 1
