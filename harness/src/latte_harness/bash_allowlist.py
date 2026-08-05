# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""The retained-bash allowlist ratchet (BP-0b).

Bash may only exist at the repo paths listed in harness/bash-allowlist.txt.
The list was seeded with the full pre-migration inventory and only ever
shrinks, until it is exactly the 15-file retained set the BP plan
records:

- a tracked bash file missing from the list fails the gate: no new bash,
  no silent re-accretion;
- a listed path that no longer exists also fails: deleting bash must
  shrink the list in the same commit, keeping the ratchet honest (the
  same shrink discipline as the qmllint baseline).

Bash is inventoried as every git-tracked ``*.sh`` file plus every
tracked file whose first line is a sh/bash shebang (hooks, packaging
helpers) - detection by content, not by a hand-maintained name list, so
extensionless bash cannot escape the ratchet by construction. Untracked
scratch files are deliberately out of scope: the ratchet guards what
lands in history, not a working tree's experiments.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

from latte_harness.log import fail, info
from latte_harness.paths import RepoPaths
from latte_harness.proc import run

TOOL = "bash-allowlist"

ALLOWLIST_NAME = "bash-allowlist.txt"

_SHEBANG_SHELLS = (b"/bin/sh", b"/bin/bash", b"env sh", b"env bash")


class AllowlistFormatError(ValueError):
    """The allowlist file violates its own hygiene rules."""


@dataclass(frozen=True, slots=True)
class Violations:
    """The two ratchet failure modes, both reported in full."""

    unlisted: tuple[str, ...]  # tracked bash not in the allowlist
    stale: tuple[str, ...]  # allowlisted paths no longer tracked

    @property
    def ok(self) -> bool:
        return not self.unlisted and not self.stale


def compare(allowed: frozenset[str], actual: frozenset[str]) -> Violations:
    return Violations(
        unlisted=tuple(sorted(actual - allowed)),
        stale=tuple(sorted(allowed - actual)),
    )


def load_allowlist(path: Path) -> frozenset[str]:
    """Read the allowlist: comments and blanks stripped, order enforced.

    Sorted-unique is required so diffs stay one-line-per-change and
    review stays trivial; violating the format is a loud failure, not a
    silent normalization.
    """
    entries = [
        line.strip()
        for line in path.read_text().splitlines()
        if line.strip() and not line.lstrip().startswith("#")
    ]
    if entries != sorted(set(entries)):
        raise AllowlistFormatError(f"{path} must be sorted and duplicate-free")
    return frozenset(entries)


def _is_shell_shebang(path: Path) -> bool:
    try:
        with path.open("rb") as handle:
            first = handle.readline(160)
    except OSError:
        # Unreadable tracked entries (dangling symlinks) are not bash.
        return False
    return first.startswith(b"#!") and any(shell in first for shell in _SHEBANG_SHELLS)


def tracked_bash(root: Path) -> frozenset[str]:
    result = run(["git", "-C", str(root), "ls-files"], capture=True, check=True)
    hits: set[str] = set()
    for rel in result.stdout.splitlines():
        if not rel:
            continue
        if rel.endswith(".sh") or _is_shell_shebang(root / rel):
            hits.add(rel)
    return frozenset(hits)


def check(paths: RepoPaths) -> Violations:
    allowed = load_allowlist(paths.harness / ALLOWLIST_NAME)
    return compare(allowed, tracked_bash(paths.root))


def main() -> None:
    paths = RepoPaths.discover()
    violations = check(paths)
    if violations.unlisted:
        for entry in violations.unlisted:
            info(TOOL, f"NOT ALLOWLISTED: {entry}")
        info(TOOL, "new bash is banned (BP migration); write it in latte_harness instead,")
        info(TOOL, f"or (retained-set change, needs the plan updated) add it to {ALLOWLIST_NAME}")
    if violations.stale:
        for entry in violations.stale:
            info(TOOL, f"STALE ENTRY: {entry}")
        info(TOOL, f"deleted bash must leave {ALLOWLIST_NAME} in the same commit (shrink-only)")
    if not violations.ok:
        fail(TOOL, f"{len(violations.unlisted)} unlisted, {len(violations.stale)} stale")
    info(TOOL, "OK")


if __name__ == "__main__":
    main()
