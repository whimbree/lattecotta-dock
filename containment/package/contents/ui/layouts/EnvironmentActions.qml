/*
    SPDX-FileCopyrightText: 2019 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.7

import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.plasma.plasmoid 2.0

import org.kde.latte.core 0.2 as LatteCore

import org.kde.latte.private.containment 0.1 as LatteContainment

import org.kde.latte.abilities.items 0.1 as AbilityItem

import "loaders" as Loaders

Loader {
    id: environmentLoader

    width: root.isHorizontal ? length : localThickness
    height: root.isVertical ? length :  localThickness

    property int alignment: LatteCore.Types.BottomEdgeCenterAlign

    readonly property bool useAllLayouts: root.myView.alignment === LatteCore.Types.Justify

    readonly property int localThickness: active ? metrics.totals.thickness + metrics.margin.screenEdge : 0
    readonly property int length: {
        if (!active) {
            return 0;
        }

        if (screenEdgeMarginEnabled && root.floatingInternalGapIsForced) {
            return root.isHorizontal ? root.width : root.height;
        }

        return useAllLayouts ? root.maxLength : background.totals.visualLength;
    }

    sourceComponent: MouseArea{
        id: mainArea
        //! Qt.MidButton was removed in Qt6 (it read as undefined, so the old
        //! `Qt.LeftButton | Qt.MidButton` silently accepted LeftButton only).
        //! Accept the middle button only while the close-active-window feature
        //! is on, so that when it is off the middle-click still falls through
        //! to the containment's own middle-click action instead of being
        //! swallowed here.
        acceptedButtons: Qt.LeftButton | (root.closeActiveWindowEnabled ? Qt.MiddleButton : Qt.NoButton)
        hoverEnabled: true

        property bool wheelIsBlocked: false

        property int lastPressX: -1
        property int lastPressY: -1

        //! One scope-local handle so every tracker read below stays off the
        //! unqualified context chain (the qmllint ratchet; the Launchers
        //! ability uses the same pattern).
        readonly property QtObject selectedTracker: selectedWindowsTracker

        //! The last-active-window chain is an async dependency that is
        //! legitimately absent: selectedWindowsTracker is null until the C++
        //! View wrapper wires latteView, and its lastActiveWindow resolves
        //! through the wm registration maps, which can miss the view's layout
        //! during startup or layout moves (Windows::addRelevantLayout records
        //! the delayed-assignment case). An input event landing in one of
        //! those windows deliberately does nothing - the same visible outcome
        //! as before this gate, where the handler died on the null read
        //! without acting, but without the TypeError. latte-dock-ng hit this
        //! null-deref shape live (3a1aeaf53). lastActiveWindowChanged fires on
        //! layout registration, so this binding wakes up when the map fills.
        readonly property bool activeWindowIsReady: !!(selectedTracker
                                                       && selectedTracker.lastActiveWindow)

        onClicked: (mouse) => {
            if (root.closeActiveWindowEnabled && mouse.button === Qt.MiddleButton && activeWindowIsReady) {
                selectedTracker.lastActiveWindow.requestClose();
            }
        }

        onPressed: (mouse) => {
            if (!root.dragActiveWindowEnabled || !activeWindowIsReady) {
                return;
            }

            if (mouse.button === Qt.LeftButton && selectedTracker.lastActiveWindow.canBeDragged()) {
                lastPressX = mouse.x;
                lastPressY = mouse.y;
                dragWindowTimer.start();
            }
        }

        onReleased: {
            lastPressX = -1;
            lastPressY = -1;
        }

        onPositionChanged: (mouse) => {
            if (!root.dragActiveWindowEnabled || !activeWindowIsReady || !(mainArea.pressedButtons & Qt.LeftButton)) {
                return;
            }

            var stepX = Math.abs(lastPressX-mouse.x);
            var stepY = Math.abs(lastPressY-mouse.y);
            var threshold = 5;

            var tryDrag = mainArea.pressed && (stepX>threshold || stepY>threshold);

            if ( tryDrag && selectedTracker.lastActiveWindow.canBeDragged()) {
                dragWindowTimer.stop();
                activateDragging();
            }
        }

        onDoubleClicked: {
            if (!root.dragActiveWindowEnabled || !activeWindowIsReady) {
                return;
            }

            dragWindowTimer.stop();
            selectedTracker.lastActiveWindow.requestToggleMaximized();
        }

        //! Qt5 fired past angle = delta/8 > 10 on the signed extreme of
        //! angleDelta (EX-15: the wheel math lives in LatteCore.WheelStepper,
        //! including the Qt5 mixed-sign quirk the SignedExtreme pick pins)
        LatteCore.WheelStepper {
            id: scrollWheelStepper
            axisPick: LatteCore.WheelStepper.SignedExtreme
            fireThreshold: 80
        }

        onWheel: (wheel) => {
            if (wheelIsBlocked) {
                return;
            }

            if (root.scrollAction === LatteContainment.Types.ScrollNone) {
                root.emptyAreasWheel(wheel);
                return;
            }

            wheelIsBlocked = true;
            scrollDelayer.start();

            var direction = scrollWheelStepper.add(wheel.angleDelta, false);

            var ctrlPressed = (wheel.modifiers & Qt.ControlModifier);

            if (direction > 0) {
                //! upwards
                if (root.scrollAction === LatteContainment.Types.ScrollDesktops) {
                    latteView.windowsTracker.switchToPreviousVirtualDesktop();
                } else if (root.scrollAction === LatteContainment.Types.ScrollActivities) {
                    latteView.windowsTracker.switchToPreviousActivity();
                } else if (root.scrollAction === LatteContainment.Types.ScrollToggleMinimized) {
                    if (!ctrlPressed) {
                        tasksLoader.item.activateNextPrevTask(true);
                    } else if (activeWindowIsReady && !selectedTracker.lastActiveWindow.isMaximized){
                        selectedTracker.lastActiveWindow.requestToggleMaximized();
                    }
                } else if (tasksLoader.active) {
                    tasksLoader.item.activateNextPrevTask(true);
                }
            } else if (direction < 0) {
                //! downwards
                if (root.scrollAction === LatteContainment.Types.ScrollDesktops) {
                    latteView.windowsTracker.switchToNextVirtualDesktop();
                } else if (root.scrollAction === LatteContainment.Types.ScrollActivities) {
                    latteView.windowsTracker.switchToNextActivity();
                } else if (root.scrollAction === LatteContainment.Types.ScrollToggleMinimized) {
                    if (!activeWindowIsReady) {
                        //! no target to toggle; deliberately do nothing
                    } else if (!ctrlPressed) {
                        if (selectedTracker.lastActiveWindow.isValid
                                && !selectedTracker.lastActiveWindow.isMinimized
                                && selectedTracker.lastActiveWindow.isMaximized){
                            //! maximized
                            selectedTracker.lastActiveWindow.requestToggleMaximized();
                        } else if (selectedTracker.lastActiveWindow.isValid
                                   && !selectedTracker.lastActiveWindow.isMinimized
                                   && !selectedTracker.lastActiveWindow.isMaximized) {
                            //! normal
                            selectedTracker.lastActiveWindow.requestToggleMinimized();
                        }
                    } else if (selectedTracker.lastActiveWindow.isMaximized) {
                        selectedTracker.lastActiveWindow.requestToggleMaximized();
                    }
                } else if (tasksLoader.active) {
                    tasksLoader.item.activateNextPrevTask(false);
                }
            }
        }

        Loaders.Tasks{
            id: tasksLoader
        }

        function activateDragging(){
            selectedTracker.requestMoveLastWindow(mainArea.mouseX, mainArea.mouseY);
            mainArea.lastPressX = -1;
            mainArea.lastPressY = -1;
        }

        //! Timers
        Timer {
            id: dragWindowTimer
            interval: 500
            onTriggered: {
                if (mainArea.pressed && mainArea.activeWindowIsReady && mainArea.selectedTracker.lastActiveWindow.canBeDragged()) {
                    mainArea.activateDragging();
                }
            }
        }

        //! A timer is needed in order to handle also touchpads that probably
        //! send too many signals very fast. This way the signals per sec are limited.
        //! The user needs to have a steady normal scroll in order to not
        //! notice a annoying delay
        Timer{
            id: scrollDelayer

            interval: 200
            onTriggered: mainArea.wheelIsBlocked = false;
        }

        //! Background Indicator
        AbilityItem.IndicatorLevel {
            id: indicatorBackLayer
            anchors.fill: parent

            level.isDrawn: root.indicators.isEnabled
            level.isBackground: true
            level.indicator: AbilityItem.IndicatorObject{
                animations: root.animations
                metrics: root.metrics
                host: root.indicators

                isEmptySpace: true
                isPressed: mainArea.pressed
                panelOpacity: root.background.currentOpacity
                shadowColor: root.myView.itemShadow.shadowSolidColor
                colorPalette: colorizerManager.applyTheme

                iconBackgroundColor: "brown"
                iconGlowColor: "pink"
            }

            Connections {
                target: mainArea
                enabled: root.indicators.info.needsMouseEventCoordinates
                function onPressed(mouse) { indicatorBackLayer.level.mousePressed(mouse.x, mouse.y, mouse.button); }
                function onReleased(mouse) { indicatorBackLayer.level.mouseReleased(mouse.x, mouse.y, mouse.button); }
            }
        }
    }

    states:[
        State {
            name: "bottomCenter"
            when: (alignment === LatteCore.Types.BottomEdgeCenterAlign)

            AnchorChanges {
                target: environmentLoader
                anchors{ top:undefined; bottom:_mainLayout.bottom; left:undefined; right:undefined;
                    horizontalCenter: _mainLayout.horizontalCenter; verticalCenter:undefined}
            }
        },
        State {
            name: "bottomLeft"
            when: (alignment === LatteCore.Types.BottomEdgeLeftAlign)

            AnchorChanges {
                target: environmentLoader
                anchors{ top:undefined; bottom:_mainLayout.bottom; left:_mainLayout.left; right:undefined;
                    horizontalCenter: undefined; verticalCenter:undefined}
            }
        },
        State {
            name: "bottomRight"
            when: (alignment === LatteCore.Types.BottomEdgeRightAlign)

            AnchorChanges {
                target: environmentLoader
                anchors{ top:undefined; bottom: _mainLayout.bottom; left:undefined; right:_mainLayout.right;
                    horizontalCenter: undefined; verticalCenter:undefined}
            }
        },
        State {
            name: "topCenter"
            when: (alignment === LatteCore.Types.TopEdgeCenterAlign)

            AnchorChanges {
                target: environmentLoader
                anchors{ top: _mainLayout.top; bottom:undefined; left:undefined; right:undefined;
                    horizontalCenter: _mainLayout.horizontalCenter; verticalCenter:undefined}
            }
        },
        State {
            name: "topLeft"
            when: (alignment === LatteCore.Types.TopEdgeLeftAlign)

            AnchorChanges {
                target: environmentLoader
                anchors{ top: _mainLayout.top; bottom:undefined; left: _mainLayout.left; right:undefined;
                    horizontalCenter: undefined; verticalCenter:undefined}
            }
        },
        State {
            name: "topRight"
            when: (alignment === LatteCore.Types.TopEdgeRightAlign)

            AnchorChanges {
                target: environmentLoader
                anchors{ top: _mainLayout.top; bottom:undefined; left:undefined; right: _mainLayout.right;
                    horizontalCenter: undefined; verticalCenter:undefined}
            }
        },
        State {
            name: "leftCenter"
            when: (alignment === LatteCore.Types.LeftEdgeCenterAlign)

            AnchorChanges {
                target: environmentLoader
                anchors{ top:undefined; bottom:undefined; left: _mainLayout.left; right:undefined;
                    horizontalCenter:undefined; verticalCenter: _mainLayout.verticalCenter}
            }
        },
        State {
            name: "leftTop"
            when: (alignment === LatteCore.Types.LeftEdgeTopAlign)

            AnchorChanges {
                target: environmentLoader
                anchors{ top:mainLayout.top; bottom:undefined; left: _mainLayout.left; right:undefined;
                    horizontalCenter:undefined; verticalCenter: undefined}
            }
        },
        State {
            name: "leftBottom"
            when: (alignment === LatteCore.Types.LeftEdgeBottomAlign)

            AnchorChanges {
                target: environmentLoader
                anchors{ top:undefined; bottom:_mainLayout.bottom; left: _mainLayout.left; right:undefined;
                    horizontalCenter:undefined; verticalCenter: undefined}
            }
        },
        State {
            name: "rightCenter"
            when: (alignment === LatteCore.Types.RightEdgeCenterAlign)

            AnchorChanges {
                target: environmentLoader
                anchors{ top:undefined; bottom:undefined; left:undefined; right: _mainLayout.right;
                    horizontalCenter:undefined; verticalCenter: _mainLayout.verticalCenter}
            }
        },
        State {
            name: "rightTop"
            when: (alignment === LatteCore.Types.RightEdgeTopAlign)

            AnchorChanges {
                target: environmentLoader
                anchors{ top:_mainLayout.top; bottom:undefined; left:undefined; right: _mainLayout.right;
                    horizontalCenter:undefined; verticalCenter: undefined}
            }
        },
        State {
            name: "rightBottom"
            when: (alignment === LatteCore.Types.RightEdgeBottomAlign)

            AnchorChanges {
                target: environmentLoader
                anchors{ top:undefined; bottom:_mainLayout.bottom; left:undefined; right: _mainLayout.right;
                    horizontalCenter:undefined; verticalCenter: undefined}
            }
        }
    ]
}
