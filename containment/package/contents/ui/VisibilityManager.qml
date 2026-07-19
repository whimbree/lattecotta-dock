/*
    SPDX-FileCopyrightText: 2016 Smith AR <audoban@openmailbox.org>
    SPDX-FileCopyrightText: 2016 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.1
import QtQuick.Window 2.2

import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.plasma.plasmoid 2.0

import org.kde.latte.core 0.2 as LatteCore
import org.kde.latte.private.containment 0.1 as LatteContainment

Item{
    id: manager
    anchors.fill: parent

    property QtObject window

    property bool isFloatingInClientSide: !root.behaveAsPlasmaPanel
                                          && screenEdgeMarginEnabled
                                          && !root.floatingInternalGapIsForced
                                          && !inSlidingIn
                                          && !inSlidingOut

    property int animationSpeed: LatteCore.WindowSystem.compositingActive ?
                                     (root.editMode ? 400 : animations.speedFactor.current * 1.62 * animations.duration.large) : 0

    property bool inClientSideScreenEdgeSliding: root.behaveAsDockWithMask && hideThickScreenGap
    property bool inNormalState: ((animations.needBothAxis.count === 0) && (animations.needLength.count === 0))
                                 || (latteView && latteView.visibility.isHidden && !latteView.visibility.containsMouse && animations.needThickness.count === 0)
    property bool inRelocationAnimation: latteView && latteView.positioner && latteView.positioner.inRelocationAnimation

    property bool inSlidingIn: false //necessary because of its init structure
    property alias inSlidingOut: slidingAnimationAutoHiddenOut.running
    property bool inRelocationHiding: false

    readonly property bool isSinkedEventEnabled: !(parabolic.isEnabled && (animations.needBothAxis.count>0 || animations.needLength.count>0))
                                                 && myView.isShownFully

    property int length: root.isVertical ?  Screen.height : Screen.width   //screenGeometry.height : screenGeometry.width

    property int slidingOutToPos: {
        if (root.behaveAsPlasmaPanel) {
            var edgeMargin = screenEdgeMarginEnabled ? Plasmoid.configuration.screenEdgeMargin : 0
            var thickmarg = latteView.visibility.isSidebar ? 0 : 1;

            return root.isHorizontal ? root.height + edgeMargin - thickmarg : root.width + edgeMargin - thickmarg;
        } else {
            var topOrLeftEdge = ((Plasmoid.location===PlasmaCore.Types.LeftEdge)||(Plasmoid.location===PlasmaCore.Types.TopEdge));
            return (topOrLeftEdge ? -metrics.mask.thickness.normal : metrics.mask.thickness.normal);
        }
    }

    //! when Latte behaves as Plasma panel
    property int thicknessAsPanel: metrics.totals.thickness

    property Item layouts: null

    property bool updateIsEnabled: autosize.inCalculatedIconSize && !inSlidingIn && !inSlidingOut && !inRelocationHiding

    //! The per-edge rect tables of updateMaskArea/updateInputGeometry live
    //! in the MaskGeometry core (containment/plugin/units/maskgeometry.h,
    //! EX-10 in docs/tracking/QML_EXTRACTION_PLAN.md; pinned by tests/units/
    //! maskgeometrytest.cpp and tests/qml/tst_maskgeometry.qml). The bridge
    //! maps the core's decisions onto the effects protocol's sentinel
    //! rects; this file keeps the signal plumbing, the update gates and
    //! the actuating assignments.
    LatteContainment.MaskGeometryBridge {
        id: maskGeometry
    }

    Connections{
        target: background.totals
        function onVisualLengthChanged() {
            manager.updateMaskArea();
        }
        function onVisualThicknessChanged() {
            manager.updateMaskArea();
        }
    }

    Connections{
        target: background.shadows
        function onHeadThicknessChanged() {
            manager.updateMaskArea();
        }
    }

    Connections{
        target: latteView ? latteView : null
        function onXChanged() {
            manager.updateMaskArea();
        }
        function onYChanged() {
            manager.updateMaskArea();
        }
        function onWidthChanged() {
            manager.updateMaskArea();
        }
        function onHeightChanged() {
            manager.updateMaskArea();
        }
    }

    Connections{
        target: animations.needBothAxis
        function onCountChanged() {
            manager.updateMaskArea();
        }
    }

    Connections{
        target: animations.needLength
        function onCountChanged() {
            manager.updateMaskArea();
        }
    }

    Connections{
        target: animations.needThickness
        function onCountChanged() {
            manager.updateMaskArea();
        }
    }

    Connections{
        target: layoutsManager
        function onCurrentLayoutIsSwitching(layoutName: string) {
            if (LatteCore.WindowSystem.compositingActive && latteView && latteView.layout && latteView.layout.name === layoutName) {
                parabolic.sglClearZoom();
            }
        }
    }

    Connections {
        target: metrics.mask.thickness
        function onMaxZoomedChanged() {
            manager.updateMaskArea();
        }
    }

    Connections {
        target: root.myView
        function onInRelocationAnimationChanged() {
            if (!root.myView.inRelocationAnimation) {
                manager.updateMaskArea();
            }
        }
    }

    Connections {
        target: latteView ? latteView.effects : null
        function onRectChanged() {
            manager.updateMaskArea();
        }
    }

    Connections {
        target: LatteCore.WindowSystem
        function onCompositingActiveChanged() {
            manager.updateMaskArea();
        }
    }

    onIsFloatingInClientSideChanged: manager.updateMaskArea();

    onInNormalStateChanged: {
        if (manager.inNormalState) {
            manager.updateMaskArea();
        }
    }

    onInSlidingInChanged: {
        if (latteView && !manager.inSlidingIn && latteView.positioner.inRelocationShowing) {
            latteView.positioner.inRelocationShowing = false;
        }
    }

    onUpdateIsEnabledChanged: {
        if (manager.updateIsEnabled) {
            manager.updateMaskArea();
        }
    }

    function slotContainsMouseChanged() : void {
        if(latteView.visibility.containsMouse && latteView.visibility.mode !== LatteCore.Types.SidebarOnDemand) {
            manager.updateMaskArea();

            if (slidingAnimationAutoHiddenOut.running && !manager.inRelocationHiding) {
                manager.slotMustBeShown();
            }
        }
    }

    function slotMustBeShown() : void {
        if (root.inStartup) {
            slidingAnimationAutoHiddenIn.init();
            return;
        }

        //! WindowsCanCover case
        if (latteView && latteView.visibility.mode === LatteCore.Types.WindowsCanCover) {
            latteView.visibility.setViewOnFrontLayer();
        }

        if (!latteView.visibility.isHidden && latteView.positioner.inSlideAnimation) {
            // Do not update when Positioner mid-slide animation takes place, for example:
            // 1. Latte panel is hiding its floating gap for maximized window
            // 2. the user clicks on an applet popup.
            // 3. Applet popups showing/hiding are triggering hidingIsBlockedChanged() signals.
            // 4. hidingIsBlockedChanged() signals create mustBeShown events when visibility::hidingIsBlocked() is not enabled.
            return;
        }

        //! Normal Dodge/AutoHide case
        if (!slidingAnimationAutoHiddenIn.running
                && !manager.inRelocationHiding
                && (latteView.visibility.isHidden || slidingAnimationAutoHiddenOut.running /*it is not already shown or is trying to hide*/)){
            slidingAnimationAutoHiddenIn.init();
        }
    }

    function slotMustBeHide() : void {
        if (root.inStartup) {
            slidingAnimationAutoHiddenOut.init();
            return;
        }

        if (manager.inSlidingIn && !manager.inRelocationHiding) {
            /*consider hiding after sliding in has finished*/
            return;
        }

        if (latteView && latteView.visibility.mode === LatteCore.Types.WindowsCanCover) {
            latteView.visibility.setViewOnBackLayer();
            return;
        }

        //! Normal Dodge/AutoHide case
        if (!slidingAnimationAutoHiddenOut.running
                && !latteView.visibility.blockHiding
                && (!latteView.visibility.containsMouse || latteView.visibility.mode === LatteCore.Types.SidebarOnDemand /*for SidebarOnDemand mouse should be ignored on hiding*/)
                && (!latteView.visibility.isHidden || slidingAnimationAutoHiddenIn.running /*it is not already hidden or is trying to show*/)) {
            slidingAnimationAutoHiddenOut.init();
        }
    }

    //! functions used for sliding out/in during location/screen changes
    function slotHideDockDuringLocationChange() : void {
        manager.inRelocationHiding = true;

        if(!slidingAnimationAutoHiddenOut.running) {
            slidingAnimationAutoHiddenOut.init();
        }
    }

    function slotShowDockAfterLocationChange() : void {
        slidingAnimationAutoHiddenIn.init();
    }

    function sendHideDockDuringLocationChangeFinished() : void {
        latteView.positioner.hidingForRelocationFinished();
    }

    function sendSlidingOutAnimationEnded() : void {
        latteView.visibility.isHidden = true;

        if (debug.maskEnabled) {
            console.log("hiding animation ended...");
        }

        manager.sendHideDockDuringLocationChangeFinished();
    }

    ///test maskArea
    function updateMaskArea() : void {
        if (!latteView || !root.viewIsAvailable) {
            return;
        }

        // debug maskArea criteria
        if (debug.maskEnabled) {
            console.log(animations.needBothAxis.count + ", " + animations.needLength.count + ", " +
                        animations.needThickness.count + ", " + latteView.visibility.isHidden);
        }

        if (!latteView.visibility.isHidden && manager.updateIsEnabled && manager.inNormalState) {
            //! Important: Local Geometry must not be updated when view ISHIDDEN
            //! because it breaks Dodge(s) modes in such case
            latteView.localGeometry = maskGeometry.localGeometryFor(Plasmoid.location,
                                                                    latteView.behaveAsPlasmaPanel,
                                                                    root.width, root.height,
                                                                    latteView.width, latteView.height,
                                                                    latteView.effects.rect,
                                                                    metrics.totals.thickness,
                                                                    metrics.mask.screenEdge);
        }

        //! Input Mask
        if (manager.updateIsEnabled) {
            manager.updateInputGeometry();
        }
    }

    function updateInputGeometry() : void {
        //! latteView is still null when this runs during startup, before the
        //! C++ View wrapper is injected
        if (!latteView) {
            return;
        }

        //! The parabolicAnimating band (needBothAxis.count > 0) is upstream
        //! 11f42978's workaround for faulty onEntered() signals from
        //! ParabolicMouseArea right after sglClearZoom: while zoomed, input
        //! covers the zoomedForItems thickness over the full length axis.
        latteView.effects.inputMask = maskGeometry.inputMaskFor(Plasmoid.location,
                                                                LatteCore.WindowSystem.compositingActive,
                                                                latteView.behaveAsPlasmaPanel,
                                                                latteView.visibility.isHidden,
                                                                latteView.visibility.isSidebar,
                                                                animations.needBothAxis.count > 0,
                                                                root.hasFloatingGapInputEventsDisabled,
                                                                metrics.mask.thickness.hidden,
                                                                metrics.mask.thickness.zoomedForItems,
                                                                metrics.margin.screenEdge,
                                                                metrics.totals.thickness,
                                                                metrics.mask.screenEdge,
                                                                latteView.localGeometry,
                                                                root.width, root.height,
                                                                latteView.width, latteView.height);
    }

    Loader{
        anchors.fill: parent
        active: debug.graphicsEnabled

        sourceComponent: Item{
            anchors.fill:parent

            Rectangle{
                id: windowBackground
                anchors.fill: parent
                border.color: "red"
                border.width: 1
                color: "transparent"
            }

            Rectangle{
                x: latteView ? latteView.effects.mask.x : -1
                y: latteView ? latteView.effects.mask.y : -1
                height: latteView ? latteView.effects.mask.height : 0
                width: latteView ? latteView.effects.mask.width : 0

                border.color: "green"
                border.width: 1
                color: "transparent"
            }
        }
    }

    /***Hiding/Showing Animations*****/

    //////////////// Animations - Slide In - Out
    SequentialAnimation{
        id: slidingAnimationAutoHiddenOut

        PropertyAnimation {
            target: !root.behaveAsPlasmaPanel ? layoutsContainer : latteView.positioner
            property: !root.behaveAsPlasmaPanel ? (root.isVertical ? "x" : "y") : "slideOffset"
            to: {
                if (root.behaveAsPlasmaPanel) {
                    return manager.slidingOutToPos;
                }

                if (LatteCore.WindowSystem.compositingActive) {
                    return manager.slidingOutToPos;
                } else {
                    if ((Plasmoid.location===PlasmaCore.Types.LeftEdge)||(Plasmoid.location===PlasmaCore.Types.TopEdge)) {
                        return manager.slidingOutToPos + 1;
                    } else {
                        return manager.slidingOutToPos - 1;
                    }
                }
            }
            duration: manager.animationSpeed
            easing.type: Easing.InQuad
        }

        ScriptAction{
            script: {
                //! latteView is still null when the hiding animation fires
                //! during startup, before the C++ View wrapper is injected
                if (!latteView) {
                    return;
                }

                latteView.visibility.isHidden = true;
            }
        }

        onStarted: {
            if (debug.maskEnabled) {
                console.log("hiding animation started...");
            }
        }

        onStopped: {
            //! Trying to move the ending part of the signals at the end of editing animation
            if (!manager.inRelocationHiding) {
                manager.updateMaskArea();
            } else {
                if (!root.editMode) {
                    manager.sendSlidingOutAnimationEnded();
                }
            }

            if (latteView) {
                latteView.visibility.slideOutFinished();
            }
            manager.updateInputGeometry();

            if (root.inStartup) {
                //! when view is first created slide-outs when that animation ends then
                //! it flags that startup has ended and first slide-in can be started
                //! this is important because if it is removed then some views
                //! wont slide-in after startup.
                root.inStartup = false;
            }
        }

        function init() : void {
            if (manager.inRelocationAnimation || root.inStartup/*used from recreating views*/ || !latteView.visibility.blockHiding) {
                start();
            }
        }
    }

    SequentialAnimation{
        id: slidingAnimationAutoHiddenIn

        PauseAnimation{
            duration: manager.inRelocationHiding && animations.active ? 500 : 0
        }

        PropertyAnimation {
            target: !root.behaveAsPlasmaPanel ? layoutsContainer : latteView.positioner
            property: !root.behaveAsPlasmaPanel ? (root.isVertical ? "x" : "y") : "slideOffset"
            to: 0
            duration: manager.animationSpeed
            easing.type: Easing.OutQuad
        }

        ScriptAction{
            script: {
                // deprecated
                // root.inStartup = false;
            }
        }

        onStarted: {
            manager.updateInputGeometry();

            if (debug.maskEnabled) {
                console.log("showing animation started...");
            }
        }

        onStopped: {
            manager.inSlidingIn = false;

            if (manager.inRelocationHiding) {
                manager.inRelocationHiding = false;
                autosize.updateIconSize();
            }

            manager.inRelocationHiding = false;
            autosize.updateIconSize();

            if (debug.maskEnabled) {
                console.log("showing animation ended...");
            }

            latteView.visibility.slideInFinished();

            //! this is needed in order to update dock absolute geometry correctly in the end AND
            //! when a floating dock is sliding-in through masking techniques
            manager.updateMaskArea();
        }

        function init() : void {
            if (!root.viewIsAvailable) {
                return;
            }

            manager.inSlidingIn = true;

            if (slidingAnimationAutoHiddenOut.running) {
                slidingAnimationAutoHiddenOut.stop();
            }

            latteView.visibility.isHidden = false;
            manager.updateMaskArea();

            start();
        }
    }

    //! Slides Animations for FLOATING+BEHAVEASPLASMAPANEL when
    //! HIDETHICKSCREENCAP dynamically is enabled/disabled
    SequentialAnimation{
        id: slidingInRealFloating

        PropertyAnimation {
            target: latteView ? latteView.positioner : null
            property: "slideOffset"
            to: 0
            duration: manager.animationSpeed
            easing.type: Easing.OutQuad
        }

        ScriptAction{
            script: {
                latteView.positioner.inSlideAnimation = false;
            }
        }

        onStopped: latteView.positioner.inSlideAnimation = false;

    }

    SequentialAnimation{
        id: slidingOutRealFloating

        ScriptAction{
            script: {
                latteView.positioner.inSlideAnimation = true;
            }
        }

        PropertyAnimation {
            target: latteView ? latteView.positioner : null
            property: "slideOffset"
            to: Plasmoid.configuration.screenEdgeMargin
            duration: manager.animationSpeed
            easing.type: Easing.InQuad
        }
    }

    Connections {
        target: root
        function onHideThickScreenGapChanged() {
            if (!latteView || !root.viewIsAvailable) {
                return;
            }

            if (root.behaveAsPlasmaPanel && !latteView.visibility.isHidden && !manager.inSlidingIn && !manager.inSlidingOut && !root.inStartup) {
                slideInOutRealFloating();
            }
        }

        function onInStartupChanged() {
            //! used for positioning properly real floating panels when there is a maximized window
            if (root.hideThickScreenGap && !root.inStartup && latteView.positioner.slideOffset===0) {
                if (root.behaveAsPlasmaPanel && !latteView.visibility.isHidden) {
                    slideInOutRealFloating();
                }
            }
        }

        function slideInOutRealFloating() : void {
            if (root.hideThickScreenGap) {
                slidingInRealFloating.stop();
                slidingOutRealFloating.start();
            } else {
                slidingOutRealFloating.stop();
                slidingInRealFloating.start();
            }
        }
    }


}
