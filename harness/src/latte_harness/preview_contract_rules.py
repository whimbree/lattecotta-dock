# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Source-scan contract test for the previews pipeline mechanisms.

The typed port of scripts/preview-contract-rules.sh (BP-1e); the plan of
record is docs/tracking/bash-python-migration-plan.md. The ten rules pin the
invariants the 2026-07-15 seven-layer hover-lag excavation
(c6eeeb20..45c15433 plus the rebuild-cost pass) earned. Every rule encodes a
trap that was hit LIVE and fixed with measurements in hand; each is a
line-level invariant a future edit can silently reorder away. When one of
these fires, read the cited commit body before "fixing" the gate.

Rules 1-4 MIGRATED (EX-01, docs/tracking/QML_EXTRACTION_PLAN.md): their
behavioral halves (burst window semantics, Defer carrying the countdown
cancel, settle never re-entering the burst check, interval < threshold) live
in tests/units/previewswitchenginetest.cpp against the engine with an
injected clock. What remains greppable here is the DELEGATION SHAPE: the QML
shell must consult the engine and apply its verdicts. Only EX-01 commits may
edit these rules (the plan's migration contract).

The grep/sed matching semantics carry over exactly. The bash used plain grep
(BRE), where ``.`` is a wildcard and ``()``/``+``/``?`` are literal; those
patterns keep the ``.`` wildcard and escape the literal metacharacters here.
Only Rule 10 used ``grep -E`` (alternation), preserved as a plain Python
alternation.
"""

from __future__ import annotations

import re

from latte_harness.paths import RepoPaths

TOOL = "preview-contract-rules"

# The four shipped QML files the rules scan.
_TASKSMAIN = "plasmoid/package/contents/ui/main.qml"
_TASKITEM = "plasmoid/package/contents/ui/task/TaskItem.qml"
_DELEGATE = "plasmoid/package/contents/ui/previews/ToolTipDelegate2.qml"
_PWTHUMB = "plasmoid/package/contents/ui/previews/PipeWireThumbnail.qml"

# End-of-block sed addresses: a line that is exactly N spaces then ``}``. The
# space counts are byte-faithful to the bash sed patterns (8 and 4 spaces).
_CLOSE_8 = re.compile(r"^        \}$")
_CLOSE_4 = re.compile(r"^    \}$")
_CLOSE_ANY = re.compile(r"\}")


def _lines(text: str) -> list[str]:
    """Line-split like grep/sed: split on ``\\n`` only, no trailing empty.

    A file ending in a newline yields no extra empty final line, matching how
    grep -n numbers lines and how sed counts them.
    """
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    return lines


def _first_lineno(pattern: re.Pattern[str], text: str) -> int | None:
    """``grep -n PATTERN | head -1 | cut -d: -f1``: first 1-based hit, or None."""
    for lineno, line in enumerate(_lines(text), start=1):
        if pattern.search(line):
            return lineno
    return None


def _last_lineno(pattern: re.Pattern[str], text: str) -> int | None:
    """``grep -n PATTERN | tail -1 | cut -d: -f1``: last 1-based hit, or None."""
    found: int | None = None
    for lineno, line in enumerate(_lines(text), start=1):
        if pattern.search(line):
            found = lineno
    return found


def _all_linenos(pattern: re.Pattern[str], text: str) -> list[int]:
    """Every 1-based line number matching ``pattern`` (grep -n line numbers)."""
    return [lineno for lineno, line in enumerate(_lines(text), start=1) if pattern.search(line)]


def _contains(pattern: re.Pattern[str], block: str) -> bool:
    """``grep -q``: does any line of ``block`` match ``pattern``?"""
    return any(pattern.search(line) for line in _lines(block))


def _sed_range(text: str, start: re.Pattern[str], end: re.Pattern[str]) -> str:
    """``sed -n '/start/,/end/p'``: lines of every start..end range, inclusive.

    Matches GNU sed's regexp-range semantics: a range opens on a line matching
    ``start`` and the ``end`` address is tested from the FOLLOWING line
    onward, so a range never closes on its own opening line; an unclosed range
    runs to end of input; a fresh ``start`` after a close opens a new range.
    """
    kept: list[str] = []
    in_range = False
    for line in _lines(text):
        if not in_range:
            if start.search(line):
                in_range = True
                kept.append(line)
        else:
            kept.append(line)
            if end.search(line):
                in_range = False
    return "\n".join(kept)


def _line_span(text: str, first: int, last: int) -> str:
    """``sed -n 'FIRST,LASTp'``: the 1-based inclusive line span."""
    return "\n".join(_lines(text)[first - 1 : last])


def _grep_after(text: str, pattern: re.Pattern[str], after: int) -> str:
    """``grep -A AFTER PATTERN``: each match line plus ``after`` lines below it.

    Overlapping windows collapse (the ``--`` separators grep would print are
    irrelevant to the downstream ``grep -q`` and are omitted).
    """
    lines = _lines(text)
    keep: set[int] = set()
    for index, line in enumerate(lines):
        if pattern.search(line):
            keep.update(range(index, min(index + after + 1, len(lines))))
    return "\n".join(lines[index] for index in sorted(keep))


def check(tasksmain: str, taskitem: str, delegate: str, pwthumb: str) -> list[tuple[str, str]]:
    """Run all ten rules; return ``(what, why)`` for each violation, in order."""
    failures: list[tuple[str, str]] = []

    def bad(what: str, why: str) -> None:
        failures.append((what, why))

    # --- Rule 1 (4b533b8d): the burst check runs BEFORE preparePreviewWindow,
    # and the check IS the engine's. Re-binding the delegate is what schedules
    # the expensive rebuild; a check placed after it defers nothing.
    defer_line = _first_lineno(
        re.compile(r"windowsPreviewDlg.shouldDeferSwitch\(taskItem\)"), taskitem
    )
    prepare_line = _first_lineno(re.compile(r"taskItem.preparePreviewWindow\(false\)"), taskitem)
    if defer_line is None or prepare_line is None or defer_line >= prepare_line:
        bad(
            "shouldDeferSwitch() must be consulted before preparePreviewWindow() in "
            "TaskItem.showPreviewWindow",
            "4b533b8d: the delegate re-bind schedules the 100-400ms rebuild; the burst gate "
            "must precede it",
        )
    defer_block = _sed_range(tasksmain, re.compile(r"function shouldDeferSwitch"), _CLOSE_8)
    if not _contains(re.compile(r"previewSwitch.shouldDeferSwitch"), defer_block):
        bad(
            "windowsPreviewDlg.shouldDeferSwitch must delegate to previewSwitch.shouldDeferSwitch",
            "EX-01: the cadence decision lives in PreviewSwitchEngine; a QML re-implementation "
            "is a second copy",
        )

    # --- Rule 2 (54ed1974): the shell applies the engine's Defer by cancelling
    # the hide countdown. The engine's SwitchResult carries the cancel as
    # contract (tested in C++); this pins the QML apply site.
    if not _contains(re.compile(r"hidePreviewWinTimer.stop\(\)"), defer_block):
        bad(
            "shouldDeferSwitch()'s defer branch must stop hidePreviewWinTimer",
            "54ed1974: without it a scrub defers every adoption while the last exit's countdown "
            "hides the dialog",
        )

    # --- Rule 3 (4b533b8d): the settle timer adopts DIRECTLY via the engine
    # (settlePending + prepare + show). Re-entering showPreviewWindow() counts
    # the re-entry as a fresh switch request and re-defers forever.
    settle_block = _sed_range(tasksmain, re.compile(r"id: previewSwitchSettleTimer"), _CLOSE_4)
    if not _contains(re.compile(r"previewSwitch.settlePending"), settle_block):
        bad(
            "previewSwitchSettleTimer must resolve its adoption through "
            "previewSwitch.settlePending",
            "EX-01: the settle decision (still-hovered, dialog-visible, pending consumption) "
            "lives in the engine",
        )
    if not _contains(re.compile(r"preparePreviewWindow"), settle_block) or not _contains(
        re.compile(r"windowsPreviewDlg.show"), settle_block
    ):
        bad(
            "previewSwitchSettleTimer must adopt directly via preparePreviewWindow + show",
            "4b533b8d: the settle path must never re-enter the burst check",
        )
    if _contains(re.compile(r"showPreviewWindow\(\)"), settle_block):
        bad(
            "previewSwitchSettleTimer must NOT call showPreviewWindow()",
            "4b533b8d: the re-entry counts itself as a fresh request and re-defers forever",
        )

    # --- Rule 4 (4b533b8d): the Timer intervals come FROM the engine, never
    # QML literals, so the running values cannot drift from the tested ones.
    # The numeric relation (settle < burst threshold) is a static_assert in
    # previewswitchengine.h plus a named test slot.
    settle_after = _grep_after(tasksmain, re.compile(r"id: previewSwitchSettleTimer"), 2)
    if not _contains(re.compile(r"interval: previewSwitch.settleInterval"), settle_after):
        bad(
            "previewSwitchSettleTimer.interval must read previewSwitch.settleInterval",
            "EX-01: a QML literal interval can drift from the tested threshold relation",
        )
    hide_after = _grep_after(tasksmain, re.compile(r"id: hidePreviewWinTimer"), 2)
    if not _contains(re.compile(r"interval: previewSwitch.hideCountdown"), hide_after):
        bad(
            "hidePreviewWinTimer.interval must read previewSwitch.hideCountdown",
            "EX-01: a QML literal interval can drift from the tested threshold relation",
        )

    # --- Rule 5 (235753b8): every rootIndex assignment bumps the refresh token,
    # and the fresh path assigns rootIndex AFTER the isGroup binding.
    # DelegateModel silently resets its root when its model swaps (isGroup flips
    # it between 1 and tasksModel) and an equal-value reassignment emits no
    # change signal: a revived group showed a single window, a fresh delegate
    # briefly rendered the top-level TASKS as instances.
    rootindex_pat = re.compile(r"toolTipDelegate.rootIndex =")
    for lineno in _all_linenos(rootindex_pat, taskitem):
        window = _line_span(taskitem, lineno, lineno + 2)
        if not _contains(re.compile(r"rootRefreshToken\+\+"), window):
            bad(
                f"toolTipDelegate.rootIndex assignment at TaskItem.qml:{lineno} lacks a "
                "rootRefreshToken++ bump within 2 lines",
                "235753b8: equal-value rootIndex reassignment emits no change; the token forces "
                "re-application",
            )
    isgroup_line = _first_lineno(re.compile(r"toolTipDelegate.isGroup = Qt.binding"), taskitem)
    last_rootindex_line = _last_lineno(rootindex_pat, taskitem)
    if isgroup_line is None or last_rootindex_line is None or last_rootindex_line <= isgroup_line:
        bad(
            "the fresh-delegate path must assign rootIndex AFTER the isGroup binding",
            "235753b8: rootIndex against a default-false isGroup builds one transient instance "
            "with winId 0",
        )

    # --- Rule 6 (d56a26aa): the previews host enforces its size imperatively.
    # The base dialog stamps mainItem's size from the window on resize/expose;
    # a plain width binding goes dormant after a late echo of the old size and
    # the window never shrinks (spotify content stuck in vscode's 1096 window).
    host_block = _sed_range(tasksmain, re.compile(r"id: previewsHost"), _CLOSE_8)
    for needle in ("onWidthChanged", "onHeightChanged", "width = contentW", "height = contentH"):
        if needle not in host_block:
            bad(
                f"previewsHost must enforce size imperatively (missing: {needle})",
                "d56a26aa: declarative bindings go dormant after the base dialog's stale size echo",
            )

    # --- Rule 7 (207a8084): preview instances incubate asynchronously.
    # Synchronous instance construction is the 100-400ms GUI stall.
    loader_block = _sed_range(delegate, re.compile(r"id: instanceLoader"), _CLOSE_ANY)
    if "asynchronous: true" not in loader_block:
        bad(
            "the ToolTipInstance loader must stay asynchronous",
            "207a8084: synchronous construction blocks input and the parabolic zoom for "
            "100-400ms per adoption",
        )

    # --- Rule 8 (f1edd103): dying tasks evict their cached delegate.
    # Item-typed auto-nulling cannot be relied on inside a var array; the
    # eviction is an explicit contract from TaskItem's destructor.
    destruction_block = _sed_range(taskitem, re.compile(r"Component.onDestruction"), _CLOSE_4)
    if "dropCachedDelegateFor" not in destruction_block:
        bad(
            "TaskItem.Component.onDestruction must call windowsPreviewDlg.dropCachedDelegateFor",
            "f1edd103: a destroyed task's parked delegate is unreachable and its bindings go stale",
        )

    # --- Rule 9 (4f96acb8 / f1edd103): thumbnail streams stop when the dialog
    # hides. Parked delegates keep streams warm ON PURPOSE while the dialog is
    # visible; the visibility gate is what bounds the cost.
    if not _contains(re.compile(r"windowsPreviewDlg.visible \?"), pwthumb):
        bad(
            "PipeWireThumbnail's uuid must stay gated on windowsPreviewDlg.visible",
            "4f96acb8/f1edd103: without the gate every cached delegate keeps kwin screencasts "
            "running after the previews close",
        )

    # --- Rule 10 (c6eeeb20): the unmap/remap workaround must not return.
    # Task switches re-anchor the MAPPED window; the old deferred-remap tore
    # the surface down per crossing (wire-logged 231ms hole plus a repeated
    # slide-in per icon).
    if _contains(re.compile(r"remapPendingTask|previewRemapTimer"), tasksmain):
        bad(
            "the preview unmap/remap machinery must not be reintroduced",
            "c6eeeb20: its premise (a mapped popup cannot re-anchor) died with the dialog's live "
            "wayland re-anchoring",
        )

    return failures


def main() -> None:
    root = RepoPaths.discover().root
    failures = check(
        (root / _TASKSMAIN).read_text(encoding="utf-8"),
        (root / _TASKITEM).read_text(encoding="utf-8"),
        (root / _DELEGATE).read_text(encoding="utf-8"),
        (root / _PWTHUMB).read_text(encoding="utf-8"),
    )
    for what, why in failures:
        print(f"{TOOL} VIOLATION: {what}")
        print(f"  -> {why}")
    if failures:
        print(f"{TOOL}: FAILED")
        raise SystemExit(1)
    print(f"{TOOL}: OK")


if __name__ == "__main__":
    main()
