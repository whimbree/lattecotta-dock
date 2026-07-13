/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.8
import QtQuick.Controls 2.15 as QQC2

import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.plasma.components 3.0 as PlasmaComponents
import org.kde.kirigami 2.20 as Kirigami

import org.kde.latte.private.app 0.1 as LatteApp
import org.kde.latte.core 0.2 as LatteCore
import org.kde.latte.private.containment 0.1 as LatteContainment

import "canvas" as CanvasComponent

Loader {
    id: canvasRoot
    active: plasmoid && plasmoid.configuration && latteView

    //! Exposed to CanvasConfigView (C++): the rearrange/exit toggle's rect in canvas-local coords. In
    //! configure-applets mode the canvas is carved click-through EXCEPT this rect, so the toggle stays
    //! clickable (to leave rearrange) while every widget interaction falls through to the dock.
    property rect rearrangeToggleRect: item ? item.rearrangeToggleRect : Qt.rect(-1, -1, 0, 0)

    sourceComponent: Item{
        id: root
        readonly property bool isVertical: plasmoid.formFactor === PlasmaCore.Types.Vertical
        readonly property bool isHorizontal: !isVertical
        readonly property rect rearrangeToggleRect: {
            var b = settingsOverlay.rearrangeToggle;
            if (!b || !b.visible) {
                return Qt.rect(-1, -1, 0, 0);
            }
            //! map the CHIP (the real click target), not the outer button
            //! item: the outer item has been observed stretched after chrome
            //! retargeting, and carving the mask from it produced a full-width
            //! stripe that ate hover and drags across every applet's middle
            var chip = b.interactiveChip ? b.interactiveChip : b;
            //! reference the reactive geometry so the binding re-evaluates when the toggle moves/resizes
            var reactive = chip.width + chip.height + b.x + b.y + b.width + b.height + root.width + root.height;
            return chip.mapToItem(root, 0, 0, chip.width, chip.height);
        }

        property int animationSpeed: LatteCore.WindowSystem.compositingActive ? 500 : 0
        property int panelAlignment: plasmoid.configuration.alignment

        readonly property real appliedOpacity: imageTiler.opacity
        readonly property real maxOpacity: {
            //! Mirrors the containment blueprint's opacity (that is the grid the user
            //! actually sees; this window's own tiler no longer renders). SettingsOverlay
            //! picks its text contrast from imageTiler.opacity, so the value must track
            //! the real grid.
            if (universalSettings.inConfigureAppletsMode || !LatteCore.WindowSystem.compositingActive) {
                return 1;
            }

            return plasmoid.configuration.editBackgroundOpacity;
        }

        property real offset: {
            if (root.isHorizontal) {
                return width * (plasmoid.configuration.offset/100);
            } else {
                return height * (plasmoid.configuration.offset/100)
            }
        }

        property string appChosenShadowColor: {
            if (plasmoid.configuration.shadowColorType === LatteContainment.Types.ThemeColorShadow) {
                var strC = String(Kirigami.Theme.textColor);
                return strC.indexOf("#") === 0 ? strC.substr(1) : strC;
            } else if (plasmoid.configuration.shadowColorType === LatteContainment.Types.UserColorShadow) {
                return plasmoid.configuration.shadowColor;
            }

            // default shadow color
            return "080808";
        }

        property string appShadowColorSolid: "#" + appChosenShadowColor

        //// BEGIN OF Behaviors
        Behavior on offset {
            NumberAnimation {
                id: offsetAnimation
                duration: animationSpeed
                easing.type: Easing.OutCubic
            }
        }
        //// END OF Behaviors

        Item {
            id: graphicsSystem
            readonly property bool isAccelerated: (GraphicsInfo.api !== GraphicsInfo.Software)
                                                  && (GraphicsInfo.api !== GraphicsInfo.Unknown)
        }

        Image{
            id: imageTiler
            anchors.fill: parent
            //! The containment draws the blueprint inside the dock window now; a second
            //! copy here paints over the dock, since this window stacks in front of it.
            //! The item stays for its geometry (the wheel MouseArea anchors to it) and
            //! its opacity value (SettingsOverlay picks text contrast from it).
            visible: false
            opacity: root.maxOpacity
            fillMode: Image.Tile
            source: latteView.layout ? latteView.layout.background : "../images/canvas/blueprint.jpg"

            Behavior on opacity {
                NumberAnimation {
                    duration: 0.8 * root.animationSpeed
                    easing.type: Easing.OutCubic
                }
            }
        }

        LatteApp.ContextMenuLayer {
            id: contextMenuLayer
            anchors.fill: parent
            view: latteView
        }

        MouseArea {
            id: editBackMouseArea
            anchors.fill: imageTiler
            visible: !universalSettings.inConfigureAppletsMode
            hoverEnabled: true

            property bool wheelIsBlocked: false;
            //! Qt5 semantics: the wheel adjusts the edit-mode grid opacity
            //! (editBackgroundOpacity), not the dock's own background setting.
            readonly property real opacityStep: 0.1
            readonly property string tooltip: i18nc("opacity for the edit mode background, %1 is opacity percentage",
                                                    "You can use mouse wheel to change the edit background opacity of %1%", Math.round(plasmoid.configuration.editBackgroundOpacity * 100))

            onWheel: (wheel) => {
                processWheel(wheel);
            }


            function processWheel(wheel) {
                if (wheelIsBlocked) {
                    return;
                }

                wheelIsBlocked = true;
                scrollDelayer.start();

                var angle = wheel.angleDelta.y / 8;

                var current = plasmoid.configuration.editBackgroundOpacity;

                if (angle > 10) {
                    plasmoid.configuration.editBackgroundOpacity = Math.min(1, current + opacityStep);
                } else if (angle < -10) {
                    plasmoid.configuration.editBackgroundOpacity = Math.max(0, current - opacityStep);
                }
            }

            //! A timer is needed in order to handle also touchpads that probably
            //! send too many signals very fast. This way the signals per sec are limited.
            //! The user needs to have a steady normal scroll in order to not
            //! notice a annoying delay
            Timer{
                id: scrollDelayer

                interval: 80
                onTriggered: editBackMouseArea.wheelIsBlocked = false;
            }
        }

        PlasmaComponents.Button {
            anchors.fill: editBackMouseArea
            opacity: 0

            QQC2.ToolTip.text: editBackMouseArea.tooltip
            QQC2.ToolTip.visible: hovered && editBackMouseArea.tooltip.length > 0
        }

        //! Settings Overlay
        CanvasComponent.SettingsOverlay {
            id: settingsOverlay
            anchors.fill: parent
        }
    }
}
