# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The structural coverage ratchet (BP-1c).

A direct port of tests/coverage/coverage-ratchet.sh
(docs/tracking/QML_EXTRACTION_PLAN.md section D). Two structural checks,
no instrumentation dependency:

1. Unit pairing. Every pure-core header under the three ``units/``
   directories, plus the app-subtree placements listed in
   tests/units/app-subtree-units.list, must have its paired test
   ``tests/units/<basename>test.cpp`` present AND registered in ctest. A
   unit cannot land untested without failing here.

2. Entry-list ratchet. The ctest entry list must match
   tests/coverage/ratchet-baseline exactly. Adding a test means adding
   its line to the baseline in the same commit (routine, monotonic
   growth); REMOVING a test is only possible by deliberately editing the
   committed baseline, which makes silent coverage loss un-mergeable.

The entry-list source of truth is ``ctest -N`` in the build dir - a
dry-run listing, never a test run - the exact source the bash read via
``ctest --test-dir <build> -N | sed -n 's/^ *Test *#[0-9]*: //p'``.

Two message prefixes carry over verbatim from the bash so log-reading
habits and the docs' quoted output stay valid: per-line failures print
``ratchet: ...`` and the success line prints ``coverage-ratchet: OK``.
Like the bash, every failure of a run is collected and printed before
the single nonzero exit (a missing baseline is the one hard early
return); the ratchet's whole job is to fail loudly and completely, so it
never stops at the first violation.
"""

from __future__ import annotations

import argparse
import re
import sys
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path

from latte_harness.log import fail, info
from latte_harness.paths import RepoPaths
from latte_harness.proc import run

# The per-line failure prefix and the success-line prefix are deliberately
# different, matching the bash script's own two prefixes exactly.
FAIL_TOOL = "ratchet"
OK_TOOL = "coverage-ratchet"

UNIT_DIRS = (
    "containment/plugin/units",
    "plasmoid/plugin/units",
    "declarativeimports/core/units",
)
APP_SUBTREE_LIST = "tests/units/app-subtree-units.list"
UNIT_TESTS_DIR = "tests/units"
BASELINE = "tests/coverage/ratchet-baseline"

# The `ctest -N` listing shape: `  Test #N: name`. Mirrors the bash
# `sed -n 's/^ *Test *#[0-9]*: //p'` including the zero-or-more-digits
# form, so the trailing `Total Tests: N` summary and any banner lines are
# ignored and only the entry names are captured.
_CTEST_ENTRY = re.compile(r"^ *Test *#[0-9]*: (.*)$")


# --- pure logic ------------------------------------------------------------


def parse_ctest_listing(text: str) -> tuple[str, ...]:
    """Extract test names from ``ctest -N`` output, in listing order."""
    names: list[str] = []
    for line in text.splitlines():
        match = _CTEST_ENTRY.match(line)
        if match is not None:
            names.append(match.group(1))
    return tuple(names)


def parse_baseline(text: str) -> tuple[str, tuple[str, ...]]:
    """Split the baseline body into (count_line, sorted_entries).

    Mirrors the bash ``grep -v '^#' | sed '/^$/d'`` then
    ``head -n1`` / ``tail -n +2 | sort``: comment lines (``#`` at column
    zero) and blank lines are dropped, the first survivor is the recorded
    count line (kept verbatim, the bash never trims it), and the rest are
    the entries, sorted. The sort is codepoint order (== ``LC_ALL=C
    sort``); equality of two identically-sorted lists is independent of
    the sort locale, so the divergence verdict is unaffected by it.
    """
    kept = [line for line in text.splitlines() if line and not line.startswith("#")]
    if not kept:
        return "", ()
    return kept[0], tuple(sorted(kept[1:]))


@dataclass(frozen=True, slots=True)
class HeaderCheck:
    """One unit header's pairing status, in header-scan order."""

    header: str  # repo-relative header path (for the message)
    base: str  # basename without the .h suffix
    has_test_source: bool  # tests/units/<base>test.cpp exists
    registered: bool  # <base>test is a ctest entry


def classify_pairings(
    headers: Sequence[str],
    test_source_stems: frozenset[str],
    ctest_names: frozenset[str],
) -> tuple[HeaderCheck, ...]:
    """Pair each header against its test source and ctest registration.

    ``test_source_stems`` is the set of ``.cpp`` stems under
    tests/units/ (e.g. ``"parabolicmathtest"``); the paired name for a
    header is ``<base>test`` where ``base`` is the header basename minus
    ``.h``. A header can fail both ways (no source and unregistered), and
    both are reported, exactly as the bash checks them independently.
    """
    checks: list[HeaderCheck] = []
    for header in headers:
        base = Path(header).stem
        test_name = f"{base}test"
        checks.append(
            HeaderCheck(
                header=header,
                base=base,
                has_test_source=test_name in test_source_stems,
                registered=test_name in ctest_names,
            )
        )
    return tuple(checks)


@dataclass(frozen=True, slots=True)
class BaselineDiff:
    """The entry-list ratchet comparison, both failure modes classified."""

    recorded_count: str  # the baseline's count line, verbatim
    recorded_entries: tuple[str, ...]  # sorted
    actual_entries: tuple[str, ...]  # the live ctest names, sorted
    removed: tuple[str, ...]  # recorded but not live (silent-loss guard)
    added: tuple[str, ...]  # live but not recorded (needs a baseline bump)

    @property
    def count_line_disagrees(self) -> bool:
        return self.recorded_count != str(len(self.recorded_entries))

    @property
    def diverged(self) -> bool:
        return self.recorded_entries != self.actual_entries

    @property
    def ok(self) -> bool:
        return not self.count_line_disagrees and not self.diverged


def classify_baseline(
    count_line: str,
    recorded_entries: Sequence[str],
    actual_names: Sequence[str],
) -> BaselineDiff:
    """Compare the recorded baseline against the live ctest entry list."""
    recorded = tuple(sorted(recorded_entries))
    actual = tuple(sorted(actual_names))
    recorded_set = frozenset(recorded)
    actual_set = frozenset(actual)
    return BaselineDiff(
        recorded_count=count_line,
        recorded_entries=recorded,
        actual_entries=actual,
        removed=tuple(sorted(recorded_set - actual_set)),
        added=tuple(sorted(actual_set - recorded_set)),
    )


# --- IO layer --------------------------------------------------------------


def ctest_entry_names(build: Path) -> tuple[str, ...]:
    """The entry-list source of truth: ``ctest -N`` in ``build``.

    A listing, never a run. Fails loudly if the build dir has no test
    registry - the bash relied on ``set -o pipefail`` to abort there;
    capture hid ctest's own message, so it is re-surfaced before the
    refusal so the failure is actionable.
    """
    result = run(["ctest", "--test-dir", str(build), "-N"], capture=True)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        fail(FAIL_TOOL, f"ctest -N in {build} exited {result.returncode}")
    return parse_ctest_listing(result.stdout)


def collect_headers(root: Path) -> tuple[tuple[str, ...], tuple[str, ...]]:
    """Assemble the unit-header inventory as repo-relative paths.

    Returns ``(headers, listed_missing)``. Headers come from every
    ``*.h`` under the three ``units/`` directories (recursively, sorted
    within each directory, directories in listed order) plus the
    app-subtree list. A list line pointing at a header that does not
    exist is a violation and is NOT counted as a header (the bash
    ``continue``).
    """
    headers: list[str] = []
    for unit_dir in UNIT_DIRS:
        directory = root / unit_dir
        if not directory.is_dir():
            continue
        found = sorted(str(path.relative_to(root)) for path in directory.rglob("*.h"))
        headers.extend(found)

    listed_missing: list[str] = []
    list_path = root / APP_SUBTREE_LIST
    if list_path.is_file():
        for line in list_path.read_text().splitlines():
            if not line or line.startswith("#"):
                continue
            if not (root / line).is_file():
                listed_missing.append(line)
                continue
            headers.append(line)
    return tuple(headers), tuple(listed_missing)


def test_source_stems(root: Path) -> frozenset[str]:
    """The ``.cpp`` stems under tests/units/ (e.g. ``"rowentrytest"``)."""
    units = root / UNIT_TESTS_DIR
    if not units.is_dir():
        return frozenset()
    return frozenset(path.stem for path in units.glob("*.cpp"))


# --- orchestration ---------------------------------------------------------


def check(root: Path, build: Path, *, baseline_path: Path | None = None) -> int:
    """Run both ratchet checks, print the report, return the exit code.

    ``baseline_path`` overrides the committed baseline; it defaults to
    ``root/tests/coverage/ratchet-baseline`` (production always uses the
    default). The override is the seam the driven negative controls use
    to introduce a violation without mutating the tracked baseline.
    """
    ctest_names = ctest_entry_names(build)
    ctest_set = frozenset(ctest_names)

    headers, listed_missing = collect_headers(root)
    checks = classify_pairings(headers, test_source_stems(root), ctest_set)

    failed = False

    for missing in listed_missing:
        info(FAIL_TOOL, f"FAIL listed unit header does not exist: {missing}")
        failed = True

    for header_check in checks:
        if not header_check.has_test_source:
            info(
                FAIL_TOOL,
                f"FAIL unit header {header_check.header} has no paired test "
                f"tests/units/{header_check.base}test.cpp",
            )
            failed = True
        if not header_check.registered:
            info(
                FAIL_TOOL,
                f"FAIL paired test {header_check.base}test is not registered in ctest",
            )
            failed = True

    baseline = baseline_path if baseline_path is not None else root / BASELINE
    if not baseline.is_file():
        # The one hard early return the bash keeps: with no baseline there
        # is nothing to ratchet against.
        info(FAIL_TOOL, f"FAIL missing {baseline}")
        return 1

    count_line, recorded_entries = parse_baseline(baseline.read_text())
    diff = classify_baseline(count_line, recorded_entries, ctest_names)

    if diff.count_line_disagrees:
        info(
            FAIL_TOOL,
            "FAIL tests/coverage/ratchet-baseline count line disagrees with its own entry list",
        )
        failed = True

    if diff.diverged:
        info(FAIL_TOOL, "FAIL ctest entry list diverged from tests/coverage/ratchet-baseline")
        info(
            FAIL_TOOL,
            f"recorded {diff.recorded_count} entries, ctest reports "
            f"{len(diff.actual_entries)}. Diff (-recorded +actual):",
        )
        for name in diff.removed:
            info(FAIL_TOOL, f"  -{name}")
        for name in diff.added:
            info(FAIL_TOOL, f"  +{name}")
        info(
            FAIL_TOOL,
            "additions are routine - update tests/coverage/ratchet-baseline in the same commit.",
        )
        info(FAIL_TOOL, "removals must be deliberate - the baseline edit is the audit trail.")
        failed = True

    if failed:
        return 1

    info(
        OK_TOOL,
        f"OK ({len(diff.actual_entries)} ctest entries, {len(headers)} unit headers paired)",
    )
    return 0


def main() -> None:
    parser = argparse.ArgumentParser(
        prog="coverage-ratchet",
        description="Unit-header/test pairing plus the committed ctest entry-list baseline.",
    )
    parser.add_argument(
        "build",
        nargs="?",
        default=None,
        help="ctest build directory (default: <repo>/build)",
    )
    parser.add_argument(
        "--baseline",
        default=None,
        help="override the ratchet baseline path (test/override seam; "
        "production uses tests/coverage/ratchet-baseline)",
    )
    args = parser.parse_args()

    paths = RepoPaths.discover()
    build = Path(args.build) if args.build is not None else paths.build
    baseline_path = Path(args.baseline) if args.baseline is not None else None
    raise SystemExit(check(paths.root, build, baseline_path=baseline_path))


if __name__ == "__main__":
    main()
