/*
    SPDX-FileCopyrightText: 2016 Smith AR <audoban@openmailbox.org>
    SPDX-FileCopyrightText: 2016 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.0
import QtQml.Models 2.2

import org.kde.latte.core 0.2 as LatteCore

//trying to do a very simple thing to count how many windows does
//a task instance has...
//Workaround the mess with launchers, startups, windows etc.

Item{
    id: windowsContainer
    property int windowsCount: {
        if (isLauncher || isStartup) {
            return 0;
        }

        if (isGroupParent) {
            return windowsRepeater.count;
        }

        return 1;
    }

    property int windowsMinimized: 0

    property bool isLauncher: IsLauncher ? true : false
    property bool isStartup: IsStartup ? true : false
    property bool isWindow: IsWindow ? true : false

    //! Holds a window id from WinIdList, or -1 for "none". On Plasma 6 Wayland
    //! those ids are QString UUIDs, not ints, so this must be var: an int
    //! property threw "Cannot assign QString to int" on every activation change
    //! and never tracked the active window, breaking group cycling. The -1
    //! sentinel and the identity comparisons below still work with strings.
    property var lastActiveWinInGroup: -1

    //states that exist in windows in a Group of windows
    property bool hasMinimized: false;
    property bool hasShown: false;
    property bool hasActive: false;

    Repeater{
        id: windowsRepeater
        model:DelegateModel {
            id: windowsLocalModel
            model: tasksModel

            delegate: Item{
                readonly property string title: display !== undefined ? display : ""
                readonly property bool isMinimized: IsMinimized === true ? true : false
                readonly property bool isActive: IsActive === true ? true : false

                onIsMinimizedChanged: windowsContainer.updateStates();
                onIsActiveChanged:  {
                    if (isActive) {
                        var winIdList = WinIdList;
                        windowsContainer.lastActiveWinInGroup = (winIdList!==undefined ? winIdList[0] : 0);
                    }
                    windowsContainer.updateStates();
                }
            }

            Component.onCompleted: {
                rootIndex = taskItem.modelIndex();
            }
        }
    }

    Connections{
        target: taskItem
        function onItemIndexChanged() {
            windowsContainer.updateStates();
        }
    }

    Connections{
        target: root
        function onInDraggingPhaseChanged() {
            windowsContainer.updateStates();
        }
    }

    //! try to give the time to the model to update its states in order to
    //! avoid any suspicious crashes during dragging grouped tasks that
    //! are synced between multiple panels/docks. At the same time in updateStates()
    //! function we block any DelegateModel updates when the user is dragging
    //! a task because this could create crashes
    Timer{
        id: initializeStatesTimer
        interval: 200
        onTriggered: windowsContainer.initializeStates();
    }

    function updateStates() {
        if (!root.inDraggingPhase) {
            initializeStatesTimer.start();
        }
    }

    function initializeStates(){
        windowsLocalModel.rootIndex = taskItem.modelIndex();

        hasMinimized = false;
        hasShown = false;
        hasActive = false;

        if(IsGroupParent){
            checkInternalStates();
        } else {
            var minimized = 0;

            if(taskItem.isActive)
                hasActive = true;

            if(taskItem.isMinimized){
                hasMinimized = true;
                minimized = minimized + 1;
            } else if (taskItem.isWindow) {
                hasShown = true;
            }

            windowsMinimized = minimized;
        }
    }

    function checkInternalStates(){
        var childs = windowsLocalModel.items;

        var minimized = 0;

        for(var i=0; i<childs.count; ++i){
            var kid = childs.get(i);

            if (kid.model.IsActive)
                hasActive = true;

            if(kid.model.IsMinimized) {
                hasMinimized = true;
                minimized = minimized + 1;
            } else if (kid.model.IsWindow) {
                hasShown = true;
            }
        }

        windowsMinimized = minimized;
    }

    function windowsTitles() {
        windowsLocalModel.rootIndex = taskItem.modelIndex();
        var result = new Array;
        var childs = windowsLocalModel.items;

        for(var i=0; i<childs.count; ++i){
            var kid = childs.get(i);
            var title = kid.model.display

            result.push(title);
        }

        return result;
    }

    //! One snapshot shape for all three cycle functions; the selection
    //! logic lives in LatteCore.WindowCycler (units/windowcycler.h). The
    //! WinIdList-undefined guard is the Qt5 activateNextTask body's - its
    //! prev/minimize mirrors read [0] unguarded and could throw on a role
    //! the model had not filled yet; unified on the guarded copy.
    function _snapshotGroupWindows() {
        var snapshot = [];
        var childs = windowsLocalModel.items;

        for (var i = 0; i < childs.count; ++i) {
            var kid = childs.get(i);
            var winIdList = kid.model.WinIdList;

            snapshot.push({
                winId: (winIdList !== undefined ? winIdList[0] : 0),
                isActive: kid.model.IsActive === true,
                isMinimized: kid.model.IsMinimized === true
            });
        }

        return snapshot;
    }

    //! cycle activation forward through the group's windows
    function activateNextTask() {
        windowsLocalModel.rootIndex = taskItem.modelIndex();

        if (!taskItem.isGroupParent) {
            return;
        }

        var target = LatteCore.WindowCycler.selectNext(windowsContainer._snapshotGroupWindows(),
                                                       windowsContainer.lastActiveWinInGroup);

        if (target < 0) {
            //! Qt5 fired an invalid-index activation request here; a group
            //! parent with no window rows is a model state worth hearing about
            console.warn("SubWindows.activateNextTask: group parent with no windows to cycle");
            return;
        }

        tasksModel.requestActivate(tasksModel.makeModelIndex(index, target));
    }

    //! cycle activation backward through the group's windows
    function activatePreviousTask() {
        windowsLocalModel.rootIndex = taskItem.modelIndex();

        if (!taskItem.isGroupParent) {
            return;
        }

        var target = LatteCore.WindowCycler.selectPrevious(windowsContainer._snapshotGroupWindows(),
                                                           windowsContainer.lastActiveWinInGroup);

        if (target < 0) {
            console.warn("SubWindows.activatePreviousTask: group parent with no windows to cycle");
            return;
        }

        tasksModel.requestActivate(tasksModel.makeModelIndex(index, target));
    }

    //! toggle the minimized state of the group's front window
    function minimizeTask() {
        windowsLocalModel.rootIndex = taskItem.modelIndex();

        if (!taskItem.isGroupParent) {
            return;
        }

        var target = LatteCore.WindowCycler.selectMinimizeTarget(windowsContainer._snapshotGroupWindows(),
                                                                 windowsContainer.lastActiveWinInGroup);

        //! every window minimized is a normal state: nothing to toggle (Qt5
        //! behavior, no warning)
        if (target >= 0) {
            tasksModel.requestToggleMinimized(tasksModel.makeModelIndex(index, target));
        }
    }

    Component.onCompleted: {
        taskItem.checkWindowsStates.connect(initializeStates);
    }

    Component.onDestruction: {
        taskItem.checkWindowsStates.disconnect(initializeStates);
    }
}
