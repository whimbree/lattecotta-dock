// SPDX-FileCopyrightText: 2026 Latte Dock contributors
// SPDX-FileCopyrightText: 2026 Bree Spektor
// SPDX-License-Identifier: GPL-2.0-or-later
//
// MultiEffect colorization - pins the colorization shader variant itself.
// No production monochromize site uses it anymore: 1932db32 moved TaskIcon's
// badge overlay and ParabolicItem's side-painting to Qt5Compat ColorOverlay
// (flat color through alpha - this shader's gray-level tint was the silent
// no-op), and those sites have their own scenes (forced_monochromatic.qml,
// applet_colorizer.qml). Shapes instead of the upstream scene's Text
// (unpinned font = latent cross-machine flake).
import QtQuick
import QtQuick.Effects

Item {
    width: 256; height: 256
    Rectangle {
        id: src
        anchors.fill: parent
        visible: false
        color: "darkorange"
        Rectangle { anchors.centerIn: parent; width: 96; height: 96; radius: 48; color: "white" }
    }
    MultiEffect {
        anchors.fill: parent
        source: src
        colorizationColor: "#3daee9"
        colorization: 0.8
    }
}
