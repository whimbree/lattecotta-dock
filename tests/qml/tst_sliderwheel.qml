/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Interaction pin for settings sliders: a wheel event over the shipped
//! Latte component changes its value by the declared step. This drives the
//! native Qt Quick Templates Slider path rather than duplicating wheel math.

import QtQuick
import QtTest

import org.kde.latte.components 1.0 as LatteComponents

Item {
    width: 320
    height: 80

    Item {
        id: focusSink
        focus: true
    }

    LatteComponents.Slider {
        id: slider
        anchors.centerIn: parent
        width: 240
        from: 16
        to: 512
        stepSize: 2
        value: 50
    }

    TestCase {
        name: "SettingsSliderWheel"
        when: windowShown

        function init() {
            slider.value = 50;
            focusSink.forceActiveFocus();
        }

        function test_nativeWheelUsesTheDeclaredStep() {
            verify(!slider.activeFocus);
            verify(!slider.wheelEnabled,
                   "an unfocused slider must leave page-wheel events alone");

            mouseWheel(slider, slider.width / 2, slider.height / 2,
                       0, 120, Qt.NoButton, Qt.NoModifier);
            compare(slider.value, 50,
                    "page scrolling over an unfocused bar must not mutate it");

            mouseClick(slider.handle,
                       slider.handle.width / 2,
                       slider.handle.height / 2,
                       Qt.LeftButton);
            verify(slider.activeFocus,
                   "clicking the slider must arm keyboard and wheel adjustment");
            verify(slider.wheelEnabled,
                   "the focused settings slider must accept wheel input");

            const valueBeforeWheel = slider.value;

            mouseWheel(slider, slider.width / 2, slider.height / 2,
                       0, 120, Qt.NoButton, Qt.NoModifier);
            compare(slider.value, valueBeforeWheel + slider.stepSize,
                    "one upward wheel notch must apply one declared step");

            mouseWheel(slider, slider.width / 2, slider.height / 2,
                       0, -120, Qt.NoButton, Qt.NoModifier);
            compare(slider.value, valueBeforeWheel,
                    "the opposite notch must restore the prior value");
        }
    }
}
