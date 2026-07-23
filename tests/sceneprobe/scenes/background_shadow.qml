// SPDX-FileCopyrightText: 2026 Bree Spektor
// SPDX-FileCopyrightText: 2026 Latte Dock contributors
// SPDX-License-Identifier: GPL-2.0-or-later
//
// A long side-dock-shaped rectangle exercises the production background
// effect. The red halo reaches the same fixed-pixel distance above and beside
// the source despite the 5:1 aspect ratio. Kirigami.ShadowedRectangle's old
// aspect-scaled render node exceeded the scene by hundreds of pixels here.
import QtQuick

import "../../../containment/package/contents/ui/colorizer" as Colorizer

Item {
    width: 256
    height: 256

    property var probeExpect: [
        { "x": 128, "y": 25, "rgba": "#552828", "tol": 0.12 },
        { "x": 104, "y": 128, "rgba": "#642424", "tol": 0.15 },
        { "x": 128, "y": 128, "rgba": "#ff4040", "tol": 0.05 },
        { "x": 10, "y": 10, "rgba": "#303030", "tol": 0.05 }
    ]

    Rectangle {
        anchors.fill: parent
        color: "#303030"
    }

    Item {
        anchors.centerIn: parent
        width: 40
        height: 200

        Colorizer.BackgroundShadow {
            anchors.fill: translucentSource
            blur: 16
            radius: translucentSource.radius
            color: "red"
        }

        Rectangle {
            id: translucentSource
            anchors.fill: parent
            radius: 12
            color: "white"
            opacity: 0.25
        }
    }
}
