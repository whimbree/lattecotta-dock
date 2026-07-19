/*
    SPDX-FileCopyrightText: 2019 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor

    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.7

import org.kde.plasma.plasmoid 2.0
import org.kde.plasma.core 2.0 as PlasmaCore

import org.kde.latte.core 0.2 as LatteCore
import org.kde.latte.private.tasks 0.1 as LatteTasks

//! The overflow/scroll math lives in the ScrollMath core
//! (plasmoid/plugin/units/scrollmath.h, EX-21 in
//! docs/tracking/QML_EXTRACTION_PLAN.md), reached through the stateless
//! LatteTasks.ScrollOverflowMath singleton; this file keeps the fact
//! assembly (ability reads, mapToItem), the orientation resolution and
//! the contentX/contentY writes the Behavior animations ride on.
Flickable{
    id: flickableContainer
    clip: false
    flickableDirection: Plasmoid.formFactor === PlasmaCore.Types.Horizontal ? Flickable.HorizontalFlick : Flickable.VerticalFlick
    interactive: false

    property int thickness:0 // through Binding to avoid binding loops
    property int length:0 // through Binding to avoid binding loops

    property real offset: 0

    readonly property bool animationsFinished: !horizontalAnimation.running && !verticalAnimation.running
    readonly property bool centered: root.alignment === LatteCore.Types.Center
    readonly property bool reversed: Qt.application.layoutDirection === Qt.RightToLeft

    readonly property bool contentsExceed: LatteTasks.ScrollOverflowMath.contentsExceed(root.scrollingEnabled, root.tasksLength, flickableContainer.length)
    readonly property int contentsExtraSpace: LatteTasks.ScrollOverflowMath.contentsExtraSpace(root.scrollingEnabled, root.tasksLength, flickableContainer.length)

    readonly property real scrollFirstPos: 0
    readonly property real scrollLastPos: contentsExtraSpace
    readonly property real scrollStep: LatteTasks.ScrollOverflowMath.wheelScrollStep(appletAbilities.metrics.totals.length)
    readonly property real currentPos: !root.vertical ? contentX : contentY

    readonly property real autoScrollTriggerLength: appletAbilities.metrics.iconSize + appletAbilities.metrics.totals.lengthEdge

    readonly property int alignment: {
        if (root.location === PlasmaCore.Types.LeftEdge) {
            if (centered) return LatteCore.Types.LeftEdgeCenterAlign;
            if (root.alignment === LatteCore.Types.Top) return LatteCore.Types.LeftEdgeTopAlign;
            if (root.alignment === LatteCore.Types.Bottom) return LatteCore.Types.LeftEdgeBottomAlign;
        }

        if (root.location === PlasmaCore.Types.RightEdge) {
            if (centered) return LatteCore.Types.RightEdgeCenterAlign;
            if (root.alignment === LatteCore.Types.Top) return LatteCore.Types.RightEdgeTopAlign;
            if (root.alignment === LatteCore.Types.Bottom) return LatteCore.Types.RightEdgeBottomAlign;
        }

        if (root.location === PlasmaCore.Types.BottomEdge) {
            if (centered) return LatteCore.Types.BottomEdgeCenterAlign;

            if ((root.alignment === LatteCore.Types.Left && !reversed)
                    || (root.alignment === LatteCore.Types.Right && reversed)) {
                return LatteCore.Types.BottomEdgeLeftAlign;
            }

            if ((root.alignment === LatteCore.Types.Right && !reversed)
                    || (root.alignment === LatteCore.Types.Left && reversed)) {
                return LatteCore.Types.BottomEdgeRightAlign;
            }
        }

        if (root.location === PlasmaCore.Types.TopEdge) {
            if (centered) return LatteCore.Types.TopEdgeCenterAlign;

            if ((root.alignment === LatteCore.Types.Left && !reversed)
                    || (root.alignment === LatteCore.Types.Right && reversed)) {
                return LatteCore.Types.TopEdgeLeftAlign;
            }

            if ((root.alignment === LatteCore.Types.Right && !reversed)
                    || (root.alignment === LatteCore.Types.Left && reversed)) {
                return LatteCore.Types.TopEdgeRightAlign;
            }
        }

        return LatteCore.Types.BottomEdgeCenterAlign;
    }

    function increasePos() {
        increasePosWithStep(scrollStep);
    }

    function decreasePos() {
        decreasePosWithStep(scrollStep);
    }

    function increasePosWithStep(step: real) {
        scrollBySignedStep(step);
    }

    function decreasePosWithStep(step: real) {
        scrollBySignedStep(-step);
    }

    //! positive scrolls toward the row end, negative toward the row start;
    //! the core clamps asymmetrically the Qt5 way (units/scrollmath.h)
    function scrollBySignedStep(signedStep: real) {
        var target = LatteTasks.ScrollOverflowMath.steppedPos(root.scrollingEnabled, root.tasksLength, flickableContainer.length,
                                                              currentPos, signedStep);
        if (!root.vertical) {
            contentX = target;
        } else {
            contentY = target;
        }
    }

    function focusOn(task: Item) {
        var viewPosition = task.mapToItem(flickableContainer, 0, 0);
        var delta = LatteTasks.ScrollOverflowMath.focusScrollDelta(root.scrollingEnabled, root.tasksLength, flickableContainer.length,
                                                                   !root.vertical ? viewPosition.x : viewPosition.y,
                                                                   !root.vertical ? task.width : task.height,
                                                                   appletAbilities.metrics.iconSize);
        if (delta !== undefined) {
            scrollBySignedStep(delta);
        }
    }

    function autoScrollFor(task: Item, duringDragging: bool) {
        //! the block conditions and the trigger-zone decision, including the
        //! Qt5 boundary/parabolic notes, live in the core (units/scrollmath.h)
        var viewPosition = task.mapToItem(flickableContainer, 0, 0);
        var delta = LatteTasks.ScrollOverflowMath.autoScrollDelta({
                        scrollingEnabled: root.scrollingEnabled,
                        contentLength: root.tasksLength,
                        viewportLength: flickableContainer.length,
                        currentPos: currentPos,
                        itemStart: !root.vertical ? viewPosition.x : viewPosition.y,
                        itemLength: !root.vertical ? task.width : task.height,
                        triggerZone: autoScrollTriggerLength,
                        autoScrollTasksEnabled: root.autoScrollTasksEnabled,
                        duringDragging: duringDragging,
                        tasksCount: root.tasksCount,
                        hoveredIsLastVisibleItem: task.itemIndex === appletAbilities.indexer.lastVisibleItemIndex,
                        parabolicZoomFactor: appletAbilities.parabolic.factor.zoom,
                        scrollAnimationRunning: horizontalAnimation.running || verticalAnimation.running,
                        totalsLength: appletAbilities.metrics.totals.length
                    });
        if (delta !== undefined) {
            scrollBySignedStep(delta);
        }
    }

    onContentsExtraSpaceChanged: {
        var corrected = LatteTasks.ScrollOverflowMath.boundsCorrection(root.scrollingEnabled, root.tasksLength, flickableContainer.length,
                                                                       currentPos);
        if (corrected !== undefined) {
            if (!root.vertical) {
                contentX = corrected;
            } else {
                contentY = corrected;
            }
        }
    }

    Behavior on contentX {
        NumberAnimation {
            id: horizontalAnimation
            duration: appletAbilities.animations.speedFactor.current*4.1*appletAbilities.animations.duration.large
            easing.type: Easing.OutQuad
        }
    }

    Behavior on contentY {
        NumberAnimation {
            id: verticalAnimation
            duration: appletAbilities.animations.speedFactor.current*4.1*appletAbilities.animations.duration.large
            easing.type: Easing.OutQuad
        }
    }

    //////////////////////////BEGIN states
    // Alignment
    // 0-Center, 1-Left, 2-Right, 3-Top, 4-Bottom
    states: [
        ///Left Edge
        State {
            name: "leftCenter"
            when: flickableContainer.alignment === LatteCore.Types.LeftEdgeCenterAlign

            AnchorChanges {
                target: flickableContainer
                anchors{ top:undefined; bottom:undefined; left:parent.left; right:undefined; horizontalCenter:undefined; verticalCenter:parent.verticalCenter}
            }
            PropertyChanges{
                target: flickableContainer;
                anchors.leftMargin: 0;    anchors.rightMargin:0;     anchors.topMargin:0;    anchors.bottomMargin:0;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: flickableContainer.offset;
            }
        },
        State {
            name: "leftTop"
            when: flickableContainer.alignment === LatteCore.Types.LeftEdgeTopAlign

            AnchorChanges {
                target: flickableContainer
                anchors{ top:parent.top; bottom:undefined; left:parent.left; right:undefined; horizontalCenter:undefined; verticalCenter:undefined}
            }
            PropertyChanges{
                target: flickableContainer;
                anchors.leftMargin: 0;    anchors.rightMargin:0;     anchors.topMargin:flickableContainer.offset;    anchors.bottomMargin:flickableContainer.lastMargin;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }
        },
        State {
            name: "leftBottom"
            when: flickableContainer.alignment === LatteCore.Types.LeftEdgeBottomAlign

            AnchorChanges {
                target: flickableContainer
                anchors{ top:undefined; bottom:parent.bottom; left:parent.left; right:undefined; horizontalCenter:undefined; verticalCenter:undefined}
            }
            PropertyChanges{
                target: flickableContainer;
                anchors.leftMargin: 0;    anchors.rightMargin:0;     anchors.topMargin:flickableContainer.lastMargin;    anchors.bottomMargin:flickableContainer.offset;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }
        },
        ///Right Edge
        State {
            name: "rightCenter"
            when: flickableContainer.alignment === LatteCore.Types.RightEdgeCenterAlign

            AnchorChanges {
                target: flickableContainer
                anchors{ top:undefined; bottom:undefined; left:undefined; right:parent.right; horizontalCenter:undefined; verticalCenter:parent.verticalCenter}
            }
            PropertyChanges{
                target: flickableContainer;
                anchors.leftMargin: 0;    anchors.rightMargin:0;     anchors.topMargin:0;    anchors.bottomMargin:0;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: flickableContainer.offset;
            }
        },
        State {
            name: "rightTop"
            when: flickableContainer.alignment === LatteCore.Types.RightEdgeTopAlign

            AnchorChanges {
                target: flickableContainer
                anchors{ top:parent.top; bottom:undefined; left:undefined; right:parent.right; horizontalCenter:undefined; verticalCenter:undefined}
            }
            PropertyChanges{
                target: flickableContainer;
                anchors.leftMargin: 0;    anchors.rightMargin:0;     anchors.topMargin:flickableContainer.offset;    anchors.bottomMargin:flickableContainer.lastMargin;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }
        },
        State {
            name: "rightBottom"
            when: flickableContainer.alignment === LatteCore.Types.RightEdgeBottomAlign

            AnchorChanges {
                target: flickableContainer
                anchors{ top:undefined; bottom:parent.bottom; left:undefined; right:parent.right; horizontalCenter:undefined; verticalCenter:undefined}
            }
            PropertyChanges{
                target: flickableContainer;
                anchors.leftMargin: 0;    anchors.rightMargin:0;     anchors.topMargin:flickableContainer.lastMargin;    anchors.bottomMargin:flickableContainer.offset;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }
        },
        ///Bottom Edge
        State {
            name: "bottomCenter"
            when: flickableContainer.alignment === LatteCore.Types.BottomEdgeCenterAlign

            AnchorChanges {
                target: flickableContainer
                anchors{ top:undefined; bottom:parent.bottom; left:undefined; right:undefined; horizontalCenter:parent.horizontalCenter; verticalCenter:undefined}
            }
            PropertyChanges{
                target: flickableContainer;
                anchors.leftMargin: 0;    anchors.rightMargin:0;     anchors.topMargin:0;    anchors.bottomMargin:0;
                anchors.horizontalCenterOffset: flickableContainer.offset; anchors.verticalCenterOffset: 0;
            }
        },
        State {
            name: "bottomLeft"
            when: flickableContainer.alignment === LatteCore.Types.BottomEdgeLeftAlign

            AnchorChanges {
                target: flickableContainer
                anchors{ top:undefined; bottom:parent.bottom; left:parent.left; right:undefined; horizontalCenter:undefined; verticalCenter:undefined}
            }
            PropertyChanges{
                target: flickableContainer;
                anchors.leftMargin: flickableContainer.offset;    anchors.rightMargin:flickableContainer.lastMargin;     anchors.topMargin:0;    anchors.bottomMargin:0;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }
        },
        State {
            name: "bottomRight"
            when: flickableContainer.alignment === LatteCore.Types.BottomEdgeRightAlign

            AnchorChanges {
                target: flickableContainer
                anchors{ top:undefined; bottom:parent.bottom; left:undefined; right:parent.right; horizontalCenter:undefined; verticalCenter:undefined}
            }
            PropertyChanges{
                target: flickableContainer;
                anchors.leftMargin: flickableContainer.lastMargin;    anchors.rightMargin:flickableContainer.offset;     anchors.topMargin:0;    anchors.bottomMargin:0;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }
        },
        ///Top Edge
        State {
            name: "topCenter"
            when: flickableContainer.alignment === LatteCore.Types.TopEdgeCenterAlign

            AnchorChanges {
                target: flickableContainer
                anchors{ top:parent.top; bottom:undefined; left:undefined; right:undefined; horizontalCenter:parent.horizontalCenter; verticalCenter:undefined}
            }
            PropertyChanges{
                target: flickableContainer;
                anchors.leftMargin: 0;    anchors.rightMargin:0;     anchors.topMargin:0;    anchors.bottomMargin:0;
                anchors.horizontalCenterOffset: flickableContainer.offset; anchors.verticalCenterOffset: 0;
            }
        },
        State {
            name: "topLeft"
            when: flickableContainer.alignment === LatteCore.Types.TopEdgeLeftAlign

            AnchorChanges {
                target: flickableContainer
                anchors{ top:parent.top; bottom:undefined; left:parent.left; right:undefined; horizontalCenter:undefined; verticalCenter:undefined}
            }
            PropertyChanges{
                target: flickableContainer;
                anchors.leftMargin: flickableContainer.offset;    anchors.rightMargin:flickableContainer.lastMargin;     anchors.topMargin:0;    anchors.bottomMargin:0;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }
        },
        State {
            name: "topRight"
            when: flickableContainer.alignment === LatteCore.Types.TopEdgeRightAlign

            AnchorChanges {
                target: flickableContainer
                anchors{ top:parent.top; bottom:undefined; left:undefined; right:parent.right; horizontalCenter:undefined; verticalCenter:undefined}
            }
            PropertyChanges{
                target: flickableContainer;
                anchors.leftMargin: flickableContainer.lastMargin;    anchors.rightMargin:flickableContainer.offset;     anchors.topMargin:0;    anchors.bottomMargin:0;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }
        }
    ]
}
