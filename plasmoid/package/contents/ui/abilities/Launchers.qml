/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.0

import org.kde.plasma.plasmoid 2.0

import org.kde.latte.core 0.2 as LatteCore
import org.kde.latte.private.tasks 0.1 as LatteTasks

import "launchers" as LaunchersPart

Item {
    id: _launchers
    readonly property bool isActive: bridge !== null

    signal launcherChanged(string launcherUrl);
    signal launcherRemoved(string launcherUrl);

    //! triggered just before action happens. They are used mostly for animation purposes
    signal launcherInAdding(string launcherUrl);
    signal launcherInRemoving(string launcherUrl);
    signal launcherInMoving(string launcherUrl, int pos);

    signal disabledIsStealingDroppedLaunchers();

    property bool isStealingDroppedLaunchers: false
    property bool isShowingAddLaunchersMessage: false

    property bool __isLoadedDuringViewStartup: false

    property string appletIndex: bridge && bridge.indexer ? String(bridge.indexer.appletIndex) : ""
    property int group: LatteCore.Types.UniqueLaunchers
    property string groupId: view && group === LatteCore.Types.UniqueLaunchers ? String(view.groupId) + "#" + appletIndex : ""

    property Item bridge: null
    property Item layout: null
    property Item view: null
    property QtObject tasksModel: null

    //! live session state injected at the instantiation site (main.qml),
    //! so the reads below stay off the context chain
    property QtObject activityInfo: null
    property bool inDraggingPhase: false

    readonly property LaunchersPart.Syncer syncer: LaunchersPart.Syncer{}
    readonly property LaunchersPart.Validator validator: LaunchersPart.Validator{
        launchersAbility: _launchers
    }

    readonly property string _NULLACTIVITYID_: "00000000-0000-0000-0000-000000000000"

    function inUniqueGroup() {
        return group === LatteCore.Types.UniqueLaunchers;
    }

    function inLayoutGroup() {
        return group === LatteCore.Types.LayoutLaunchers;
    }

    function inGlobalGroup() {
        return group === LatteCore.Types.GlobalLaunchers;
    }

    function isSeparator(launcher) {
        return LatteTasks.LauncherListOps.isSeparatorName(launcher);
    }

    function separatorExists(separator: string) : bool {
        return (_launchers.tasksModel.launcherPosition(separator)>=0);
    }

    //! first free internal separator name, "" when all 19 are taken; the
    //! model query stays here (launcherPosition is live state), the
    //! candidate space and the allocation decision live in the core
    function freeAvailableSeparatorName() : string {
        var taken = LatteTasks.LauncherListOps.separatorCandidates().filter(separatorExists);
        return LatteTasks.LauncherListOps.freeSeparatorName(taken);
    }

    function hasLauncher(url) {
        return _launchers.tasksModel.launcherPosition(url) >= 0;
    }

    function addLauncher(launcherUrl) {
        if (bridge) {
            bridge.launchers.host.addSyncedLauncher(syncer.clientId,
                                                    _launchers.group,
                                                    _launchers.groupId,
                                                    launcherUrl);
        } else {
            _launchers.tasksModel.requestAddLauncher(launcherUrl);
            _launchers.launcherChanged(launcherUrl);
        }
    }

    function addDroppedLauncher(launcherUrl) {
        //workaround to protect in case the launcher contains the iconData
        var pos = launcherUrl.indexOf("?iconData=");

        if (pos>0) {
            launcherUrl = launcherUrl.substring( 0, launcherUrl.indexOf("?iconData=" ) );
        }

        var path = launcherUrl;
        var filename = path.split("/").pop();
        _launchers.launcherInAdding(filename);

        tasksModel.requestAddLauncher(launcherUrl);
        _launchers.launcherChanged(launcherUrl);
        tasksModel.syncLaunchers();
    }

    function addDroppedLaunchers(urls) {
        //! inform synced docks for new dropped launchers
        if (bridge) {
            bridge.launchers.host.addDroppedLaunchers(syncer.clientId,
                                                      _launchers.group,
                                                      _launchers.groupId,
                                                      urls);
        } else {
            urls.forEach(function (item) {
                addDroppedLauncher(item);
            });
        }
    }

    function addInternalSeparatorAtPos(pos) {
        var separatorName = freeAvailableSeparatorName();

        if (separatorName !== "") {
            _launchers.launcherInMoving(separatorName, Math.max(0,pos));
            addLauncher(separatorName);
        }
    }

    function removeInternalSeparatorAtPos(pos) {
        var item = childAtLayoutIndex(pos);

        if (item.isSeparator) {
            removeLauncher(item.launcherUrl);
        }
    }

    function removeLauncher(launcherUrl) {
        if (bridge) {
            bridge.launchers.host.removeSyncedLauncher(syncer.clientId,
                                                       _launchers.group,
                                                       _launchers.groupId,
                                                       launcherUrl);
        } else {
            _launchers.launcherInRemoving(launcherUrl);
            _launchers.tasksModel.requestRemoveLauncher(launcherUrl);
            _launchers.launcherRemoved(launcherUrl);
        }
    }

    function addLauncherToActivity(launcherUrl, activityId) {
        if (bridge) {
            bridge.launchers.host.addSyncedLauncherToActivity(syncer.clientId,
                                                              _launchers.group,
                                                              _launchers.groupId,
                                                              launcherUrl,
                                                              activityId);
        } else {
            if (activityId !== activityInfo.currentActivity && isOnAllActivities(launcherUrl)) {
                _launchers.launcherInRemoving(launcherUrl);
            }

            _launchers.tasksModel.requestAddLauncherToActivity(launcherUrl, activityId);
            _launchers.launcherChanged(launcherUrl);
        }
    }

    function removeLauncherFromActivity(launcherUrl, activityId) {
        if (bridge) {
            bridge.launchers.host.removeSyncedLauncherFromActivity(syncer.clientId,
                                                                   _launchers.group,
                                                                   _launchers.groupId,
                                                                   launcherUrl,
                                                                   activityId);
        } else {
            if (activityId === activityInfo.currentActivity) {
                _launchers.launcherInRemoving(launcherUrl);
            }
            _launchers.tasksModel.requestRemoveLauncherFromActivity(launcherUrl, activityId);
            _launchers.launcherChanged(launcherUrl);
        }
    }

    function validateSyncedLaunchersOrder() {
        if (bridge) {
            bridge.launchers.host.validateSyncedLaunchersOrder(syncer.clientId,
                                                               _launchers.group,
                                                               _launchers.groupId,
                                                               currentShownLauncherList());
        } else {
            /*validator.stop();
            validator.launchers = orderedLaunchers;
            validator.start();*/
        }
    }

    function inCurrentActivity(launcherUrl) {
        if (!hasLauncher(launcherUrl)) {
            return false;
        }

        var activities = _launchers.tasksModel.launcherActivities(launcherUrl);

        if (activities.length ===0
                || activities.indexOf(_NULLACTIVITYID_) >= 0
                || activities.indexOf(activityInfo.currentActivity) >= 0) {
            return true;
        }

        return false;
    }

    function isOnAllActivities(launcherUrl) {
        var activities = _launchers.tasksModel.launcherActivities(launcherUrl);
        return (activities.indexOf(_NULLACTIVITYID_) >= 0)
    }

    function childAtLayoutIndex(position) {
        var tasks = layout.children;

        if (position < 0) {
            return;
        }

        for(var i=0; i<tasks.length; ++i){
            var task = tasks[i];

            if (task.lastValidIndex === position
                    || (task.lastValidIndex === -1 && task.itemIndex === position )) {
                return task;
            }
        }

        return undefined;
    }

    function indexOfLayoutLauncher(url) {
        var tasks = layout.children;

        for(var i=0; i<tasks.length; ++i){
            var task = tasks[i];

            if (task && (task.launcherUrl===url)) {
                return task.itemIndex;
            }
        }

        return -1;
    }

    function currentShownLauncherList() {
        var launch = [];

        var tasks = _launchers.layout.children;
        for(var i=0; i<tasks.length; ++i){
            var task = _launchers.childAtLayoutIndex(i);

            if (task!==undefined && task.launcherUrl!=="" && _launchers.inCurrentActivity(task.launcherUrl)) {
                launch.push(task.launcherUrl);
            }
        }

        return launch;
    }


    //! uncalled Qt5-inherited ability API (verified against f0ad7b23: no
    //! caller there either); kept for the ability surface, the stored
    //! record grammar itself lives in the core (EX-11)
    function currentStoredLauncherList() {
        var launchersList = [];

        if (bridge && bridge.launchers.host.isReady) {
            if (!_launchers.inUniqueGroup()) {
                if (_launchers.inLayoutGroup()) {
                    launchersList = bridge.launchers.host.layoutLaunchers;
                } else if (_launchers.inGlobalGroup()) {
                    launchersList = bridge.launchers.host.universalLaunchers;
                }
            }
        } else {
            launchersList = Plasmoid.configuration.launchers59;
        }

        return LatteTasks.LauncherListOps.launchersForActivity(launchersList,
                                                               _launchers.activityInfo.currentActivity);
    }

    function importLauncherListInModel() {
        if (bridge && bridge.launchers.host.isReady && !inUniqueGroup()) {
            if (inLayoutGroup()) {
                console.log("Tasks: Applying LAYOUT Launchers List...");
                tasksModel.launcherList = bridge.launchers.host.layoutLaunchers;
            } else if (inGlobalGroup()) {
                console.log("Tasks: Applying GLOBAL Launchers List...");
                tasksModel.launcherList = bridge.launchers.host.universalLaunchers;
            }
        } else {
            console.log("Tasks: Applying UNIQUE Launchers List...");
            tasksModel.launcherList = Plasmoid.configuration.launchers59;
        }
    }


    //! Connections
    Component.onCompleted: {
        if (isActive) {
            bridge.launchers.client = _launchers;
        }
    }

    Component.onDestruction: {
        if (isActive) {
            bridge.launchers.client = null;
        }
    }

    onIsActiveChanged: {
        if (isActive) {
            bridge.launchers.client = _launchers;
        }
    }

    onGroupChanged:{
        // view is the same object appletAbilities.myView exposes (assigned
        // at the AppletAbilities instantiation), read off the context chain
        if(_launchers.view.isReady) {
            _launchers.importLauncherListInModel();
        }
    }

    Connections {
        target: _launchers.view
        function onIsReadyChanged() {
            if(_launchers.view.isReady) {
                if (!_launchers.inUniqueGroup()) {
                    _launchers.importLauncherListInModel();
                }
            }
        }
    }

    Connections {
        target: _launchers.bridge ? _launchers.bridge.launchers.host : null
        function onIsReadyChanged() {
            if (_launchers.bridge && _launchers.bridge.launchers.host.isReady && !_launchers.__isLoadedDuringViewStartup) {
                _launchers.__isLoadedDuringViewStartup = true;
                _launchers.importLauncherListInModel();
            }
        }
    }

    Connections {
        target: _launchers.tasksModel
        function onLauncherListChanged() {
            if (_launchers.bridge && _launchers.bridge.launchers.host.isReady) {
                if (!_launchers.inUniqueGroup()) {
                    if (_launchers.inLayoutGroup()) {
                        _launchers.bridge.launchers.host.setLayoutLaunchers(_launchers.tasksModel.launcherList);
                    } else if (_launchers.inGlobalGroup()) {
                        _launchers.bridge.launchers.host.setUniversalLaunchers(_launchers.tasksModel.launcherList);
                    }
                } else {
                    Plasmoid.configuration.launchers59 = _launchers.tasksModel.launcherList;
                }

                if (_launchers.inDraggingPhase) {
                    _launchers.validateSyncedLaunchersOrder();
                }
            } else if (!_launchers.view.isReady) {
                // This way we make sure that a delayed view.layout initialization does not store irrelevant launchers from different
                // group to UNIQUE launchers group
                Plasmoid.configuration.launchers59 = _launchers.tasksModel.launcherList;
            }
        }
    }
}
