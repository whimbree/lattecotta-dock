// SPDX-FileCopyrightText: 2026 Latte Dock contributors
// SPDX-FileCopyrightText: 2026 Bree Spektor
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Forced monochromatic icons - mirrors the 1932db32 ColorOverlay sites:
// ParabolicItem.qml's side-painting Loader (anchors.fill the content,
// ColorOverlay painting the palette textColor through the icon's alpha) with
// TaskIcon.qml's provider-stability rider - the source's layer held ON
// permanently while the overlay exists (taskIconItem's layer.enabled gate),
// so the overlay samples a LAYERED source like production. Same silent-no-op
// family as applet_colorizer: Qt5-faithful monochromize is flat textColor
// through the alpha; MultiEffect.colorization multiplies by the source's
// gray level, so dark icon pixels forced to a light textColor came out dark.
// The two glyph bars have different source darkness and must both read the
// SAME flat textColor - flatness across source luminances is precisely what
// separates overlay semantics from the colorization tint.
import QtQuick
import Qt5Compat.GraphicalEffects as GraphicalEffects

Item {
    width: 256; height: 256

    // Icon box spans (64,64)-(192,192); bars at y [80,104], [116,140],
    // [152,176] in scene coordinates.
    property var probeExpect: [
        // dark gray bar painted flat in the light textColor
        { "x": 128, "y": 92, "rgba": "#fcfcfc", "tol": 0.10 },
        // near-black bar reads the SAME color (colorization would leave it
        // near-black; even partial tinting breaks the flatness)
        { "x": 110, "y": 164, "rgba": "#fcfcfc", "tol": 0.10 },
        // transparent gap between bars: the overlay respects the alpha
        { "x": 128, "y": 110, "rgba": "#303030", "tol": 0.05 },
        { "x": 10, "y": 10, "rgba": "#303030", "tol": 0.05 }
    ]

    Rectangle { anchors.fill: parent; color: "#303030" }

    // task icon stand-in: glyph bars of differing darkness on transparent
    // margins; layer held ON like taskIconItem's forceMonochromaticIcons gate
    Item {
        id: taskIconItem
        anchors.centerIn: parent
        width: 128; height: 128
        layer.enabled: true
        Rectangle { x: 16; y: 16; width: 96; height: 24; radius: 4; color: "#31363b" }
        Rectangle { x: 16; y: 52; width: 96; height: 24; radius: 4; color: "#232629" }
        Rectangle { x: 16; y: 88; width: 60; height: 24; radius: 4; color: "#0a0a0a" }
    }

    // ParabolicItem's side-painting Loader shape
    Loader {
        anchors.fill: taskIconItem
        active: true
        sourceComponent: GraphicalEffects.ColorOverlay {
            anchors.fill: parent
            color: "#fcfcfc" // latteBridge.colorPalette.textColor stand-in
            source: taskIconItem
        }
    }
}
