/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// EX-03 ParabolicMathCore (docs/tracking/QML_EXTRACTION_PLAN.md): equivalence tests
// for the zoom curve against the QML implementation being replaced, plus
// the stack-assembly and swap contracts of applyParabolicEffect.
//
// Reference-table generation method (per docs/reference/TESTING.md): the rows in
// scaleForItem_equivalenceTable were produced by driving the SHIPPED
// declarativeimports/abilities/definition/ParabolicEffect.qml scaleForItem()
// through qmltestrunner offscreen at HEAD 2f23f9bd (a scratch TestCase
// swept zoom x percentage x slice x count and printed 12-decimal values);
// they were NOT derived from this header. The same body was read against
// the Qt5 ancestor f0ad7b23 linearEffect() and is algebraically identical
// (the port only split it into scaleForItem/scaleLinear).

#include <QtTest>

#include "../../declarativeimports/core/units/parabolicmath.h"

using namespace Latte::ParabolicMath;

class ParabolicMathTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void scaleLinear_isTheZoomSlope();
    void scaleForItem_equivalenceTable();
    void zoomOne_yieldsAllOnes();
    void computeScales_stackShapeAndOrder();
    void computeScales_clampsPercentage();
    void computeScales_pointerAtBoundaries();
    void computeScales_singleStep();
    void computeScales_zeroSpreadSteps();
    void computeScales_reversedSwapsStacks();
    void computeScales_mirrorSymmetry();
};

void ParabolicMathTest::scaleLinear_isTheZoomSlope()
{
    QCOMPARE(scaleLinear(1.6, 0.0), 0.0);
    QCOMPARE(scaleLinear(1.6, 1.0), 0.6);
    QCOMPARE(scaleLinear(1.0, 0.7), 0.0);
    QCOMPARE(scaleLinear(2.0, 0.5), 0.5);
}

void ParabolicMathTest::scaleForItem_equivalenceTable()
{
    // zoom | pct | slice index | slice count | shipped QML result
    struct Row { double zoom, pct; int idx, count; double expected; };
    const Row table[] = {
        // the default zoom 1.6, complete grid for counts 1..3
        {1.6, 0.00, 1, 1, 1.000000000000}, {1.6, 0.25, 1, 1, 1.150000000000},
        {1.6, 0.50, 1, 1, 1.300000000000}, {1.6, 0.75, 1, 1, 1.450000000000},
        {1.6, 1.00, 1, 1, 1.600000000000},
        {1.6, 0.00, 1, 2, 1.000000000000}, {1.6, 0.00, 2, 2, 1.300000000000},
        {1.6, 0.25, 1, 2, 1.075000000000}, {1.6, 0.25, 2, 2, 1.375000000000},
        {1.6, 0.50, 1, 2, 1.150000000000}, {1.6, 0.50, 2, 2, 1.450000000000},
        {1.6, 0.75, 1, 2, 1.225000000000}, {1.6, 0.75, 2, 2, 1.525000000000},
        {1.6, 1.00, 1, 2, 1.300000000000}, {1.6, 1.00, 2, 2, 1.600000000000},
        {1.6, 0.00, 1, 3, 1.000000000000}, {1.6, 0.00, 2, 3, 1.200000000000},
        {1.6, 0.00, 3, 3, 1.400000000000},
        {1.6, 0.25, 1, 3, 1.050000000000}, {1.6, 0.25, 2, 3, 1.250000000000},
        {1.6, 0.25, 3, 3, 1.450000000000},
        {1.6, 0.50, 1, 3, 1.100000000000}, {1.6, 0.50, 2, 3, 1.300000000000},
        {1.6, 0.50, 3, 3, 1.500000000000},
        {1.6, 0.75, 1, 3, 1.150000000000}, {1.6, 0.75, 2, 3, 1.350000000000},
        {1.6, 0.75, 3, 3, 1.550000000000},
        {1.6, 1.00, 1, 3, 1.200000000000}, {1.6, 1.00, 2, 3, 1.400000000000},
        {1.6, 1.00, 3, 3, 1.600000000000},
        // spot rows at other zooms
        {1.2, 0.25, 1, 3, 1.016666666667}, {1.2, 0.50, 2, 3, 1.100000000000},
        {1.2, 1.00, 3, 3, 1.200000000000}, {1.2, 0.75, 2, 2, 1.175000000000},
        {2.0, 0.25, 1, 2, 1.125000000000}, {2.0, 0.25, 2, 2, 1.625000000000},
        {2.0, 0.50, 1, 1, 1.500000000000}, {2.0, 0.75, 1, 2, 1.375000000000},
    };

    for (const auto &row : table) {
        const double got = scaleForItem(row.zoom, row.pct, row.idx, row.count);
        QVERIFY2(qAbs(got - row.expected) < 1e-9,
                 qPrintable(QStringLiteral("zoom=%1 pct=%2 idx=%3 count=%4: got %5 expected %6")
                            .arg(row.zoom).arg(row.pct).arg(row.idx).arg(row.count)
                            .arg(got, 0, 'f', 12).arg(row.expected, 0, 'f', 12)));
    }
}

void ParabolicMathTest::zoomOne_yieldsAllOnes()
{
    // zoom 1 means the effect is off: every scale is exactly 1
    for (double pct : {0.0, 0.25, 0.5, 0.75, 1.0}) {
        const auto stacks = computeScales(pct, 3, 1.0, false);
        for (double s : stacks.left) {
            QCOMPARE(s, 1.0);
        }
        for (double s : stacks.right) {
            QCOMPARE(s, 1.0);
        }
    }
}

void ParabolicMathTest::computeScales_stackShapeAndOrder()
{
    // the QML loop runs i = spreadSteps..1 and appends the 1.0 clearing
    // entry: nearest neighbor FIRST (largest scale), descending outwards,
    // 1.0 last. Receivers consume [0] and pass the tail down the row.
    const auto stacks = computeScales(0.5, 3, 1.6, false);

    QCOMPARE(stacks.left.size(), 4);
    QCOMPARE(stacks.right.size(), 4);
    QCOMPARE(stacks.left.last(), 1.0);
    QCOMPARE(stacks.right.last(), 1.0);

    for (int i = 0; i + 1 < stacks.left.size(); ++i) {
        QVERIFY(stacks.left[i] >= stacks.left[i + 1]);
        QVERIFY(stacks.right[i] >= stacks.right[i + 1]);
    }

    // left stack samples the mirrored percentage (1 - pct), right the
    // percentage itself, both at the nearest (largest) slice first
    QVERIFY(qAbs(stacks.left.first() - scaleForItem(1.6, 0.5, 3, 3)) < 1e-12);
    QVERIFY(qAbs(stacks.right.first() - scaleForItem(1.6, 0.5, 3, 3)) < 1e-12);

    const auto offCenter = computeScales(0.2, 3, 1.6, false);
    QVERIFY(qAbs(offCenter.left.first() - scaleForItem(1.6, 0.8, 3, 3)) < 1e-12);
    QVERIFY(qAbs(offCenter.right.first() - scaleForItem(1.6, 0.2, 3, 3)) < 1e-12);
}

void ParabolicMathTest::computeScales_clampsPercentage()
{
    // the QML clamped itemMousePosition/itemLength into [0,1]; the core
    // owns that clamp now (pointer coordinates can overshoot the item)
    const auto below = computeScales(-0.4, 2, 1.6, false);
    const auto zero = computeScales(0.0, 2, 1.6, false);
    QCOMPARE(below.left, zero.left);
    QCOMPARE(below.right, zero.right);

    const auto above = computeScales(1.7, 2, 1.6, false);
    const auto one = computeScales(1.0, 2, 1.6, false);
    QCOMPARE(above.left, one.left);
    QCOMPARE(above.right, one.right);
}

void ParabolicMathTest::computeScales_pointerAtBoundaries()
{
    // pointer at the item's start: the left neighbor side samples pct 1
    // (fully zoomed toward it), the right side samples pct 0
    const auto atStart = computeScales(0.0, 2, 1.6, false);
    QVERIFY(qAbs(atStart.left.first() - scaleForItem(1.6, 1.0, 2, 2)) < 1e-12);
    QVERIFY(qAbs(atStart.right.first() - scaleForItem(1.6, 0.0, 2, 2)) < 1e-12);

    const auto atEnd = computeScales(1.0, 2, 1.6, false);
    QVERIFY(qAbs(atEnd.left.first() - scaleForItem(1.6, 0.0, 2, 2)) < 1e-12);
    QVERIFY(qAbs(atEnd.right.first() - scaleForItem(1.6, 1.0, 2, 2)) < 1e-12);
}

void ParabolicMathTest::computeScales_singleStep()
{
    // spread 3 (the default) means one neighbor each side plus clearing
    const auto stacks = computeScales(0.5, 1, 1.6, false);
    QCOMPARE(stacks.left.size(), 2);
    QCOMPARE(stacks.right.size(), 2);
    QVERIFY(qAbs(stacks.left.first() - 1.3) < 1e-9);
    QVERIFY(qAbs(stacks.right.first() - 1.3) < 1e-9);
    QCOMPARE(stacks.left.last(), 1.0);
}

void ParabolicMathTest::computeScales_zeroSpreadSteps()
{
    // spread 1 degenerates to no neighbors: only the clearing entry, so
    // the propagation still terminates cleanly at the first receiver
    const auto stacks = computeScales(0.5, 0, 1.6, false);
    QCOMPARE(stacks.left, QVector<double>({1.0}));
    QCOMPARE(stacks.right, QVector<double>({1.0}));
}

void ParabolicMathTest::computeScales_reversedSwapsStacks()
{
    // RTL horizontal rows swap the two stacks wholesale (the QML swap)
    const auto normal = computeScales(0.3, 3, 1.6, false);
    const auto reversed = computeScales(0.3, 3, 1.6, true);
    QCOMPARE(reversed.left, normal.right);
    QCOMPARE(reversed.right, normal.left);
}

void ParabolicMathTest::computeScales_mirrorSymmetry()
{
    // left(p) mirrors right(1-p): the curve has no left/right bias
    const auto atP = computeScales(0.3, 3, 1.6, false);
    const auto atMirror = computeScales(0.7, 3, 1.6, false);
    QCOMPARE(atP.left, atMirror.right);
    QCOMPARE(atP.right, atMirror.left);
}

QTEST_GUILESS_MAIN(ParabolicMathTest)
#include "parabolicmathtest.moc"
