/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.0

import org.kde.latte.private.tasks 0.1 as LatteTasks

//! Launchers Validate Timer: reconciles the shown launcher order with the
//! synced goal order the host pushed (Syncer assigns launchers and starts
//! this timer).
//!
//! EX-11: the order algebra lives in the core planner (planMoves in
//! units/launcherlistops.h); this shell applies ONE move per 400ms tick -
//! the Qt5 cadence - and replans on the next tick, which strictly shrinks
//! the plan (the termination guarantee the old upwardIsBetter direction
//! heuristic lacked, pinned by tests/units/launcherlistopstest.cpp).
Timer{
    id:launchersOrderValidatorTimer
    interval: 400

    //! the launchers ability: shown-list read, layout lookup, tasks model
    required property QtObject launchersAbility

    //! the goal order (the host's synced list)
    property var launchers: []

    onTriggered: {
        var current = launchersAbility.currentShownLauncherList();
        var plan = LatteTasks.LauncherListOps.planMoves(current, launchers);

        if (plan === undefined || plan === null) {
            //! not a duplicate-free permutation of the goal: the model is
            //! still settling mid-animation (Qt5's launcherValidPos===-1
            //! restart branch) - retry once it has caught up
            restart();
            return;
        }

        if (plan.length === 0) {
            stop();
            console.log("launchers synced at:" + launchers);
            launchers.length = 0;
            launchersAbility.tasksModel.syncLaunchers();
            return;
        }

        var move = plan[0];
        var layoutIndex = launchersAbility.indexOfLayoutLauncher(current[move.from]);

        if (layoutIndex === -1) {
            console.log(" launcher was not found in model, syncing stopped...");
            stop();
            return;
        }

        console.log(" moving:" + layoutIndex + " _ " + move.to);
        launchersAbility.tasksModel.move(layoutIndex, move.to);
        restart();
    }
}
