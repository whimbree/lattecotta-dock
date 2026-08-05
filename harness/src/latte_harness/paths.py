# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Repo-relative path discovery.

Every harness entry point resolves the repo root the way the bash
scripts' ``$(dirname "$0")/..`` did, but from any cwd: walk upward until
a directory carries both flake.nix and .git. Hardcoded absolute paths
are banned (the portability half of the BP contract); everything roots
here.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


class RepoRootNotFoundError(RuntimeError):
    """No ancestor of the start path is the repo root."""


def find_repo_root(start: Path | None = None) -> Path:
    """Walk upward from ``start`` (default: this file) to the repo root.

    ``.git`` is checked with exists(), not is_dir(): in a linked git
    worktree (the implementation agents' isolation mode) it is a file.
    """
    origin = (start if start is not None else Path(__file__)).resolve()
    for candidate in [origin, *origin.parents]:
        if (candidate / "flake.nix").is_file() and (candidate / ".git").exists():
            return candidate
    raise RepoRootNotFoundError(f"no repo root (flake.nix + .git) above {origin}")


@dataclass(frozen=True, slots=True)
class RepoPaths:
    """The repo directories the harness touches, rooted once."""

    root: Path

    @classmethod
    def discover(cls, start: Path | None = None) -> RepoPaths:
        return cls(root=find_repo_root(start))

    @property
    def build(self) -> Path:
        return self.root / "build"

    @property
    def harness(self) -> Path:
        return self.root / "harness"

    @property
    def scripts(self) -> Path:
        return self.root / "scripts"

    @property
    def tests(self) -> Path:
        return self.root / "tests"

    @property
    def tests_e2e(self) -> Path:
        return self.root / "tests" / "e2e"
