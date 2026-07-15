/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Pins termination and bounds of the automatic item sizer's shrink/grow
//! stepping (ad9b823f). The upstream 747d4870 form exited on the equalities
//! nextIconSize !== 16 / !== maxIconSize, so any size not congruent to the
//! bound modulo the step (the live case: iconSize=78 written by a killed
//! edit-mode session) stepped right past it and spun forever, starving the
//! event loop at startup. The functions under test are the REAL shipped
//! logic: AutoSize.qml dispatches through code/autosize.js, which is
//! imported here straight from the containment package. A regression back
//! to the equality exits hangs these loops and fails the run via the
//! harness timeout instead of a live dock at 100% CPU.

import QtQuick
import QtTest

import "../../containment/package/contents/code/autosize.js" as AutoSizeLogic

TestCase {
    id: root
    name: "AutoSizeStepping"

    readonly property int automaticStep: 8 //! AutoSize.qml's readonly automaticStep

    function test_shrinkTerminatesForEveryIconSize() {
        //! the ad9b823f trigger: an unsized window makes root.maxLength 0 and
        //! toShrinkLimit negative, so the length condition can never break the
        //! loop and only the floor exit can. Every icon size from 16 to 128 -
        //! multiples of the step and non-multiples like the live 78 alike -
        //! must land exactly on the 16px floor and return.
        for (var size = 16; size <= 128; ++size) {
            var res = AutoSizeLogic.shrinkStep(size, size, 10 * size, -1, automaticStep);
            compare(res.iconSize, 16,
                    "maxIconSize " + size + ": an unsatisfiable shrink limit must exit at the floor");
        }
    }

    function test_shrinkStaysWithinBounds() {
        //! a satisfiable limit exits early on the length condition; the result
        //! must stay inside [16, maxIconSize - step] (floor-clamped) for every
        //! start size
        for (var size = 17; size <= 128; ++size) {
            //! current layout: 8 items at 'size' px; the limit allows roughly half
            var limit = 4 * size;
            var res = AutoSizeLogic.shrinkStep(size, size, 8 * size, limit, automaticStep);
            verify(res.iconSize >= 16,
                   "maxIconSize " + size + ": result " + res.iconSize + " fell below the floor");
            verify(res.iconSize <= Math.max(16, size - automaticStep),
                   "maxIconSize " + size + ": a shrink pass must step down at least once");

            if (res.iconSize > 16) {
                //! only a floor exit is allowed to leave the limit unsatisfied
                verify(res.nextLength <= limit,
                       "maxIconSize " + size + ": projected length " + res.nextLength + " does not fit the limit");
            }
        }
    }

    function test_shrinkTerminatesForDegenerateInputs() {
        //! inputs below the floor or nonsensical limits must still return
        var res = AutoSizeLogic.shrinkStep(8, 8, 100, -1, automaticStep);
        compare(res.iconSize, 16, "a max size below the floor clamps up to the floor");

        res = AutoSizeLogic.shrinkStep(64, 64, 0, -1, automaticStep);
        compare(res.iconSize, 16, "zero layout length with a negative limit still exits at the floor");
    }

    function test_growTerminatesForEveryIconSize() {
        //! an always-satisfied grow limit must climb to the ceiling and stop
        //! there for every start size, including non-multiples of the step
        for (var size = 16; size <= 128; ++size) {
            var res = AutoSizeLogic.growStep(128, size, 10, Number.MAX_VALUE, automaticStep);
            compare(res.foundGoodSize, 128,
                    "iconSize " + size + ": an unbounded grow limit must saturate at maxIconSize");
        }
    }

    function test_growReportsWhenNoStepFits() {
        //! when even the first candidate exceeds the limit the pass reports -1
        //! and must not loop
        for (var size = 16; size <= 128; ++size) {
            var res = AutoSizeLogic.growStep(128, size, 10 * size, -1, automaticStep);
            compare(res.foundGoodSize, -1,
                    "iconSize " + size + ": an unsatisfiable grow limit reports no good size");
        }
    }

    function test_growStopsAtCeilingEqualToCurrent() {
        //! growing while already at the ceiling must exit immediately with the
        //! ceiling (the equality form spun here when current === max)
        var res = AutoSizeLogic.growStep(78, 78, 10, Number.MAX_VALUE, automaticStep);
        compare(res.foundGoodSize, 78, "already at a non-multiple ceiling: saturates and returns");
    }
}
