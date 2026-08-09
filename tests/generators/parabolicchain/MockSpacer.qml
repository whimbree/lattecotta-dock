/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Mock scope around the REAL ParabolicEdgeSpacer.qml (same context-chain
// technique as MockApplet). directRenderingEnabled=true on the hub keeps
// the length Behavior disabled so reads are immediate.
import QtQuick 2.7

import "../../../containment/package/contents/ui/layouts" as RealLayouts

Item {
    id: spacerHolder

    property int beginIndex: 1
    property int spacerIndex: -1
    property bool isTail: true

    property var parabolic: null   // assigned by the harness (the hub)

    readonly property real spacerLength: realSpacer.length
    function resetLength() { realSpacer.length = 0; }

    QtObject {
        id: mockSpeed
        property real normal: 1
    }
    QtObject {
        id: mockDuration
        property int small: 1
    }
    Item {
        id: animations
        property var speedFactor: mockSpeed
        property var duration: mockDuration
    }

    QtObject {
        id: mockTotals
        property real length: 40
    }
    Item {
        id: metrics
        property var totals: mockTotals
    }

    Item {
        id: myView
        property int alignment: 0 // LatteCore.Types.Center
    }

    Item {
        id: debug
        property bool spacersEnabled: false
    }

    ParallelAnimation {
        id: restoreAnimation
    }

    RealLayouts.ParabolicEdgeSpacer {
        id: realSpacer
        index: spacerHolder.spacerIndex
    }
}
