/*
    SPDX-FileCopyrightText: 2018 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.7

import org.kde.plasma.plasmoid 2.0
import org.kde.plasma.components 3.0 as PlasmaComponents
import org.kde.plasma.core 2.0 as PlasmaCore

import org.kde.latte.core 0.2 as LatteCore

MouseArea{
    id: rulerMouseArea
    hoverEnabled: true
    cursorShape: root.isHorizontal ? Qt.SizeHorCursor : Qt.SizeVerCursor

    onVisibleChanged: {
        if (!visible) {
            tooltip.visible = false;
        }
    }

    //! Qt5 fired past angle = angleDelta.y/8 > 12, i.e. angleDelta > 96
    //! (EX-15: the wheel math lives in LatteCore.WheelStepper)
    LatteCore.WheelStepper {
        id: lengthWheelStepper
        axisPick: LatteCore.WheelStepper.VerticalOnly
        fireThreshold: 96
    }

    onWheel: (wheel) => {
        const direction = lengthWheelStepper.add(wheel.angleDelta, false);

        if (direction !== 0) {
            rulerMouseArea.updateMaxLength(6 * direction);
        }
    }

    //! the wheel step delegates to the shared clamp authority (EX-18,
    //! app/settings/lengthoffsetclamp.h): the configuration tab's Maximum
    //! slider drives the same core, so the two can no longer drift apart.
    //! Same-value writes are skipped: QQmlPropertyMap emits valueChanged
    //! on every insert, and the Qt5 body only wrote the keys its taken
    //! branches touched.
    function updateMaxLength(step: int) {
        var clamped = lengthClamp.clampMaxLengthByStep(plasmoid.configuration.maxLength,
                                                       plasmoid.configuration.minLength,
                                                       plasmoid.configuration.offset,
                                                       step,
                                                       plasmoid.configuration.alignment);

        if (plasmoid.configuration.minLength !== clamped.minLength) {
            plasmoid.configuration.minLength = clamped.minLength;
        }

        plasmoid.configuration.maxLength = clamped.maxLength;

        if (plasmoid.configuration.offset !== clamped.offset) {
            plasmoid.configuration.offset = clamped.offset;
        }
    }
}
