/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.7

import org.kde.plasma.plasmoid 2.0
import org.kde.plasma.core 2.0 as PlasmaCore

import org.kde.latte.core 0.2 as LatteCore

import org.kde.latte.abilities.host 0.1 as AbilityHost

AbilityHost.ParabolicEffect {
    id: parabolic

    property Item animations: null
    property Item debug: null
    property Item layouts: null
    property QtObject view: null
    property QtObject settings: null

    readonly property bool horizontal: Plasmoid.formFactor === PlasmaCore.Types.Horizontal

    property bool restoreZoomIsBlockedFromApplet: false
    property int lastParabolicItemIndex: -1

    Connections {
        target: parabolic
        onRestoreZoomIsBlockedChanged: {
            if (!parabolic.restoreZoomIsBlocked) {
                parabolic.startRestoreZoomTimer();
            } else {
                parabolic.stopRestoreZoomTimer();
            }

        }

        onCurrentParabolicItemChanged: {
            if (!currentParabolicItem) {
                parabolic.startRestoreZoomTimer();
            } else {
                parabolic.stopRestoreZoomTimer();
            }
        }
    }

    Connections{
        target: parabolic.view && parabolic.view.visibility ? parabolic.view.visibility : root
        ignoreUnknownSignals : true
        onContainsMouseChanged: {
            if (!parabolic.view.visibility.containsMouse && !restoreZoomTimer.running) {
                parabolic.startRestoreZoomTimer()
            }
        }
    }

    Connections {
        target: parabolic.layouts
        onContextMenuIsShownChanged: {
            if (!parabolic.layouts.contextMenuIsShown && !restoreZoomTimer.running) {
                parabolic.startRestoreZoomTimer();
            }
        }
    }

    //! do not update during dragging/moving applets inConfigureAppletsMode
    readonly property bool isBindingUpdateEnabled: !(root.dragOverlay && root.dragOverlay.pressed)

    Binding{
        target: parabolic
        property: "restoreZoomIsBlockedFromApplet"
        when: isBindingUpdateEnabled
        restoreMode: Binding.RestoreNone
        value: {
            var grid;

            for (var l=0; l<=2; ++l) {
                if (l===0) {
                    grid = layouts.startLayout;
                } else if (l===1) {
                    grid = layouts.mainLayout;
                } else if (l===2) {
                    grid = layouts.endLayout;
                }

                for (var i=0; i<grid.children.length; ++i){
                    var appletItem = grid.children[i];
                    if (appletItem
                            && appletItem.communicator
                            && appletItem.communicator.parabolicEffectIsSupported
                            && appletItem.communicator.bridge.parabolic.client.local.restoreZoomIsBlocked) {
                        return true;
                    }
                }
            }

            return false;
        }
    }

    //! EX-02 (docs/QML_EXTRACTION_PLAN.md): the neighbor scale-stack
    //! routing computed by LatteCore.ParabolicRouter in one call instead of
    //! the per-item signal-decider recursion. The signals survive only as
    //! the application mechanism: every emission is either exactly-targeted
    //! (apply newScales[0] / hand a stack to a bridge client as-received)
    //! or the single clear-tail [1] emission whose receiver-side broadcast
    //! arm clears every item beyond it - including items in OTHER layouts
    //! across the index gaps, which is why the clear is one emission and
    //! not per-position applications.
    function applyParabolicEffect(itemIndex, itemMousePosition, itemLength) {
        var reversed = Qt.application.layoutDirection === Qt.RightToLeft && horizontal;
        var stacks = LatteCore.ParabolicMath.computeScales(itemMousePosition / itemLength, _spreadSteps, factor.zoom, reversed);

        routeFromIndex(itemIndex+1, stacks.right, false);
        routeFromIndex(itemIndex-1, stacks.left, true);

        return {leftScale:stacks.left[0], rightScale:stacks.right[0]};
    }

    //! route a scale stack entering at entryIndex (also the bridge re-entry
    //! point: clientRequestUpdate* calls this at appletIndex-/+1)
    function routeFromIndex(entryIndex, newScales, islower) {
        var info = _rowInfoFor(entryIndex);

        if (!info) {
            //! a gap index between layouts: the chain emitted here too -
            //! nothing matches exactly, only clear-tail broadcasts act
            _emitScales(entryIndex, newScales, islower);
            return;
        }

        var absorbing = root.myView.alignment === LatteCore.Types.Center
                || root.myView.alignment === LatteCore.Types.Justify;
        var plan = LatteCore.ParabolicRouter.route(info.kinds,
                                                   entryIndex - info.rowBase,
                                                   islower ? -1 : 1,
                                                   newScales,
                                                   _spreadSteps,
                                                   absorbing);

        for (var i = 0; i < plan.actions.length; ++i) {
            var action = plan.actions[i];
            if (action.kind === "emit") {
                _emitScales(info.rowBase + action.pos, action.stack, islower);
            } else {
                //! spacers left the signal graph: they only ever react when
                //! exactly targeted (no broadcast arm, Qt5-inherited)
                info.items[action.pos].applyParabolicAbsorb(action.factor);
            }
        }

        if (plan.overflow.length > 0) {
            //! the walk left the row edge: emit at the boundary index, as
            //! the chain did - live stacks match nobody there, clear-tails
            //! broadcast into the neighbor layouts
            _emitScales(islower ? info.rowBase - 1 : info.rowBase + info.kinds.length,
                        plan.overflow, islower);
        }
    }

    function _emitScales(delegateIndex, newScales, islower) {
        if (islower) {
            parabolic.sglUpdateLowerItemScale(delegateIndex, newScales);
        } else {
            parabolic.sglUpdateHigherItemScale(delegateIndex, newScales);
        }
    }

    //! the row snapshot of the layout containing entryIndex: kinds for the
    //! router, the items themselves for spacer absorb calls. Index holes
    //! (mid-churn) stay DeadStop, which is what the chain did at a missing
    //! index: the live walk dies there
    function _rowInfoFor(entryIndex) {
        var grids = [layouts.startLayout, layouts.mainLayout, layouts.endLayout];

        for (var g = 0; g < grids.length; ++g) {
            var grid = grids[g];
            var lo = -1;
            var hi = -1;

            for (var i = 0; i < grid.children.length; ++i) {
                var child = grid.children[i];
                if (!child || child.index === undefined || child.index < 0) {
                    continue;
                }
                if (lo === -1 || child.index < lo) {
                    lo = child.index;
                }
                if (child.index > hi) {
                    hi = child.index;
                }
            }

            if (lo === -1 || entryIndex < lo || entryIndex > hi) {
                continue;
            }

            var size = hi - lo + 1;
            var kinds = new Array(size);
            var items = new Array(size);
            for (i = 0; i < size; ++i) {
                kinds[i] = LatteCore.ParabolicRouter.DeadStop;
                items[i] = null;
            }

            for (i = 0; i < grid.children.length; ++i) {
                child = grid.children[i];
                if (!child || child.index === undefined || child.index < 0) {
                    continue;
                }
                var pos = child.index - lo;
                items[pos] = child;
                kinds[pos] = _kindOf(child);
            }

            return {rowBase: lo, kinds: kinds, items: items};
        }

        return null;
    }

    function _kindOf(item) {
        if (item.isParabolicEdgeSpacer === true) {
            return LatteCore.ParabolicRouter.EdgeSpacer;
        }

        if (!item.hasParabolicMessagesHandler) {
            //! no ParabolicArea instance means no connected slots: a live
            //! stack dies at this item (zoom-unsupported applet with thin
            //! tooltips disabled)
            return LatteCore.ParabolicRouter.DeadStop;
        }

        if (item.communicator && item.communicator.parabolicEffectIsSupported) {
            return LatteCore.ParabolicRouter.BridgeClient;
        }

        if (item.isSeparator || item.isMarginsAreaSeparator || item.isHidden) {
            return LatteCore.ParabolicRouter.Transparent;
        }

        return LatteCore.ParabolicRouter.Normal;
    }

    function startRestoreZoomTimer(){
        if (restoreZoomIsBlocked) {
            return;
        }

        restoreZoomTimer.start();
    }

    function stopRestoreZoomTimer(){
        if (restoreZoomTimer.running) {
            restoreZoomTimer.stop();
        }
    }

    function setDirectRenderingEnabled(value) {
        _privates.directRenderingEnabled = value;
    }

    function setCurrentParabolicItem(item) {
        view.parabolic.currentItem = item;
    }

    function setCurrentParabolicItemIndex(index) {
        if (!directRenderingEnabled
                && lastParabolicItemIndex > -1
                && index > -1
                && Math.abs(lastParabolicItemIndex-index) >=2 ) {
            //! rapid movement
            setDirectRenderingEnabled(true);
        }

        lastParabolicItemIndex = index;
    }

    //! TIMERS

    //! Timer to check if the mouse is still outside the latteView in order to restore applets scales to 1.0
    Timer{
        id: restoreZoomTimer
        interval: 50

        onTriggered: {
            if (parabolic.restoreZoomIsBlocked || currentParabolicItem) {
                return;
            }

            parabolic.lastParabolicItemIndex = -1;
            parabolic.setDirectRenderingEnabled(false);
            parabolic.sglClearZoom();

            if (debug.timersEnabled) {
                console.log("containment timer: RestoreZoomTimer called...");
            }
        }
    }
}
