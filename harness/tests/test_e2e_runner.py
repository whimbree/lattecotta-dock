# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The e2e runner's pure logic: the bit-identical classification matrix, marker
extraction strictness, recipe discovery, and the classifier self-test.

Every cell of the PASS / FAIL / XFAIL / XPASS matrix and every marker refusal is
pinned here (the docs/reference/TESTING.md contract). The full vehicle bring-up
is driven by the run-matrix self-test and the curated nets, not here; these hold
the decision points a unit test can pin without a running compositor.
"""

from pathlib import Path

import pytest

from latte_harness.e2e_runner import (
    Counters,
    Expectation,
    ExpectFail,
    ExpectStatus,
    MarkerError,
    RecipeResult,
    Unmarked,
    classify_recipe_result,
    discover_recipes,
    extract_recipe_expectation,
    parse_expectation_value,
    recipe_mode,
    resolve_recipe,
    run_expectation_selftest,
)

# ---- classification matrix (every cell) ------------------------------------


def test_unmarked_zero_is_pass() -> None:
    result, message = classify_recipe_result(Unmarked(), 0, "foo")
    assert result is RecipeResult.PASS
    assert message == "run-e2e: PASS foo"


def test_unmarked_nonzero_is_fail() -> None:
    result, message = classify_recipe_result(Unmarked(), 1, "foo")
    assert result is RecipeResult.FAIL
    assert message == "run-e2e: FAIL foo"


def test_expect_fail_nonzero_is_xfail() -> None:
    result, message = classify_recipe_result(ExpectFail(), 3, "foo")
    assert result is RecipeResult.XFAIL
    assert message == "run-e2e: XFAIL foo (expected failure of a known-open bug, not counted)"


def test_expect_fail_zero_is_xpass() -> None:
    result, message = classify_recipe_result(ExpectFail(), 0, "foo")
    assert result is RecipeResult.XPASS
    assert "XPASS foo" in message
    assert "remove '# e2e-expect: fail'" in message


def test_expect_status_match_is_xfail() -> None:
    result, message = classify_recipe_result(ExpectStatus(57), 57, "foo")
    assert result is RecipeResult.XFAIL
    assert message == "run-e2e: XFAIL foo (matched reserved status 57 for the known-open bug)"


def test_expect_status_zero_is_xpass() -> None:
    result, message = classify_recipe_result(ExpectStatus(57), 0, "foo")
    assert result is RecipeResult.XPASS
    assert "expected reserved status 57 but passed" in message


def test_expect_status_other_nonzero_is_fail() -> None:
    # a nonzero exit OUTSIDE the reserved signature is a real failure, not XFAIL
    result, message = classify_recipe_result(ExpectStatus(57), 1, "foo")
    assert result is RecipeResult.FAIL
    assert message == (
        "run-e2e: FAIL foo (expected reserved status 57, got 1; "
        "failure is outside the known signature)"
    )


# ---- marker extraction strictness ------------------------------------------


def test_no_marker_is_unmarked() -> None:
    assert extract_recipe_expectation("#!/usr/bin/env bash\necho hi\n") == Unmarked()


def test_legacy_fail_marker() -> None:
    assert extract_recipe_expectation("# e2e-expect: fail\n") == ExpectFail()


def test_exact_status_marker() -> None:
    assert extract_recipe_expectation("# e2e-expect: status 42\n") == ExpectStatus(42)


@pytest.mark.parametrize("status", [1, 255])
def test_status_boundaries_accepted(status: int) -> None:
    assert extract_recipe_expectation(f"# e2e-expect: status {status}\n") == ExpectStatus(status)


def _refusal(text: str) -> str:
    parsed = extract_recipe_expectation(text)
    assert isinstance(parsed, MarkerError), f"expected refusal, got {parsed!r}"
    return parsed.message


def test_blank_marker_refused() -> None:
    assert "blank" in _refusal("# e2e-expect:   \n")


def test_duplicate_marker_refused() -> None:
    assert "2 e2e-expect declarations" in _refusal("# e2e-expect: fail\n# e2e-expect: fail\n")


def test_conflicting_markers_refused() -> None:
    # two different markers are still just "more than one declaration"
    assert "2 e2e-expect declarations" in _refusal("# e2e-expect: fail\n# e2e-expect: status 42\n")


def test_malformed_marker_refused_missing_colon() -> None:
    assert "malformed" in _refusal("# e2e-expect status 42\n")


def test_malformed_marker_refused_no_space_after_hash() -> None:
    # the loose scan matches "#e2e-expect", the strict prefix does not
    assert "malformed" in _refusal("#e2e-expect: fail\n")


def test_malformed_marker_refused_when_indented() -> None:
    # sed's `^[[:space:]]*#...` matched an indented marker; the strict prefix
    # check then requires the exact "# e2e-expect:" at column 0
    assert "malformed" in _refusal("    # e2e-expect: fail\n")


def test_unknown_value_refused() -> None:
    assert "invalid" in _refusal("# e2e-expect: unknown\n")


def test_status_zero_refused() -> None:
    assert "invalid" in _refusal("# e2e-expect: status 0\n")


def test_status_out_of_range_refused() -> None:
    assert "invalid" in _refusal("# e2e-expect: status 256\n")


def test_marker_refusal_precedes_any_run() -> None:
    # the refusal is a value, returned before the recipe would be executed;
    # there is no execution path from extract, so a bad marker can never run
    assert isinstance(extract_recipe_expectation("# e2e-expect: status 999\n"), MarkerError)


@pytest.mark.parametrize(
    ("value", "expected"),
    [
        ("", Unmarked()),
        ("fail", ExpectFail()),
        ("status 1", ExpectStatus(1)),
        ("status 255", ExpectStatus(255)),
    ],
)
def test_parse_expectation_value_valid(value: str, expected: Expectation) -> None:
    assert parse_expectation_value(value) == expected


@pytest.mark.parametrize("value", ["status 0", "status 256", "status  42", "unknown", "STATUS 1"])
def test_parse_expectation_value_invalid(value: str) -> None:
    assert parse_expectation_value(value) is None


# ---- e2e-mode marker -------------------------------------------------------


@pytest.mark.parametrize(
    ("text", "expected"),
    [
        ("# e2e-mode: nested-only\n", "nested-only"),
        ("# e2e-mode: live-only\n", "live-only"),
        ("# e2e-mode:nested-only\n", "nested-only"),
        ("#!/usr/bin/env bash\n# e2e-mode: nested-only\n", "nested-only"),
        ("echo hi\n", ""),
        ("#e2e-mode: nested-only\n", ""),  # no space after hash: not the exact prefix
        ("   # e2e-mode: nested-only\n", ""),  # indented: not anchored at column 0
    ],
)
def test_recipe_mode(text: str, expected: str) -> None:
    assert recipe_mode(text) == expected


def test_recipe_mode_first_line_wins() -> None:
    assert recipe_mode("# e2e-mode: nested-only\n# e2e-mode: live-only\n") == "nested-only"


# ---- discovery -------------------------------------------------------------


def _make_recipe(path: Path) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("#!/usr/bin/env bash\n")
    return path


def test_discovery_excludes_lib_and_subdirs(tmp_path: Path) -> None:
    e2e = tmp_path / "tests" / "e2e"
    _make_recipe(e2e / "000-smoke.sh")
    _make_recipe(e2e / "010-wheel.sh")
    _make_recipe(e2e / "lib.sh")  # sourced, never a recipe
    _make_recipe(e2e / "matrix" / "matrix-lib.sh")  # subdir lib: the discovery bug
    _make_recipe(e2e / "audit" / "audit-lib.sh")
    _make_recipe(e2e / "fixtures" / "sc-w1" / "launcher.sh")

    found = discover_recipes(e2e, [])

    assert [p.name for p in found] == ["000-smoke.sh", "010-wheel.sh"]


def test_discovery_includes_top_level_py(tmp_path: Path) -> None:
    e2e = tmp_path / "tests" / "e2e"
    _make_recipe(e2e / "000-smoke.sh")
    _make_recipe(e2e / "005-probe.py")  # a .py recipe runs side by side (BP-3)
    _make_recipe(e2e / "matrix" / "helper.py")  # subdir .py still excluded

    found = discover_recipes(e2e, [])

    assert [p.name for p in found] == ["000-smoke.sh", "005-probe.py"]


def test_explicit_name_resolves_bare_to_sh(tmp_path: Path) -> None:
    e2e = tmp_path / "e2e"
    _make_recipe(e2e / "000-smoke.sh")
    assert discover_recipes(e2e, ["000-smoke"]) == [e2e / "000-smoke.sh"]


def test_explicit_name_resolves_dot_sh(tmp_path: Path) -> None:
    e2e = tmp_path / "e2e"
    _make_recipe(e2e / "000-smoke.sh")
    assert resolve_recipe(e2e, "000-smoke.sh") == e2e / "000-smoke.sh"


def test_explicit_name_resolves_py_when_only_py_exists(tmp_path: Path) -> None:
    e2e = tmp_path / "e2e"
    _make_recipe(e2e / "005-probe.py")
    assert resolve_recipe(e2e, "005-probe") == e2e / "005-probe.py"
    assert resolve_recipe(e2e, "005-probe.py") == e2e / "005-probe.py"


def test_explicit_name_prefers_sh_over_py(tmp_path: Path) -> None:
    e2e = tmp_path / "e2e"
    _make_recipe(e2e / "dup.sh")
    _make_recipe(e2e / "dup.py")
    # ".sh then .py": an existing .sh wins even for an explicit .py name
    assert resolve_recipe(e2e, "dup.py") == e2e / "dup.sh"


def test_missing_name_resolves_to_sh_path_for_a_loud_refusal(tmp_path: Path) -> None:
    e2e = tmp_path / "e2e"
    e2e.mkdir(parents=True)
    # neither exists: resolve to the .sh path so the loop's "missing or
    # non-executable recipe" refusal fires with a sensible name
    assert resolve_recipe(e2e, "nope") == e2e / "nope.sh"


def test_discovery_sorts_mixed_sh_and_py(tmp_path: Path) -> None:
    e2e = tmp_path / "e2e"
    for name in ("030-c.sh", "010-a.py", "020-b.sh"):
        _make_recipe(e2e / name)
    assert [p.name for p in discover_recipes(e2e, [])] == ["010-a.py", "020-b.sh", "030-c.sh"]


# ---- counters --------------------------------------------------------------


def test_counters_record_tally() -> None:
    counters = Counters()
    counters.record(RecipeResult.PASS)
    counters.record(RecipeResult.FAIL)
    counters.record(RecipeResult.XFAIL)
    counters.record(RecipeResult.XPASS)
    counters.record(RecipeResult.SKIP)
    assert (counters.passed, counters.failed, counters.xfailed, counters.skipped) == (1, 2, 1, 1)


# ---- the self-test itself --------------------------------------------------


def test_expectation_selftest_is_green() -> None:
    # the runtime --self-test-expectations surface must pass on the real
    # classifier (the bash startup guard exits 2 if it does not)
    assert run_expectation_selftest(quiet=True) == 0
