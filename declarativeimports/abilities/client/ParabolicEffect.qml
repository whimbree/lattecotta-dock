/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.7

import org.kde.plasma.plasmoid 2.0
import org.kde.plasma.core 2.0 as PlasmaCore

import org.kde.latte.core 0.2 as LatteCore
import org.kde.latte.abilities.definition 0.1 as AbilityDefinition

AbilityDefinition.ParabolicEffect {
    id: parabolic
    property Item bridge: null
    property Item indexer: null
    property Item layout: null

    isEnabled: ref.parabolic.isEnabled
    factor: ref.parabolic.factor
    restoreZoomIsBlocked: bridge ? (bridge.parabolic.host.restoreZoomIsBlocked || local.restoreZoomIsBlocked) : local.restoreZoomIsBlocked
    currentParabolicItem: ref.parabolic.currentParabolicItem
    spread: ref.parabolic.spread

    readonly property bool isActive: bridge !== null
    //! private properties can not go to definition because can not be made readonly in there
    //! special care must be taken in order to be redefined in local properties
    readonly property bool directRenderingEnabled: ref.parabolic._privates.directRenderingEnabled
    readonly property bool horizontal: Plasmoid.formFactor === PlasmaCore.Types.Horizontal
    readonly property bool isHovered: {
        if (bridge && bridge.parabolic.host.currentParabolicItem) {
            return bridge.parabolic.host.currentParabolicItem.parent.parent.parent === layout;
        }

        return false;
    }

    readonly property AbilityDefinition.ParabolicEffect local: AbilityDefinition.ParabolicEffect {
        id: _localref
        readonly property bool directRenderingEnabled: _localref._privates.directRenderingEnabled
        spread: 3
    }

    Item {
        id: ref
        readonly property Item parabolic: parabolic.bridge ? parabolic.bridge.parabolic.host : parabolic.local
    }

    onIsActiveChanged: {
        if (isActive) {
            bridge.parabolic.client = parabolic;
        }
    }

    Component.onCompleted: {
        if (isActive) {
            bridge.parabolic.client = parabolic;
        }
    }

    Component.onDestruction: {
        if (isActive) {
            bridge.parabolic.client = null;
        }
    }

    Connections {
        target: parabolic
        function onRestoreZoomIsBlockedChanged() {
            if (!(parabolic.bridge || parabolic.bridge.host)) {
                if (!parabolic.restoreZoomIsBlocked) {
                    parabolic.startRestoreZoomTimer();
                } else {
                    parabolic.stopRestoreZoomTimer();
                }
            }
        }

        function onCurrentParabolicItemChanged() {
            if (!parabolic.bridge || !parabolic.bridge.host) {
                if (!parabolic.currentParabolicItem) {
                    parabolic.startRestoreZoomTimer();
                } else {
                    parabolic.stopRestoreZoomTimer();
                }
            }
        }
    }

    function startRestoreZoomTimer(){
        if (restoreZoomIsBlocked) {
            return;
        }

        if (bridge) {
            bridge.parabolic.host.startRestoreZoomTimer();
        } else {
            restoreZoomTimer.start();
        }
    }

    function stopRestoreZoomTimer(){
        if (bridge) {
            bridge.parabolic.host.stopRestoreZoomTimer();
        } else {
            restoreZoomTimer.stop();
        }
    }

    function setDirectRenderingEnabled(value) {
        if (bridge) {
            bridge.parabolic.host.setDirectRenderingEnabled(value);
        } else {
            local._privates.directRenderingEnabled = value;
        }
    }

    function setCurrentParabolicItem(item) {
        if (bridge) {
            bridge.parabolic.host.setCurrentParabolicItem(item);
        } else {
            local.currentParabolicItem = item;
        }
    }

    //! EX-02 (docs/QML_EXTRACTION_PLAN.md): the routing computed by
    //! LatteCore.ParabolicRouter in one call instead of the per-item
    //! signal-decider recursion. The signals survive as the application
    //! mechanism only (exact apply + the clear-tail broadcast arm in
    //! ParabolicEventsArea); the bridge exports that sltTrack* used to
    //! filter out of raw emissions now come straight from the route
    //! result: an in-row clear emission exports [1], a stack leaving the
    //! row edge exports as-is.
    function applyParabolicEffect(itemIndex, itemMousePosition, itemLength) {
        var reversed = Qt.application.layoutDirection === Qt.RightToLeft && horizontal;
        var stacks = LatteCore.ParabolicMath.computeScales(itemMousePosition / itemLength, _spreadSteps, factor.zoom, reversed);

        routeScalesFromIndex(itemIndex+1, stacks.right, false);
        routeScalesFromIndex(itemIndex-1, stacks.left, true);

        return {leftScale:stacks.left[0], rightScale:stacks.right[0]};
    }

    function routeScalesFromIndex(entryIndex, newScales, islower) {
        //! task rows carry no edge spacers and no nested bridge clients;
        //! spacersAbsorbing is inert here
        var plan = LatteCore.ParabolicRouter.route(_routerRowOfTasks(),
                                                   entryIndex,
                                                   islower ? -1 : 1,
                                                   newScales,
                                                   _spreadSteps,
                                                   false);

        for (var i = 0; i < plan.actions.length; ++i) {
            var action = plan.actions[i];
            if (islower) {
                sglUpdateLowerItemScale(action.pos, action.stack);
            } else {
                sglUpdateHigherItemScale(action.pos, action.stack);
            }
        }

        if (bridge) {
            if (plan.clearEmissionPos >= 0) {
                //! the chain's sltTrack* forwarded every in-row clear-tail
                //! emission out through the bridge
                if (islower) {
                    bridge.parabolic.clientRequestUpdateLowerItemScale([1]);
                } else {
                    bridge.parabolic.clientRequestUpdateHigherItemScale([1]);
                }
            }

            if (plan.overflow.length > 0) {
                if (islower) {
                    bridge.parabolic.clientRequestUpdateLowerItemScale(plan.overflow);
                } else {
                    bridge.parabolic.clientRequestUpdateHigherItemScale(plan.overflow);
                }
            }
        }
    }

    //! positions 0..itemsCount-1 keyed by itemIndex; holes (mid-churn
    //! index inconsistencies) stay DeadStop, matching the chain where a
    //! missing index matched no slot and the live walk died
    function _routerRowOfTasks() {
        var count = indexer.itemsCount;
        var kinds = new Array(count);
        for (var i = 0; i < count; ++i) {
            kinds[i] = LatteCore.ParabolicRouter.DeadStop;
        }

        var children = indexer.layout.children;
        for (i = 0; i < children.length; ++i) {
            var item = children[i];
            if (!item || item.itemIndex === undefined || item.itemIndex < 0 || item.itemIndex >= count) {
                continue;
            }
            kinds[item.itemIndex] = (item.isSeparator || item.isHidden)
                    ? LatteCore.ParabolicRouter.Transparent
                    : LatteCore.ParabolicRouter.Normal;
        }

        return kinds;
    }

    function hostRequestUpdateLowerItemScale(newScales){
        //! function called from host: the stack enters this row at its
        //! highest index and travels down
        routeScalesFromIndex(indexer.itemsCount-1, newScales, true);
    }

    function hostRequestUpdateHigherItemScale(newScales){
        //! function called from host: the stack enters at index 0 and
        //! travels up
        routeScalesFromIndex(0, newScales, false);
    }

    function setCurrentParabolicItemIndex(index) {
        if (bridge) {
            bridge.parabolic.host.setCurrentParabolicItemIndex(index);
        }
    }

    function invkClearZoom() {
        if (parabolic.restoreZoomIsBlocked) {
            return
        }

        if (bridge) {
            bridge.parabolic.host.sglClearZoom();
        } else {
            parabolic.sglClearZoom();
        }
    }

    //! TIMERS

    //! Timer to check if the mouse is outside the applet in order to restore items scales to 1.0
    //! IMPORTANT ::: This timer should be used only when the Latte plasmoid is not inside a Latte dock
    Timer{
        id: restoreZoomTimer
        interval: 50

        onTriggered: {
            if(parabolic.bridge) {
                console.log("Plasmoid, restoreZoomTimer was called, even though it shouldn't...");
            }

            parabolic.setDirectRenderingEnabled(false);
            parabolic.invkClearZoom();
        }
    }
}
