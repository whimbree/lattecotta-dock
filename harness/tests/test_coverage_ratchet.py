# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The coverage ratchet's pure logic and its two driven failure modes.

Pure tests pin the pairing detection and the baseline diff
classification. The driven negative controls run the real module against
the real configured build dir and read the process exit code unpiped: a
ratchet that cannot fail loudly is decoration (the non-vacuous-guard
rule). They skip when the build dir is not yet configured, because the
harness-check gate leg runs before build-check.sh builds the tree - in
that ordering the ratchet is exercised end-to-end via its shim instead.
"""

import subprocess
import sys
from pathlib import Path

import pytest

from latte_harness.coverage_ratchet import (
    classify_baseline,
    classify_pairings,
    parse_baseline,
    parse_ctest_listing,
)
from latte_harness.paths import RepoPaths

_PATHS = RepoPaths.discover()
_BUILD = _PATHS.build

requires_build = pytest.mark.skipif(
    not (_BUILD / "CTestTestfile.cmake").is_file(),
    reason="no configured ctest build dir (build-check.sh not yet run); "
    "the ratchet is exercised via its shim in build-check",
)


def _run_ratchet(build: Path, *, baseline: Path | None = None) -> subprocess.CompletedProcess[str]:
    argv = [sys.executable, "-m", "latte_harness.coverage_ratchet", str(build)]
    if baseline is not None:
        argv += ["--baseline", str(baseline)]
    return subprocess.run(argv, capture_output=True, text=True, check=False)


# --- pure logic: ctest listing + baseline parsing --------------------------


def test_parse_ctest_listing_extracts_names_and_ignores_summary() -> None:
    sample = (
        "Test project /home/x/build\n"
        "  Test #1: alpha\n"
        "  Test #2: beta\n"
        "  Test #10: gamma\n"
        "\n"
        "Total Tests: 3\n"
    )
    assert parse_ctest_listing(sample) == ("alpha", "beta", "gamma")


def test_parse_ctest_listing_empty_is_empty() -> None:
    assert parse_ctest_listing("") == ()


def test_parse_baseline_strips_comments_blanks_and_sorts() -> None:
    text = "# header comment\n126\nbeta\nalpha\n\ngamma\n"
    count, entries = parse_baseline(text)
    assert count == "126"
    assert entries == ("alpha", "beta", "gamma")


def test_parse_baseline_empty_body() -> None:
    assert parse_baseline("# only a comment\n") == ("", ())


# --- pure logic: pairing detection -----------------------------------------


def test_classify_pairings_all_paired_and_registered() -> None:
    checks = classify_pairings(["a/foo.h"], frozenset({"footest"}), frozenset({"footest"}))
    assert len(checks) == 1
    assert checks[0].base == "foo"
    assert checks[0].has_test_source
    assert checks[0].registered


def test_classify_pairings_flags_missing_test_source() -> None:
    checks = classify_pairings(["a/foo.h"], frozenset(), frozenset({"footest"}))
    assert not checks[0].has_test_source
    assert checks[0].registered


def test_classify_pairings_flags_unregistered() -> None:
    checks = classify_pairings(["a/foo.h"], frozenset({"footest"}), frozenset())
    assert checks[0].has_test_source
    assert not checks[0].registered


def test_classify_pairings_flags_both_missing_source_and_unregistered() -> None:
    checks = classify_pairings(["a/foo.h"], frozenset(), frozenset())
    assert not checks[0].has_test_source
    assert not checks[0].registered


# --- pure logic: baseline diff classification ------------------------------


def test_classify_baseline_clean_when_entries_match() -> None:
    diff = classify_baseline("2", ["a", "b"], ["b", "a"])
    assert diff.ok
    assert not diff.diverged
    assert not diff.count_line_disagrees


def test_classify_baseline_count_line_disagrees_with_its_own_list() -> None:
    diff = classify_baseline("3", ["a", "b"], ["a", "b"])
    assert diff.count_line_disagrees
    assert not diff.diverged  # entries match; only the count line lies
    assert not diff.ok


def test_classify_baseline_detects_deliberate_removal() -> None:
    # ctest dropped an entry the baseline still records: the removal path
    # the audit-trail message guards ("removals must be deliberate").
    diff = classify_baseline("2", ["a", "b"], ["a"])
    assert diff.diverged
    assert diff.removed == ("b",)
    assert diff.added == ()


def test_classify_baseline_detects_routine_addition() -> None:
    # ctest gained an entry the baseline has not yet recorded.
    diff = classify_baseline("1", ["a"], ["a", "b"])
    assert diff.diverged
    assert diff.added == ("b",)
    assert diff.removed == ()


# --- driven negative controls against the real build dir -------------------


@requires_build
def test_driven_real_build_passes() -> None:
    # Same verdict as the bash version on the current tree (the equivalence
    # contract's positive half): the real inventory ratchets clean.
    result = _run_ratchet(_BUILD)
    assert result.returncode == 0, result.stdout + result.stderr
    assert result.stdout.startswith("coverage-ratchet: OK ("), result.stdout


@requires_build
def test_driven_removed_baseline_entry_is_refused(tmp_path: Path) -> None:
    # A baseline copy with one entry removed: the silent-coverage-loss slip
    # the ratchet exists to make un-mergeable. Both the divergence guard
    # and the count-line guard must fire, with a nonzero exit.
    real = _PATHS.root / "tests" / "coverage" / "ratchet-baseline"
    lines = real.read_text().splitlines()
    trimmed = tmp_path / "ratchet-baseline"
    trimmed.write_text("\n".join(lines[:-1]) + "\n")

    result = _run_ratchet(_BUILD, baseline=trimmed)

    assert result.returncode == 1, result.stdout + result.stderr
    assert (
        "ratchet: FAIL ctest entry list diverged from tests/coverage/ratchet-baseline"
        in result.stdout
    ), result.stdout
    assert (
        "ratchet: FAIL tests/coverage/ratchet-baseline count line disagrees "
        "with its own entry list" in result.stdout
    ), result.stdout


@requires_build
def test_driven_unpaired_header_is_detected() -> None:
    # A synthetic pure-core header with no paired test in a real units/
    # directory. The pairing check must catch it and refuse; the probe is
    # removed on every exit path so the tree is never left dirty.
    probe = _PATHS.root / "containment" / "plugin" / "units" / "__ratchet_probe__.h"
    probe.write_text("// synthetic BP-1c ratchet negative control; safe to delete\n")
    try:
        result = _run_ratchet(_BUILD)
    finally:
        probe.unlink(missing_ok=True)

    assert result.returncode == 1, result.stdout + result.stderr
    assert (
        "ratchet: FAIL unit header containment/plugin/units/__ratchet_probe__.h "
        "has no paired test tests/units/__ratchet_probe__test.cpp" in result.stdout
    ), result.stdout
