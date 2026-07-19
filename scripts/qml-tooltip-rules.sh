#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Source-scan rule test for the Wayland popup-tooltip-surface defect family
# (latte-plasma6-defect-families: "a per-control QQC2.ToolTip maps its OWN
# surface at the cursor").
#
# Rule: the edit-mode CANVAS OVERLAY click-target controls must not carry an
# attached QQC2/Controls.ToolTip. These controls are drawn OVER the dock by
# CanvasConfigView with a click-through input mask, so a tooltip popup maps a
# separate Wayland surface AT THE CURSOR, lands on top of the very control it
# describes, and swallows the press - the "Rearrange..." toggle went
# unclickable whenever edit-mode space was tight (caught live on a top panel,
# 2026-07-17; docs/tracking/panel-issues-plan.md issue 3). The same family also
# produced the ~20Hz edit-handle flicker. The fix, already committed in
# ConfigOverlay.qml (the handle buttons) and now in Button.qml (the header
# buttons), renders the hint IN-WINDOW as a plain, pointer-transparent
# Rectangle instead. This rule keeps that popup tooltip from creeping back
# into those controls.
#
# SCOPE is deliberately narrow: ordinary Latte config DIALOG windows use
# QQC2.ToolTip legitimately (a windowed dialog is not a layer-shell overlay
# with a click-through mask), so this bans the pattern only in the named
# edit-mode overlay CLICK-TARGET files, not tree-wide. The max-length Ruler
# (canvas/maxlength/Ruler.qml) carries the same pattern on its drag handle and
# is an owed sibling migration tracked in docs/tracking/panel-issues-plan.md - it is
# intentionally not yet listed here, and gets added the day it is migrated.
#
# Plain source scan, no staged install: the rule holds for the shipped QML
# whether or not a build exists.
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"

# Edit-mode canvas overlay controls that are CLICK TARGETS and therefore must
# never re-grow a popup tooltip surface. Both already carry the in-code
# "don't re-add a QQC2.ToolTip here" contract comment.
guarded=(
    "$repo/shell/package/contents/configuration/canvas/controls/Button.qml"
    "$repo/containment/package/contents/ui/editmode/ConfigOverlay.qml"
)

fail=0

for f in "${guarded[@]}"; do
    if [[ ! -f "$f" ]]; then
        echo "FAIL: guarded file missing (moved or renamed without updating this rule): $f" >&2
        fail=1
        continue
    fi

    # Attached-property assignments only: `QQC2.ToolTip.text:` / `.visible:`
    # etc. (namespace, `.ToolTip.`, an identifier, then a colon). Comment lines
    # are skipped so the file's own explanatory prose about the banned pattern
    # does not trip the rule. A bare `Qt.ToolTip` window flag (used by the
    # in-window Latte::Dialog handle) is a different thing and stays allowed.
    violations="$(grep -nE '(QQC2|Controls|PlasmaComponents|PC3|QtQuick\.Controls)\.ToolTip\.[A-Za-z]+[[:space:]]*:' "$f" \
        | grep -vE '^[0-9]+:[[:space:]]*//' || true)"
    if [[ -n "$violations" ]]; then
        echo "FAIL: attached ToolTip on an edit-mode overlay click target (Wayland popup" >&2
        echo "surface eats the press - render the hint in-window instead, see the file's" >&2
        echo "'don't re-add a QQC2.ToolTip here' comment and docs/tracking/panel-issues-plan.md #3):" >&2
        echo "$f:" >&2
        echo "$violations" >&2
        fail=1
    fi
done

if [[ "$fail" != 0 ]]; then
    exit 1
fi

echo "qml-tooltip-rules: OK (no attached popup ToolTip on the edit-mode overlay click targets)"
