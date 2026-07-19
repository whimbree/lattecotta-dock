/*
    SPDX-FileCopyrightText: 2019 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.7

import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.draganddrop 2.0 as DragDrop
import org.kde.plasma.plasmoid 2.0

import org.kde.latte.core 0.2 as LatteCore

//! Thin drag/drop shell (EX-14, docs/tracking/QML_EXTRACTION_PLAN.md): every
//! classification and routing decision comes from
//! LatteCore.DropClassifier; this file keeps only scene effects (spacer
//! parenting/opacity, launcher messages, processMimeData). Handlers stay
//! arrow-syntax members so they cannot silently disconnect (b474adad).
DragDrop.DropArea {
    id: dragArea

    // The containment ContainmentItem root; processMimeData() lives there in Plasma 6.
    property Item containmentItem

    //! injected from main.qml (ids outrank scope properties there);
    //! function-context reads only, per the strict-on-touch injection rule
    property Item dndSpacer
    property QtObject fastLayoutManager
    property Item animations

    property bool containsDrag: false

    readonly property Item dragInfo: Item {
        readonly property bool entered: latteView && latteView.containsDrag
        property bool isTask: false
        property bool isPlasmoid: false
        property bool isSeparator: false
        property bool isLatteTasks: false
        property bool onlyLaunchers: false

        property bool computationsAreValid: false
    }

    Connections{
        target: dragArea.containmentItem ? dragArea.containmentItem.dragInfo : null

        function onEnteredChanged() {
            if(!dragArea.containmentItem.dragInfo.entered) {
                dragArea.clearInfo();
            }
        }
    }

    Connections{
        target: latteView

        function onContainsDragChanged() {
            if(!latteView.containsDrag) {
                dragArea.clearInfo();
            }
        }
    }

    function clearInfo(): void {
        clearInfoTimer.restart();
    }

    //! Give the time when an applet is dropped to be positioned properly
    Timer {
        id: clearInfoTimer
        interval: 100

        onTriggered: {
            dragArea.dragInfo.computationsAreValid = false;

            dragArea.dragInfo.isTask = false;
            dragArea.dragInfo.isPlasmoid = false;
            dragArea.dragInfo.isSeparator = false;
            dragArea.dragInfo.isLatteTasks = false;
            dragArea.dragInfo.onlyLaunchers = false;

            dragArea.dndSpacer.parent = dragArea.containmentItem;
            dragArea.dndSpacer.opacity = 0;
        }
    }

    onDragEnter: (event) => {
        containsDrag = true;
        clearInfoTimer.stop();

        const flags = LatteCore.DropClassifier.classifyContainmentDrag(
                    event.mimeData,
                    (url) => { return latteView.extendedInterface.isApplication(url); });

        dragInfo.isTask = flags.isTask;
        dragInfo.isPlasmoid = flags.isPlasmoid;
        dragInfo.isSeparator = flags.isSeparator;
        dragInfo.isLatteTasks = flags.isLatteTasks;
        dragInfo.onlyLaunchers = flags.onlyLaunchers;
        dragInfo.computationsAreValid = true;

        const action = LatteCore.DropClassifier.containmentDragEnterAction(
                    flags.isTask, flags.onlyLaunchers, Plasmoid.immutable,
                    containmentItem.myView.isShownFully,
                    containmentItem.launchers.hasStealingApplet);

        if (action === LatteCore.DropClassifier.EnterReject) {
            event.ignore();
            clearInfo();
            return;
        }

        //! Send signal AFTER the dragging is confirmed otherwise the restore mask signal from animations
        //! may not be triggered #408926
        animations.needLength.addEvent(dragArea);

        if (action === LatteCore.DropClassifier.EnterShowAddLaunchersMessage) {
            containmentItem.launchers.showAddLaunchersMessageInStealingApplet();
            dndSpacer.opacity = 0;
            dndSpacer.parent = containmentItem;
            return;
        }

        fastLayoutManager.insertAtCoordinates(dndSpacer, event.x, event.y);
        dndSpacer.opacity = 1;
    }

    onDragMove: (event) => {
        containsDrag = true;
        clearInfoTimer.stop();

        const action = LatteCore.DropClassifier.containmentDragMoveAction(
                    dragInfo.isTask, dragInfo.onlyLaunchers,
                    containmentItem.launchers.hasStealingApplet);

        if (action === LatteCore.DropClassifier.MoveLeaveUnchanged) {
            return;
        }

        if (action === LatteCore.DropClassifier.MoveShowAddLaunchersMessage) {
            containmentItem.launchers.showAddLaunchersMessageInStealingApplet();
            dndSpacer.opacity = 0;
            dndSpacer.parent = containmentItem;
            return;
        }

        fastLayoutManager.insertAtCoordinates(dndSpacer, event.x, event.y);
        dndSpacer.opacity = 1;
    }

    onDragLeave: {
        containsDrag = false;
        animations.needLength.removeEvent(dragArea);

        if (containmentItem.launchers.hasStealingApplet) {
            containmentItem.launchers.hideAddLaunchersMessageInStealingApplet();
        }

        dndSpacer.opacity = 0;
        dndSpacer.parent = containmentItem;
    }

    onDrop: (event) => {
        containsDrag = false;
        animations.needLength.removeEvent(dragArea);

        if (containmentItem.launchers.hasStealingApplet) {
            containmentItem.launchers.hideAddLaunchersMessageInStealingApplet();
        }

        const action = LatteCore.DropClassifier.containmentDropAction(
                    dragInfo.isTask, dragInfo.onlyLaunchers,
                    containmentItem.myView.isShownFully,
                    containmentItem.launchers.hasStealingApplet);

        if (action === LatteCore.DropClassifier.DropIgnore) {
            //! the spacer is deliberately left as-is: Qt5 returned early here
            return;
        }

        if (action === LatteCore.DropClassifier.DropAddLaunchersToStealingApplet) {
            containmentItem.launchers.addDroppedLaunchersInStealingApplet(event.mimeData.urls);
        } else {
            var dndindex = fastLayoutManager.dndSpacerIndex();
            var eventx = event.x;
            var eventy = event.y;

            if (dndindex >= 0) {
                var masquearadedIndexFromPoint = fastLayoutManager.indexToMasquearadedPoint(fastLayoutManager.dndSpacerIndex());
                eventx = masquearadedIndexFromPoint.x;
                eventy = masquearadedIndexFromPoint.y;
            }

            containmentItem.processMimeData(event.mimeData, eventx, eventy);
            //! inform others what plasmoid was drag n' dropped to be added
            latteView.extendedInterface.appletDropped(event.mimeData, eventx, eventy);
            event.accept(event.proposedAction);
        }

        dndSpacer.opacity = 0;
    }
}
