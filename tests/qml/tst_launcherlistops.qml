/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Pins the launcher-list behavior EX-11 extracts, against the REAL shipped
//! TasksExtendedManager.qml and launchers/Validator.qml, instantiated here
//! and driven through their public function surface. Written against the
//! pre-extraction QML bodies and kept green across the EX-11 cutovers, so
//! it is the twin-equivalence proof that the C++ core preserves shipped
//! behavior; tests/units/launcherlistopstest.cpp carries the same semantics
//! against the core directly.
//!
//! What is deliberately NOT pinned: the exact move SEQUENCE the validator
//! issues for non-adjacent permutations. Qt5's direction heuristic and the
//! EX-11 pull planner converge to the same final order but may differ in
//! intermediate moves (e.g. a 3-rotation is one Qt5 move, two planner
//! moves); only convergence, the final order, the single syncLaunchers,
//! and the settle/stop branches are observable contract. The adjacent-swap
//! single-move case IS pinned - both implementations do it in one move.
//!
//! Context plumbing (the tst_tooltiptext pattern): both shipped components
//! take their collaborators as injected properties (the EX-11 seams) -
//! launchersAbility/tasksModel for TasksExtendedManager, launchersAbility
//! for the Validator - assigned from stand-in objects on this root.

import QtQuick 2.7
import QtTest 1.2

import "../../plasmoid/package/contents/ui" as TasksUi
import "../../plasmoid/package/contents/ui/abilities/launchers" as LaunchersParts

Item {
    id: root
    width: 400
    height: 300

    // ---- stand-ins injected into TasksExtendedManager.qml ----

    property QtObject launchersAbility: QtObject {
        signal launcherInRemoving(string launcherUrl)
        signal launcherInAdding(string launcherUrl)
        signal launcherInMoving(string launcherUrl, int pos)

        property int validateCallCount: 0
        function validateSyncedLaunchersOrder() { validateCallCount++; }
    }

    property QtObject tasksModel: QtObject {
        property var moveLog: []
        property int syncCount: 0
        function move(from, to) { moveLog.push([from, to]); }
        function syncLaunchers() { syncCount++; }
    }

    TasksUi.TasksExtendedManager {
        id: extManager
        launchersAbility: root.launchersAbility
        tasksModel: root.tasksModel
    }

    // ---- stand-in injected into launchers/Validator.qml ----

    //! Simulates the launchers ability surface the validator drives: shown
    //! is the current shown-launcher order, and tasksModel.move mutates it
    //! the way libtaskmanager's row move reorders the real model.
    property QtObject _launchers: QtObject {
        id: fakeLaunchers

        property var shown: []
        //! launchers the layout lookup cannot resolve (returns -1), the
        //! shipped "launcher was not found in model" stop branch
        property var missingFromLayout: []

        function currentShownLauncherList() { return shown.slice(); }

        function indexOfLayoutLauncher(url) {
            if (missingFromLayout.indexOf(url) >= 0) {
                return -1;
            }
            return shown.indexOf(url);
        }

        property QtObject tasksModel: QtObject {
            property var moveLog: []
            property int syncCount: 0
            function move(from, to) {
                moveLog.push([from, to]);
                var list = fakeLaunchers.shown.slice();
                var item = list.splice(from, 1)[0];
                list.splice(to, 0, item);
                fakeLaunchers.shown = list;
            }
            function syncLaunchers() { syncCount++; }
        }
    }

    LaunchersParts.Validator {
        id: validator
        launchersAbility: root._launchers
    }

    SignalSpy {
        id: waitingRemovedSpy
        target: extManager
        signalName: "waitingLauncherRemoved"
    }

    TestCase {
        name: "LauncherListOps"
        when: windowShown

        function init() {
            // between-test reset: the GC path the shipped 30s timer runs.
            // Pre-cutover the arrays are QML properties spliced in place;
            // post-cutover the registries live in C++ behind clearRegistries()
            // (the GC handler's extracted body) - this test spans both lives.
            if (typeof extManager.clearRegistries === "function") {
                extManager.clearRegistries();
            } else {
                extManager.waitingLaunchers.splice(0, extManager.waitingLaunchers.length);
                extManager.immediateLaunchers.splice(0, extManager.immediateLaunchers.length);
                extManager.launchersToBeAdded.splice(0, extManager.launchersToBeAdded.length);
                extManager.launchersToBeRemoved.splice(0, extManager.launchersToBeRemoved.length);
                extManager.launchersToBeMoved.splice(0, extManager.launchersToBeMoved.length);
                extManager.frozenTasks.splice(0, extManager.frozenTasks.length);
            }
            extManager.launchersToBeMovedCount = 0;
            extManager.launchersToBeAddedCount = 0;
            extManager.launchersToBeRemovedCount = 0;
            waitingRemovedSpy.clear();
            root.launchersAbility.validateCallCount = 0;
            root.tasksModel.moveLog = [];
            root.tasksModel.syncCount = 0;

            validator.stop();
            validator.launchers = [];
            root._launchers.shown = [];
            root._launchers.missingFromLayout = [];
            root._launchers.tasksModel.moveLog = [];
            root._launchers.tasksModel.syncCount = 0;
        }

        // ---- waiting launchers: substring-equality registry ----

        function test_waitingLaunchersMatchBySubstring() {
            // the same launcher travels as bare url and as url?iconData=...
            // (launcherUrlWithIcon); substring matching makes the two
            // spellings one launcher - the WHY behind equals()
            extManager.addWaitingLauncher("file:///a.desktop");
            verify(extManager.waitingLauncherExists("file:///a.desktop"));
            verify(extManager.waitingLauncherExists("file:///a.desktop?iconData=payload"));

            // add-if-absent through the substring probe: the iconData
            // spelling is already covered, no second record
            extManager.addWaitingLauncher("file:///a.desktop?iconData=payload");
            compare(extManager.waitingLaunchersLength(), 1);
        }

        function test_removeWaitingLauncherSignalsOnlyOnRealRemoval() {
            extManager.addWaitingLauncher("file:///a.desktop");

            extManager.removeWaitingLauncher("file:///other.desktop");
            compare(waitingRemovedSpy.count, 0);

            extManager.removeWaitingLauncher("file:///a.desktop?iconData=x");
            compare(waitingRemovedSpy.count, 1);
            compare(extManager.waitingLaunchersLength(), 0);
        }

        function test_emptyUrlNeverMatchesWaiting() {
            // equals() requires both sides nonempty: an empty probe never
            // matches, an empty record is inert
            extManager.addWaitingLauncher("");
            verify(!extManager.waitingLauncherExists(""));
        }

        // ---- immediate launchers: exact-equality registry ----

        function test_immediateLaunchersMatchExactly() {
            extManager.addImmediateLauncher("file:///a.desktop");
            verify(extManager.immediateLauncherExists("file:///a.desktop"));
            // exact mode: the iconData spelling is a DIFFERENT key here
            verify(!extManager.immediateLauncherExists("file:///a.desktop?iconData=x"));

            extManager.addImmediateLauncher("file:///a.desktop");
            extManager.removeImmediateLauncher("file:///a.desktop");
            verify(!extManager.immediateLauncherExists("file:///a.desktop"));
        }

        // ---- to-be-added: substring registry with count latch ----

        function test_toBeAddedCountsRealInsertsOnly() {
            root.launchersAbility.launcherInAdding("file:///a.desktop");
            root.launchersAbility.launcherInAdding("file:///a.desktop?iconData=x");
            compare(extManager.launchersToBeAddedCount, 1);
            compare(extManager.launchersInPausedStateCount, 1);

            extManager.removeToBeAddedLauncher("file:///a.desktop");
            compare(extManager.launchersToBeAddedCount, 0);
            compare(extManager.launchersInPausedStateCount, 0);
        }

        // ---- to-be-removed: exact registry with count latch ----

        function test_toBeRemovedIsExactAndCounted() {
            root.launchersAbility.launcherInRemoving("file:///a.desktop");
            verify(extManager.isLauncherToBeRemoved("file:///a.desktop"));
            verify(!extManager.isLauncherToBeRemoved("file:///a.desktop?iconData=x"));
            compare(extManager.launchersToBeRemovedCount, 1);

            extManager.removeToBeRemovedLauncher("file:///a.desktop");
            verify(!extManager.isLauncherToBeRemoved("file:///a.desktop"));
            compare(extManager.launchersToBeRemovedCount, 0);
        }

        // ---- frozen tasks: upsert registry ----

        function test_frozenTasksUpsertAndRemove() {
            extManager.setFrozenTask("file:///a.desktop", 1.5);
            compare(extManager.getFrozenTask("file:///a.desktop").zoom, 1.5);

            extManager.setFrozenTask("file:///a.desktop", 1.8);
            compare(extManager.getFrozenTask("file:///a.desktop").zoom, 1.8);

            extManager.removeFrozenTask("file:///a.desktop");
            verify(!extManager.getFrozenTask("file:///a.desktop"));
        }

        // ---- to-be-moved: intent registry + the delayed move pipeline ----

        function test_moveLauncherPipeline() {
            root.launchersAbility.launcherInMoving("file:///sep.desktop", 3);
            verify(extManager.isLauncherToBeMoved("file:///sep.desktop"));
            compare(extManager.launchersToBeMovedCount, 1);

            extManager.moveLauncherToCorrectPos("file:///sep.desktop", 0);
            // intent is consumed immediately; the model move runs on the
            // 50ms timer, the sync on the 450ms follow-up
            verify(!extManager.isLauncherToBeMoved("file:///sep.desktop"));

            tryVerify(function() { return root.tasksModel.moveLog.length === 1; }, 2000);
            compare(root.tasksModel.moveLog[0][0], 0);
            compare(root.tasksModel.moveLog[0][1], 3);

            tryVerify(function() { return root.tasksModel.syncCount === 1; }, 2000);
            compare(root.launchersAbility.validateCallCount, 1);
            // the count is a paused-state latch: reset by the sync
            // completion, not by the registry removal
            compare(extManager.launchersToBeMovedCount, 0);
        }

        function test_moveIntentClampsNegativePosition() {
            // the shipped Math.max(0,toPos) clamp at addLauncherToBeMoved
            root.launchersAbility.launcherInMoving("file:///sep.desktop", -4);
            verify(extManager.isLauncherToBeMoved("file:///sep.desktop"));

            extManager.moveLauncherToCorrectPos("file:///sep.desktop", 2);
            tryVerify(function() { return root.tasksModel.moveLog.length === 1; }, 2000);
            compare(root.tasksModel.moveLog[0][1], 0);
            tryVerify(function() { return root.tasksModel.syncCount === 1; }, 2000);
        }

        // ---- validator: order reconciliation over the real Timer ----

        function test_validatorAdjacentSwapConvergesInOneMove() {
            root._launchers.shown = ["file:///a.desktop", "file:///c.desktop", "file:///b.desktop"];
            validator.launchers = ["file:///a.desktop", "file:///b.desktop", "file:///c.desktop"];
            validator.start();

            tryVerify(function() { return root._launchers.tasksModel.syncCount === 1; }, 5000);
            compare(JSON.stringify(root._launchers.shown),
                    JSON.stringify(["file:///a.desktop", "file:///b.desktop", "file:///c.desktop"]));
            // adjacent transposition is one move in both the Qt5 heuristic
            // and the EX-11 planner
            compare(root._launchers.tasksModel.moveLog.length, 1);
            verify(!validator.running);
        }

        function test_validatorPermutationConverges() {
            root._launchers.shown = ["file:///c.desktop", "file:///a.desktop", "file:///b.desktop"];
            validator.launchers = ["file:///a.desktop", "file:///b.desktop", "file:///c.desktop"];
            validator.start();

            tryVerify(function() { return root._launchers.tasksModel.syncCount === 1; }, 8000);
            compare(JSON.stringify(root._launchers.shown),
                    JSON.stringify(["file:///a.desktop", "file:///b.desktop", "file:///c.desktop"]));
            verify(!validator.running);
        }

        function test_validatorRetriesWhileMembershipDiffersThenConverges() {
            // the shown list carries a launcher the goal does not know
            // (model still settling; Qt5's launcherValidPos===-1 restart
            // branch): the validator must keep retrying without syncing,
            // then converge once the memberships agree.
            // Same-length lists deliberately: a LONGER goal falls into the
            // shipped "why we reached ???" branch that neither stops nor
            // restarts - a distinct Qt5 dead-stop this case must not touch.
            root._launchers.shown = ["file:///a.desktop", "file:///y.desktop"];
            validator.launchers = ["file:///a.desktop", "file:///b.desktop"];
            validator.start();

            wait(900); // at least two 400ms ticks
            compare(root._launchers.tasksModel.syncCount, 0);
            verify(validator.running);

            root._launchers.shown = ["file:///b.desktop", "file:///a.desktop"];
            tryVerify(function() { return root._launchers.tasksModel.syncCount === 1; }, 5000);
            compare(JSON.stringify(root._launchers.shown),
                    JSON.stringify(["file:///a.desktop", "file:///b.desktop"]));
            verify(!validator.running);
        }

        function test_validatorStopsWhenLauncherLeftTheModel() {
            // every reorder candidate resolves to no layout index: the
            // shipped "launcher was not found in model, syncing stopped"
            root._launchers.shown = ["file:///b.desktop", "file:///a.desktop"];
            root._launchers.missingFromLayout = ["file:///a.desktop", "file:///b.desktop"];
            validator.launchers = ["file:///a.desktop", "file:///b.desktop"];
            validator.start();

            tryVerify(function() { return !validator.running; }, 5000);
            compare(root._launchers.tasksModel.syncCount, 0);
            compare(root._launchers.tasksModel.moveLog.length, 0);
        }

        function test_validatorAlreadySyncedStopsAndSyncs() {
            root._launchers.shown = ["file:///a.desktop", "file:///b.desktop"];
            validator.launchers = ["file:///a.desktop", "file:///b.desktop"];
            validator.start();

            tryVerify(function() { return root._launchers.tasksModel.syncCount === 1; }, 5000);
            compare(root._launchers.tasksModel.moveLog.length, 0);
            verify(!validator.running);
        }
    }
}
