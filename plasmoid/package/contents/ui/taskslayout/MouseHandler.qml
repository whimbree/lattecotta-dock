/*
    SPDX-FileCopyrightText: 2016 Smith AR <audoban@openmailbox.org>
    SPDX-FileCopyrightText: 2016 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/


import QtQuick 2.0

import org.kde.plasma.plasmoid 2.0
import org.kde.draganddrop 2.0

import org.kde.taskmanager 0.1 as TaskManager

import org.kde.latte.core 0.2 as LatteCore

//! Thin drag/drop shell (EX-14, docs/tracking/QML_EXTRACTION_PLAN.md): mime
//! classification and the onDragMove suppression/reorder routing come
//! from LatteCore.DropClassifier; this file keeps the scene effects
//! (model moves, hover bookkeeping, preview windows) and the trivial
//! live reads the snapshot marshals. Identity comparisons (above vs
//! ignoredItem/hoveredItem/dragSource) stay here, where the object
//! references live.
Item {
    // signal urlDropped(url url)
    id: dArea
    signal urlsDropped(var urls)

    //! injected from main.qml (document ids outrank these names there)
    property Item tasksRoot
    property QtObject backend
    property QtObject tasksModel
    property QtObject windowsPreviewDlg
    property Item toolTipDelegate
    property Item appletAbilities

    property Item target
    property Item ignoredItem
    property bool moved: false
    property bool containsDrag: false

    property alias hoveredItem: dropHandler.hoveredItem

    readonly property alias isMovingTask: dropHandler.inMovingTask
    readonly property alias isDroppingFiles: dropHandler.inDroppingFiles
    readonly property alias isDroppingOnlyLaunchers: dropHandler.inDroppingOnlyLaunchers
    readonly property alias isDroppingSeparator: dropHandler.inDroppingSeparator

    Timer {
        id: ignoreItemTimer

        repeat: false
        interval: 200

        onTriggered: {
            dArea.ignoredItem = null;
        }
    }

    Connections {
        target: dArea.tasksRoot
        function onDragSourceChanged() {
            if (!dArea.tasksRoot.dragSource) {
                dArea.ignoredItem = null;
                ignoreItemTimer.stop();
            }
        }
    }

    DropArea {
        id: dropHandler
        anchors.fill: parent
        preventStealing: true

        property bool inDroppingOnlyLaunchers: false
        property bool inDroppingSeparator: false
        property bool inMovingTask: false
        property bool inDroppingFiles: false

        //! the OR of the stored flags rather than a core call: it must
        //! recompute when clearDroppingFlags() resets them, and the flag
        //! values themselves are the classifier's single-authority answer
        readonly property bool eventIsAccepted: inMovingTask || inDroppingSeparator || inDroppingOnlyLaunchers || inDroppingFiles

        property int droppedPosition: -1;
        property Item hoveredItem

        function clearDroppingFlags(): void {
            inDroppingFiles = false;
            inDroppingOnlyLaunchers = false;
            inDroppingSeparator = false;
            inMovingTask = false;
        }

        onHoveredItemChanged: {
            if (hoveredItem && dArea.windowsPreviewDlg.activeItem && hoveredItem !== dArea.windowsPreviewDlg.activeItem ) {
                dArea.windowsPreviewDlg.hide(6.7);
            }
        }

        onDragEnter: (event) => {
            const flags = LatteCore.DropClassifier.classifyTasksDrag(
                        event.mimeData,
                        (url) => { return dArea.backend.isApplication(url); });

            inMovingTask = flags.movingTask;
            inDroppingOnlyLaunchers = flags.droppingOnlyLaunchers;
            inDroppingSeparator = flags.droppingSeparator;
            inDroppingFiles = flags.droppingFiles;

            if (!eventIsAccepted) {
                clearDroppingFlags();
                event.ignore();
                return;
            }

            dArea.containsDrag = true;
        }

        onDragMove: (event) => {
            if (!eventIsAccepted) {
                clearDroppingFlags();
                event.ignore();
                return;
            }

            dArea.containsDrag = true;

            //! upstream carried `if (target.animating) return;` here, inherited
            //! from plasma-desktop's TaskList which defines that property;
            //! Latte's target is a plain ListView that never did, so the guard
            //! read undefined and was dead in upstream and both forks alike
            //! (verified 2026-07-16 across all three trees). The real
            //! anti-jitter mechanism in this path is the ignoredItem timer.
            //! If a decision freeze during moveDisplaced ever proves needed,
            //! expose a displaced-animation count on icList and gate on that,
            //! not on a name that does not exist.

            var eventToTarget = mapToItem(dArea.target, event.x, event.y);

            var above = dArea.target.childAtPos(eventToTarget.x, eventToTarget.y);

            // The suppression rules routed below: if we're mixing launcher
            // tasks with other tasks and are moving a (small) launcher task
            // across a non-launcher task, don't allow the latter to be the
            // move target twice in a row for a while, as it will naturally
            // be moved underneath the cursor as result of the initial move,
            // due to being far larger than the launcher delegate.
            // TODO: This restriction (minus the timer, which improves things)
            // has been proven out in the EITM fork, but could be improved later
            // by tracking the cursor movement vector and allowing the drag if
            // the movement direction has reversed, establishing user intent to
            // move back.
            const decision = LatteCore.DropClassifier.decideTasksDragMove({
                dragSource: dArea.tasksRoot.dragSource ? {
                    itemIndex: dArea.tasksRoot.dragSource.itemIndex,
                    isLauncher: dArea.tasksRoot.dragSource.m.IsLauncher === true,
                    isAbove: dArea.tasksRoot.dragSource === above
                } : null,
                above: above ? {
                    itemIndex: above.itemIndex,
                    hasModelData: above.m !== null && above.m !== undefined,
                    isLauncher: above.m ? above.m.IsLauncher === true : false
                } : null,
                hasIgnoredItem: dArea.ignoredItem !== null,
                aboveIsIgnored: above === dArea.ignoredItem,
                aboveIsHovered: hoveredItem === above,
                sortIsManual: dArea.tasksModel.sortMode === TaskManager.TasksModel.SortManual,
                posX: eventToTarget.x,
                posY: eventToTarget.y,
                vertical: dArea.tasksRoot.vertical,
                itemStep: dArea.appletAbilities.metrics.totals.length
            });

            switch (decision.action) {
            case LatteCore.DropClassifier.MoveTaskSuppressRepeatTarget:
                return;
            case LatteCore.DropClassifier.MoveTaskReorder: {
                dArea.tasksRoot.dragSource.z = 100;
                dArea.ignoredItem = above;

                const pos = dArea.tasksRoot.dragSource.itemIndex;
                dArea.tasksModel.move(pos, decision.moveTo);

                ignoreItemTimer.restart();
                break;
            }
            case LatteCore.DropClassifier.MoveTaskHoverAbove:
                hoveredItem = above;
                activationTimer.restart();
                break;
            case LatteCore.DropClassifier.MoveTaskClearHover:
                hoveredItem = null;
                activationTimer.stop();
                break;
            }

            if (hoveredItem && dArea.windowsPreviewDlg.visible && dArea.toolTipDelegate.rootIndex !== hoveredItem.modelIndex() ) {
                dArea.windowsPreviewDlg.hide(6);
            }
        }

        onDragLeave: {
            dArea.containsDrag = false;
            hoveredItem = null;
            clearDroppingFlags();

            activationTimer.stop();
        }

        onDrop: (event) => {
            const action = LatteCore.DropClassifier.tasksDropAction(
                        inMovingTask, inDroppingOnlyLaunchers, inDroppingSeparator, inDroppingFiles);

            if (action === LatteCore.DropClassifier.TasksDropIgnore) {
                clearDroppingFlags();
                event.ignore();
                return;
            }

            // Reject internal drops.
            dArea.containsDrag = false;

            if (action === LatteCore.DropClassifier.TasksDropAddSeparator) {
                dArea.appletAbilities.launchers.addInternalSeparatorAtPos(
                            LatteCore.DropClassifier.separatorDropPosition(
                                hoveredItem !== null, hoveredItem ? hoveredItem.itemIndex : -1));
            } else if (action === LatteCore.DropClassifier.TasksDropUrls) {
                dArea.urlsDropped(event.mimeData.urls);
            }

            clearDroppingFlags();
        }

        Timer {
            id: activationTimer

            interval: 250
            repeat: false

            onTriggered: {
                if (dropHandler.inDroppingOnlyLaunchers || dropHandler.inDroppingSeparator) {
                    return;
                }

                if (dropHandler.hoveredItem.m.IsGroupParent === true) {
                    dArea.tasksRoot.showPreviewForTasks(dropHandler.hoveredItem);
                    // groupDialog.visualParent = dropHandler.hoveredItem;
                    // groupDialog.visible = true;
                } else if (dropHandler.hoveredItem.m.IsLauncher !== true) {
                    if(dArea.windowsPreviewDlg.visible && dArea.toolTipDelegate.currentItem !== dropHandler.hoveredItem.itemIndex ) {
                        dArea.windowsPreviewDlg.hide(5);
                        dArea.toolTipDelegate.currentItem=-1;
                    }

                    dArea.tasksModel.requestActivate(dropHandler.hoveredItem.modelIndex());
                }
            }
        }
    }
    /*
    MouseArea {
        id: wheelHandler

        anchors.fill: parent
        property int wheelDelta: 0;
        enabled: Plasmoid.configuration.wheelEnabled

        onWheel: wheelDelta = TaskTools.wheelActivateNextPrevTask(wheelDelta, wheel.angleDelta.y);
    } */
}
