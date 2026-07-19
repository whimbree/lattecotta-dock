/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// EX-15 WheelAccumulator (docs/tracking/QML_EXTRACTION_PLAN.md): per-site tests for
// the wheel delta accumulation/threshold semantics extracted from four
// hand-rolled QML onWheel bodies. The expected values were derived by
// reading the shipped QML handlers at HEAD 2f9f1df0 (AudioStream.qml,
// TaskMouseArea.qml, EnvironmentActions.qml, RulerMouseArea.qml) and the
// Qt5 ancestors at f0ad7b23 - sites 2-4 match Qt5 exactly; site 1 follows
// 299a241b's plasma-pa-matched semantics, which WIN over Qt5 there (the
// deviation is recorded in that commit body and in the EX-15 spec).
//
// Threshold unit mapping: Qt5 compared angle = angleDelta/8 against 12
// (or 10) in JS floats; angleDelta components are ints, so angle > 12 is
// exactly angleDelta > 96 and angle > 10 exactly angleDelta > 80.

#include <QtTest>

#include "../../declarativeimports/core/units/wheelaccumulator.h"
#include "../../declarativeimports/core/wheelstepper.h"

using namespace Latte;

//! QPoint's constructor order is (x, y); every call site below reads
//! like the QML it replaces (angleDelta.x horizontal, angleDelta.y
//! vertical), so misordered fixtures cannot slip through review
static QPoint angleDelta(int x, int y)
{
    return {x, y};
}

//! the four shipped site configurations, one place each

static WheelAccumulator audioBadgeAccumulator()
{
    // AudioStream.qml: plasma-pa semantics (299a241b)
    return WheelAccumulator(WheelAxisPick::VerticalElseNegatedHorizontal,
                            AccumulateEveryStep{.stepSize = 120, .resetOnReversal = true});
}

static WheelAccumulator taskScrollStepper()
{
    // TaskMouseArea.qml: mainAngle > 12 on the dominant axis
    return WheelAccumulator(WheelAxisPick::DominantAxis,
                            FireOncePastThreshold{.threshold = 96});
}

static WheelAccumulator emptyAreaStepper()
{
    // EnvironmentActions.qml: angle > 10 on the signed extreme
    return WheelAccumulator(WheelAxisPick::SignedExtreme,
                            FireOncePastThreshold{.threshold = 80});
}

static WheelAccumulator rulerStepper()
{
    // RulerMouseArea.qml: angle > 12, vertical only
    return WheelAccumulator(WheelAxisPick::VerticalOnly,
                            FireOncePastThreshold{.threshold = 96});
}

class WheelAccumulatorTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // site 1: AudioStream.qml (accumulate-and-step, plasma-pa semantics)
    void audioBadge_wholeNotchFiresImmediately();
    void audioBadge_highResolutionDeltasAccumulateToOneStep();
    void audioBadge_largeEventFiresEveryStepItContains();
    void audioBadge_reversalDropsResidue();
    void audioBadge_withoutReversalResetResidueSurvivesReversal();
    void audioBadge_invertedFlipsTheDirection();
    void audioBadge_horizontalFallbackIsNegatedAndVerticalWins();

    // site 2: TaskMouseArea.qml (threshold-with-axis-priority)
    void taskScroll_dominantAxisPicksLargerMagnitude();
    void taskScroll_axisTiesGoHorizontal();

    // site 3: EnvironmentActions.qml (threshold on the signed extreme)
    void emptyArea_signedExtremeTakesMaxWhenBothNonNegative();
    void emptyArea_anyNegativeComponentForcesTheMin();

    // site 4: RulerMouseArea.qml (threshold, vertical only)
    void ruler_verticalOnlyIgnoresHorizontal();

    // firing-mode contracts shared across sites
    void thresholdFiring_isStrictAndFiresExactlyOnce();
    void thresholdFiring_carriesNoResidueAcrossEvents();
    void zeroDeltaEvent_isInertInBothModes();

    // the one authority for TaskMouseArea's parallel-scroll read
    void verticalIsDominant_isTheStrictComparison();

    // the WheelStepper QML boundary (wheelstepper.cpp, compiled into
    // this sanitized binary): configuration refusals and delegation
    void stepper_refusesAnUnconfiguredMode();
    void stepper_refusesADoublyConfiguredMode();
    void stepper_refusesResetOnReversalInThresholdMode();
    void stepper_rejectsAnOutOfRangeAxisPick();
    void stepper_delegatesToTheCore();
    void stepper_reconfigurationDropsTheResidue();
};

void WheelAccumulatorTest::audioBadge_wholeNotchFiresImmediately()
{
    auto wheel = audioBadgeAccumulator();
    QCOMPARE(wheel.add(angleDelta(0, 120)), 1);
    QCOMPARE(wheel.add(angleDelta(0, -120)), -1);
    // 119 is one unit short of a step; the next unit completes it
    QCOMPARE(wheel.add(angleDelta(0, 119)), 0);
    QCOMPARE(wheel.add(angleDelta(0, 1)), 1);
}

void WheelAccumulatorTest::audioBadge_highResolutionDeltasAccumulateToOneStep()
{
    // a high-resolution wheel sending quarter-notch events reaches one
    // step on the fourth event (the 299a241b plasma-pa case)
    auto wheel = audioBadgeAccumulator();
    QCOMPARE(wheel.add(angleDelta(0, 30)), 0);
    QCOMPARE(wheel.add(angleDelta(0, 30)), 0);
    QCOMPARE(wheel.add(angleDelta(0, 30)), 0);
    QCOMPARE(wheel.add(angleDelta(0, 30)), 1);
}

void WheelAccumulatorTest::audioBadge_largeEventFiresEveryStepItContains()
{
    // however many steps fit in one event fire immediately - no lockout
    auto wheel = audioBadgeAccumulator();
    QCOMPARE(wheel.add(angleDelta(0, 360)), 3);
    QCOMPARE(wheel.add(angleDelta(0, -240)), -2);

    // a partial step stays as residue for the next event
    QCOMPARE(wheel.add(angleDelta(0, 250)), 2); // residue 10
    QCOMPARE(wheel.add(angleDelta(0, 110)), 1); // 10 + 110 = 120
}

void WheelAccumulatorTest::audioBadge_reversalDropsResidue()
{
    // the first step of the new direction needs the same travel as every
    // other step: +90 of residue must not subsidize a downward step
    auto wheel = audioBadgeAccumulator();
    QCOMPARE(wheel.add(angleDelta(0, 90)), 0);
    QCOMPARE(wheel.add(angleDelta(0, -30)), 0); // residue reset, then -30
    QCOMPARE(wheel.add(angleDelta(0, -90)), -1); // -30 + -90 = -120
}

void WheelAccumulatorTest::audioBadge_withoutReversalResetResidueSurvivesReversal()
{
    // the resetOnReversal knob genuinely gates the reset (no site ships
    // false today; the parameter is part of the spec's interface)
    WheelAccumulator wheel(WheelAxisPick::VerticalElseNegatedHorizontal,
                           AccumulateEveryStep{.stepSize = 120, .resetOnReversal = false});
    QCOMPARE(wheel.add(angleDelta(0, 90)), 0);
    QCOMPARE(wheel.add(angleDelta(0, -30)), 0); // residue 60, not -30
    QCOMPARE(wheel.add(angleDelta(0, 60)), 1); // 60 + 60 = 120
}

void WheelAccumulatorTest::audioBadge_invertedFlipsTheDirection()
{
    // natural-scrolling devices report inverted; plasma-pa honors it
    auto wheel = audioBadgeAccumulator();
    QCOMPARE(wheel.add(angleDelta(0, 120), true), -1);
    QCOMPARE(wheel.add(angleDelta(0, -120), true), 1);

    // inversion applies before the reversal check: an inverted +90
    // followed by a plain +30 IS a reversal (-90 then +30)
    auto reversing = audioBadgeAccumulator();
    QCOMPARE(reversing.add(angleDelta(0, 90), true), 0); // residue -90
    QCOMPARE(reversing.add(angleDelta(0, 30), false), 0); // reset, then +30
    QCOMPARE(reversing.add(angleDelta(0, 90), false), 1); // 30 + 90 = 120
}

void WheelAccumulatorTest::audioBadge_horizontalFallbackIsNegatedAndVerticalWins()
{
    // plasma-pa's fallback: a pure horizontal wheel maps to the OPPOSITE
    // sign (tilting right lowers the volume)
    auto wheel = audioBadgeAccumulator();
    QCOMPARE(wheel.add(angleDelta(120, 0)), -1);
    QCOMPARE(wheel.add(angleDelta(-120, 0)), 1);

    // any nonzero vertical wins, however large the horizontal
    QCOMPARE(wheel.add(angleDelta(-999, 120)), 1);
}

void WheelAccumulatorTest::taskScroll_dominantAxisPicksLargerMagnitude()
{
    auto wheel = taskScrollStepper();
    QCOMPARE(wheel.add(angleDelta(0, 97)), 1);
    QCOMPARE(wheel.add(angleDelta(3, -97)), -1);
    // the horizontal component is used AS-IS, not negated (unlike the
    // audio badge's plasma-pa fallback)
    QCOMPARE(wheel.add(angleDelta(97, 3)), 1);
    QCOMPARE(wheel.add(angleDelta(-97, 50)), -1);
}

void WheelAccumulatorTest::taskScroll_axisTiesGoHorizontal()
{
    // Qt5's verticalDirection used STRICT abs(y) > abs(x): equal
    // magnitudes resolve to the horizontal component
    auto wheel = taskScrollStepper();
    QCOMPARE(wheel.add(angleDelta(97, -97)), 1);
    QCOMPARE(wheel.add(angleDelta(-97, 97)), -1);
}

void WheelAccumulatorTest::emptyArea_signedExtremeTakesMaxWhenBothNonNegative()
{
    auto wheel = emptyAreaStepper();
    QCOMPARE(wheel.add(angleDelta(0, 120)), 1);
    // a pure horizontal scroll also counts as "upwards" here (Qt5)
    QCOMPARE(wheel.add(angleDelta(120, 0)), 1);
    QCOMPARE(wheel.add(angleDelta(81, 40)), 1);
}

void WheelAccumulatorTest::emptyArea_anyNegativeComponentForcesTheMin()
{
    // the Qt5 quirk, preserved deliberately (Qt5-faithful): mixed signs
    // take the min, so a strong upward y with a tiny negative x fires
    // NOTHING, and any negative component past the threshold fires down
    auto wheel = emptyAreaStepper();
    QCOMPARE(wheel.add(angleDelta(-1, 200)), 0);
    QCOMPARE(wheel.add(angleDelta(1, -200)), -1);
    QCOMPARE(wheel.add(angleDelta(-81, -3)), -1);
}

void WheelAccumulatorTest::ruler_verticalOnlyIgnoresHorizontal()
{
    auto wheel = rulerStepper();
    QCOMPARE(wheel.add(angleDelta(960, 0)), 0);
    QCOMPARE(wheel.add(angleDelta(960, 97)), 1);
    QCOMPARE(wheel.add(angleDelta(0, -97)), -1);
}

void WheelAccumulatorTest::thresholdFiring_isStrictAndFiresExactlyOnce()
{
    // strict >: the threshold value itself does not fire (Qt5's
    // angle > 12 with angle == 12, i.e. angleDelta == 96)
    auto atTwelve = rulerStepper();
    QCOMPARE(atTwelve.add(angleDelta(0, 96)), 0);
    QCOMPARE(atTwelve.add(angleDelta(0, 97)), 1);
    QCOMPARE(atTwelve.add(angleDelta(0, -96)), 0);
    QCOMPARE(atTwelve.add(angleDelta(0, -97)), -1);
    // one step however large the event - no accumulation in this mode
    QCOMPARE(atTwelve.add(angleDelta(0, 960)), 1);

    auto atTen = emptyAreaStepper();
    QCOMPARE(atTen.add(angleDelta(0, 80)), 0);
    QCOMPARE(atTen.add(angleDelta(0, 81)), 1);
}

void WheelAccumulatorTest::thresholdFiring_carriesNoResidueAcrossEvents()
{
    // ten sub-threshold events never add up to a step
    auto wheel = taskScrollStepper();
    for (int i = 0; i < 10; ++i) {
        QCOMPARE(wheel.add(angleDelta(0, 90)), 0);
    }
    // and a fired step leaves nothing behind either
    QCOMPARE(wheel.add(angleDelta(0, 97)), 1);
    QCOMPARE(wheel.add(angleDelta(0, 90)), 0);
}

void WheelAccumulatorTest::zeroDeltaEvent_isInertInBothModes()
{
    // some touchpads emit a (0,0) angleDelta at gesture end; zero is
    // neither direction, so it must not reset the residue
    auto accumulating = audioBadgeAccumulator();
    QCOMPARE(accumulating.add(angleDelta(0, 90)), 0);
    QCOMPARE(accumulating.add(angleDelta(0, 0)), 0);
    QCOMPARE(accumulating.add(angleDelta(0, 30)), 1); // residue survived

    auto thresholding = rulerStepper();
    QCOMPARE(thresholding.add(angleDelta(0, 0)), 0);
}

void WheelAccumulatorTest::verticalIsDominant_isTheStrictComparison()
{
    QVERIFY(WheelAccumulator::verticalIsDominant(angleDelta(4, 5)));
    QVERIFY(WheelAccumulator::verticalIsDominant(angleDelta(4, -5)));
    QVERIFY(!WheelAccumulator::verticalIsDominant(angleDelta(5, 5)));
    QVERIFY(!WheelAccumulator::verticalIsDominant(angleDelta(5, 4)));
    QVERIFY(!WheelAccumulator::verticalIsDominant(angleDelta(-6, 5)));
}

void WheelAccumulatorTest::stepper_refusesAnUnconfiguredMode()
{
    WheelStepper stepper;
    stepper.setAxisPick(WheelStepper::VerticalOnly);

    QTest::ignoreMessage(QtCriticalMsg,
                         QRegularExpression("exactly one of stepSize / fireThreshold"));
    QCOMPARE(stepper.add(QPointF(0, 120), false), 0);
}

void WheelAccumulatorTest::stepper_refusesADoublyConfiguredMode()
{
    WheelStepper stepper;
    stepper.setAxisPick(WheelStepper::VerticalOnly);
    stepper.setStepSize(120);
    stepper.setFireThreshold(96);

    QTest::ignoreMessage(QtCriticalMsg,
                         QRegularExpression("exactly one of stepSize / fireThreshold"));
    QCOMPARE(stepper.add(QPointF(0, 120), false), 0);
}

void WheelAccumulatorTest::stepper_refusesResetOnReversalInThresholdMode()
{
    WheelStepper stepper;
    stepper.setAxisPick(WheelStepper::VerticalOnly);
    stepper.setFireThreshold(96);
    stepper.setResetOnReversal(true);

    QTest::ignoreMessage(QtCriticalMsg,
                         QRegularExpression("resetOnReversal is an accumulate-mode knob"));
    QCOMPARE(stepper.add(QPointF(0, 120), false), 0);
}

void WheelAccumulatorTest::stepper_rejectsAnOutOfRangeAxisPick()
{
    WheelStepper stepper;
    stepper.setAxisPick(WheelStepper::SignedExtreme);

    QTest::ignoreMessage(QtCriticalMsg, QRegularExpression("out-of-range axisPick"));
    stepper.setAxisPick(static_cast<WheelStepper::AxisPick>(42));
    QCOMPARE(stepper.axisPick(), WheelStepper::SignedExtreme);
}

void WheelAccumulatorTest::stepper_delegatesToTheCore()
{
    // the audio badge configuration through the boundary: same results
    // as audioBadge_* prove for the bare core
    WheelStepper stepper;
    stepper.setAxisPick(WheelStepper::VerticalElseNegatedHorizontal);
    stepper.setStepSize(120);
    stepper.setResetOnReversal(true);

    QCOMPARE(stepper.add(QPointF(0, 360), false), 3);
    QCOMPARE(stepper.add(QPointF(0, 90), false), 0);
    QCOMPARE(stepper.add(QPointF(0, -30), false), 0); // reversal reset
    QCOMPARE(stepper.add(QPointF(0, -90), false), -1);
    QCOMPARE(stepper.add(QPointF(0, 120), true), -1); // inverted

    QVERIFY(stepper.verticalIsDominant(QPointF(4, 5)));
    QVERIFY(!stepper.verticalIsDominant(QPointF(5, 5)));
}

void WheelAccumulatorTest::stepper_reconfigurationDropsTheResidue()
{
    WheelStepper stepper;
    stepper.setAxisPick(WheelStepper::VerticalOnly);
    stepper.setStepSize(120);

    QCOMPARE(stepper.add(QPointF(0, 90), false), 0); // residue 90

    stepper.setStepSize(240);
    // a surviving residue would make 90 + 150 = 240 fire; a fresh
    // accumulator needs the full 240 again
    QCOMPARE(stepper.add(QPointF(0, 150), false), 0);
    QCOMPARE(stepper.add(QPointF(0, 90), false), 1);
}

QTEST_GUILESS_MAIN(WheelAccumulatorTest)

#include "wheelaccumulatortest.moc"
