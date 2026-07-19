#!/usr/bin/env bash
# Source-scan contract test for the previews pipeline mechanisms landed
# 2026-07-15 (the seven-layer hover-lag excavation, c6eeeb20..45c15433 plus
# the rebuild-cost pass). Every rule below encodes a trap that was hit LIVE
# and fixed with measurements in hand; each is a line-level invariant a
# future edit can silently reorder away, so they are pinned as greps the
# same way qml-effect-rules.sh pins the effect contracts. When one of these
# fires, read the cited commit body before "fixing" the gate.
set -euo pipefail

repo="$(cd "$(dirname "$0")/.." && pwd)"

tasksmain="$repo/plasmoid/package/contents/ui/main.qml"
taskitem="$repo/plasmoid/package/contents/ui/task/TaskItem.qml"
delegate="$repo/plasmoid/package/contents/ui/previews/ToolTipDelegate2.qml"
pwthumb="$repo/plasmoid/package/contents/ui/previews/PipeWireThumbnail.qml"

fail=0
bad() { echo "preview-contract-rules VIOLATION: $1"; echo "  -> $2"; fail=1; }

# Rules 1-4 MIGRATED (EX-01, docs/tracking/QML_EXTRACTION_PLAN.md): their behavioral
# halves (burst window semantics, Defer carrying the countdown cancel,
# settle never re-entering the burst check, interval < threshold) live in
# tests/units/previewswitchenginetest.cpp against the engine with an
# injected clock. What remains greppable here is the DELEGATION SHAPE: the
# QML shell must consult the engine and apply its verdicts. Only EX-01
# commits may edit these rules (the plan's migration contract).

# --- Rule 1 (4b533b8d): the burst check runs BEFORE preparePreviewWindow,
# and the check IS the engine's. Re-binding the delegate is what schedules
# the expensive rebuild; a check placed after it defers nothing.
defer_line=$(grep -n "windowsPreviewDlg.shouldDeferSwitch(taskItem)" "$taskitem" | head -1 | cut -d: -f1 || true)
prepare_line=$(grep -n "taskItem.preparePreviewWindow(false)" "$taskitem" | head -1 | cut -d: -f1 || true)
if [[ -z "$defer_line" || -z "$prepare_line" || "$defer_line" -ge "$prepare_line" ]]; then
    bad "shouldDeferSwitch() must be consulted before preparePreviewWindow() in TaskItem.showPreviewWindow" \
        "4b533b8d: the delegate re-bind schedules the 100-400ms rebuild; the burst gate must precede it"
fi
if ! sed -n '/function shouldDeferSwitch/,/^        }$/p' "$tasksmain" | grep -q "previewSwitch.shouldDeferSwitch"; then
    bad "windowsPreviewDlg.shouldDeferSwitch must delegate to previewSwitch.shouldDeferSwitch" \
        "EX-01: the cadence decision lives in PreviewSwitchEngine; a QML re-implementation is a second copy"
fi

# --- Rule 2 (54ed1974): the shell applies the engine's Defer by cancelling
# the hide countdown. The engine's SwitchResult carries the cancel as
# contract (tested in C++); this pins the QML apply site.
if ! sed -n '/function shouldDeferSwitch/,/^        }$/p' "$tasksmain" | grep -q "hidePreviewWinTimer.stop()"; then
    bad "shouldDeferSwitch()'s defer branch must stop hidePreviewWinTimer" \
        "54ed1974: without it a scrub defers every adoption while the last exit's countdown hides the dialog"
fi

# --- Rule 3 (4b533b8d): the settle timer adopts DIRECTLY via the engine
# (settlePending + prepare + show). Re-entering showPreviewWindow() counts
# the re-entry as a fresh switch request and re-defers forever.
settle_block=$(sed -n '/id: previewSwitchSettleTimer/,/^    }$/p' "$tasksmain")
if ! grep -q "previewSwitch.settlePending" <<<"$settle_block"; then
    bad "previewSwitchSettleTimer must resolve its adoption through previewSwitch.settlePending" \
        "EX-01: the settle decision (still-hovered, dialog-visible, pending consumption) lives in the engine"
fi
if ! grep -q "preparePreviewWindow" <<<"$settle_block" || ! grep -q "windowsPreviewDlg.show" <<<"$settle_block"; then
    bad "previewSwitchSettleTimer must adopt directly via preparePreviewWindow + show" \
        "4b533b8d: the settle path must never re-enter the burst check"
fi
if grep -q "showPreviewWindow()" <<<"$settle_block"; then
    bad "previewSwitchSettleTimer must NOT call showPreviewWindow()" \
        "4b533b8d: the re-entry counts itself as a fresh request and re-defers forever"
fi

# --- Rule 4 (4b533b8d): the Timer intervals come FROM the engine, never
# QML literals, so the running values cannot drift from the tested ones.
# The numeric relation (settle < burst threshold) is a static_assert in
# previewswitchengine.h plus a named test slot.
if ! grep -A2 "id: previewSwitchSettleTimer" "$tasksmain" | grep -q "interval: previewSwitch.settleInterval"; then
    bad "previewSwitchSettleTimer.interval must read previewSwitch.settleInterval" \
        "EX-01: a QML literal interval can drift from the tested threshold relation"
fi
if ! grep -A2 "id: hidePreviewWinTimer" "$tasksmain" | grep -q "interval: previewSwitch.hideCountdown"; then
    bad "hidePreviewWinTimer.interval must read previewSwitch.hideCountdown" \
        "EX-01: a QML literal interval can drift from the tested threshold relation"
fi

# --- Rule 5 (235753b8): every rootIndex assignment bumps the refresh token,
# and the fresh path assigns rootIndex AFTER the isGroup binding.
# DelegateModel silently resets its root when its model swaps (isGroup flips
# it between 1 and tasksModel) and an equal-value reassignment emits no
# change signal: a revived group showed a single window, a fresh delegate
# briefly rendered the top-level TASKS as instances.
while IFS=: read -r line _; do
    window=$(sed -n "$((line)),$((line+2))p" "$taskitem")
    if ! grep -q "rootRefreshToken++" <<<"$window"; then
        bad "toolTipDelegate.rootIndex assignment at TaskItem.qml:$line lacks a rootRefreshToken++ bump within 2 lines" \
            "235753b8: equal-value rootIndex reassignment emits no change; the token forces re-application"
    fi
done < <(grep -n "toolTipDelegate.rootIndex =" "$taskitem")
isgroup_line=$(grep -n "toolTipDelegate.isGroup = Qt.binding" "$taskitem" | head -1 | cut -d: -f1 || true)
last_rootindex_line=$(grep -n "toolTipDelegate.rootIndex =" "$taskitem" | tail -1 | cut -d: -f1 || true)
if [[ -z "$isgroup_line" || -z "$last_rootindex_line" || "$last_rootindex_line" -le "$isgroup_line" ]]; then
    bad "the fresh-delegate path must assign rootIndex AFTER the isGroup binding" \
        "235753b8: rootIndex against a default-false isGroup builds one transient instance with winId 0"
fi

# --- Rule 6 (d56a26aa): the previews host enforces its size imperatively.
# The base dialog stamps mainItem's size from the window on resize/expose;
# a plain width binding goes dormant after a late echo of the old size and
# the window never shrinks (spotify content stuck in vscode's 1096 window).
host_block=$(sed -n '/id: previewsHost/,/^        }$/p' "$tasksmain")
for needle in "onWidthChanged" "onHeightChanged" "width = contentW" "height = contentH"; do
    if ! grep -q "$needle" <<<"$host_block"; then
        bad "previewsHost must enforce size imperatively (missing: $needle)" \
            "d56a26aa: declarative bindings go dormant after the base dialog's stale size echo"
    fi
done

# --- Rule 7 (207a8084): preview instances incubate asynchronously.
# Synchronous instance construction is the 100-400ms GUI stall.
if ! sed -n '/id: instanceLoader/,/}/p' "$delegate" | grep -q "asynchronous: true"; then
    bad "the ToolTipInstance loader must stay asynchronous" \
        "207a8084: synchronous construction blocks input and the parabolic zoom for 100-400ms per adoption"
fi

# --- Rule 8 (f1edd103): dying tasks evict their cached delegate.
# Item-typed auto-nulling cannot be relied on inside a var array; the
# eviction is an explicit contract from TaskItem's destructor.
if ! sed -n '/Component.onDestruction/,/^    }$/p' "$taskitem" | grep -q "dropCachedDelegateFor"; then
    bad "TaskItem.Component.onDestruction must call windowsPreviewDlg.dropCachedDelegateFor" \
        "f1edd103: a destroyed task's parked delegate is unreachable and its bindings go stale"
fi

# --- Rule 9 (4f96acb8 / f1edd103): thumbnail streams stop when the dialog
# hides. Parked delegates keep streams warm ON PURPOSE while the dialog is
# visible; the visibility gate is what bounds the cost.
if ! grep -q "windowsPreviewDlg.visible ?" "$pwthumb"; then
    bad "PipeWireThumbnail's uuid must stay gated on windowsPreviewDlg.visible" \
        "4f96acb8/f1edd103: without the gate every cached delegate keeps kwin screencasts running after the previews close"
fi

# --- Rule 10 (c6eeeb20): the unmap/remap workaround must not return.
# Task switches re-anchor the MAPPED window; the old deferred-remap tore
# the surface down per crossing (wire-logged 231ms hole plus a repeated
# slide-in per icon).
if grep -qE "remapPendingTask|previewRemapTimer" "$tasksmain"; then
    bad "the preview unmap/remap machinery must not be reintroduced" \
        "c6eeeb20: its premise (a mapped popup cannot re-anchor) died with the dialog's live wayland re-anchoring"
fi

if [[ "$fail" == 1 ]]; then
    echo "preview-contract-rules: FAILED"
    exit 1
fi

echo "preview-contract-rules: OK"
