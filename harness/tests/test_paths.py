# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Repo-root discovery: found from inside the tree, refused outside it."""

from pathlib import Path

import pytest

from latte_harness.paths import RepoPaths, RepoRootNotFoundError, find_repo_root


def test_finds_root_from_module_location() -> None:
    root = find_repo_root()
    assert (root / "flake.nix").is_file()
    assert (root / "harness").is_dir()


def test_refuses_outside_a_repo(tmp_path: Path) -> None:
    with pytest.raises(RepoRootNotFoundError):
        find_repo_root(tmp_path)


def test_worktree_git_file_counts_as_root(tmp_path: Path) -> None:
    # A linked worktree's .git is a FILE; discovery must accept it.
    (tmp_path / "flake.nix").touch()
    (tmp_path / ".git").write_text("gitdir: /elsewhere\n")
    assert find_repo_root(tmp_path) == tmp_path


def test_repo_paths_root_consistency() -> None:
    paths = RepoPaths.discover()
    assert paths.build == paths.root / "build"
    assert paths.tests_e2e == paths.root / "tests" / "e2e"
    assert paths.harness.is_dir()
