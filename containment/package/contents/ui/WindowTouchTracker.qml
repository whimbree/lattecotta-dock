/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.15

import org.kde.taskmanager 0.1 as TaskManager

Item {
    id: trackerRoot

    required property QtObject dockView

    visible: false
    width: 0
    height: 0

    TaskManager.VirtualDesktopInfo {
        id: virtualDesktopInfo
    }

    TaskManager.ActivityInfo {
        id: activityInfo
    }

    TaskManager.TasksModel {
        id: tasksModel

        virtualDesktop: virtualDesktopInfo.currentDesktop
        activity: activityInfo.currentActivity

        filterByVirtualDesktop: true
        filterByActivity: true
        //! A spanning window must be evaluated independently against every
        //! view's stable trigger. Screen filtering would remove it from one
        //! side before the exact geometry intersection can make that choice.
        filterByScreen: false

        groupMode: TaskManager.TasksModel.GroupDisabled
        sortMode: TaskManager.TasksModel.SortDisabled
    }

    Binding {
        target: trackerRoot.dockView
                ? trackerRoot.dockView.windowTouchTracker
                : null
        property: "model"
        when: trackerRoot.dockView
              && trackerRoot.dockView.windowTouchTracker
        value: tasksModel
        restoreMode: Binding.RestoreNone
    }
}
