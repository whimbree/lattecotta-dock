# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Negative controls for the ten preview-pipeline contract rules.

A baseline that passes every rule is mutated once per rule; each mutation
must produce that rule's violation and no other. The real shipped tree must
also render clean (the BP equivalence contract).
"""

import pytest

from latte_harness import preview_contract_rules as pcr
from latte_harness.paths import RepoPaths

# A minimal main.qml that passes all main.qml-scanned rules. Indentation is
# load-bearing: the sed end addresses match 8- and 4-space ``}`` lines.
GOOD_MAIN = """\
        function shouldDeferSwitch(taskItem) {
            hidePreviewWinTimer.stop();
            return previewSwitch.shouldDeferSwitch(taskItem);
        }
    Timer {
        id: previewSwitchSettleTimer
        interval: previewSwitch.settleInterval
        onTriggered: {
            if (previewSwitch.settlePending) {
                taskItem.preparePreviewWindow(false);
                windowsPreviewDlg.show();
            }
        }
    }
    Timer {
        id: hidePreviewWinTimer
        interval: previewSwitch.hideCountdown
    }
        PlasmaCore.Dialog {
            id: previewsHost
            onWidthChanged: mainItem.width = contentW
            onHeightChanged: mainItem.height = contentH
        }
"""

GOOD_TASKITEM = """\
    function showPreviewWindow() {
        if (windowsPreviewDlg.shouldDeferSwitch(taskItem)) {
            return;
        }
        taskItem.preparePreviewWindow(false);
    }
    Component.onDestruction: {
        windowsPreviewDlg.dropCachedDelegateFor(taskItem);
    }
    function refresh() {
        toolTipDelegate.isGroup = Qt.binding(function() { return isGroup; });
        toolTipDelegate.rootIndex = tasksModel.makeModelIndex(itemIndex);
        rootRefreshToken++;
    }
"""

GOOD_DELEGATE = """\
    Loader {
        id: instanceLoader
        asynchronous: true
        sourceComponent: toolTipComponent
    }
"""

GOOD_PWTHUMB = '    property string uuid: windowsPreviewDlg.visible ? sourceUuid : ""\n'


def _whats(
    *,
    main: str = GOOD_MAIN,
    taskitem: str = GOOD_TASKITEM,
    delegate: str = GOOD_DELEGATE,
    pwthumb: str = GOOD_PWTHUMB,
) -> list[str]:
    return [what for what, _why in pcr.check(main, taskitem, delegate, pwthumb)]


def test_baseline_passes_every_rule() -> None:
    assert _whats() == []


def test_rule1_defer_must_precede_prepare() -> None:
    taskitem = GOOD_TASKITEM.replace(
        "        if (windowsPreviewDlg.shouldDeferSwitch(taskItem)) {\n"
        "            return;\n"
        "        }\n"
        "        taskItem.preparePreviewWindow(false);\n",
        "        taskItem.preparePreviewWindow(false);\n"
        "        if (windowsPreviewDlg.shouldDeferSwitch(taskItem)) {\n"
        "            return;\n"
        "        }\n",
    )
    assert taskitem != GOOD_TASKITEM  # the swap actually took
    assert any(
        "must be consulted before preparePreviewWindow()" in w for w in _whats(taskitem=taskitem)
    )


def test_rule1_must_delegate_to_engine() -> None:
    main = GOOD_MAIN.replace("previewSwitch.shouldDeferSwitch", "engine.decide")
    assert any("must delegate to previewSwitch.shouldDeferSwitch" in w for w in _whats(main=main))


def test_rule2_defer_branch_must_stop_hide_timer() -> None:
    main = GOOD_MAIN.replace("hidePreviewWinTimer.stop();", "// removed")
    assert any("must stop hidePreviewWinTimer" in w for w in _whats(main=main))


def test_rule3_settle_must_use_settlepending() -> None:
    main = GOOD_MAIN.replace("previewSwitch.settlePending", "engine.pending")
    assert any("through previewSwitch.settlePending" in w for w in _whats(main=main))


def test_rule3_settle_must_adopt_directly() -> None:
    main = GOOD_MAIN.replace("windowsPreviewDlg.show();", "// removed")
    assert any("adopt directly via preparePreviewWindow + show" in w for w in _whats(main=main))


def test_rule3_settle_must_not_reenter_showpreviewwindow() -> None:
    main = GOOD_MAIN.replace("taskItem.preparePreviewWindow(false);", "showPreviewWindow();")
    assert any("must NOT call showPreviewWindow()" in w for w in _whats(main=main))


def test_rule4_settle_interval_from_engine() -> None:
    main = GOOD_MAIN.replace("interval: previewSwitch.settleInterval", "interval: 90")
    assert any("must read previewSwitch.settleInterval" in w for w in _whats(main=main))


def test_rule4_hide_interval_from_engine() -> None:
    main = GOOD_MAIN.replace("interval: previewSwitch.hideCountdown", "interval: 300")
    assert any("must read previewSwitch.hideCountdown" in w for w in _whats(main=main))


def test_rule5_rootindex_needs_token_bump() -> None:
    taskitem = GOOD_TASKITEM.replace("        rootRefreshToken++;\n", "")
    assert any("lacks a rootRefreshToken++ bump" in w for w in _whats(taskitem=taskitem))


def test_rule5_rootindex_after_isgroup() -> None:
    taskitem = GOOD_TASKITEM.replace(
        "        toolTipDelegate.isGroup = Qt.binding(function() { return isGroup; });\n", ""
    )
    assert any(
        "must assign rootIndex AFTER the isGroup binding" in w for w in _whats(taskitem=taskitem)
    )


def test_rule6_previewshost_imperative_size() -> None:
    main = GOOD_MAIN.replace("            onWidthChanged: mainItem.width = contentW\n", "")
    whats = _whats(main=main)
    assert any("missing: onWidthChanged" in w for w in whats)
    assert any("missing: width = contentW" in w for w in whats)


def test_rule7_loader_must_stay_async() -> None:
    delegate = GOOD_DELEGATE.replace("asynchronous: true", "asynchronous: false")
    assert any("must stay asynchronous" in w for w in _whats(delegate=delegate))


def test_rule8_destruction_evicts_cached_delegate() -> None:
    taskitem = GOOD_TASKITEM.replace("dropCachedDelegateFor", "somethingElse")
    assert any(
        "must call windowsPreviewDlg.dropCachedDelegateFor" in w for w in _whats(taskitem=taskitem)
    )


def test_rule9_thumbnail_gated_on_visible() -> None:
    pwthumb = GOOD_PWTHUMB.replace("windowsPreviewDlg.visible ?", "true ?")
    assert any("must stay gated on windowsPreviewDlg.visible" in w for w in _whats(pwthumb=pwthumb))


def test_rule10_no_remap_machinery() -> None:
    main = GOOD_MAIN + "\n    property int remapPendingTask: -1\n"
    assert any("unmap/remap machinery must not be reintroduced" in w for w in _whats(main=main))


def test_current_tree_passes(capsys: pytest.CaptureFixture[str]) -> None:
    root = RepoPaths.discover().root
    failures = pcr.check(
        (root / "plasmoid/package/contents/ui/main.qml").read_text(encoding="utf-8"),
        (root / "plasmoid/package/contents/ui/task/TaskItem.qml").read_text(encoding="utf-8"),
        (root / "plasmoid/package/contents/ui/previews/ToolTipDelegate2.qml").read_text(
            encoding="utf-8"
        ),
        (root / "plasmoid/package/contents/ui/previews/PipeWireThumbnail.qml").read_text(
            encoding="utf-8"
        ),
    )
    assert failures == []
