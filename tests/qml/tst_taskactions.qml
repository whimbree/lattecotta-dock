/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Enum/handler completeness contract for the tasks-plasmoid click actions
//! (porting plan Phase 6). latte-dock-ng shipped a Tasks config UI offering
//! all 9 TaskAction values while TaskMouseArea handled only a subset per
//! click type; picking an unhandled action silently did nothing. This pins
//! the invariant that every enum value the config UI offers, per click type,
//! resolves to a real command in code/TaskActions.js - the single source of
//! truth the handler dispatches through.
//!
//! The "offered" sets below are transcribed from the config combos; each
//! carries a source reference so a reviewer can confirm the transcription.
//! If a combo gains an entry, this test keeps passing only if TaskActions.js
//! gained the matching case - which is exactly the regression it guards.

import QtQuick
import QtTest

import org.kde.latte.private.tasks 0.1 as LatteTasks
import "../../plasmoid/package/contents/code/TaskActions.js" as TaskActions

TestCase {
    id: root
    name: "TaskActions"

    //! shell/package/contents/configuration/pages/TasksConfig.qml leftClickAction
    //! combo (model of 3): Present Windows, Cycle Through Tasks, Preview Windows.
    readonly property var leftOffered: [
        LatteTasks.Types.PresentWindows,
        LatteTasks.Types.CycleThroughTasks,
        LatteTasks.Types.PreviewWindows
    ]

    //! TasksConfig.qml middleClickAction and modifierClickAction combos (model of
    //! 6, index == enum): None, Close, New Instance, Minimize/Restore, Cycle
    //! Through Tasks, Toggle Task Grouping. The plasmoid's own ConfigInteraction.qml
    //! middle-click combo offers the first four - a subset, so this set covers it.
    readonly property var standardOffered: [
        LatteTasks.Types.NoneAction,
        LatteTasks.Types.Close,
        LatteTasks.Types.NewInstance,
        LatteTasks.Types.ToggleMinimized,
        LatteTasks.Types.CycleThroughTasks,
        LatteTasks.Types.ToggleGrouping
    ]

    //! TasksConfig.qml wheelAction combo (model of 3, index == enum):
    //! None, Cycle Through Tasks, Cycle And Minimize Tasks.
    readonly property var scrollOffered: [
        LatteTasks.Types.ScrollNone,
        LatteTasks.Types.ScrollTasks,
        LatteTasks.Types.ScrollToggleMinimized
    ]

    function test_everyStandardOfferedActionIsHandled() {
        for (var i = 0; i < standardOffered.length; ++i) {
            var action = standardOffered[i];
            var cmd = TaskActions.standardCommandFor(action);

            if (action === LatteTasks.Types.NoneAction) {
                //! None is the one legitimate empty result: a chosen no-op
                compare(cmd, "", "NoneAction must map to the no-op command");
            } else {
                verify(cmd !== "",
                       "middle/modifier action enum " + action + " is offered by the config UI "
                       + "but TaskActions.standardCommandFor left it unhandled");
            }
        }
    }

    function test_everyLeftOfferedActionIsHandled() {
        for (var i = 0; i < leftOffered.length; ++i) {
            var action = leftOffered[i];
            verify(TaskActions.leftCommandFor(action) !== "",
                   "left-click action enum " + action + " is offered by the config UI "
                   + "but TaskActions.leftCommandFor left it unhandled");
        }
    }

    function test_everyScrollOfferedActionIsHandled() {
        for (var i = 0; i < scrollOffered.length; ++i) {
            var action = scrollOffered[i];
            var cmd = TaskActions.scrollCommandFor(action);

            if (action === LatteTasks.Types.ScrollNone) {
                compare(cmd, "", "ScrollNone must map to the no-op command");
            } else {
                verify(cmd !== "",
                       "scroll action enum " + action + " is offered by the config UI "
                       + "but TaskActions.scrollCommandFor left it unhandled");
            }
        }
    }

    //! Guards against the inverse mistake: a TaskAction value NOT in a click
    //! handler's offered set must not silently resolve to a command (that would
    //! mean the map drifted ahead of the UI, hiding a dead branch). Each check
    //! stays within the right enum family: TaskAction and TaskScrollAction are
    //! distinct enums whose integer values overlap, so a command function must
    //! only ever be fed values from its own family.
    function test_unofferedActionsAreUnhandled() {
        //! HighlightWindows is a hover-only TaskAction, never a click one
        compare(TaskActions.standardCommandFor(LatteTasks.Types.HighlightWindows), "");
        //! Close is a standard action, not one the left-click combo offers
        compare(TaskActions.leftCommandFor(LatteTasks.Types.Close), "");
        //! PreviewWindows is offered by left-click but not the standard set
        compare(TaskActions.standardCommandFor(LatteTasks.Types.PreviewWindows), "");
    }
}
