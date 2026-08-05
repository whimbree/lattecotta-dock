# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The allowlist ratchet's two failure modes and its format hygiene.

The negative controls matter more than the happy path: a ratchet that
cannot fail is decoration (the non-vacuous-guard rule)."""

from pathlib import Path

import pytest

from latte_harness.bash_allowlist import (
    AllowlistFormatError,
    check,
    compare,
    is_shell_shebang,
    load_allowlist,
    tracked_bash,
)
from latte_harness.paths import RepoPaths


def test_clean_when_sets_match() -> None:
    both = frozenset({"a.sh", "b.sh"})
    assert compare(both, both).ok


def test_detects_new_bash() -> None:
    violations = compare(frozenset({"a.sh"}), frozenset({"a.sh", "sneaky.sh"}))
    assert violations.unlisted == ("sneaky.sh",)
    assert not violations.ok


def test_detects_stale_entries() -> None:
    violations = compare(frozenset({"a.sh", "deleted.sh"}), frozenset({"a.sh"}))
    assert violations.stale == ("deleted.sh",)
    assert not violations.ok


def test_load_strips_comments_and_blanks(tmp_path: Path) -> None:
    listing = tmp_path / "list.txt"
    listing.write_text("# comment\n\na.sh\nb.sh\n")
    assert load_allowlist(listing) == frozenset({"a.sh", "b.sh"})


@pytest.mark.parametrize("body", ["b.sh\na.sh\n", "a.sh\na.sh\n"])
def test_load_refuses_unsorted_or_duplicates(tmp_path: Path, body: str) -> None:
    listing = tmp_path / "list.txt"
    listing.write_text(body)
    with pytest.raises(AllowlistFormatError):
        load_allowlist(listing)


def test_real_repo_is_currently_clean() -> None:
    # The committed allowlist matches the tracked inventory right now;
    # this is the same check the gate leg runs, kept in pytest so a
    # drifting branch fails at the earliest (unit-test) layer too.
    violations = check(RepoPaths.discover())
    assert violations.ok, f"unlisted={violations.unlisted} stale={violations.stale}"


@pytest.mark.parametrize(
    ("first_line", "expected"),
    [
        ("#!/bin/sh", True),
        ("#!/bin/bash", True),
        ("#!/usr/bin/env bash", True),
        ("#!/usr/bin/env -S bash -eu", True),  # env flag form
        ("#!/usr/bin/env shellcheck", False),  # 'sh' substring must not match
        ("#!/usr/bin/env python3", False),
        ("# not a shebang at all", False),
        ("", False),
    ],
)
def test_shebang_matcher_is_token_precise(tmp_path: Path, first_line: str, expected: bool) -> None:
    probe = tmp_path / "probe"
    probe.write_text(f"{first_line}\necho hi\n")
    assert is_shell_shebang(probe) is expected


def test_shebang_detection_catches_extensionless_bash() -> None:
    # The inventory is by content, not name: these tracked files carry no
    # .sh extension and were exactly the gap a name-based inventory left
    # open (found during the PR #153 review pass). If any of them leaves
    # the tree, replace it with another extensionless tracked hook.
    inventory = tracked_bash(RepoPaths.discover().root)
    for extensionless in (
        "packaging/debian/build-package",
        "packaging/void/build-package",
        "scripts/git-hooks/pre-push",
    ):
        assert extensionless in inventory, f"{extensionless} escaped the shebang inventory"
