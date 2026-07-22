/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Shell pin for the automatic item sizer (EX-04). The fit calculation and
//! branch selection live in
//! the AutoSizeEngine core now (containment/plugin/units/autosizeengine.h;
//! tests/units/autosizeenginetest.cpp pins the full case tables, including
//! the ad9b823f termination property over sizes 16..256 and the largest-fit
//! result independent of the configured ceiling's remainder modulo 8).
//! What must stay pinned HERE is the shell: the AutoSizeStepper the
//! containment ability instantiates resolves from the staged install,
//! delegates to the core, maps the core's alternatives onto the sizer's
//! -1 = automatic sentinel at the boundary, and holds the prediction
//! history across passes.

import QtQuick
import QtTest

import org.kde.latte.private.containment 0.1 as LatteContainment

TestCase {
    id: root
    name: "AutoSizeStepperShell"

    LatteContainment.AutoSizeStepper {
        id: stepper
    }

    function init() {
        //! tests share the stepper instance; none may inherit history
        stepper.clearHistory();
    }

    function test_shrinkAppliesLargestFittingSize() {
        //! layout 1000 at icon 64 against maxLength 1100 and one zoomed 64px
        //! item reserved: toShrinkLimit 997.6. Size 63 projects 984.375 and
        //! is the largest fit below the ceiling.
        var result = stepper.step(1000, 1100, 64, 64, 64, 1.6, -1);
        verify(result.found, "an overflowing layout must find a shrunk size");
        compare(result.nextIconSize, 63);
    }

    function test_shrinkTerminatesForTheLiveIconSize78() {
        //! the ad9b823f named case through the shell: a barely positive
        //! maxLength makes every shrink limit unsatisfiable, so only the
        //! floor exit can terminate the loop - at the non-step-multiple 78
        //! the inherited equality exit spun forever
        var result = stepper.step(780, 1, 78, 78, 78, 1.6, -1);
        verify(result.found, "the unsatisfiable shrink must still land on the floor");
        compare(result.nextIconSize, 16);
    }

    function test_growToCeilingRestoresAutomaticSentinel() {
        //! a grow that reaches maxIconSize maps to the sizer's -1 sentinel
        //! at this boundary (the core reports a distinct alternative)
        var result = stepper.step(500, 2000, 64, 32, 64, 1.6, 32);
        verify(result.found, "a roomy layout grown from its own applied size must apply");
        compare(result.nextIconSize, -1);
    }

    function test_growMidRangeAppliesConcreteSize() {
        //! toGrowLimit 900: size 57 fits and size 58 does not
        var result = stepper.step(500, 1500, 500, 32, 64, 1.0, 32);
        verify(result.found);
        compare(result.nextIconSize, 57);
    }

    function test_automaticSizingNeverGrows() {
        //! appliedIconSize -1 passes the sizer's untouched sentinel in: the
        //! search never grows from a size it did not apply itself
        var result = stepper.step(500, 2000, 64, 32, 64, 1.6, -1);
        verify(!result.found, "automatic sizing must not grow");
    }

    function test_robustnessBandKeepsCurrent() {
        //! layout 890 sits between toGrowLimit 877.12 and toShrinkLimit
        //! 897.6: the asymmetric limits' margin, neither branch fires
        var result = stepper.step(890, 1000, 64, 64, 64, 1.6, 64);
        verify(!result.found, "inside the band the size must stay put");
    }

    function test_liveShapedGrowUsesTheLargestFittingPixelSize() {
        //! At size 44 the 965px row has room to grow beneath a 1132px grow
        //! limit. Size 51 fits and size 52 does not; the shell must expose
        //! the largest fit instead of stopping because an 8px jump missed it.
        var result = stepper.step(965, 1228, 50, 44, 68, 1.6, 44);
        verify(result.found);
        compare(result.nextIconSize, 51);
    }

    function test_historyPersistsAcrossPassesAndClears() {
        //! the endless-loop protector needs state across calls: a grow, the
        //! shrink undoing it, then the identical grow again is rejected;
        //! clearing the history re-arms it (the sizer's isActive flip)
        var grow = stepper.step(500, 1500, 500, 32, 64, 1.0, 32);
        verify(grow.found);
        compare(grow.nextIconSize, 57);

        var shrink = stepper.step(890.625, 800, 100, 57, 64, 1.0, 57);
        verify(shrink.found);
        compare(shrink.nextIconSize, 44);

        var blocked = stepper.step(500, 1500, 500, 32, 64, 1.0, 32);
        verify(!blocked.found, "the protector must block the repeating grow");

        stepper.clearHistory();
        var rearmed = stepper.step(500, 1500, 500, 32, 64, 1.0, 32);
        verify(rearmed.found, "a cleared history re-arms the grow");
        compare(rearmed.nextIconSize, 57);
    }

    function test_invalidMeasurementIsRefused() {
        //! the shell's maxLength <= 0 contract returns before calling in;
        //! if a call arrives anyway the stepper refuses loudly (qCritical
        //! in the log) and reports nothing found instead of searching
        //! against garbage limits - same for the other measurements no
        //! valid shell can produce
        verify(!stepper.step(1000, 0, 64, 64, 64, 1.6, -1).found,
               "a zero maxLength must be refused");
        verify(!stepper.step(-5, 1000, 64, 64, 64, 1.6, -1).found,
               "a negative layout length must be refused");
        verify(!stepper.step(1000, 1000, -3, 64, 64, 1.6, -1).found,
               "a negative item length must be refused");
        verify(!stepper.step(1000, 1000, 64, 0, 64, 1.6, -1).found,
               "a zero current icon size must be refused");
        verify(!stepper.step(1000, 1000, 64, 64, 0, 1.6, -1).found,
               "a zero max icon size must be refused");
        verify(!stepper.step(1000, 1000, 64, 64, 64, 0.5, -1).found,
               "a zoom factor below 1 must be refused");
    }
}
