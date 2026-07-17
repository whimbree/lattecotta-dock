// SPDX-FileCopyrightText: 2026 Latte Dock contributors
// SPDX-FileCopyrightText: 2026 Bree Spektor
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Parabolic zoom - mirrors declarativeimports/abilities/items/basicitem/
// ParabolicItem.qml (_contentItemContainer): the zoom is SIZE-driven, not a
// scale transform - the container's width follows newTempSize
// (iconSize * zoom) while anchored to the screen edge, and zoom changes
// animate (Behavior on zoom, NumberAnimation + Easing.OutCubic). The scene
// reproduces that shape with an animated zoom 1.0 -> 1.6 under the fixed-step
// clock, in the NumberAnimation-on-property form multieffect_degenerate
// proved deterministic. The 48ms duration completes before the last rendered
// frame (t=80ms), so the readback is at zoom 1.6 exactly and the
// expectations do not depend on easing interpolation.
//
// Text-free by design: production parabolic content is icons, not text, and
// an unpinned font is a latent cross-machine flake (the fork's parabolic_zoom
// carries one) - see docs/agent-logs/2026-07-16-sceneprobe-followup-scenes.md.
import QtQuick

Item {
    id: sceneRoot
    width: 256; height: 256

    readonly property int iconSize: 80
    property real zoom: 1.0
    NumberAnimation on zoom {
        from: 1.0; to: 1.6
        duration: 48
        easing.type: Easing.OutCubic
        running: true
    }

    // Zoomed box spans x [64,192], y [116,244]; the unzoomed box would span
    // x [88,168], y [164,244]. Every expectation is chosen against those two
    // rectangles.
    property var probeExpect: [
        // inside the zoomed box, OUTSIDE the unzoomed one: reads the icon
        // color only if the zoom actually applied and completed
        { "x": 128, "y": 130, "rgba": "#ff8c00", "tol": 0.05 },
        // the white center detail: fails if the container grows around the
        // wrong anchor (production anchors the screen-edge side)
        { "x": 128, "y": 180, "rgba": "#ffffff", "tol": 0.05 },
        // just outside the zoomed box: the container must not overgrow
        { "x": 40, "y": 200, "rgba": "#303030", "tol": 0.05 },
        // background far away stays untouched
        { "x": 10, "y": 10, "rgba": "#303030", "tol": 0.05 }
    ]

    Rectangle { anchors.fill: parent; color: "#303030" }

    // _contentItemContainer stand-in: size-driven zoom, screen-edge anchored
    Item {
        id: contentItemContainer
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12 // screen-edge margin stand-in
        anchors.horizontalCenter: parent.horizontalCenter
        width: sceneRoot.iconSize * sceneRoot.zoom
        height: width

        // icon stand-in (shapes, not text)
        Rectangle {
            anchors.fill: parent
            radius: width / 8
            color: "#ff8c00"
            Rectangle {
                anchors.centerIn: parent
                width: parent.width * 0.4
                height: width
                radius: width / 2
                color: "white"
            }
        }
    }
}
