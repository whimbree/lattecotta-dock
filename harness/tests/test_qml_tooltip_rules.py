# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-License-Identifier: GPL-2.0-or-later
"""Negative controls for the tooltip-rule scanner, plus a current-tree pass.

An attached popup ToolTip on a guarded overlay click target must be
detected; comment mentions and the bare Qt.ToolTip window flag must not
trip; a missing guarded file must fail loudly; and the real tree must pass.
"""

import pytest

from latte_harness import qml_tooltip_rules as tooltip


@pytest.mark.parametrize(
    "line",
    [
        "QQC2.ToolTip.text: i18n('hi')",
        "Controls.ToolTip.visible: hovered",
        "PlasmaComponents.ToolTip.text: label",
        "PC3.ToolTip.delay: 0",
        "QtQuick.Controls.ToolTip.text: label",
    ],
)
def test_flags_attached_tooltip(line: str) -> None:
    assert tooltip.tooltip_violations(line + "\n") == [f"1:{line}"]


def test_ignores_comment_mentioning_tooltip() -> None:
    # The file's own "don't re-add a QQC2.ToolTip here" prose must not trip.
    text = "    // don't re-add a QQC2.ToolTip.text: here\n"
    assert tooltip.tooltip_violations(text) == []


def test_ignores_bare_qt_tooltip_window_flag() -> None:
    # Qt.ToolTip (a window-flag, not an attached ToolTip.<prop>) stays allowed.
    assert tooltip.tooltip_violations("flags: Qt.ToolTip\n") == []


def test_reports_correct_line_number() -> None:
    text = "Button {\n    text: 'x'\n    QQC2.ToolTip.text: 'y'\n}\n"
    assert tooltip.tooltip_violations(text) == ["3:    QQC2.ToolTip.text: 'y'"]


def test_missing_guarded_file_fails(
    monkeypatch: pytest.MonkeyPatch, capsys: pytest.CaptureFixture[str]
) -> None:
    monkeypatch.setattr(tooltip, "_GUARDED", ("no/such/overlay/Control.qml",))
    with pytest.raises(SystemExit) as excinfo:
        tooltip.main()
    assert excinfo.value.code == 1
    assert "guarded file missing" in capsys.readouterr().err


def test_current_tree_passes(capsys: pytest.CaptureFixture[str]) -> None:
    tooltip.main()  # raises SystemExit(1) if a guarded control regrows a tooltip
    assert capsys.readouterr().out.startswith("qml-tooltip-rules: OK")
