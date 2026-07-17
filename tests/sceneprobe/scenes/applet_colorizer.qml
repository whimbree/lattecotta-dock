// SPDX-FileCopyrightText: 2026 Latte Dock contributors
// SPDX-FileCopyrightText: 2026 Bree Spektor
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Applet colorizer stack - mirrors containment/package/contents/ui/applet/
// colorizer/Applet.qml (the 1f835402 structure): Qt5Compat ColorOverlay
// sampling the applet wrapper, with the item shadow as the ColorOverlay's
// layer.effect (a sibling shadow double-draws; the layer REPLACES the
// rendering). This scene exists to catch the "colorizing is a silent no-op"
// family: correct ColorOverlay semantics paint applyColor FLAT through the
// wrapper's alpha, so dark content under a light scheme becomes light. The
// MultiEffect.colorization substitution both reference forks shipped
// multiplies by the source's gray level and re-outputs dark pixels - the
// content-pixel expectation below rejects exactly that output.
import QtQuick
import Qt5Compat.GraphicalEffects as GraphicalEffects
import org.kde.latte.components 1.0 as LatteComponents

Item {
    width: 256; height: 256

    // Wrapper content bar spans (83,103)-(173,153); the colorizer item spans
    // (48,48)-(208,208), so the shadow point below the bar is inside it.
    property var probeExpect: [
        // dark content colorized to the light applyColor, FLAT: the
        // colorization no-op re-outputs #232629 here, far beyond the tol
        { "x": 128, "y": 128, "rgba": "#eff0f1", "tol": 0.10 },
        // red shadow presence below the bar: the layer-effect ShadowedItem
        // must sample and paint. Measured from the actual lavapipe render
        // (2026-07-16, pinned Mesa 26.1.2 / Qt 6.11); tol sized so a missing
        // shadow reading background #303030 (red delta 36) is rejected
        { "x": 128, "y": 157, "rgba": "#542828", "tol": 0.10 },
        // transparent wrapper area away from the shadow: background only
        { "x": 60, "y": 60, "rgba": "#303030", "tol": 0.05 },
        { "x": 10, "y": 10, "rgba": "#303030", "tol": 0.05 }
    ]

    Rectangle { anchors.fill: parent; color: "#303030" }

    // applet wrapper stand-in: dark content on transparent, like dark clock
    // text under a light color scheme
    Item {
        id: wrapper
        anchors.centerIn: parent
        width: 160; height: 160
        Rectangle {
            anchors.centerIn: parent
            width: 90; height: 50; radius: 6
            color: "#232629"
        }
    }

    GraphicalEffects.ColorOverlay {
        id: colorizer
        anchors.fill: wrapper
        color: "#eff0f1" // colorizerManager.applyColor stand-in (light scheme)
        source: wrapper

        // the shadow is the colorizer's layer EFFECT, exactly as production:
        // a sibling ShadowedItem sampling this item double-struck the
        // colorized content with a shifted ghost copy (1f835402 defect 3)
        layer.enabled: true
        layer.effect: LatteComponents.ShadowedItem {
            shadowColor: "red"
            shadowSizePx: 16
            shadowVerticalOffset: 2
        }
    }
}
