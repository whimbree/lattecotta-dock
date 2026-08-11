/*
    SPDX-FileCopyrightText: 2016 Smith AR <audoban@openmailbox.org>
    SPDX-FileCopyrightText: 2016 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.7
import QtQuick.Layouts

import org.kde.plasma.plasmoid 2.0
import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.kirigami 2.20 as Kirigami
import org.kde.plasma.components 3.0 as PlasmaComponents
import org.kde.kquickcontrolsaddons 2.0

import org.kde.latte.core 0.2 as LatteCore
import org.kde.latte.components 1.0 as LatteComponents

MouseArea {
    id: configurationArea
    z: 1000

    width: Plasmoid.formFactor === PlasmaCore.Types.Horizontal ? root.width : thickness
    height: Plasmoid.formFactor === PlasmaCore.Types.Vertical ? root.height : thickness

    visible: root.inConfigureAppletsMode
    hoverEnabled: root.inConfigureAppletsMode

    //! LeftButton only (the MouseArea default, made explicit - Qt5 upstream and
    //! the CaptSilver Qt6 port both set no acceptedButtons here either). The drag
    //! gesture is a left-press; a right-click mid-drag must still fall through to
    //! the containment context menu rather than being swallowed here. That
    //! right-click steals the pointer grab, which is exactly what fires onCanceled,
    //! where the dragged applet is un-stranded (see restoreDraggedApplet / D285).
    //! The goal is that the applet un-strands, NOT that right-click stops opening a
    //! menu, so this stays left-only.
    acceptedButtons: Qt.LeftButton

    focus: true
    cursorShape: {
        if (currentApplet && tooltip.visible && currentApplet.latteStyleApplet) {
            return root.isHorizontal ? Qt.SizeHorCursor : Qt.SizeVerCursor;
        }

        return Qt.ArrowCursor;
    }

    property bool isResizingLeft: false
    property bool isResizingRight: false
    property Item currentApplet
    property Item previousCurrentApplet
    readonly property alias draggedPlaceHolder: placeHolder

    property Item currentHoveredLayout: {
        if (placeHolder.parent !== configurationArea) {
            return placeHolder.parent;
        }

        return currentApplet ? currentApplet.parent : null
    }

    //! REAL, not int: wayland delivers fractional pointer coordinates, and
    //! the drag applies per-event deltas (x += mouse.x - lastX). An int here
    //! truncates the stored position every event, so each move injects the
    //! previous event's lost fraction into the applet position - hundreds of
    //! events per drag accumulate into visible cursor-to-widget drift
    //! (user-observed while shuffling a widget back and forth). Qt5/X11 got
    //! away with int because pointer coordinates were integral there.
    property real lastX
    property real lastY
    property real appletX
    property real appletY

    readonly property int thickness: metrics.mask.thickness.maxNormal - metrics.extraThicknessForNormal
    readonly property int spacerHandleSize: Kirigami.Units.smallSpacing

    onHeightChanged: tooltip.visible = false;
    onWidthChanged: tooltip.visible = false;



    function hoveredItem(x, y) {
        //! main layout
        var relevantLayout = mapFromItem(layoutsContainer.mainLayout, 0, 0);
        var item = layoutsContainer.mainLayout.childAt(x-relevantLayout.x, y-relevantLayout.y);

        if (!item) {
            // start layout
            relevantLayout = mapFromItem(layoutsContainer.startLayout,0,0);
            item = layoutsContainer.startLayout.childAt(x-relevantLayout.x, y-relevantLayout.y);
        }

        if (!item) {
            // end layout
            relevantLayout = mapFromItem(layoutsContainer.endLayout,0,0);
            item = layoutsContainer.endLayout.childAt(x-relevantLayout.x, y-relevantLayout.y);
        }

        return item;
    }

    function relevantLayoutForApplet(curapplet) {
        var relevantLayout;

        if (curapplet.parent === layoutsContainer.mainLayout) {
            relevantLayout = mapFromItem(layoutsContainer.mainLayout, 0, 0);
        } else if (curapplet.parent === layoutsContainer.startLayout) {
            relevantLayout = mapFromItem(layoutsContainer.startLayout, 0, 0);
        } else if (curapplet.parent === layoutsContainer.endLayout) {
            relevantLayout = mapFromItem(layoutsContainer.endLayout, 0, 0);
        }

        return relevantLayout;
    }


    onPositionChanged: (mouse) => {
        if (pressed) {
            if(currentApplet){
                if (Plasmoid.formFactor === PlasmaCore.Types.Vertical) {
                    currentApplet.y += (mouse.y - lastY);
                } else {
                    currentApplet.x += (mouse.x - lastX);
                }
            }

            lastX = mouse.x;
            lastY = mouse.y;

            var mousesink = {x: mouse.x, y: mouse.y};

            //! ignore thickness moving at all cases
            if (Plasmoid.formFactor === PlasmaCore.Types.Horizontal) {
                mousesink.y = configurationArea.height / 2;
            } else {
                mousesink.x = configurationArea.width / 2;
            }

            var item = hoveredItem(mousesink.x, mousesink.y);

            if (item && item !== placeHolder) {
                var posInItem = mapToItem(item, mousesink.x, mousesink.y);

                if ((Plasmoid.formFactor === PlasmaCore.Types.Vertical && posInItem.y < item.height/2) ||
                        (Plasmoid.formFactor !== PlasmaCore.Types.Vertical && posInItem.x < item.width/2)) {
                    fastLayoutManager.insertBefore(item, placeHolder);
                } else {
                    fastLayoutManager.insertAfter(item, placeHolder);
                }
            }

        } else {
            var item = hoveredItem(mouse.x, mouse.y);

            if (root.dragOverlay && item && !item.isParabolicEdgeSpacer) {
                root.dragOverlay.currentApplet = item;
            } else {
                currentApplet = null;
                root.dragOverlay.currentApplet = null;
            }
        }

        if (root.dragOverlay.currentApplet) {
            hideTimer.stop();

            tooltip.visible = true;
            tooltip.raise();
        }
    }

    onExited: hideTimer.restart();

    onCurrentAppletChanged: {
        previousCurrentApplet = currentApplet;

        if (!currentApplet || !root.dragOverlay.currentApplet) {
            hideTimer.restart();
            return;
        }

        var relevantLayout = relevantLayoutForApplet(currentApplet) ;

        if (!relevantLayout) {
            return;
        }

        lockButton.checked = currentApplet.lockZoom;
        colorizingButton.checked = !currentApplet.userBlocksColorizing;
    }

    //! Un-lift and re-home the dragged applet, reversing the onPressed lift: the
    //! applet returns from root (z 900, the lift) into the layout at the placeHolder
    //! slot, the placeHolder goes back to configurationArea, justify re-runs when it
    //! is the alignment, the order is saved, and the fill applets re-fit the settled
    //! layout. Shared by onReleased (the normal drop) and onCanceled (the grab-lost
    //! abort). Without the onCanceled path this restore ran ONLY on release, so a
    //! right-click mid-drag - which opens the containment context menu and steals the
    //! pointer grab, making Qt fire canceled instead of released - left the applet
    //! parented to root at z 900, stranded outside the dock over the edit chrome
    //! (D285, the drag-cancel stranding). Only the un-lift/re-home lives here; the
    //! resize-length commit and the gesture-flag reset stay in onReleased so a
    //! canceled gesture commits nothing it never intended.
    function restoreDraggedApplet() {
        fastLayoutManager.insertBefore(placeHolder, currentApplet);
        placeHolder.parent = configurationArea;
        currentApplet.z = 1;

        if (root.myView.alignment === LatteCore.Types.Justify) {
            fastLayoutManager.moveAppletsBasedOnJustifyAlignment();
        }

        fastLayoutManager.save();
        layouter.updateSizeForAppletsInFill();
    }

    onPressed: (mouse) => {
        if (!root.dragOverlay.currentApplet) {
            return;
        }

        var relevantApplet = mapFromItem(currentApplet, 0, 0);
        var rootArea = mapFromItem(root, 0, 0);

        appletX = mouse.x - relevantApplet.x + rootArea.x;
        appletY = mouse.y - relevantApplet.y + rootArea.y;

        lastX = mouse.x;
        lastY = mouse.y;
        fastLayoutManager.insertBefore(currentApplet, placeHolder);
        currentApplet.parent = root;
        currentApplet.x = root.isHorizontal ? lastX - currentApplet.width/2 : lastX-appletX;
        currentApplet.y = root.isVertical ? lastY - currentApplet.height/2 : lastY-appletY;
        currentApplet.z = 900;
    }

    onReleased: {
        if (!handle.visible) {
            tooltip.visible = false;
        }

        if (!root.dragOverlay.currentApplet) {
            return;
        }

        //! release-specific: commit the resize length the drop settled on, then
        //! clear the resize gesture flags. These do NOT belong in the shared restore -
        //! a canceled gesture (onCanceled) must not commit a length it never intended.
        if(currentApplet && currentApplet.applet){
            if (Plasmoid.formFactor === PlasmaCore.Types.Vertical) {
                currentApplet.applet.plasmoid.configuration.length = handle.height;
            } else {
                currentApplet.applet.plasmoid.configuration.length = handle.width;
            }
        }

        configurationArea.isResizingLeft = false;
        configurationArea.isResizingRight = false;

        restoreDraggedApplet();
    }

    //! The grab-lost abort (D285, the drag-cancel stranding): a right-click during a
    //! held drag opens the containment context menu, which steals the pointer grab,
    //! so Qt fires canceled instead of released and onReleased never runs. Qt5 Latte
    //! had no onCanceled here (verified against KDE upstream ConfigOverlay.qml at
    //! ref=master); this handler is the platform-forced addition for Qt6's grab-steal
    //! behavior. Restore the applet exactly as a drop would, minus the resize commit a
    //! cancel never intended. Guarded like onReleased: no dragged applet, nothing to do.
    onCanceled: {
        //! the same "no dragged applet, nothing to restore" guard onReleased makes:
        //! root.dragOverlay is this configurationArea, so root.dragOverlay.currentApplet
        //! IS the local currentApplet (onReleased's body reads it the same bare way).
        if (!currentApplet) {
            return;
        }

        restoreDraggedApplet();
    }

    onWheel: (wheel) => {
        if (!currentApplet || !currentApplet.latteStyleApplet) {
            return;
        }

        var angle = wheel.angleDelta.y / 8;

        if (angle > 12)
            currentApplet.latteStyleApplet.increaseLength();
        else if (angle < 12)
            currentApplet.latteStyleApplet.decreaseLength();
    }

    Connections {
        target: currentApplet
        onWidthChanged: {
            if (configurationArea.pressed && root.isHorizontal) {
                currentApplet.x = configurationArea.lastX - currentApplet.width/2;
            }
        }

        onHeightChanged: {
            if (configurationArea.pressed && root.isVertical) {
                currentApplet.y = configurationArea.lastY - currentApplet.height/2;
            }
        }
    }

    Item {
        id: placeHolder
        visible: configurationArea.pressed
        width: currentApplet !== null ? (root.isVertical ? currentApplet.width : Math.min(root.maxLength / 2, currentApplet.width)) : 0
        height: currentApplet !== null ? (!root.isVertical ? currentApplet.height : Math.min(root.maxLength / 2, currentApplet.height)) : 0

        readonly property bool isPlaceHolder: true
        readonly property int length: root.isVertical ? height : width
    }

    Timer {
        id: hideTimer
        interval: animations.duration.large * 2
        onTriggered: {
            if (!tooltipMouseArea.containsMouse) {
                tooltip.visible = false;
                currentApplet = null;
            }
        }
    }

    Item {
        id: handle
        parent: currentApplet ? currentApplet : configurationArea
        anchors.fill: parent
        visible: currentApplet && (configurationArea.containsMouse || tooltipMouseArea.containsMouse)

        Loader {
            anchors.fill: parent
            active: root.debug.graphicsEnabled
            sourceComponent: Rectangle {
                color: "transparent"
                border.width:1
                border.color: "yellow"
            }
        }

        //BEGIN functions
        //END functions

        Item {
            id: handleVisualItem
            width: root.isHorizontal ? parent.width : thickness
            height: root.isHorizontal ? thickness : parent.height

            readonly property int thickness: root.isHorizontal ? parent.height - metrics.margin.screenEdge : parent.width - metrics.margin.screenEdge

            Rectangle{
                anchors.fill: parent
                color: Kirigami.Theme.backgroundColor
                radius: 3
                opacity: 0.35
            }

            Kirigami.Icon {
                source: "transform-move"
                width: Math.min(144, root.metrics.iconSize)
                height: width
                anchors.centerIn: parent
                opacity: 0.9
                layer.enabled: root.environment.isGraphicsSystemAccelerated
                layer.effect: LatteComponents.ShadowedItem {
                    shadowSizePx: root.myView.itemShadow.size
                    shadowColor: root.myView.itemShadow.shadowColor
                }
            }


            states:[
                State{
                    name: "bottom"
                    when: Plasmoid.location === PlasmaCore.Types.BottomEdge

                    AnchorChanges{
                        target: handleVisualItem;
                        anchors.horizontalCenter: parent.horizontalCenter; anchors.verticalCenter: undefined;
                        anchors.right: undefined; anchors.left: undefined; anchors.top: undefined; anchors.bottom: parent.bottom;
                    }
                    PropertyChanges{
                        target: handleVisualItem;
                        anchors.leftMargin: 0;    anchors.rightMargin: 0;     anchors.topMargin:0;    anchors.bottomMargin: metrics.margin.screenEdge;
                        anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
                    }
                },
                State{
                    name: "top"
                    when: Plasmoid.location === PlasmaCore.Types.TopEdge

                    AnchorChanges{
                        target: handleVisualItem;
                        anchors.horizontalCenter: parent.horizontalCenter; anchors.verticalCenter: undefined;
                        anchors.right: undefined; anchors.left: undefined; anchors.top: parent.top; anchors.bottom: undefined;
                    }
                    PropertyChanges{
                        target: handleVisualItem;
                        anchors.leftMargin: 0;    anchors.rightMargin: 0;     anchors.topMargin: metrics.margin.screenEdge;    anchors.bottomMargin: 0;
                        anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
                    }
                },
                State{
                    name: "left"
                    when: Plasmoid.location === PlasmaCore.Types.LeftEdge

                    AnchorChanges{
                        target: handleVisualItem;
                        anchors.horizontalCenter: undefined; anchors.verticalCenter: parent.verticalCenter;
                        anchors.right: undefined; anchors.left: parent.left; anchors.top: undefined; anchors.bottom: undefined;
                    }
                    PropertyChanges{
                        target: handleVisualItem;
                        anchors.leftMargin: metrics.margin.screenEdge;    anchors.rightMargin: 0;     anchors.topMargin:0;    anchors.bottomMargin: 0;
                        anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
                    }
                },
                State{
                    name: "right"
                    when: Plasmoid.location === PlasmaCore.Types.RightEdge

                    AnchorChanges{
                        target: handleVisualItem;
                        anchors.horizontalCenter: undefined; anchors.verticalCenter: parent.verticalCenter;
                        anchors.right: parent.right; anchors.left: undefined; anchors.top: undefined; anchors.bottom: undefined;
                    }
                    PropertyChanges{
                        target: handleVisualItem;
                        anchors.leftMargin: 0;    anchors.rightMargin: metrics.margin.screenEdge;     anchors.topMargin:0;    anchors.bottomMargin: 0;
                        anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
                    }
                }
            ]

        }

        Behavior on opacity {
            NumberAnimation {
                duration: animations.duration.large
                easing.type: Easing.InOutQuad
            }
        }
    }
    //! Latte's Dialog, not PlasmaCore.Dialog: this modal keeps its window
    //! mapped while visualParent hops between hovered applets, and on
    //! wayland the base class cannot reposition a mapped popup reliably
    //! (libplasma re-sends the frozen show-time position; watched live:
    //! the window stayed parked over the first hovered applet while only
    //! its content followed the pointer, so hovering a far applet looked
    //! like the modal never appeared). Latte::Quick::Dialog recomputes the
    //! anchored position fresh on every Move/Expose/Show and on anchor
    //! changes while visible - same fix as the task previews window.
    LatteCore.Dialog {
        id: tooltip
        visualParent: currentApplet

        type: PlasmaCore.Dialog.Dock
        flags: Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus | Qt.BypassWindowManagerHint | Qt.ToolTip
        location: Plasmoid.location
        edge: Plasmoid.location


        onVisualParentChanged: {
            var curapplet = configurationArea.currentApplet;

            if (visualParent && curapplet
                    && (curapplet.applet || curapplet.isSeparator || curapplet.isInternalViewSplitter)) {

                configureButton.visible = !curapplet.isInternalViewSplitter
                        && (curapplet.applet.plasmoid.pluginName !== "org.kde.latte.plasmoid")
                        && curapplet.applet.plasmoid.internalAction("configure")
                        && curapplet.applet.plasmoid.internalAction("configure").enabled;
                closeButton.visible = !curapplet.isInternalViewSplitter && curapplet.applet.plasmoid.internalAction("remove") && curapplet.applet.plasmoid.internalAction("remove").enabled;
                lockButton.visible = !curapplet.isInternalViewSplitter
                        && !curapplet.communicator.indexerIsSupported
                        && !curapplet.communicator.appletBlocksParabolicEffect
                        && !curapplet.isSeparator;

                colorizingButton.visible = root.colorizerEnabled && !curapplet.appletBlocksColorizing && !curapplet.isInternalViewSplitter;

                tooltipMouseArea.appletTitle = curapplet.isInternalViewSplitter ? i18n("Justify Splitter") : curapplet.applet.plasmoid.title;
            }
        }

        mainItem: MouseArea {
            id: tooltipMouseArea
            enabled: currentApplet
            width: handleRow.childrenRect.width + (2 * handleRow.spacing)
            height: Math.max(configureButton.height, label.contentHeight, closeButton.height)
            hoverEnabled: true

            //! Qt5 hung these hints on the buttons as popup tooltips; popups flicker
            //! here (the no-QQC2.ToolTip rule below), so the hovered button's hint
            //! rides the in-dialog label instead, taking the applet title's place
            //! for as long as the button is hovered.
            property string appletTitle
            //! the label is sized to the widest string it can ever carry (see the
            //! TextMetrics beside it), so the title-to-hint swap never resizes this
            //! dialog: a hover-driven resize moves the buttons under the resting
            //! pointer, hover drops and re-fires, and the handle strobes - the same
            //! loop family as the banned popup tooltips. Geometry that never reacts
            //! to hover cannot loop.
            //! the hint strings live in the sizing TextMetrics beside the label -
            //! single authority for both the shown text and the reserved width
            readonly property string hoveredButtonHint: {
                if (configureButton.hovered) {
                    return configureHintMetrics.text;
                }
                if (colorizingButton.hovered) {
                    return paintingHintMetrics.text;
                }
                if (lockButton.hovered) {
                    return parabolicHintMetrics.text;
                }
                if (closeButton.hovered) {
                    return removeHintMetrics.text;
                }
                return "";
            }
            LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
            LayoutMirroring.childrenInherit: true

            onEntered: hideTimer.stop();
            onExited: hideTimer.restart();

            //! These handle buttons deliberately carry NO QQC2.ToolTip. On Wayland an attached
            //! ToolTip pops a separate surface at the cursor the instant a button is hovered; the
            //! compositor then sends a leave to the button AND this wrapping MouseArea, the ToolTip
            //! hides, the cursor re-enters, and it loops ~20Hz — the edit-handle flicker that also
            //! ate clicks (hideTimer riding a false window). Don't re-add per-button tooltips here;
            //! if hints are needed, drive the in-Dialog label instead of a popup.
            Row {
                id: handleRow
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: 2*Kirigami.Units.smallSpacing

                Row{
                    spacing: Kirigami.Units.smallSpacing
                    PlasmaComponents.ToolButton {
                        id: configureButton
                        anchors.verticalCenter: parent.verticalCenter
                        icon.name: "configure"
                        //! screen-reader names (Phase 10 AT-SPI rollout): each
                        //! handle button reuses its sizing TextMetrics hint -
                        //! the same single authority the hovered-hint label
                        //! reads - so Orca and the visual hint always agree.
                        //! Press works natively (a QQC2 button triggers
                        //! clicked() on an AT press); only names were missing.
                        Accessible.name: configureHintMetrics.text
                        onClicked: {
                            tooltip.visible = false;
                            currentApplet.applet.plasmoid.internalAction("configure").trigger();
                        }
                    }

                    PlasmaComponents.Label {
                        id: label
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.rightMargin: Kirigami.Units.smallSpacing
                        textFormat: Text.PlainText
                        maximumLineCount: 1
                        horizontalAlignment: Text.AlignHCenter
                        width: Math.max(titleMetrics.advanceWidth,
                                        configureHintMetrics.advanceWidth,
                                        paintingHintMetrics.advanceWidth,
                                        parabolicHintMetrics.advanceWidth,
                                        removeHintMetrics.advanceWidth)
                        text: tooltipMouseArea.hoveredButtonHint !== ""
                              ? tooltipMouseArea.hoveredButtonHint
                              : tooltipMouseArea.appletTitle

                        TextMetrics {
                            id: titleMetrics
                            font: label.font
                            text: tooltipMouseArea.appletTitle
                        }
                        TextMetrics {
                            id: configureHintMetrics
                            font: label.font
                            text: i18n("Configure applet")
                        }
                        TextMetrics {
                            id: paintingHintMetrics
                            font: label.font
                            text: i18n("Enable painting for this applet")
                        }
                        TextMetrics {
                            id: parabolicHintMetrics
                            font: label.font
                            text: i18n("Disable parabolic effect for this applet")
                        }
                        TextMetrics {
                            id: removeHintMetrics
                            font: label.font
                            text: i18n("Remove applet")
                        }
                    }

                    Row{
                        spacing: Kirigami.Units.smallSpacing/2

                        PlasmaComponents.ToolButton{
                            id: colorizingButton
                            checkable: true
                            icon.name: "color-picker"
                            Accessible.name: paintingHintMetrics.text

                            onClicked: {
                                fastLayoutManager.setOption(configurationArea.currentApplet.applet.plasmoid.id, "userBlocksColorizing", !checked);
                            }
                        }

                        PlasmaComponents.ToolButton{
                            id: lockButton
                            checkable: true
                            icon.name: checked ? "lock" : "unlock"
                            Accessible.name: parabolicHintMetrics.text

                            onClicked: {
                                fastLayoutManager.setOption(configurationArea.currentApplet.applet.plasmoid.id, "lockZoom", checked);
                            }
                        }

                        PlasmaComponents.ToolButton {
                            id: closeButton
                            anchors.verticalCenter: parent.verticalCenter
                            icon.name: "delete"
                            Accessible.name: removeHintMetrics.text
                            onClicked: {
                                tooltip.visible = false;
                                if(currentApplet && currentApplet.applet) {
                                    // latteView is the containment's injected relationship boundary.
                                    // qmllint disable unqualified
                                    latteView.removeApplet(currentApplet.applet.plasmoid.id);
                                    // qmllint enable unqualified
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    states: [
        State {
            name: "bottom"
            when: (Plasmoid.location === PlasmaCore.Types.BottomEdge)

            AnchorChanges {
                target: configurationArea
                anchors{ top:undefined; bottom:parent.bottom; left:undefined; right:undefined;
                    horizontalCenter:parent.horizontalCenter; verticalCenter:undefined}
            }
        },
        State {
            name: "top"
            when: (Plasmoid.location === PlasmaCore.Types.TopEdge)

            AnchorChanges {
                target: configurationArea
                anchors{ top:parent.top; bottom:undefined; left:undefined; right:undefined;
                    horizontalCenter:parent.horizontalCenter; verticalCenter:undefined}
            }
        },
        State {
            name: "left"
            when: (Plasmoid.location === PlasmaCore.Types.LeftEdge)

            AnchorChanges {
                target: configurationArea
                anchors{ top:undefined; bottom:undefined; left:parent.left; right:undefined;
                    horizontalCenter:undefined; verticalCenter:parent.verticalCenter}
            }
        },
        State {
            name: "right"
            when: (Plasmoid.location === PlasmaCore.Types.RightEdge)

            AnchorChanges {
                target: configurationArea
                anchors{ top:undefined; bottom:undefined; left:undefined; right:parent.right;
                    horizontalCenter:undefined; verticalCenter:parent.verticalCenter}
            }
        }
    ]
}
