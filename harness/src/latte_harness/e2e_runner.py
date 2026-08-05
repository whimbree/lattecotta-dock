# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""End-to-end recipe classification and discovery: the pure core of the
scripts/run-e2e.sh port (BP-2b, the bash-to-python migration's e2e runner chunk).

This half carries what a unit test can pin without a running compositor: the
``# e2e-mode`` / ``# e2e-expect`` marker parsing, the bit-identical
PASS/FAIL/XFAIL/XPASS classification matrix and its self-test, and recipe
discovery. The driver that brings up the vehicle and runs the recipes is added
on top of this core (BP-2b).

Recipes are OPAQUE EXECUTABLES given a documented environment, which is what
lets .sh and .py recipes run side by side during the BP-3 transition: the
driver never reads a recipe's body except for its two marker lines.

DISCOVERY DEVIATION (deliberate, recorded). The bash run-e2e.sh discovered
recipes with a RECURSIVE ``find "$repo/tests/e2e" -name '*.sh' ! -name
'lib.sh'``, which descended into subdirectories and ran non-recipe files as
recipes: the driver libraries and drivers under tests/e2e/matrix/ (matrix-lib,
dnd-lib, golden-bridge, task-reorder-lib, multi-output-lib,
applet-reorder-driver), tests/e2e/audit/audit-lib.sh, and the launched-app
fixtures tests/e2e/fixtures/sc-w1/{launcher,rate-launcher}.sh - nine files that
are sourced or exec'd by real recipes, never recipes themselves. The BP-2a A/B
measurement of the recursive discovery (33/52 in the plan; the recursive find
matches 62 ``*.sh`` files on the current tree, 9 of them those non-recipes)
recorded those as guaranteed failures: the sourced-only libs error or the
non-executable ones trip the "missing or non-executable recipe" refusal, so a
green suite was impossible while they sat in the run.

This port discovers RECIPES ONLY: a file is a recipe when it sits DIRECTLY
under tests/e2e/ (not a subdirectory) and is not lib.sh; tests/e2e/*.py files
are discovered the same way (the BP-3 transition property, .sh and .py side by
side). The nine subdirectory files leave the denominator entirely because they
were never recipes. Explicit positional invocation still resolves a bare name
(with or without a .sh/.py extension) to tests/e2e/<name>.sh then
tests/e2e/<name>.py - the front doors and the asan gate pass explicit names.

The classification matrix and its self-test port BIT-IDENTICALLY from the bash
(the ``# e2e-expect`` contract in docs/reference/TESTING.md): the
``--self-test-expectations`` entry the bash exposed runs the same self-test,
and every cell is pinned as a unit test.
"""

from __future__ import annotations

import re
import subprocess
import sys
from collections.abc import Sequence
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
from typing import assert_never

TOOL = "run-e2e"


# ---- expectation model -----------------------------------------------------
#
# The bash carried the expectation as a bare string ("" | "fail" | "status N").
# Modelling it as a closed union makes the invalid state unrepresentable and
# lets classify() dispatch with a compiler-checked match (the step-2.5 law).


@dataclass(frozen=True, slots=True)
class Unmarked:
    """No ``# e2e-expect`` marker: a nonzero exit is a real failure."""


@dataclass(frozen=True, slots=True)
class ExpectFail:
    """``# e2e-expect: fail``: any nonzero exit is the known-open bug (XFAIL)."""


@dataclass(frozen=True, slots=True)
class ExpectStatus:
    """``# e2e-expect: status N``: only exit N proves the known signature."""

    status: int


Expectation = Unmarked | ExpectFail | ExpectStatus


@dataclass(frozen=True, slots=True)
class MarkerError:
    """A malformed/blank/duplicate/out-of-range marker: refuse before running."""

    message: str


class RecipeResult(Enum):
    PASS = "PASS"
    FAIL = "FAIL"
    XFAIL = "XFAIL"
    XPASS = "XPASS"
    SKIP = "SKIP"


# ---- marker parsing (bit-identical to the bash, unit-tested directly) -------

# The value-form of `# e2e-expect: status N`: N is 1..999 by shape, then bounded
# to <= 255 (a Unix exit code) below - the bash `^status ([1-9][0-9]{0,2})$`.
_STATUS_RE = re.compile(r"^status ([1-9][0-9]{0,2})$")

# sed `^[[:space:]]*#[[:space:]]*e2e-expect`: a comment marker with optional
# leading and post-hash whitespace. Kept to the ASCII space class the bash used
# (no \n inside a splitlines() line).
_WS = r"[ \t\r\f\v]"
_EXPECT_MARKER_RE = re.compile(_WS + r"*#" + _WS + r"*e2e-expect")

# The strict prefix the bash required with a literal glob check; a marker that
# the loose scan matched but that does not start with exactly this is malformed.
_EXPECT_PREFIX = "# e2e-expect:"

# sed `s/^# e2e-mode: *//p`: an exact "# e2e-mode:" prefix then optional spaces.
_MODE_PREFIX = "# e2e-mode:"


def parse_expectation_value(value: str) -> Expectation | None:
    """The bash valid_recipe_expectation, returning the parsed form or None.

    None means invalid. The empty string maps to Unmarked to keep this a total
    function (valid_recipe_expectation accepted ""), though extract handles the
    no-marker and blank-marker cases before ever calling this with an empty
    value.
    """
    if value == "":
        return Unmarked()
    if value == "fail":
        return ExpectFail()
    match = _STATUS_RE.match(value)
    if match is not None:
        status = int(match.group(1))
        if status <= 255:
            return ExpectStatus(status)
    return None


def extract_recipe_expectation(text: str) -> Expectation | MarkerError:
    """The bash extract_recipe_expectation over a recipe's full text.

    Refuses (returns MarkerError) on a duplicate/conflicting, malformed, blank,
    or out-of-range marker BEFORE the recipe would run - the strictness the
    docs/reference/TESTING.md contract records. No marker returns Unmarked.
    """
    declarations = [line for line in text.splitlines() if _EXPECT_MARKER_RE.match(line)]
    if not declarations:
        return Unmarked()
    if len(declarations) != 1:
        return MarkerError(
            f"found {len(declarations)} e2e-expect declarations; use exactly one nonempty marker"
        )
    declaration = declarations[0]
    if not declaration.startswith(_EXPECT_PREFIX):
        return MarkerError(
            f"malformed e2e-expect declaration '{declaration}'; "
            "expected '# e2e-expect: fail' or '# e2e-expect: status N'"
        )
    value = declaration[len(_EXPECT_PREFIX) :].lstrip(" \t\r\f\v")
    if value == "":
        return MarkerError("blank e2e-expect declaration; remove it for unmarked behavior")
    parsed = parse_expectation_value(value)
    if parsed is None:
        return MarkerError(
            f"invalid e2e-expect value '{value}'; allowed values are fail or status 1..255"
        )
    return parsed


def recipe_mode(text: str) -> str:
    """The ``# e2e-mode`` constraint, or '' when unmarked (the bash sed | head -1).

    Only spaces are stripped after the colon, matching the sed ` *`; the first
    matching line wins.
    """
    for line in text.splitlines():
        if line.startswith(_MODE_PREFIX):
            return line[len(_MODE_PREFIX) :].lstrip(" ")
    return ""


def classify_recipe_result(exp: Expectation, status: int, name: str) -> tuple[RecipeResult, str]:
    """The bash classify_recipe_result: (result, the exact message it printed)."""
    match exp:
        case Unmarked():
            if status == 0:
                return RecipeResult.PASS, f"{TOOL}: PASS {name}"
            return RecipeResult.FAIL, f"{TOOL}: FAIL {name}"
        case ExpectFail():
            if status == 0:
                return RecipeResult.XPASS, (
                    f"{TOOL}: XPASS {name} (expected to fail but passed - "
                    "remove '# e2e-expect: fail', the guarded condition is fixed)"
                )
            return RecipeResult.XFAIL, (
                f"{TOOL}: XFAIL {name} (expected failure of a known-open bug, not counted)"
            )
        case ExpectStatus(status=expected):
            if status == 0:
                return RecipeResult.XPASS, (
                    f"{TOOL}: XPASS {name} (expected reserved status {expected} but passed - "
                    "the guarded condition is fixed)"
                )
            if status == expected:
                return RecipeResult.XFAIL, (
                    f"{TOOL}: XFAIL {name} (matched reserved status {expected} "
                    "for the known-open bug)"
                )
            return RecipeResult.FAIL, (
                f"{TOOL}: FAIL {name} (expected reserved status {expected}, got {status}; "
                "failure is outside the known signature)"
            )
        case _:
            assert_never(exp)


def _expectation_text(exp: Expectation) -> str:
    """The bash recipe_expectation string, used only in self-test diagnostics."""
    match exp:
        case Unmarked():
            return ""
        case ExpectFail():
            return "fail"
        case ExpectStatus(status=status):
            return f"status {status}"
        case _:
            assert_never(exp)


# ---- run counters ----------------------------------------------------------


@dataclass(slots=True)
class Counters:
    failed: int = 0
    skipped: int = 0
    ran: int = 0
    passed: int = 0
    xfailed: int = 0

    def record(self, result: RecipeResult) -> None:
        """The bash record_recipe_result tally (SKIP is counted at its site)."""
        match result:
            case RecipeResult.PASS:
                self.passed += 1
            case RecipeResult.XFAIL:
                self.xfailed += 1
            case RecipeResult.FAIL | RecipeResult.XPASS:
                self.failed += 1
            case RecipeResult.SKIP:
                self.skipped += 1


# ---- discovery -------------------------------------------------------------


def discover_recipes(tests_e2e: Path, names: Sequence[str]) -> list[Path]:
    """Explicit names, else every top-level recipe (the DISCOVERY DEVIATION).

    Top-level ``*.sh`` (never lib.sh) plus top-level ``*.py``, sorted; NEVER a
    subdirectory (that recursion ran non-recipe libs). Explicit names resolve
    through resolve_recipe.
    """
    if names:
        return [resolve_recipe(tests_e2e, name) for name in names]
    recipes = [p for p in tests_e2e.glob("*.sh") if p.name != "lib.sh"]
    recipes += list(tests_e2e.glob("*.py"))
    return sorted(recipes)


def resolve_recipe(tests_e2e: Path, name: str) -> Path:
    """A bare or extensioned name to its recipe path, resolving .sh then .py.

    The bash resolved ``${name%.sh}.sh``; this also accepts a .py name and
    prefers an existing .sh over .py. A name matching neither resolves to the
    .sh path so the loop's "missing or non-executable recipe" refusal fires with
    a sensible name (the bash behavior).
    """
    stem = name
    for ext in (".sh", ".py"):
        if stem.endswith(ext):
            stem = stem[: -len(ext)]
            break
    sh = tests_e2e / f"{stem}.sh"
    if sh.exists():
        return sh
    py = tests_e2e / f"{stem}.py"
    if py.exists():
        return py
    return sh


# ---- the classifier self-test (the bash run_expectation_selftest) ----------


def run_expectation_selftest(quiet: bool = False) -> int:
    """Drive every classification and marker cell; 0 on success, 1 on any miss.

    A faithful port of the bash run_expectation_selftest: the same cells, the
    same accumulator check (1 pass / 4 fail / 2 xfail), and the same
    capture-status probes. ``quiet`` hides the stdout progress like the bash
    ``run_expectation_selftest >/dev/null`` startup guard; failures still print
    to stderr. Unit tests pin every cell independently; this is the runtime
    ``--self-test-expectations`` surface the bash exposed.
    """
    failures = 0
    counters = Counters()

    def out(msg: str) -> None:
        if not quiet:
            print(msg, flush=True)

    def note_fail(msg: str) -> None:
        nonlocal failures
        print(msg, file=sys.stderr, flush=True)
        failures += 1

    def check_result(label: str, exp: Expectation, status: int, expected: RecipeResult) -> None:
        result, _ = classify_recipe_result(exp, status, "selftest")
        if result is expected:
            out(f"  ok   {label} -> {expected.value}")
        else:
            note_fail(f"  FAIL {label} -> {result.value}, expected {expected.value}")
        counters.record(result)

    def check_marker(
        label: str, text: str, expected: Expectation | None, error_fragment: str
    ) -> None:
        parsed = extract_recipe_expectation(text)
        if not isinstance(parsed, MarkerError):
            if error_fragment or parsed != expected:
                note_fail(f"  FAIL {label} marker accepted as '{_expectation_text(parsed)}'")
        elif not error_fragment or error_fragment not in parsed.message:
            note_fail(f"  FAIL {label} marker error: {parsed.message}")

    check_result("pass", Unmarked(), 0, RecipeResult.PASS)
    check_result("fail", Unmarked(), 1, RecipeResult.FAIL)
    check_result("legacy-xfail", ExpectFail(), 1, RecipeResult.XFAIL)
    check_result("legacy-xpass", ExpectFail(), 0, RecipeResult.XPASS)
    check_result("exact-xfail", ExpectStatus(42), 42, RecipeResult.XFAIL)
    check_result("exact-xpass", ExpectStatus(42), 0, RecipeResult.XPASS)
    check_result("status-mismatch", ExpectStatus(42), 1, RecipeResult.FAIL)
    if (counters.passed, counters.failed, counters.xfailed) != (1, 4, 2):
        failures += 1

    dup = "2 e2e-expect declarations"
    check_marker("no-marker", "#!/usr/bin/env bash\n", Unmarked(), "")
    check_marker("legacy", "# e2e-expect: fail\n", ExpectFail(), "")
    check_marker("exact", "# e2e-expect: status 42\n", ExpectStatus(42), "")
    check_marker("blank", "# e2e-expect:   \n", None, "blank")
    check_marker("duplicate", "# e2e-expect: fail\n# e2e-expect: fail\n", None, dup)
    check_marker("conflict", "# e2e-expect: fail\n# e2e-expect: status 42\n", None, dup)
    check_marker("malformed", "# e2e-expect status 42\n", None, "malformed")
    check_marker("unknown", "# e2e-expect: unknown\n", None, "invalid")
    check_marker("zero", "# e2e-expect: status 0\n", None, "invalid")
    check_marker("range", "# e2e-expect: status 256\n", None, "invalid")

    if subprocess.run(["bash", "-c", "exit 42"], check=False).returncode != 42:
        failures += 1
    if subprocess.run(["bash", "-c", "exit 0"], check=False).returncode != 0:
        failures += 1

    if failures != 0:
        return 1
    out(f"{TOOL}: PASS expectation classifier self-test")
    return 0
