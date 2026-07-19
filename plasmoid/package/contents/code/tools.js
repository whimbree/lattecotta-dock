/*
    SPDX-FileCopyrightText: 2016 Smith AR <audoban@openmailbox.org>
    SPDX-FileCopyrightText: 2016-2018 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

.import org.kde.latte.core 0.2 as LatteCore

function wheelActivateNextPrevTask(wheelDelta, eventDelta) {
    // magic number 120 for common "one click"
    // See: http://qt-project.org/doc/qt-5/qml-qtquick-wheelevent.html#angleDelta-prop
    wheelDelta += eventDelta;
    var increment = 0;
    while (wheelDelta >= 120) {
        wheelDelta -= 120;
        increment++;
    }
    while (wheelDelta <= -120) {
        wheelDelta += 120;
        increment--;
    }
    while (increment != 0) {
        activateNextPrevTask(increment < 0)
        increment += (increment < 0) ? 1 : -1;
    }

    return wheelDelta;
}

function activateTask(index, model, modifiers, task) {
    if (modifiers & Qt.ControlModifier) {
        tasksModel.requestNewInstance(index);
    } else if (task.isGroupParent) {
        task.activateNextTask();
       // if (backend.canPresentWindows()) {
        //    backend.presentWindows(model.LegacyWinIdList);
       // }
        /*} else if (groupDialog.visible) {
            groupDialog.visible = false;
        } else {
            groupDialog.visualParent = task;
            groupDialog.visible = true;
        }*/
    } else {
        if (model.IsMinimized === true) {
            tasksModel.requestToggleMinimized(index);
            tasksModel.requestActivate(index);
        } else if (model.IsActive === true) {
            tasksModel.requestToggleMinimized(index);
        } else {
            tasksModel.requestActivate(index);
        }
    }
}


//! The bar's task delegates in MODEL order. icList's contentItem holds them
//! in CREATION order plus view chrome (the same collection main.qml's
//! taskExists() and icList's childAtPos() walk, with the same
//! objectName === "TaskItem" discriminator), so order by itemIndex; a dying
//! delegate reports itemIndex -1 and drops out.
function tasksInBarOrder() {
    var children = icList.contentItem.children;
    var byIndex = [];

    for (var i = 0; i < children.length; ++i) {
        var child = children[i];
        if (child.objectName !== "TaskItem" || child.itemIndex < 0) {
            continue;
        }
        byIndex[child.itemIndex] = child;
    }

    var ordered = [];
    for (var i = 0; i < byIndex.length; ++i) {
        if (byIndex[i] !== undefined) {
            ordered.push(byIndex[i]);
        }
    }

    return ordered;
}

//! Thin shell over LatteCore.WindowCycler (units/windowcycler.h; the
//! containment loaders/Tasks.qml activateNextPrevTask is its twin): the
//! core owns the launcher/startup filtering, group expansion and
//! wraparound choice; the live parts kept here are the bar-order delegate
//! walk above, the model-index construction and the activeTask identity
//! match.
function activateNextPrevTask(next) {
    var tasks = tasksInBarOrder();
    var entries = [];

    for (var i = 0; i < tasks.length; ++i) {
        entries.push({
            isLauncher: tasks[i].isLauncher === true,
            isStartup: tasks[i].isStartup === true,
            isGroupParent: tasks[i].isGroupParent === true,
            childCount: tasks[i].isGroupParent === true ? tasksModel.rowCount(tasks[i].modelIndex()) : 0
        });
    }

    var positions = LatteCore.WindowCycler.flattenTasksForCycling(entries);
    var activeTaskIndex = tasksModel.activeTask;
    var taskIndexList = [];
    var activeAt = -1;

    for (var p = 0; p < positions.length; ++p) {
        var task = tasks[positions[p].row];
        var modelIndex = (positions[p].childRow >= 0
                ? tasksModel.makeModelIndex(task.itemIndex, positions[p].childRow)
                : task.modelIndex());

        if (modelIndex === activeTaskIndex) {
            activeAt = p;
        }

        taskIndexList.push(modelIndex);
    }

    if (!taskIndexList.length) {
        // a bar of launchers only: nothing to cycle (Qt5 behavior)
        return;
    }

    var target = LatteCore.WindowCycler.selectAdjacentTask(taskIndexList.length, activeAt, next);

    if (target < 0) {
        // only reachable through the wrapper's malformed-input refusal,
        // which already reported the bug loudly
        return;
    }

    tasksModel.requestActivate(taskIndexList[target]);
}

//! insertIndexAt moved to C++ (LatteCore.DropClassifier, EX-14 in
//! docs/tracking/QML_EXTRACTION_PLAN.md); MouseHandler.qml was its only caller.
