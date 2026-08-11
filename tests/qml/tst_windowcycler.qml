/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Pins the group-window cycle target selection (EX-16) against the REAL
//! shipped SubWindows.qml, instantiated here and driven through its
//! activateNextTask()/activatePreviousTask()/minimizeTask() functions.
//! Written against the pre-extraction QML bodies and kept green across the
//! EX-16 cutover, so it is the twin-equivalence proof that the C++ core
//! preserves shipped behavior; the vectors in tests/units/windowcyclertest.cpp
//! are this table's, cross-checked here against the shipped QML on every run.
//!
//! Context plumbing mirrors production: SubWindows sits inside TaskItem's
//! tasksModel delegate, so its bare IsLauncher/IsStartup/IsGroupParent/index
//! reads are model roles - a one-row Repeater over a dynamicRoles ListModel
//! provides them the same way here. The inner window list (production: the
//! same tasksModel with the TaskItem's row as rootIndex) is a second
//! dynamicRoles ListModel reached through the context-chain name tasksModel,
//! with the libtaskmanager request slots replaced by recorders. Model
//! indexes travel as "row:child" string tokens so the === comparisons the
//! shipped code performs keep value semantics.

import QtQuick 2.7
import QtQml.Models 2.2
import QtTest 1.2

import "../../plasmoid/package/contents/ui/task" as Task

Item {
    id: root
    width: 400
    height: 200

    // plasmoid main.qml root properties SubWindows resolves by context
    property bool inDraggingPhase: false
    // TaskItem property the windowsCount binding resolves by context
    property bool isGroupParent: taskItem.isGroupParent

    //! the group's window list; roles are what the shipped delegate and the
    //! cycle functions read (display, IsActive, IsMinimized, WinIdList)
    ListModel {
        id: tasksModel
        dynamicRoles: true

        property var activateRequests: []
        property var minimizeToggleRequests: []

        function makeModelIndex(row, childRow) {
            return row + ":" + childRow;
        }
        function requestActivate(idx) {
            activateRequests.push(idx);
        }
        function requestToggleMinimized(idx) {
            minimizeToggleRequests.push(idx);
        }
    }

    //! rootIndex donor: taskItem.modelIndex() must hand SubWindows'
    //! DelegateModel a real (root) QModelIndex, exactly what the production
    //! group parent's modelIndex() yields for our flat window list
    DelegateModel {
        id: rootIndexDonor
        model: tasksModel
        delegate: Item {}
    }

    Item {
        id: taskItem
        property bool isGroupParent: true
        property bool isActive: false
        property bool isMinimized: false
        property bool isWindow: true
        property int itemIndex: 0
        signal checkWindowsStates()
        function modelIndex() {
            return rootIndexDonor.rootIndex;
        }
    }

    //! the outer delegate context (production: TaskItem's row in
    //! tasksModel); provides the bare role reads and the `index` the
    //! shipped functions resolve through the delegate context
    ListModel {
        id: outerModel
        dynamicRoles: true
        Component.onCompleted: append({
            IsLauncher: false,
            IsStartup: false,
            IsWindow: true,
            IsGroupParent: true
        })
    }

    Repeater {
        id: subWindowsRepeater
        model: outerModel
        delegate: Task.SubWindows {}
    }

    TestCase {
        name: "WindowCyclerSubWindows"
        when: windowShown

        //! ListModel/dynamicRoles stores JS arrays as nested ListModels
        //! (no .length); an array-like object survives the round trip and
        //! serves the exact reads the shipped code performs on the real
        //! QVariantList roles ([0] and !== undefined)
        function arrayLike(values) {
            var wrapped = { length: values.length };
            for (var i = 0; i < values.length; ++i) {
                wrapped[i] = values[i];
            }
            return wrapped;
        }

        function subWindows() {
            var item = subWindowsRepeater.itemAt(0);
            verify(item !== null, "SubWindows instantiated");
            return item;
        }

        //! rows: [{winId, active, minimized}] - wayland-shaped string ids
        function setWindows(rows) {
            tasksModel.clear();
            tasksModel.activateRequests = [];
            tasksModel.minimizeToggleRequests = [];

            for (var i = 0; i < rows.length; ++i) {
                tasksModel.append({
                    display: "win-" + rows[i].winId,
                    IsActive: rows[i].active === true,
                    IsMinimized: rows[i].minimized === true,
                    WinIdList: arrayLike([rows[i].winId])
                });
            }

            var sub = subWindows();
            sub.lastActiveWinInGroup = -1;
            taskItem.isGroupParent = true;
            //! DelegateModel absorbs the rows through the event loop
            tryCompare(sub, "windowsCount", rows.length);
            return sub;
        }

        // --- activateNextTask -------------------------------------------

        function test_nextFromActiveMidList() {
            var sub = setWindows([
                { winId: "uuid-a", active: true },
                { winId: "uuid-b" },
                { winId: "uuid-c" }
            ]);
            sub.activateNextTask();
            compare(tasksModel.activateRequests, ["0:1"]);
        }

        function test_nextWrapsPastEnd() {
            var sub = setWindows([
                { winId: "uuid-a" },
                { winId: "uuid-b" },
                { winId: "uuid-c", active: true }
            ]);
            sub.activateNextTask();
            compare(tasksModel.activateRequests, ["0:0"]);
        }

        function test_nextSingleActiveWindowWrapsToItself() {
            var sub = setWindows([{ winId: "uuid-a", active: true }]);
            sub.activateNextTask();
            compare(tasksModel.activateRequests, ["0:0"]);
        }

        function test_nextNoActiveFallsToLastActive() {
            var sub = setWindows([
                { winId: "uuid-a" },
                { winId: "uuid-b" },
                { winId: "uuid-c" }
            ]);
            sub.lastActiveWinInGroup = "uuid-b";
            sub.activateNextTask();
            compare(tasksModel.activateRequests, ["0:1"]);
        }

        function test_nextNoActiveStaleLastActiveFallsToFirst() {
            var sub = setWindows([
                { winId: "uuid-a" },
                { winId: "uuid-b" }
            ]);
            sub.lastActiveWinInGroup = "uuid-gone";
            sub.activateNextTask();
            compare(tasksModel.activateRequests, ["0:0"]);
        }

        function test_nextNoActiveNoLastActiveFallsToFirst() {
            var sub = setWindows([
                { winId: "uuid-a" },
                { winId: "uuid-b" }
            ]);
            sub.activateNextTask();
            compare(tasksModel.activateRequests, ["0:0"]);
        }

        function test_nextMultipleActivesFirstWins() {
            var sub = setWindows([
                { winId: "uuid-a" },
                { winId: "uuid-b", active: true },
                { winId: "uuid-c", active: true },
                { winId: "uuid-d" }
            ]);
            sub.activateNextTask();
            compare(tasksModel.activateRequests, ["0:2"]);
        }

        function test_nextNotGroupParentDoesNothing() {
            var sub = setWindows([
                { winId: "uuid-a", active: true },
                { winId: "uuid-b" }
            ]);
            taskItem.isGroupParent = false;
            sub.activateNextTask();
            compare(tasksModel.activateRequests, []);
        }

        // --- activatePreviousTask ---------------------------------------

        function test_previousFromActiveMidList() {
            var sub = setWindows([
                { winId: "uuid-a" },
                { winId: "uuid-b", active: true },
                { winId: "uuid-c" }
            ]);
            sub.activatePreviousTask();
            compare(tasksModel.activateRequests, ["0:0"]);
        }

        function test_previousWrapsPastStart() {
            var sub = setWindows([
                { winId: "uuid-a", active: true },
                { winId: "uuid-b" },
                { winId: "uuid-c" }
            ]);
            sub.activatePreviousTask();
            compare(tasksModel.activateRequests, ["0:2"]);
        }

        function test_previousMultipleActivesLastWins() {
            var sub = setWindows([
                { winId: "uuid-a", active: true },
                { winId: "uuid-b" },
                { winId: "uuid-c", active: true }
            ]);
            sub.activatePreviousTask();
            compare(tasksModel.activateRequests, ["0:1"]);
        }

        function test_previousNoActiveFallsToLastActive() {
            var sub = setWindows([
                { winId: "uuid-a" },
                { winId: "uuid-b" },
                { winId: "uuid-c" }
            ]);
            sub.lastActiveWinInGroup = "uuid-c";
            sub.activatePreviousTask();
            compare(tasksModel.activateRequests, ["0:2"]);
        }

        function test_previousNoActiveStaleLastActiveFallsToFirst() {
            var sub = setWindows([
                { winId: "uuid-a" },
                { winId: "uuid-b" }
            ]);
            sub.lastActiveWinInGroup = "uuid-gone";
            sub.activatePreviousTask();
            compare(tasksModel.activateRequests, ["0:0"]);
        }

        function test_previousNotGroupParentDoesNothing() {
            var sub = setWindows([
                { winId: "uuid-a", active: true },
                { winId: "uuid-b" }
            ]);
            taskItem.isGroupParent = false;
            sub.activatePreviousTask();
            compare(tasksModel.activateRequests, []);
        }

        // --- minimizeTask -----------------------------------------------

        function test_minimizeTogglesActiveWindow() {
            var sub = setWindows([
                { winId: "uuid-a" },
                { winId: "uuid-b", active: true },
                { winId: "uuid-c" }
            ]);
            sub.minimizeTask();
            compare(tasksModel.minimizeToggleRequests, ["0:1"]);
        }

        function test_minimizeMultipleActivesLastWins() {
            var sub = setWindows([
                { winId: "uuid-a", active: true },
                { winId: "uuid-b", active: true },
                { winId: "uuid-c" }
            ]);
            sub.minimizeTask();
            compare(tasksModel.minimizeToggleRequests, ["0:1"]);
        }

        function test_minimizeNoActiveFallsToShownLastActive() {
            var sub = setWindows([
                { winId: "uuid-a" },
                { winId: "uuid-b" },
                { winId: "uuid-c" }
            ]);
            sub.lastActiveWinInGroup = "uuid-b";
            sub.minimizeTask();
            compare(tasksModel.minimizeToggleRequests, ["0:1"]);
        }

        function test_minimizeSkipsMinimizedLastActive() {
            var sub = setWindows([
                { winId: "uuid-a" },
                { winId: "uuid-b", minimized: true },
                { winId: "uuid-c" }
            ]);
            sub.lastActiveWinInGroup = "uuid-b";
            sub.minimizeTask();
            //! the minimized last-active is refused; the ladder falls to the
            //! last non-minimized window scanning from the end
            compare(tasksModel.minimizeToggleRequests, ["0:2"]);
        }

        function test_minimizeNoActiveNoLastActiveFallsToLastShown() {
            var sub = setWindows([
                { winId: "uuid-a" },
                { winId: "uuid-b" },
                { winId: "uuid-c", minimized: true }
            ]);
            sub.minimizeTask();
            compare(tasksModel.minimizeToggleRequests, ["0:1"]);
        }

        function test_minimizeAllMinimizedDoesNothing() {
            var sub = setWindows([
                { winId: "uuid-a", minimized: true },
                { winId: "uuid-b", minimized: true }
            ]);
            sub.minimizeTask();
            compare(tasksModel.minimizeToggleRequests, []);
        }

        function test_minimizeNotGroupParentDoesNothing() {
            var sub = setWindows([
                { winId: "uuid-a", active: true }
            ]);
            taskItem.isGroupParent = false;
            sub.minimizeTask();
            compare(tasksModel.minimizeToggleRequests, []);
        }

        // --- empty group (EX-16's recorded deviation from Qt5) ----------
        //! Qt5 fired an invalid-index activation request at a childless
        //! group parent; the cutover warns and does nothing instead

        function test_nextOnChildlessGroupWarnsInsteadOfFiring() {
            var sub = setWindows([]);
            ignoreWarning(new RegExp(".*no windows to cycle.*"));
            sub.activateNextTask();
            compare(tasksModel.activateRequests, []);
        }

        function test_previousOnChildlessGroupWarnsInsteadOfFiring() {
            var sub = setWindows([]);
            ignoreWarning(new RegExp(".*no windows to cycle.*"));
            sub.activatePreviousTask();
            compare(tasksModel.activateRequests, []);
        }

        function test_minimizeOnChildlessGroupStaysSilent() {
            //! nothing-to-toggle is a normal state, not a warning (Qt5
            //! already did nothing here)
            var sub = setWindows([]);
            sub.minimizeTask();
            compare(tasksModel.minimizeToggleRequests, []);
        }

        // --- last-active tracking through the shipped delegate ----------

        function test_delegateActivationRecordsLastActiveWinId() {
            var sub = setWindows([
                { winId: "uuid-a" },
                { winId: "uuid-b" }
            ]);
            //! flipping a row's IsActive drives the shipped delegate's
            //! onIsActiveChanged, which must record the FIRST WinIdList
            //! entry as the group's last-active window
            tasksModel.setProperty(1, "IsActive", true);
            tryCompare(sub, "lastActiveWinInGroup", "uuid-b");

            tasksModel.setProperty(1, "IsActive", false);
            sub.activateNextTask();
            compare(tasksModel.activateRequests, ["0:1"]);
        }
    }
}
