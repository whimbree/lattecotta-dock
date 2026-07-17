// SPDX-FileCopyrightText: 2026 Latte Dock contributors
// SPDX-FileCopyrightText: 2026 Bree Spektor
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Indicator glow - mirrors indicators/default/package/ui/main.qml firstPoint:
// the default indicator's active-task dot as it instantiates
// LatteComponents.GlowPoint - line-style active width, showGlow (the
// glowApplyTo=All branch), glow3D with showBorder: glow3D, roundCorners,
// location BottomEdge, contrastColor standing in for the indicator
// shadowColor. GlowPoint is the QtQuick.Shapes port (RadialGradient /
// LinearGradient ShapePaths replacing the Qt5 GraphicalEffects gradients);
// the halo expectation fails if those gradients stop painting - the "glow
// silently vanished" family.
import QtQuick
import org.kde.latte.components 1.0 as LatteComponents
import org.kde.plasma.core 2.0 as PlasmaCore

Item {
    width: 256; height: 256

    // The line spans x [68,188], y [200,216]; the glow grid (fullCorner 96)
    // reaches ~48px above the line's vertical center (y=208).
    // Expected values measured from the actual lavapipe render (2026-07-16,
    // pinned Mesa 26.1.2 / Qt 6.11).
    property var probeExpect: [
        // line center: the active basicColor #3daee9 through the glow3D
        // shadow blend
        { "x": 128, "y": 208, "rgba": "#54aedd", "tol": 0.10 },
        // glow halo above the line: the Shapes gradient blend over the
        // background; a vanished glow reads #181818 (blue delta 127),
        // rejected far beyond the tol
        { "x": 128, "y": 190, "rgba": "#2e7397", "tol": 0.10 },
        // background far from the glow stays untouched
        { "x": 10, "y": 10, "rgba": "#181818", "tol": 0.05 }
    ]

    Rectangle { anchors.fill: parent; color: "#181818" }

    LatteComponents.GlowPoint {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 40
        width: 120 // stateWidth: line-style active spans the item length
        height: 16
        size: 16 // root.size stand-in
        location: PlasmaCore.Types.BottomEdge
        basicColor: "#3daee9" // isActiveColor stand-in
        contrastColor: "#b0b0b0" // indicator shadowColor stand-in
        glowOpacity: 0.75
        roundCorners: true
        showGlow: true // glowEnabled && glowApplyTo === All
        glow3D: true
        showBorder: true // production binds showBorder: glow3D
    }
}
