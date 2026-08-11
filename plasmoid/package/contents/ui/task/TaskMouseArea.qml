/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.0

import org.kde.plasma.plasmoid 2.0
import org.kde.plasma.core 2.0 as PlasmaCore

import org.kde.latte.core 0.2 as LatteCore
import org.kde.latte.private.tasks 0.1 as LatteTasks

import "../../code/TaskActions.js" as TaskActions

MouseArea {
    id: taskMouseArea
    anchors.fill: parent
    acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
    hoverEnabled: taskItem.visible && (!inAnimation) && (!isStartup) && (!root.taskInAnimation)
                  &&(!inBouncingAnimation) && !isSeparator

    property bool pressed: false

    required property QtObject dispatchReporter
    required property var dispatchModel
    required property bool dispatchIsLauncher
    required property int configuredMiddleClickAction

    readonly property alias hoveredTimer: _hoveredTimer

    //! Runs the window operation the middle-click / modifier-click combos map
    //! their (identical) TaskAction set onto. The action->command mapping is
    //! the single source of truth in code/TaskActions.js so the config combos
    //! can never offer a value with no handler branch; this switch is the
    //! executor for the tokens it returns.
    function stableRowIdentity() {
        var launcherIdentity = taskMouseArea.dispatchModel.LauncherUrlWithoutIcon
                ? String(taskMouseArea.dispatchModel.LauncherUrlWithoutIcon) : "";
        return launcherIdentity.length > 0 ? launcherIdentity : String(taskMouseArea.dispatchModel.AppId || "");
    }

    function recordMiddleClickDispatch(operation) {
        taskMouseArea.dispatchReporter.recordMiddleClickDispatch(taskMouseArea.stableRowIdentity(),
                                                                 taskMouseArea.dispatchIsLauncher,
                                                                 taskMouseArea.configuredMiddleClickAction,
                                                                 operation);
    }

    function executeStandardAction(action, recordsMiddleClick) {
        var command = TaskActions.standardCommandFor(action);

        if (recordsMiddleClick) {
            taskMouseArea.recordMiddleClickDispatch(command);
        }

        switch (command) {
        case "close":
            tasksModel.requestClose(modelIndex());
            break;
        case "newInstance":
            tasksModel.requestNewInstance(modelIndex());
            break;
        case "toggleMinimized":
            tasksModel.requestToggleMinimized(modelIndex());
            break;
        case "cycleOrActivate":
            if (isGroupParent) {
                subWindows.activateNextTask();
            } else {
                activateTask();
            }
            break;
        case "toggleGrouping":
            tasksModel.requestToggleGrouping(modelIndex());
            break;
        default:
            //! "" == NoneAction, a real no-op
            break;
        }
    }

    Connections {
        target: taskMouseArea
        function onPressed(mouse) { taskItem.mousePressed(mouse.x, mouse.y, mouse.button); }
        function onReleased(mouse) { taskItem.mouseReleased(mouse.x, mouse.y, mouse.button); }
    }

    onEntered: {
        if (isLauncher && windowsPreviewDlg.visible) {
            windowsPreviewDlg.hide(1);
        }

        //! show previews if enabled
        if(isAbleToShowPreview && !showPreviewsIsBlockedFromReleaseEvent && !isLauncher
                && (((root.showPreviews || (windowsPreviewDlg.visible && !isLauncher))
                     && windowsPreviewDlg.activeItem !== taskItem)
                    || root.highlightWindows)){

            if (!root.disableAllWindowsFunctionality) {
                //! don't delay showing preview in normal states,
                //! that is when the dock wasn't hidden
                if (!hoveredTimer.running && !windowsPreviewDlg.visible) {
                    //! first task with no previews shown can trigger the delay
                    hoveredTimer.start();
                } else if (windowsPreviewDlg.visible) {
                    //! when the previews are already shown, update them immediately
                    taskItem.showPreviewWindow();

                    if (taskItem.isWindow && root.highlightWindows) {
                        root.windowsHovered(model.WinIdList, taskItem.containsMouse);
                    }
                }
            }
        }

        taskItem.showPreviewsIsBlockedFromReleaseEvent = false;

        if (root.autoScrollTasksEnabled) {
            scrollableList.autoScrollFor(taskItem, false);
        }
    }

    onExited: {
        taskItem.isAbleToShowPreview = true;

        if (root.showPreviews) {
            root.hidePreview(17.5);
        }
    }

    // IMPORTANT: This must be improved ! even for small milliseconds  it reduces performance
    onPositionChanged: (mouse) => {
        if (taskItem.abilities.myView.isReady && !taskItem.abilities.myView.isShownFully) {
            return;
        }

        if((inAnimation == false)&&(!root.taskInAnimation)&&(!root.disableRestoreZoom) && hoverEnabled){
            // mouse.button is always 0 here, hence checking with mouse.buttons
            if (pressX != -1 && mouse.buttons == Qt.LeftButton
                    && isDragged
                    && (Math.abs(pressX - mouse.x) + Math.abs(pressY - mouse.y) >= Qt.styleHints.startDragDistance) ) {
                taskItem.contentItem.monochromizedItem.grabToImage((result) => {
                    pressX = -1;
                    pressY = -1;
                    root.dragSource = taskItem;
                    dragHelper.Drag.imageSource = result.url;
                    dragHelper.Drag.mimeData = backend.generateMimeData(model.MimeType, model.MimeData, model.LauncherUrlWithoutIcon);
                    dragHelper.Drag.active = true;
                });
            }
        }
    }

    onContainsMouseChanged:{
        if(!containsMouse && !inAnimation) {
            pressed=false;
        }

        ////disable hover effect///
        if (isWindow && root.highlightWindows && !containsMouse) {
            root.windowsHovered(model.WinIdList, false);
        }
    }

    onPressed: (mouse) => {
        //console.log("Pressed Task Delegate..");
        if (LatteCore.WindowSystem.compositingActive && !LatteCore.WindowSystem.isPlatformWayland) {
            if(root.leftClickAction !== LatteTasks.Types.PreviewWindows) {
                isAbleToShowPreview = false;
                windowsPreviewDlg.hide(2);
            }
        }

        slotPublishGeometries();

        var modAccepted = modifierAccepted(mouse);

        if ((mouse.button == Qt.LeftButton)||(mouse.button == Qt.MiddleButton) || modAccepted) {
            lastButtonClicked = mouse.button;
            pressed = true;
            pressX = mouse.x;
            pressY = mouse.y;

            if(!modAccepted){
                _resistanerTimer.start();
            }
        }
        else if (mouse.button == Qt.RightButton && !modAccepted){
            // When we're a launcher, there's no window controls, so we can show all
            // places without the menu getting super huge.
            if (model.IsLauncher === true && !isSeparator) {
                showContextMenu({showAllPlaces: true})
            } else {
                showContextMenu();
            }
        }
    }

    onReleased: (mouse) => {
        //console.log("Released Task Delegate...");
        _resistanerTimer.stop();

        if(pressed && (!inBlockingAnimation || inAttentionBuiltinAnimation) && !isSeparator){

            if (modifierAccepted(mouse) && !root.disableAllWindowsFunctionality){
                if( !taskItem.isLauncher ){
                    executeStandardAction(root.modifierClickAction, false);
                } else {
                    activateTask();
                }
            } else if (mouse.button == Qt.MiddleButton && !root.disableAllWindowsFunctionality){
                if( !taskItem.isLauncher ){
                    executeStandardAction(root.middleClickAction, true);
                } else {
                    taskMouseArea.recordMiddleClickDispatch("activate");
                    activateTask();
                }
            } else if (mouse.button == Qt.LeftButton){
                var canPresentWindowsIsSupported = LatteCore.WindowSystem.compositingActive && backend.windowViewAvailable;

                if( !taskItem.isLauncher && !root.disableAllWindowsFunctionality ){
                    if ( (root.leftClickAction === LatteTasks.Types.PreviewWindows && isGroupParent)
                            || ( !canPresentWindowsIsSupported
                                && root.leftClickAction === LatteTasks.Types.PresentWindows
                                && isGroupParent) ) {
                        if(windowsPreviewDlg.activeItem !== taskItem || !windowsPreviewDlg.visible){
                            showPreviewWindow();
                        } else {
                            forceHidePreview(21.1);
                        }
                    } else if ( (root.leftClickAction === LatteTasks.Types.PresentWindows && !(isGroupParent && !LatteCore.WindowSystem.compositingActive))
                               || ((root.leftClickAction === LatteTasks.Types.PreviewWindows && !isGroupParent)) ) {
                        activateTask();
                    } else if (root.leftClickAction === LatteTasks.Types.CycleThroughTasks) {
                        if (isGroupParent) {
                            subWindows.activateNextTask();
                        } else {
                            activateTask();
                        }
                    }
                } else {
                    activateTask();
                }
            }

            backend.cancelHighlightWindows();
        }

        pressed = false;
    }

    //! Qt5 fired past mainAngle = delta/8 > 12 on the dominant axis of
    //! angleDelta, ties to horizontal (EX-15: the wheel math lives in
    //! LatteCore.WheelStepper; verticalIsDominant is the same authority
    //! the DominantAxis pick uses, so the parallel-scroll read below can
    //! never drift from the fired direction)
    LatteCore.WheelStepper {
        id: taskWheelStepper
        axisPick: LatteCore.WheelStepper.DominantAxis
        fireThreshold: 96
    }

    onWheel: (wheel) => {
        var wheelActionsEnabled = (root.taskScrollAction !== LatteTasks.Types.ScrollNone || root.manualScrollTasksEnabled);

        if (isSeparator
                || wheelIsBlocked
                || !wheelActionsEnabled
                || inBouncingAnimation
                || !taskItem.abilities.myView.isShownFully){

            return;
        }

        wheelIsBlocked = true;
        scrollDelayer.start();

        var direction = taskWheelStepper.add(wheel.angleDelta, false);
        var verticalDirection = taskWheelStepper.verticalIsDominant(wheel.angleDelta);

        var parallelScrolling = (verticalDirection && Plasmoid.formFactor === PlasmaCore.Types.Vertical)
                || (!verticalDirection && Plasmoid.formFactor === PlasmaCore.Types.Horizontal);

        if (direction > 0) {
            slotPublishGeometries();

            var overflowScrollingAccepted = (root.manualScrollTasksEnabled
                                             && scrollableList.contentsExceed
                                             && (root.manualScrollTasksType === LatteTasks.Types.ManualScrollVerticalHorizontal
                                                 || (root.manualScrollTasksType === LatteTasks.Types.ManualScrollOnlyParallel && parallelScrolling)) );


            if (overflowScrollingAccepted) {
                scrollableList.decreasePos();
            } else {
                if (isLauncher || root.disableAllWindowsFunctionality) {
                    taskItem.activateLauncher();
                } else if (isGroupParent) {
                    subWindows.activateNextTask();
                } else {
                    var taskIndex = modelIndex();

                    if (isMinimized) {
                        tasksModel.requestToggleMinimized(taskIndex);
                    }

                    tasksModel.requestActivate(taskIndex);
                }

                // hidePreviewWindow();
            }
        } else if (direction < 0) {
            slotPublishGeometries();

            var overflowScrollingAccepted = (root.manualScrollTasksEnabled
                                             && scrollableList.contentsExceed
                                             && (root.manualScrollTasksType === LatteTasks.Types.ManualScrollVerticalHorizontal
                                                 || (root.manualScrollTasksType === LatteTasks.Types.ManualScrollOnlyParallel && parallelScrolling)) );


            if (overflowScrollingAccepted) {
                scrollableList.increasePos();
            } else {
                if (isLauncher || root.disableAllWindowsFunctionality) {
                    // do nothing
                } else if (isGroupParent) {
                    if (root.taskScrollAction === LatteTasks.Types.ScrollToggleMinimized) {
                        subWindows.minimizeTask();
                    } else {
                        subWindows.activatePreviousTask();
                    }
                } else {
                    var taskIndex = modelIndex();

                    var hidingTask = (!isMinimized && root.taskScrollAction === LatteTasks.Types.ScrollToggleMinimized);

                    if (isMinimized || hidingTask) {
                        tasksModel.requestToggleMinimized(taskIndex);
                    }

                    if (!hidingTask) {
                        tasksModel.requestActivate(taskIndex);
                    }
                }

                // hidePreviewWindow();
            }
        }
    }

    //A Timer to check how much time the task is hovered in order to check if we must
    //show window previews
    Timer {
        id: _hoveredTimer
        interval: Math.max(150,Plasmoid.configuration.previewsDelay)
        repeat: false

        onTriggered: {
            if (root.disableAllWindowsFunctionality || !isAbleToShowPreview) {
                return;
            }

            if (taskItem.containsMouse) {
                if (root.showPreviews || (windowsPreviewDlg.visible && !isLauncher)) {
                    taskItem.showPreviewWindow();
                }

                if (taskItem.isWindow && root.highlightWindows) {
                    root.windowsHovered(model.WinIdList, taskItem.containsMouse);
                }
            }
        }
    }

    //A Timer to help in resist a bit to dragging, the user must try
    //to press a little first before dragging Started
    Timer {
        id: _resistanerTimer
        interval: taskItem.resistanceDelay
        repeat: false

        onTriggered: {
            if (!taskItem.inBlockingAnimation){
                taskItem.isDragged = true;
            }

            if (taskItem.abilities.debug.timersEnabled) {
                console.log("plasmoid timer: resistanerTimer called...");
            }
        }
    }

}
