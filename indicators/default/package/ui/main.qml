/*
    SPDX-FileCopyrightText: 2019 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.0
import QtQuick.Layouts 1.1

import org.kde.plasma.plasmoid 2.0
import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.plasma.components 3.0 as PlasmaComponents

import org.kde.latte.core 0.2 as LatteCore
import org.kde.latte.components 1.0 as LatteComponents

LatteComponents.IndicatorItem{
    id: root
    extraMaskThickness: reversedEnabled && glowEnabled ? 1.7 * (factor * indicator.maxIconSize) : 0

    enabledForApplets: indicator && indicator.configuration ? indicator.configuration.enabledForApplets : true
    lengthPadding: indicator && indicator.configuration ? indicator.configuration.lengthPadding : 0.08
    backgroundCornerMargin: indicator && indicator.configuration ? indicator.configuration.backgroundCornerMargin : 1.00

    readonly property real factor: indicator.configuration.size
    readonly property int size: factor * indicator.currentIconSize
    readonly property int thickLocalMargin: indicator.configuration.thickMargin * indicator.currentIconSize

    readonly property int screenEdgeMargin: Plasmoid.location === PlasmaCore.Types.Floating || reversedEnabled ? 0 : indicator.screenEdgeMargin

    readonly property int thicknessMargin: screenEdgeMargin + thickLocalMargin + (glowEnabled ? 1 : 0)

    //! D14: the indicator colorPalette serves an invalid textColor on this
    //! binding's first evaluation at creation, before the palette resolves;
    //! guard so the invalid interim is not handed to the C++ boundary
    //! (declarativeimports/core/tools.cpp). Valid branch unchanged.
    property real textColorBrightness: indicator.colorPalette.textColor.valid ? LatteCore.Tools.colorBrightness(indicator.colorPalette.textColor) : 0

    //! buttonFocusColor is a Plasma color-group name; a Kirigami.Theme fallback palette
    //! (used when no colorization is active) lacks it and would read undefined -> black.
    //! Mirror the colorizer Manager and fall back to the palette's focusColor.
    property color isActiveColor: indicator.colorPalette.buttonFocusColor !== undefined
                                  ? indicator.colorPalette.buttonFocusColor
                                  : indicator.colorPalette.focusColor
    property color minimizedColor: {
        if (minimizedTaskColoredDifferently) {
            return (textColorBrightness > 127.5 ? Qt.darker(indicator.colorPalette.textColor, 1.7) : Qt.lighter(indicator.colorPalette.textColor, 7));
        }

        return isActiveColor;
    }

    property color notActiveColor: indicator.isMinimized ? minimizedColor : isActiveColor

    //! Common Options
    readonly property bool reversedEnabled: indicator.configuration.reversed

    //! Configuration Options
    readonly property bool extraDotOnActive: indicator.configuration.extraDotOnActive
    readonly property bool minimizedTaskColoredDifferently: indicator.configuration.minimizedTaskColoredDifferently
    readonly property int activeStyle: indicator.configuration.activeStyle
    //!glow options
    readonly property bool glowEnabled: indicator.configuration.glowEnabled
    readonly property bool glow3D: indicator.configuration.glow3D
    readonly property int glowApplyTo: indicator.configuration.glowApplyTo
    readonly property real glowOpacity: indicator.configuration.glowOpacity
    readonly property int glowMargins: glowEnabled ? 12 : 0

    /*Rectangle{
        anchors.fill: parent
        border.width: 1
        border.color: "blue"
        color: "transparent"
    }*/

    Grid{
        id: grid
        columns: Plasmoid.formFactor === PlasmaCore.Types.Vertical ? 1 : 0
        rows: Plasmoid.formFactor !== PlasmaCore.Types.Vertical ? 1 : 0
        columnSpacing: 0
        rowSpacing: 0

        LatteComponents.GlowPoint{
            id:firstPoint
            width: stateWidth
            height: stateHeight
            opacity: {
                if (root.indicator.isEmptySpace) {
                    return 0;
                }

                if (root.indicator.isTask) {
                    return root.indicator.isLauncher || (root.indicator.inRemoving && !isAnimating) ? 0 : 1
                }

                if (root.indicator.isApplet) {
                    return (root.indicator.isActive || isAnimating) ? 1 : 0
                }

                //! not yet classified (the root.indicator level initializes before
                //! the host sets isTask/isApplet); falling through returned
                //! undefined, which cannot assign to a double
                return 0;
            }

            basicColor: root.indicator.isActive || (root.indicator.isGroup && root.indicator.hasShown) ? root.isActiveColor : root.notActiveColor

            size: root.size
            glow3D: glow3D
            animation: Math.max(1.65*3*LatteCore.Environment.longDuration,root.indicator.durationTime*3*LatteCore.Environment.longDuration)
            location: Plasmoid.location
            glowOpacity: root.glowOpacity
            contrastColor: root.indicator.shadowColor
            attentionColor: root.indicator.colorPalette.negativeTextColor

            roundCorners: true
            showAttention: root.indicator.inAttention
            showGlow: {
                if (root.glowEnabled && (root.glowApplyTo === 2 /*All*/ || showAttention ))
                    return true;
                else if (root.glowEnabled && root.glowApplyTo === 1 /*OnActive*/ && root.indicator.hasActive)
                    return true;
                else
                    return false;
            }
            showBorder: glow3D

            property int stateWidth: {
                if (!vertical && isActive && root.activeStyle === 0 /*Line*/) {
                    return (root.indicator.isGroup ? root.width - secondPoint.width : root.width - spacer.width) - root.glowMargins;
                }

                return root.size;
            }

            property int stateHeight: {
                if (vertical && isActive && root.activeStyle === 0 /*Line*/) {
                    return (root.indicator.isGroup ? root.height - secondPoint.height : root.height - spacer.height) - root.glowMargins;
                }

                return root.size;
            }

            property int animationTime: root.indicator.durationTime* (0.75*LatteCore.Environment.longDuration)

            property bool isActive: root.indicator.hasActive || root.indicator.isActive

            property bool vertical: Plasmoid.formFactor === PlasmaCore.Types.Vertical

            property real scaleFactor: root.indicator.scaleFactor

            readonly property bool isAnimating: inGrowAnimation || inShrinkAnimation
            property bool inGrowAnimation: false
            property bool inShrinkAnimation: false

            property bool isBindingBlocked: isAnimating

            readonly property bool isActiveStateForAnimation: root.indicator.isActive && root.activeStyle === 0 /*Line*/

            onIsActiveStateForAnimationChanged: {
                if (root.activeStyle === 0 /*Line*/) {
                    if (isActiveStateForAnimation) {
                        inGrowAnimation = true;
                        inShrinkAnimation = false;
                    } else {
                        inGrowAnimation = false;
                        inShrinkAnimation = true;
                    }
                } else {
                    inGrowAnimation = false;
                    inShrinkAnimation = false;
                }
            }

            onWidthChanged: {
                if (!vertical) {
                    if (inGrowAnimation && width >= stateWidth) {
                        inGrowAnimation = false;
                    } else if (inShrinkAnimation && width <= stateWidth) {
                        inShrinkAnimation = false;
                    }
                }
            }

            onHeightChanged: {
                if (vertical) {
                    if (inGrowAnimation && height >= stateHeight) {
                        inGrowAnimation = false;
                    } else if (inShrinkAnimation && height <= stateHeight) {
                        inShrinkAnimation = false;
                    }
                }
            }

            Behavior on width {
                enabled: (!firstPoint.vertical && (firstPoint.isAnimating || firstPoint.opacity === 0/*first showing requirement*/))
                NumberAnimation {
                    duration: firstPoint.animationTime
                    easing.type: Easing.Linear
                }
            }

            Behavior on height {
                enabled: (firstPoint.vertical && (firstPoint.isAnimating || firstPoint.opacity === 0/*first showing requirement*/))
                NumberAnimation {
                    duration: firstPoint.animationTime
                    easing.type: Easing.Linear
                }
            }
        }

        Item{
            id:spacer
            width: secondPoint.visible ? 0.5*root.size : 0
            height: secondPoint.visible ? 0.5*root.size : 0
        }

        LatteComponents.GlowPoint{
            id:secondPoint
            width: visible ? root.size : 0
            height: width

            size: root.size
            glow3D: glow3D
            animation: Math.max(1.65*3*LatteCore.Environment.longDuration,root.indicator.durationTime*3*LatteCore.Environment.longDuration)
            location: Plasmoid.location
            glowOpacity: root.glowOpacity
            contrastColor: root.indicator.shadowColor
            showBorder: glow3D

            basicColor: state2Color
            roundCorners: true
            showGlow: root.glowEnabled  && root.glowApplyTo === 2 /*All*/
            visible:  ( root.indicator.isGroup && ((root.extraDotOnActive && root.activeStyle === 0) /*Line*/
                                              || root.activeStyle === 1 /*Dot*/
                                              || !root.indicator.hasActive) ) ? true: false

            //when there is no active window
            property color state1Color: root.indicator.hasShown ? root.isActiveColor : root.minimizedColor
            //when there is active window
            property color state2Color: root.indicator.hasMinimized ? root.minimizedColor : root.isActiveColor
        }
    }

    states: [
        State {
            name: "left"
            when: ((Plasmoid.location === PlasmaCore.Types.LeftEdge && !root.reversedEnabled) ||
                   (Plasmoid.location === PlasmaCore.Types.RightEdge && root.reversedEnabled))

            AnchorChanges {
                target: grid
                anchors{ verticalCenter:parent.verticalCenter; horizontalCenter:undefined;
                    top:undefined; bottom:undefined; left:parent.left; right:undefined;}
            }
            PropertyChanges{
                target: grid
                anchors.leftMargin: root.thicknessMargin;    anchors.rightMargin: 0;     anchors.topMargin:0;    anchors.bottomMargin:0;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }
        },
        State {
            name: "bottom"
            when: (Plasmoid.location === PlasmaCore.Types.Floating ||
                   (Plasmoid.location === PlasmaCore.Types.BottomEdge && !root.reversedEnabled) ||
                   (Plasmoid.location === PlasmaCore.Types.TopEdge && root.reversedEnabled))

            AnchorChanges {
                target: grid
                anchors{ verticalCenter:undefined; horizontalCenter:parent.horizontalCenter;
                    top:undefined; bottom:parent.bottom; left:undefined; right:undefined;}
            }
            PropertyChanges{
                target: grid
                anchors.leftMargin: 0;    anchors.rightMargin: 0;     anchors.topMargin:0;    anchors.bottomMargin: root.thicknessMargin;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }
        },
        State {
            name: "top"
            when: ((Plasmoid.location === PlasmaCore.Types.TopEdge && !root.reversedEnabled) ||
                   (Plasmoid.location === PlasmaCore.Types.BottomEdge && root.reversedEnabled))

            AnchorChanges {
                target: grid
                anchors{ verticalCenter:undefined; horizontalCenter:parent.horizontalCenter;
                    top:parent.top; bottom:undefined; left:undefined; right:undefined;}
            }
            PropertyChanges{
                target: grid
                anchors.leftMargin: 0;    anchors.rightMargin: 0;     anchors.topMargin: root.thicknessMargin;    anchors.bottomMargin:0;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }
        },
        State {
            name: "right"
            when: ((Plasmoid.location === PlasmaCore.Types.RightEdge && !root.reversedEnabled) ||
                   (Plasmoid.location === PlasmaCore.Types.LeftEdge && root.reversedEnabled))

            AnchorChanges {
                target: grid
                anchors{ verticalCenter:parent.verticalCenter; horizontalCenter:undefined;
                    top:undefined; bottom:undefined; left:undefined; right:parent.right;}
            }
            PropertyChanges{
                target: grid
                anchors.leftMargin: 0;    anchors.rightMargin: root.thicknessMargin;     anchors.topMargin:0;    anchors.bottomMargin:0;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }
        }
    ]
}// number of windows root.indicator

