# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Source-scan rule test for two hard-won Qt6 QML effect contracts.

The typed port of scripts/qml-effect-rules.sh (BP-1e); the plan of record
is docs/tracking/bash-python-migration-plan.md. The rules and their war
stories (latte-plasma6-defect-families, families 7 and 3) are preserved
verbatim from the bash version:

Rule 1: no autoPaddingEnabled anywhere in shipped QML except the literal
``autoPaddingEnabled: false``. autoPadding recomputes the effect's padding
and re-dirties it continuously, so every window carrying such an effect
re-rendered empty frames forever - measured 18.2% idle CPU and ~19,500
failing statx/s from per-frame theme lookups before e3376405 made
ShadowedItem's padding static. Effects must carry a STATIC per-side
paddingRect instead (per-side semantics: 6c7001ce).

Rule 2: every when-gated Binding element in shipped QML must declare an
explicit restoreMode. Qt6 changed the Binding default from RestoreNone to
RestoreBindingOrValue, so a ``Binding { when: }`` meant to FREEZE its
target's last value on deactivation instead RESETS it to the declared
default - the regression that collapsed hovered applets to zero size. The
tree ships 100+ when-gated freeze Bindings, all RestoreNone; the Qt
semantics behind the rule are pinned by
tests/contracts/tst_bindingrestorecontracts.qml. An explicit non-RestoreNone
mode is allowed - the rule bans relying on the silent default, not making a
deliberate choice.

This is a plain source scan, not a staged install: the rules must hold for
every shipped QML file whether or not a build exists.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

from latte_harness.paths import RepoPaths

TOOL = "qml-effect-rules"

# The shipped QML trees the rules cover, scanned recursively for *.qml.
_SHIPPED_DIRS = ("containment", "plasmoid", "shell", "declarativeimports", "indicators")

# Rule 1. The bash used two greps: an ``autoPaddingEnabled[[:space:]]*:``
# include, then a ``grep -v`` that drops the one legal assignment,
# ``autoPaddingEnabled: false`` followed by whitespace, ``;``, ``/`` (a
# comment) or end of line. A line is a violation when it matches the
# assignment shape but NOT the legal-false shape. ``[[:space:]]`` is the
# POSIX class ([ \t\n\r\f\v]); on a single scanned line only space/tab
# actually occur, but the full class is kept for byte-faithfulness.
_SPACE = r"[ \t\n\r\f\v]"
_AUTOPAD_ASSIGN = re.compile(rf"autoPaddingEnabled{_SPACE}*:")
_AUTOPAD_FALSE = re.compile(rf"autoPaddingEnabled{_SPACE}*:{_SPACE}*false({_SPACE}|[;/]|$)")

# Rule 2. The Binding element opener: ``Binding {`` or ``Binding on prop {``,
# with the whitespace class [ \t\n] so a declaration split across lines still
# matches (the bash awk used the same class). when/restoreMode are matched
# with [ \t] only, exactly as the awk did.
_BINDING_OPEN = re.compile(r"Binding([ \t\n]+on[ \t]+[A-Za-z_.]+)?[ \t\n]*\{")
_WHEN_GATE = re.compile(r"when[ \t]*:")
_RESTORE_MODE = re.compile(r"restoreMode[ \t]*:")


def _shipped_qml_files(root: Path) -> list[Path]:
    """Every shipped ``*.qml`` file, dirs in declared order, sorted within.

    The bash used ``grep -r`` / ``find`` whose traversal order is
    filesystem-dependent; sorting each directory makes the violation
    output deterministic without changing the verdict or any line's format.
    """
    files: list[Path] = []
    for name in _SHIPPED_DIRS:
        files.extend(sorted((root / name).rglob("*.qml")))
    return files


def autopadding_violations(text: str, path: str) -> list[str]:
    """Rule 1 over one file's text; grep-n-shaped ``path:lineno:line`` hits."""
    hits: list[str] = []
    for lineno, line in enumerate(text.splitlines(), start=1):
        if _AUTOPAD_ASSIGN.search(line) and not _AUTOPAD_FALSE.search(line):
            hits.append(f"{path}:{lineno}:{line}")
    return hits


def binding_violations(text: str, filename: str) -> list[str]:
    """Rule 2 over one file's text; ``filename:lineno`` per offending Binding.

    A faithful port of the bash awk brace-matcher: reconstruct the source
    (records split on ``\\n`` only, each re-terminated with ``\\n`` so the
    text always ends in a newline, matching awk's RS handling), find each
    Binding opener, brace-match its block, and flag a block that is
    when-gated but declares no restoreMode. After a block is handled the
    scan resumes past its closing brace, so a Binding nested inside another
    Binding's block is not independently checked - the exact awk behavior.
    """
    records = text.split("\n")
    if records and records[-1] == "":
        records.pop()  # awk's RS="\n" produces no trailing empty record
    src = "".join(record + "\n" for record in records)

    hits: list[str] = []
    n = len(src)
    pos = 0
    while True:
        opener = _BINDING_OPEN.search(src, pos)
        if opener is None:
            break
        start = opener.start()
        brace = opener.end() - 1  # index of the '{'
        depth = 0
        j = brace
        while j < n:
            char = src[j]
            if char == "{":
                depth += 1
            elif char == "}":
                depth -= 1
                if depth == 0:
                    j += 1
                    break
            j += 1
        block = src[brace:j]
        if _WHEN_GATE.search(block) and not _RESTORE_MODE.search(block):
            lineno = src.count("\n", 0, start) + 1
            hits.append(f"{filename}:{lineno}")
        pos = j
    return hits


def main() -> None:
    root = RepoPaths.discover().root
    files = _shipped_qml_files(root)
    failed = False

    autopad: list[str] = []
    bindings: list[str] = []
    for path in files:
        text = path.read_text(encoding="utf-8")
        autopad.extend(autopadding_violations(text, str(path)))
        bindings.extend(binding_violations(text, str(path)))

    if autopad:
        print(
            "FAIL: autoPaddingEnabled must only ever be assigned the literal "
            "'false' in shipped QML:",
            file=sys.stderr,
        )
        print("\n".join(autopad), file=sys.stderr)
        failed = True

    if bindings:
        print(
            "FAIL: when-gated Binding elements without an explicit restoreMode (Qt6 default",
            file=sys.stderr,
        )
        print(
            "RestoreBindingOrValue resets the frozen value; declare restoreMode, usually",
            file=sys.stderr,
        )
        print(
            "Binding.RestoreNone - see tests/contracts/tst_bindingrestorecontracts.qml):",
            file=sys.stderr,
        )
        print("\n".join(bindings), file=sys.stderr)
        failed = True

    if failed:
        raise SystemExit(1)

    print(
        f"{TOOL}: OK (autoPaddingEnabled only ever disabled; "
        "every when-gated Binding declares restoreMode)"
    )


if __name__ == "__main__":
    main()
