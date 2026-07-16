/*
    SPDX-FileCopyrightText: 2022 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.7

Item {
    id: edgeSpacer
    width: length
    height: length

    readonly property bool isParabolicEdgeSpacer: true
    readonly property bool isHidden: true

    readonly property bool isAutoFillApplet: false
    readonly property bool isInternalViewSplitter: false
    readonly property bool isPlaceHolder: false

    readonly property int animationTime: animations.speedFactor.normal * (1.2*animations.duration.small)

    property int index: -1
    property real length: 0

    Behavior on length {
        id: animatedLengthBehavior
        enabled: !parabolic.directRenderingEnabled || restoreAnimation.running
        NumberAnimation {
            duration: 3 * edgeSpacer.animationTime
            easing.type: Easing.OutCubic
        }
    }

    Behavior on length {
        enabled: !animatedLengthBehavior.enabled
        NumberAnimation { duration: 0 }
    }

    ParallelAnimation{
        id: restoreAnimation

        PropertyAnimation {
            target: edgeSpacer
            property: "length"
            to: 0
            duration: 4 * edgeSpacer.animationTime
            easing.type: Easing.InCubic
        }
    }

    //! EX-02 (docs/QML_EXTRACTION_PLAN.md): called directly by the
    //! parabolic ability's routing - spacers left the signal graph (they
    //! only ever react when exactly targeted; no broadcast arm,
    //! Qt5-inherited). The factor already carries the absorption sum and
    //! the alignment gate (inert alignments arrive as 0).
    function applyParabolicAbsorb(factor) {
        length = factor * metrics.totals.length;
    }

    function sltClearZoom(){
        restoreAnimation.start();
    }

    Component.onCompleted: {
        parabolic.sglClearZoom.connect(sltClearZoom);
    }

    Component.onDestruction: {
        parabolic.sglClearZoom.disconnect(sltClearZoom);
    }

    Loader{
        anchors.fill: parent
        active: debug.spacersEnabled

        sourceComponent: Rectangle{
            color: "#44ff0000"
            border.width: 1
            border.color: "red"
        }
    }
}
