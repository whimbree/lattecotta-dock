# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Source-scan rule test for the Wayland popup-tooltip-surface defect family.

The typed port of scripts/qml-tooltip-rules.sh (BP-1e); the plan of record
is docs/tracking/bash-python-migration-plan.md. The rule and its scope are
preserved verbatim from the bash version (latte-plasma6-defect-families:
"a per-control QQC2.ToolTip maps its OWN surface at the cursor").

Rule: the edit-mode CANVAS OVERLAY click-target controls must not carry an
attached QQC2/Controls.ToolTip. These controls are drawn OVER the dock by
CanvasConfigView with a click-through input mask, so a tooltip popup maps a
separate Wayland surface AT THE CURSOR, lands on top of the very control it
describes, and swallows the press - the "Rearrange..." toggle went
unclickable whenever edit-mode space was tight (caught live on a top panel,
2026-07-17; docs/tracking/panel-issues-plan.md issue 3). The same family also
produced the ~20Hz edit-handle flicker. The fix, committed in
ConfigOverlay.qml (the handle buttons) and Button.qml (the header buttons),
renders the hint IN-WINDOW as a plain, pointer-transparent Rectangle
instead. This rule keeps that popup tooltip from creeping back in.

SCOPE is deliberately narrow: ordinary Latte config DIALOG windows use
QQC2.ToolTip legitimately (a windowed dialog is not a layer-shell overlay
with a click-through mask), so this bans the pattern only in the named
edit-mode overlay CLICK-TARGET files, not tree-wide. The max-length Ruler
(canvas/maxlength/Ruler.qml) carries the same pattern on its drag handle and
is an owed sibling migration tracked in docs/tracking/panel-issues-plan.md - it
is intentionally not yet listed here, and gets added the day it is migrated.

Plain source scan, no staged install: the rule holds for the shipped QML
whether or not a build exists.
"""

from __future__ import annotations

import re
import sys

from latte_harness.paths import RepoPaths

TOOL = "qml-tooltip-rules"

# Edit-mode canvas overlay controls that are CLICK TARGETS and therefore must
# never re-grow a popup tooltip surface. Both already carry the in-code
# "don't re-add a QQC2.ToolTip here" contract comment.
_GUARDED = (
    "shell/package/contents/configuration/canvas/controls/Button.qml",
    "containment/package/contents/ui/editmode/ConfigOverlay.qml",
)

# Attached-property assignments only: ``QQC2.ToolTip.text:`` / ``.visible:``
# etc. (a namespace, ``.ToolTip.``, an identifier, then a colon). A bare
# ``Qt.ToolTip`` window flag (used by the in-window Latte::Dialog handle) is a
# different thing and stays allowed. Mirrors the bash ``grep -nE`` exactly.
_ATTACHED_TOOLTIP = re.compile(
    r"(QQC2|Controls|PlasmaComponents|PC3|QtQuick\.Controls)\.ToolTip\.[A-Za-z]+[ \t\n\r\f\v]*:"
)

# Comment lines are skipped so the file's own explanatory prose about the
# banned pattern does not trip the rule. The bash filtered the grep -n output
# (``lineno:content``) with ``grep -vE '^[0-9]+:[[:space:]]*//'``; this matches
# the same composed ``lineno:content`` string, anchored at the start.
_COMMENT_LINE = re.compile(r"[0-9]+:[ \t\n\r\f\v]*//")


def tooltip_violations(text: str) -> list[str]:
    """Non-comment attached-ToolTip hits in one file; ``lineno:line`` each."""
    hits: list[str] = []
    for lineno, line in enumerate(text.splitlines(), start=1):
        if _ATTACHED_TOOLTIP.search(line):
            entry = f"{lineno}:{line}"
            if not _COMMENT_LINE.match(entry):
                hits.append(entry)
    return hits


def main() -> None:
    root = RepoPaths.discover().root
    failed = False

    for rel in _GUARDED:
        target = root / rel
        if not target.is_file():
            print(
                "FAIL: guarded file missing (moved or renamed without updating "
                f"this rule): {target}",
                file=sys.stderr,
            )
            failed = True
            continue

        violations = tooltip_violations(target.read_text(encoding="utf-8"))
        if violations:
            print(
                "FAIL: attached ToolTip on an edit-mode overlay click target (Wayland popup",
                file=sys.stderr,
            )
            print(
                "surface eats the press - render the hint in-window instead, see the file's",
                file=sys.stderr,
            )
            print(
                "'don't re-add a QQC2.ToolTip here' comment and "
                "docs/tracking/panel-issues-plan.md #3):",
                file=sys.stderr,
            )
            print(f"{target}:", file=sys.stderr)
            print("\n".join(violations), file=sys.stderr)
            failed = True

    if failed:
        raise SystemExit(1)

    print(f"{TOOL}: OK (no attached popup ToolTip on the edit-mode overlay click targets)")


if __name__ == "__main__":
    main()
