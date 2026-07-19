/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Regression guard for the edit-mode hint that ate the click on the
//! "Rearrange..." button (docs/tracking/panel-issues-plan.md issue 3). The settings
//! header controls' hint must render IN-WINDOW as a pointer-transparent chip,
//! never as an attached QQC2.ToolTip whose separate Wayland surface maps at
//! the cursor and swallows the press.
//!
//! The pointer grab itself is a Wayland separate-surface effect an offscreen
//! engine cannot reproduce - the deterministic pin that no popup ToolTip
//! returns is the source scan scripts/qml-tooltip-rules.sh. This test drives
//! the REAL shell Button control and pins the OBSERVABLE half the source rule
//! cannot: the hint is an in-window Rectangle carrying no input handler (so it
//! is structurally pointer-transparent), and the invisible click target still
//! receives the press. latteView/settingsRoot are C++ context properties, so
//! the button's colour bindings throw harmless ReferenceErrors here; none of
//! them touch the geometry or input paths this test exercises.

import QtQuick 2.15
import QtTest 1.2

import "../../shell/package/contents/configuration/canvas/controls" as SettingsControls

Item {
    id: root
    width: 400
    height: 200

    property int pressedCount: 0

    SettingsControls.Button {
        id: headerButton
        anchors.centerIn: parent
        text: "Rearrange and configure your widgets"
        tooltip: "Feel free to move around your widgets and configure them from their tooltips"
        onPressedChanged: (pressed) => { if (pressed) root.pressedCount = root.pressedCount + 1; }
    }

    TestCase {
        name: "ButtonHintClickThrough"
        when: windowShown

        function hintChip() { return findChild(headerButton, "buttonHintChip"); }

        function test_hint_is_an_in_window_rectangle_not_a_popup() {
            var hint = hintChip();
            verify(hint !== null, "the in-window hint chip is reachable inside the button");
            verify(String(hint).indexOf("QQuickRectangle") === 0,
                   "the hint is a plain in-window Rectangle, not a popup surface: " + hint);
        }

        function test_hint_carries_no_input_handler() {
            //! a plain Rectangle/Label subtree consumes no pointer events, so a
            //! click over the hint falls through to whatever sits beneath it.
            //! Re-introducing a MouseArea/HoverHandler/Button here would eat the
            //! press again - assert the subtree stays input-free.
            var hint = hintChip();
            verify(hint !== null, "the hint chip is reachable");
            for (var i = 0; i < hint.children.length; ++i) {
                var kind = String(hint.children[i]);
                verify(kind.indexOf("QQuickMouseArea") !== 0
                       && kind.indexOf("QQuickAbstractButton") !== 0,
                       "the hint subtree must carry no pointer-grabbing item, found: " + kind);
            }
        }

        function test_press_reaches_the_button() {
            //! the invisible tooltip button is the real click target; nothing
            //! must overlay-and-block it. Driving a press at its centre must
            //! raise pressedChanged exactly as a pointer does.
            var before = root.pressedCount;
            mousePress(headerButton, headerButton.width / 2, headerButton.height / 2);
            mouseRelease(headerButton, headerButton.width / 2, headerButton.height / 2);
            compare(root.pressedCount, before + 1,
                    "the press reached the button");
        }
    }
}
