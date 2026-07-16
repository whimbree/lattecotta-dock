/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.7
import org.kde.plasma.core 2.0 as PlasmaCore

Item {
    id: _parabolicArea
    signal parabolicEntered(real mouseX, real mouseY);
    signal parabolicMove(real mouseX, real mouseY);
    signal parabolicExited();

    readonly property bool containsMouse: (appletItem.parabolic.currentParabolicItem === _parabolicArea) || parabolicMouseArea.containsMouse

    readonly property bool hasParabolicMessagesEnabled: parabolicAreaLoader.hasParabolicMessagesEnabled
    readonly property bool isParabolicEnabled: parabolicAreaLoader.isParabolicEnabled
    readonly property bool isThinTooltipEnabled: parabolicAreaLoader.isThinTooltipEnabled    

    property real length: root.isHorizontal ? appletItem.width : appletItem.height
    property var lastMousePoint: { "x": 0, "y": 0 }

    MouseArea {
        id: parabolicMouseArea
        anchors.fill: parent
        enabled: visible
        hoverEnabled: true
        visible: appletItem.parabolicEffectIsSupported
                 && !communicator.indexerIsSupported
                 && appletItem.parabolic.currentParabolicItem !== _parabolicArea

        // VisibilityManager.qml tries to workaround faulty onEntered() signals from this MouseArea
        // by specifying inputThickness when ParabolicEffect is applied. (inputThickness->animated scenario)
        //
        // Such is a case is when dock is at the bottom and user moves its
        // mouse at top edge of parabolized item. When mouse exits
        // slightly ParabolicMouseArea this mousearea here gets a mouseEntered
        // signal even though it should not and immediately gets also
        // a mouseExited signal to correct things. This happens exactly
        // after Paraboli.sglClearZoom() signal has been triggered.

        onEntered: {
            appletItem.parabolic.setCurrentParabolicItem(_parabolicArea);

            if (isParabolicEnabled) {
                var vIndex = appletItem.indexer.visibleIndex(index);
                appletItem.parabolic.setCurrentParabolicItemIndex(vIndex);
            }

            //! mouseX/Y can be trusted in that case in comparison to tasks that the relevant ParabolicAreaMouseArea does not
            _parabolicArea.parabolicEntered(mouseX, mouseY);
        }
    }

    onParabolicEntered: (mouseX, mouseY) => {
        lastMousePoint.x = mouseX;
        lastMousePoint.y = mouseY;

        if (isThinTooltipEnabled && !(isSeparator || isSpacer || isMarginsAreaSeparator)) {
            appletItem.thinTooltip.show(appletItem.tooltipVisualParent, applet.plasmoid.title);
        }

        if (restoreAnimation.running) {
            restoreAnimation.stop();
        }

        if (!appletItem.myView.isShownFully
                || appletItem.originalAppletBehavior
                || !appletItem.parabolicEffectIsSupported
                || communicator.requires.parabolicEffectLocked
                || communicator.indexerIsSupported) {
            return;
        }

        if (isParabolicEnabled) {
            if (root.isHorizontal){
                appletItem.layouts.currentSpot = Math.round(mouseX);
                calculateParabolicScales(mouseX);
            } else{
                appletItem.layouts.currentSpot = Math.round(mouseY);
                calculateParabolicScales(mouseY);
            }
        }
    }

    onParabolicMove: (mouseX, mouseY) => {
        lastMousePoint.x = mouseX;
        lastMousePoint.y = mouseY;

        if (!appletItem.myView.isShownFully
                || appletItem.originalAppletBehavior
                || !appletItem.parabolicEffectIsSupported
                || communicator.requires.parabolicEffectLocked
                || communicator.indexerIsSupported) {
            return;
        }

        if (isParabolicEnabled) {
            if( ((wrapper.zoomScale === 1 || wrapper.zoomScale === appletItem.parabolic.factor.zoom) && !parabolic.directRenderingEnabled) || parabolic.directRenderingEnabled) {
                if (root.isHorizontal){
                    var step = Math.abs(appletItem.layouts.currentSpot-mouseX);
                    if (step >= appletItem.animations.hoverPixelSensitivity){
                        appletItem.layouts.currentSpot = Math.round(mouseX);
                        calculateParabolicScales(mouseX);
                    }
                }
                else{
                    var step = Math.abs(appletItem.layouts.currentSpot-mouseY);
                    if (step >= appletItem.animations.hoverPixelSensitivity){
                        appletItem.layouts.currentSpot = Math.round(mouseY);
                        calculateParabolicScales(mouseY);
                    }
                }
            }
        }
    }

    onParabolicExited: {
        if (isThinTooltipEnabled) {
            appletItem.thinTooltip.hide(appletItem.tooltipVisualParent);
        }
    }

    Connections{
        target: appletItem.myView

        //! During dock sliding-in because the parabolic effect isn't triggered
        //! immediately but we wait first the dock to go to its final normal
        //! place we might miss the activation of the parabolic effect.
        //! By catching that signal we are trying to solve this.
        onIsShownFullyChanged: {
            if (appletItem.myView.isShownFully && _parabolicArea.containsMouse) {
                _parabolicArea.parabolicMove(_parabolicArea.lastMousePoint.x, _parabolicArea.lastMousePoint.y);
            }
        }
    }

    function calculateParabolicScales(currentMousePosition){
        if (parabolic.factor.zoom===1 || parabolic.restoreZoomIsBlocked) {
            return;
        }

        if (wrapper.zoomScale === 1 && (appletItem.firstAppletInContainer || appletItem.lastAppletInContainer)) {
            //! first hover of first or last items in container
            //! this way we make sure that neighbour items will increase their zoom faster
            var substep = length/4;
            var center = length/2;
            currentMousePosition = Math.min(Math.max(currentMousePosition, center-substep), center+substep);
        }

        //use the new parabolic effect manager in order to handle all parabolic effect messages
        var scales = parabolic.applyParabolicEffect(index, currentMousePosition, length);
        wrapper.zoomScale = parabolic.factor.zoom;
    } //scale


    function updateScale(nIndex, nScale){
        if(appletItem && (appletItem.index === nIndex) /*&& !appletItem.containsMouse*/){ /*disable it in order to increase parabolic effect responsiveness*/
            if ( (parabolicEffectIsSupported && !appletItem.originalAppletBehavior && !appletItem.communicator.indexerIsSupported)
                    && (applet && applet.plasmoid.status !== PlasmaCore.Types.HiddenStatus)){
                    wrapper.zoomScale = Math.max(1, nScale);
            }
        }
    }

    //! EX-02 (docs/QML_EXTRACTION_PLAN.md): application-only arms; the
    //! routing decisions (consume/forward/absorb/stop) live in
    //! LatteCore.ParabolicRouter, driven by the parabolic ability. Exact
    //! match applies newScales[0] (or hands the stack to a bridge client
    //! as-received); a clear-tail [1] emission additionally clears every
    //! item beyond its position through the broadcast arm - one emission
    //! reaches items in other layouts across the index gaps.
    function sltApplyItemScale(delegateIndex, newScales, islower) {
        var clearrequested = (newScales.length===1) && (newScales[0]===1);

        if (delegateIndex === appletItem.index) {
            if (communicator.parabolicEffectIsSupported) {
                if (islower) {
                    communicator.bridge.parabolic.client.hostRequestUpdateLowerItemScale(newScales);
                } else {
                    communicator.bridge.parabolic.client.hostRequestUpdateHigherItemScale(newScales);
                }
            } else {
                updateScale(appletItem.index, newScales[0]);
            }
            return;
        }

        if (clearrequested
                && (islower ? appletItem.index < delegateIndex : appletItem.index > delegateIndex)) {
            if (communicator.parabolicEffectIsSupported) {
                if (islower) {
                    communicator.bridge.parabolic.client.hostRequestUpdateLowerItemScale(newScales);
                } else {
                    communicator.bridge.parabolic.client.hostRequestUpdateHigherItemScale(newScales);
                }
            } else {
                updateScale(appletItem.index, 1);
            }
        }
    }

    function sltUpdateLowerItemScale(delegateIndex, newScales) {
        sltApplyItemScale(delegateIndex, newScales, true);
    }

    function sltUpdateHigherItemScale(delegateIndex, newScales) {
        sltApplyItemScale(delegateIndex, newScales, false);
    }

    Component.onCompleted: {
        parabolic.sglUpdateLowerItemScale.connect(sltUpdateLowerItemScale);
        parabolic.sglUpdateHigherItemScale.connect(sltUpdateHigherItemScale);
    }

    Component.onDestruction: {
        parabolic.sglUpdateLowerItemScale.disconnect(sltUpdateLowerItemScale);
        parabolic.sglUpdateHigherItemScale.disconnect(sltUpdateHigherItemScale);
    }
}
