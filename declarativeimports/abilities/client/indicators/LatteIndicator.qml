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
import org.kde.kirigami 2.20 as Kirigami

import org.kde.latte.core 0.2 as LatteCore
import org.kde.latte.components 1.0 as LatteComponents

LatteComponents.IndicatorItem{
    id: root
    extraMaskThickness: reversedEnabled && glowEnabled ? 1.7 * (factor * indicator.maxIconSize) : 0

    enabledForApplets: indicator && indicator.configuration ? indicator.configuration.enabledForApplets : true
    lengthPadding: indicator && indicator.configuration ? indicator.configuration.lengthPadding : 0.08

    readonly property real factor: 0.08
    readonly property int size: factor * indicator.currentIconSize

    readonly property int screenEdgeMargin: Plasmoid.location === PlasmaCore.Types.Floating || reversedEnabled ? 0 : indicator.screenEdgeMargin

    //! D14: Kirigami's attached PlatformTheme serves an invalid QColor on this
    //! binding's first evaluation at creation, before its palette resolves; the
    //! change notify recomputes it a beat later. Guard so the invalid interim is
    //! not handed to the C++ boundary (declarativeimports/core/tools.cpp); the
    //! valid branch is unchanged so the settled brightness is identical.
    property real textColorBrightness: Kirigami.Theme.textColor.valid ? LatteCore.Tools.colorBrightness(Kirigami.Theme.textColor) : 0

    property color isActiveColor: Kirigami.Theme.focusColor
    property color minimizedColor: {
        if (minimizedTaskColoredDifferently) {
            return (textColorBrightness > 127.5 ? Qt.darker(Kirigami.Theme.textColor, 1.7) : Qt.lighter(Kirigami.Theme.textColor, 7));
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

    /*Rectangle{
        anchors.fill: parent
        border.width: 1
        border.color: "yellow"
        color: "transparent"
        opacity:0.6
    }*/

    Item{
        id: mainIndicatorElement

        width: flowItem.width
        height: flowItem.height

        Flow{
            id: flowItem
            flow: Plasmoid.formFactor === PlasmaCore.Types.Vertical ? Flow.TopToBottom : Flow.LeftToRight

            LatteComponents.GlowPoint{
                id:firstPoint
                opacity: {
                    if (root.indicator.isEmptySpace) {
                        return 0;
                    }

                    if (root.indicator.isTask) {
                        return root.indicator.isLauncher || (root.indicator.inRemoving && !activeAndReverseAnimation.running) ? 0 : 1
                    }

                    if (root.indicator.isApplet) {
                        return (root.indicator.isActive || activeAndReverseAnimation.running) ? 1 : 0
                    }
                }

                basicColor: root.indicator.isActive || (root.indicator.isGroup && root.indicator.hasShown) ? root.isActiveColor : root.notActiveColor

                size: root.size
                glow3D: glow3D
                animation: Math.max(1.65*3*LatteCore.Environment.longDuration,root.indicator.durationTime*3*LatteCore.Environment.longDuration)
                location: Plasmoid.location
                glowOpacity: root.glowOpacity
                contrastColor: root.indicator.shadowColor
                attentionColor: Kirigami.Theme.negativeTextColor

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
                showBorder: root.glowEnabled && glow3D

                property int stateWidth: root.indicator.isGroup ? root.width - secondPoint.width : root.width - spacer.width
                property int stateHeight: root.indicator.isGroup ? root.height - secondPoint.height : root.height - spacer.height

                property int animationTime: root.indicator.durationTime* (0.7*LatteCore.Environment.longDuration)

                property bool isActive: root.indicator.hasActive || root.indicator.isActive

                property bool vertical: Plasmoid.formFactor === PlasmaCore.Types.Vertical

                property real scaleFactor: root.indicator.scaleFactor

                function updateInitialSizes(){
                    if(root){
                        if(vertical)
                            width = root.size;
                        else
                            height = root.size;

                        if(vertical && isActive && root.activeStyle === 0 /*Line*/)
                            height = stateHeight;
                        else
                            height = root.size;

                        if(!vertical && isActive && root.activeStyle === 0 /*Line*/)
                            width = stateWidth;
                        else
                            width = root.size;
                    }
                }


                onIsActiveChanged: {
                    if (root.activeStyle === 0 /*Line*/)
                        activeAndReverseAnimation.start();
                }

                onScaleFactorChanged: {
                    if(!activeAndReverseAnimation.running && !vertical && isActive && root.activeStyle === 0 /*Line*/){
                        width = stateWidth;
                    }
                    else if (!activeAndReverseAnimation.running && vertical && isActive && root.activeStyle === 0 /*Line*/){
                        height = stateHeight;
                    }
                }

                onStateWidthChanged:{
                    if(!activeAndReverseAnimation.running && !vertical && isActive && root.activeStyle === 0 /*Line*/)
                        width = stateWidth;
                }

                onStateHeightChanged:{
                    if(!activeAndReverseAnimation.running && vertical && isActive && root.activeStyle === 0 /*Line*/)
                        height = stateHeight;
                }

                onVerticalChanged: updateInitialSizes();

                Component.onCompleted: {
                    updateInitialSizes();

                    if (root.indicator) {
                        root.indicator.onCurrentIconSizeChanged.connect(updateInitialSizes);
                    }
                }

                Component.onDestruction: {
                    if (root.indicator) {
                        root.indicator.onCurrentIconSizeChanged.disconnect(updateInitialSizes);
                    }
                }

                NumberAnimation{
                    id: activeAndReverseAnimation
                    target: firstPoint
                    property: Plasmoid.formFactor === PlasmaCore.Types.Vertical ? "height" : "width"
                    to: root.indicator.hasActive && root.activeStyle === 0 /*Line*/
                        ? (Plasmoid.formFactor === PlasmaCore.Types.Vertical ? firstPoint.stateHeight : firstPoint.stateWidth) : root.size
                    duration: firstPoint.animationTime
                    easing.type: Easing.InQuad

                    onStopped: firstPoint.updateInitialSizes()
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
                showBorder: root.glowEnabled && glow3D

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
                    target: mainIndicatorElement
                    anchors{ verticalCenter:parent.verticalCenter; horizontalCenter:undefined;
                        top:undefined; bottom:undefined; left:parent.left; right:undefined;}
                }
                PropertyChanges{
                    target: mainIndicatorElement
                    anchors.leftMargin: root.screenEdgeMargin;    anchors.rightMargin: 0;     anchors.topMargin:0;    anchors.bottomMargin:0;
                    anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
                }
            },
            State {
                name: "bottom"
                when: (Plasmoid.location === PlasmaCore.Types.Floating ||
                       (Plasmoid.location === PlasmaCore.Types.BottomEdge && !root.reversedEnabled) ||
                       (Plasmoid.location === PlasmaCore.Types.TopEdge && root.reversedEnabled))

                AnchorChanges {
                    target: mainIndicatorElement
                    anchors{ verticalCenter:undefined; horizontalCenter:parent.horizontalCenter;
                        top:undefined; bottom:parent.bottom; left:undefined; right:undefined;}
                }
                PropertyChanges{
                    target: mainIndicatorElement
                    anchors.leftMargin: 0;    anchors.rightMargin: 0;     anchors.topMargin:0;    anchors.bottomMargin: root.screenEdgeMargin;
                    anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
                }
            },
            State {
                name: "top"
                when: ((Plasmoid.location === PlasmaCore.Types.TopEdge && !root.reversedEnabled) ||
                       (Plasmoid.location === PlasmaCore.Types.BottomEdge && root.reversedEnabled))

                AnchorChanges {
                    target: mainIndicatorElement
                    anchors{ verticalCenter:undefined; horizontalCenter:parent.horizontalCenter;
                        top:parent.top; bottom:undefined; left:undefined; right:undefined;}
                }
                PropertyChanges{
                    target: mainIndicatorElement
                    anchors.leftMargin: 0;    anchors.rightMargin: 0;     anchors.topMargin: root.screenEdgeMargin;    anchors.bottomMargin:0;
                    anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
                }
            },
            State {
                name: "right"
                when: ((Plasmoid.location === PlasmaCore.Types.RightEdge && !root.reversedEnabled) ||
                       (Plasmoid.location === PlasmaCore.Types.LeftEdge && root.reversedEnabled))

                AnchorChanges {
                    target: mainIndicatorElement
                    anchors{ verticalCenter:parent.verticalCenter; horizontalCenter:undefined;
                        top:undefined; bottom:undefined; left:undefined; right:parent.right;}
                }
                PropertyChanges{
                    target: mainIndicatorElement
                    anchors.leftMargin: 0;    anchors.rightMargin: root.screenEdgeMargin;     anchors.topMargin:0;    anchors.bottomMargin:0;
                    anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
                }
            }
        ]
    }
}// number of windows root.indicator

