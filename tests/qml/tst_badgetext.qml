/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! EX-20 BadgeMath e2e layer (docs/QML_EXTRACTION_PLAN.md): drives the REAL
//! shipped BadgeText.qml through the installed org.kde.latte.components
//! module and asserts the shell delegates to the LatteCore.BadgeMath
//! singleton and applies the results - the section-D rule that a cutover
//! may not reduce qmltest coverage of QML-computed values. The Canvas
//! pixels themselves are presentation; the arc numbers the canvas consumes
//! are asserted through the same singleton the shell calls.

import QtQuick
import QtTest

import org.kde.latte.core 0.2 as LatteCore
import org.kde.latte.components 1.0 as LatteComponents

TestCase {
    id: root
    name: "BadgeTextTest"
    when: windowShown

    LatteComponents.BadgeText {
        id: badge
        width: 24
        height: 24
        showNumber: true
        proportion: 0
    }

    // the shown label lives on the inner Text element (the component's
    // root is the badge Rectangle and exposes no text property)
    function shownLabel() : string {
        return findChild(badge, "badgeLabelText").text;
    }

    function test_typeResolvesThroughModuleImport() {
        verify(badge !== null, "BadgeText must resolve as a type from org.kde.latte.components");
        verify(findChild(badge, "badgeLabelText") !== null, "the label Text is reachable");
    }

    function test_countLabelClampsAndFormats() {
        badge.showNumber = true;
        badge.showText = false;

        badge.numberValue = 1;
        compare(shownLabel(), "1", "single digit renders plainly");

        badge.numberValue = 9999;
        compare(shownLabel(), Number(9999).toLocaleString(Qt.locale(), 'f', 0),
                "9999 is still in range and renders with Qt5's locale grouping");

        badge.numberValue = 10000;
        compare(shownLabel(), "9,999+", "beyond 9999 clamps to the overlay marker");
    }

    function test_localeGroupingEquivalence() {
        //! delegation equivalence with the Qt5 body this shell replaced:
        //! numberValue.toLocaleString(Qt.locale(), 'f', 0) for every
        //! in-range value - pins that the C++ QLocale rendering and the JS
        //! rendering agree in whatever locale the harness runs under
        badge.showNumber = true;
        badge.showText = false;

        const samples = [2, 10, 99, 100, 999, 1000, 4321, 9999];
        for (let i = 0; i < samples.length; ++i) {
            badge.numberValue = samples[i];
            compare(shownLabel(), Number(samples[i]).toLocaleString(Qt.locale(), 'f', 0),
                    "locale-grouped rendering for " + samples[i]);
        }
    }

    function test_zeroCountFallsThroughToTextMode() {
        //! Qt5 branch structure: showNumber with numberValue <= 0 produced
        //! no label and fell through to the text mode
        badge.showNumber = true;
        badge.showText = true;
        badge.textValue = "⌘";

        badge.numberValue = 0;
        compare(shownLabel(), "⌘", "zero count yields the text value");

        badge.showText = false;
        compare(shownLabel(), "", "no count and no text renders empty");
    }

    function test_ringGeometryDelegates() {
        badge.fullCircle = true;
        compare(badge.stdThickness, LatteCore.BadgeMath.ringOuterRadius(badge.height),
                "outer radius follows the core");
        compare(badge.stdThickness, badge.height / 2, "outer radius is half the height");
        compare(badge.circleThicknessAttr, 0, "a full circle has no hollow");

        badge.fullCircle = false;
        compare(badge.circleThicknessAttr, 0.9 * (badge.height / 2),
                "the ring hollows to 90% of the outer radius");
        badge.fullCircle = true;
    }

    function test_arcNumbersTheCanvasConsumes() {
        compare(LatteCore.BadgeMath.arcStartRadian(), -Math.PI / 2,
                "the sweep starts at twelve o'clock");
        compare(LatteCore.BadgeMath.arcSweepRadian(0.25), Math.PI / 2, "quarter sweep");
        compare(LatteCore.BadgeMath.arcSweepRadian(0.5), Math.PI, "half sweep");
        compare(LatteCore.BadgeMath.arcSweepRadian(1.0), 2 * Math.PI, "full sweep");
    }

    function test_proportionDecisionMatchesProgressOverlay() {
        //! the ProgressOverlay shell feeds these exact argument shapes
        compare(LatteCore.BadgeMath.proportionFor(true, 50, false, 0), 0.5,
                "visible progress maps 0..100 to 0..1");
        compare(LatteCore.BadgeMath.proportionFor(false, 0, true, 0), 1,
                "a visible count fills the circle");
        compare(LatteCore.BadgeMath.proportionFor(false, 0, false, 3), 1,
                "an external badge indicator fills the circle");
        compare(LatteCore.BadgeMath.proportionFor(false, 0, false, 0), 0,
                "nothing to show draws nothing");
    }
}
