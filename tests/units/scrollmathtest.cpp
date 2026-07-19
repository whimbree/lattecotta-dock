/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// EX-21 ScrollOverflowMath (docs/tracking/QML_EXTRACTION_PLAN.md): the task-row
// overflow scrolling math extracted from ScrollableList.qml - the overflow
// predicate with its int-world floor, the asymmetric step clamps, the
// scroll-into-view distance, the autoscroll trigger zones with their block
// row, and the extra-space-change bounds correction. Every expected value
// was derived by hand-executing the Qt5/HEAD QML body (the two are
// byte-identical for these functions, read at f0ad7b23 and HEAD); the
// offscreen tests/qml/tst_scrollablelist.qml cross-checks the same
// scenarios against the shipped QML on every run.
//
// The ScrollOverflowMath wrapper is compiled into this binary (sanitized,
// per the step-2.5 rule) and its boundary refusals are tested here too.

#include <QtTest>

#include <QRegularExpression>

#include <optional>

#include "../../plasmoid/plugin/scrolloverflowmath.h"
#include "../../plasmoid/plugin/units/scrollmath.h"

using namespace Latte::ScrollMath;

namespace {

//! the harness numbers used throughout: a 400px viewport, 24px icons,
//! icon slots of 70px with 6px edge -> 30px trigger zone
constexpr int kViewport = 400;
constexpr double kIconSize = 24.0;
constexpr double kTotalsLength = 70.0;
constexpr double kTriggerZone = 30.0;

ScrollEnv overflowEnv(double contentLength = 600.0, double currentPos = 0.0)
{
    return {true, contentLength, kViewport, currentPos};
}

AutoScrollGuards permissiveGuards()
{
    // enabled, five tasks, not the last item, no parabolic zoom
    return {true, false, 5, false, 1.0};
}

std::optional<double> autoScrollAt(const ScrollEnv &env, const ItemSpan &item,
                                   const AutoScrollGuards &guards,
                                   bool animationRunning = false)
{
    return autoScrollDelta(env, item, kTriggerZone, guards, animationRunning, kTotalsLength);
}

}

class ScrollMathTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void contentsExceed_overflowTable();
    void contentsExtraSpace_truncatesLikeAnIntProperty();
    void wheelScrollStep_isThreeAndAHalfSlots();
    void steppedPos_clampTable();
    void steppedPos_zeroStepKeepsQt5IncreaseSemantics();
    void focusScrollDelta_needsOverflow();
    void focusScrollDelta_offStartScrollsBackWithMargin();
    void focusScrollDelta_offEndScrollsForwardWithMargin();
    void focusScrollDelta_visibleItemTable();
    void autoScrollDelta_blockTable();
    void autoScrollDelta_startZone();
    void autoScrollDelta_endZone();
    void autoScrollDelta_boundaryEqualitiesBlockTheirZone();
    void autoScrollDelta_zoneBoundariesAreExclusive();
    void autoScrollDelta_startZoneWinsWhenZonesOverlap();
    void autoScrollDelta_animationRunningScalesTheStep();
    void boundsCorrection_table();
    void wrapper_forwardsAndConvertsNullopt();
    void wrapper_refusesMissingFacts();
    void wrapper_refusesJunkNumbers();
};

void ScrollMathTest::contentsExceed_overflowTable()
{
    // scrolling | content | viewport | exceed
    struct Row { bool enabled; double content; int viewport; bool exceed; };
    const Row table[] = {
        {false, 1000.0, kViewport, false},   // setting off blocks overflow entirely
        {true, 300.0, kViewport, false},     // underflow
        {true, 400.0, kViewport, false},     // exact fit is not overflow (strict >)
        {true, 400.6, kViewport, false},     // the int-world floor: a fractional
                                             // sliver beyond the viewport is not overflow
        {true, 401.0, kViewport, true},      // one whole pixel beyond is
        {true, 600.0, kViewport, true},
        {true, 0.0, 0, false},               // empty row, zero viewport
        {true, 0.4, 0, false},               // floor(0.4) = 0, not > 0
        {true, 1.0, 0, true},
    };

    for (const auto &row : table) {
        const ScrollEnv env{row.enabled, row.content, row.viewport, 0.0};
        QVERIFY2(contentsExceed(env) == row.exceed,
                 qPrintable(QStringLiteral("enabled=%1 content=%2 viewport=%3: expected %4")
                            .arg(row.enabled).arg(row.content).arg(row.viewport).arg(row.exceed)));
    }
}

void ScrollMathTest::contentsExtraSpace_truncatesLikeAnIntProperty()
{
    // Qt5 bound the real difference to an int QML property (ECMAScript
    // ToInt32: truncation toward zero); scrollLastPos read that int back
    QCOMPARE(contentsExtraSpace(overflowEnv(600.0)), 200);
    QCOMPARE(contentsExtraSpace(overflowEnv(600.7)), 200); // 200.7 truncates
    QCOMPARE(contentsExtraSpace(overflowEnv(401.0)), 1);   // minimum overflow
    QCOMPARE(contentsExtraSpace(overflowEnv(400.6)), 0);   // no overflow -> 0
    QCOMPARE(contentsExtraSpace({false, 600.0, kViewport, 0.0}), 0);
}

void ScrollMathTest::wheelScrollStep_isThreeAndAHalfSlots()
{
    QCOMPARE(wheelScrollStep(kTotalsLength), 245.0);
    QCOMPARE(wheelScrollStep(0.0), 0.0);
}

void ScrollMathTest::steppedPos_clampTable()
{
    // extra space is 200 throughout; pos | step | result
    struct Row { double pos, step, result; };
    const Row table[] = {
        {0.0, 50.0, 50.0},      // forward inside the range
        {0.0, 245.0, 200.0},    // forward clamps at the last position
        {200.0, 10.0, 200.0},   // already at the end stays there
        {100.0, -50.0, 50.0},   // backward inside the range
        {100.0, -245.0, 0.0},   // backward clamps at the first position
        {0.0, -10.0, 0.0},      // already at the start stays there
        {199.5, 0.75, 200.0},   // fractional forward clamp
    };

    for (const auto &row : table) {
        QCOMPARE(steppedPos(overflowEnv(600.0, row.pos), row.step), row.result);
    }

    // no overflow: the whole valid range is [0, 0]
    const ScrollEnv flat{true, 300.0, kViewport, 0.0};
    QCOMPARE(steppedPos(flat, 100.0), 0.0);
    QCOMPARE(steppedPos(flat, -100.0), 0.0);
}

void ScrollMathTest::steppedPos_zeroStepKeepsQt5IncreaseSemantics()
{
    // Qt5's increasePosWithStep(0) was min(lastPos, pos): an out-of-range
    // position clamps DOWN even with a zero step, an in-range one holds.
    // The signed form routes step >= 0 through exactly that branch.
    QCOMPARE(steppedPos(overflowEnv(600.0, 100.0), 0.0), 100.0);
    QCOMPARE(steppedPos(overflowEnv(600.0, 350.0), 0.0), 200.0);
}

void ScrollMathTest::focusScrollDelta_needsOverflow()
{
    // Qt5 focusOn returned before measuring when the row does not overflow
    const ScrollEnv flat{true, 300.0, kViewport, 0.0};
    QCOMPARE(focusScrollDelta(flat, {-50.0, 60.0}, kIconSize), std::nullopt);

    const ScrollEnv disabled{false, 600.0, kViewport, 0.0};
    QCOMPARE(focusScrollDelta(disabled, {-50.0, 60.0}, kIconSize), std::nullopt);
}

void ScrollMathTest::focusScrollDelta_offStartScrollsBackWithMargin()
{
    // Qt5: distance = |start - iconSize|, scrolled toward the start
    QCOMPARE(focusScrollDelta(overflowEnv(), {-70.0, 60.0}, kIconSize),
             std::optional<double>(-94.0)); // fully off screen
    QCOMPARE(focusScrollDelta(overflowEnv(), {-10.0, 60.0}, kIconSize),
             std::optional<double>(-34.0)); // partially off screen
}

void ScrollMathTest::focusScrollDelta_offEndScrollsForwardWithMargin()
{
    // Qt5: distance = |start - viewport + length + iconSize|, toward the end
    QCOMPARE(focusScrollDelta(overflowEnv(), {420.0, 60.0}, kIconSize),
             std::optional<double>(104.0)); // fully off screen
    QCOMPARE(focusScrollDelta(overflowEnv(), {380.0, 60.0}, kIconSize),
             std::optional<double>(64.0));  // partially off screen
}

void ScrollMathTest::focusScrollDelta_visibleItemTable()
{
    // fully visible items produce no scroll; the boundaries are exclusive
    // (start == 0 is not < 0; end == viewport is not > viewport)
    const ItemSpan visible[] = {
        {170.0, 60.0},  // mid-viewport
        {0.0, 60.0},    // flush at the start
        {340.0, 60.0},  // flush at the end (340 + 60 == 400)
    };
    for (const auto &item : visible) {
        QCOMPARE(focusScrollDelta(overflowEnv(), item, kIconSize), std::nullopt);
    }
}

void ScrollMathTest::autoScrollDelta_blockTable()
{
    // an item deep inside the start trigger zone, scrolled off the first
    // position - it would scroll were it not for the blocking fact
    const ScrollEnv env = overflowEnv(600.0, 100.0);
    const ItemSpan inStartZone{10.0, 60.0};

    // each row flips exactly one fact off the permissive baseline
    AutoScrollGuards disabledNotDragging = permissiveGuards();
    disabledNotDragging.autoScrollTasksEnabled = false;
    QCOMPARE(autoScrollAt(env, inStartZone, disabledNotDragging), std::nullopt);

    AutoScrollGuards disabledButDragging = disabledNotDragging;
    disabledButDragging.duringDragging = true; // a drag overrides the setting
    QCOMPARE(autoScrollAt(env, inStartZone, disabledButDragging),
             std::optional<double>(-kTotalsLength));

    AutoScrollGuards twoTasks = permissiveGuards();
    twoTasks.tasksCount = 2; // fewer than 3 tasks never autoscrolls
    QCOMPARE(autoScrollAt(env, inStartZone, twoTasks), std::nullopt);

    AutoScrollGuards threeTasks = permissiveGuards();
    threeTasks.tasksCount = 3; // the boundary is inclusive
    QCOMPARE(autoScrollAt(env, inStartZone, threeTasks),
             std::optional<double>(-kTotalsLength));

    AutoScrollGuards lastItemZoomed = permissiveGuards();
    lastItemZoomed.hoveredIsLastVisibleItem = true;
    lastItemZoomed.parabolicZoomFactor = 1.6; // Qt5: parabolic + last task blocks
    QCOMPARE(autoScrollAt(env, inStartZone, lastItemZoomed), std::nullopt);

    AutoScrollGuards lastItemNoZoom = permissiveGuards();
    lastItemNoZoom.hoveredIsLastVisibleItem = true;
    lastItemNoZoom.parabolicZoomFactor = 1.0; // zoom exactly 1 does not block
    QCOMPARE(autoScrollAt(env, inStartZone, lastItemNoZoom),
             std::optional<double>(-kTotalsLength));

    AutoScrollGuards zoomedNotLast = permissiveGuards();
    zoomedNotLast.parabolicZoomFactor = 1.6; // zoom alone does not block
    QCOMPARE(autoScrollAt(env, inStartZone, zoomedNotLast),
             std::optional<double>(-kTotalsLength));

    // no overflow blocks regardless of the guards
    const ScrollEnv flat{true, 300.0, kViewport, 100.0};
    QCOMPARE(autoScrollAt(flat, inStartZone, permissiveGuards()), std::nullopt);
}

void ScrollMathTest::autoScrollDelta_startZone()
{
    const ScrollEnv env = overflowEnv(600.0, 100.0);
    QCOMPARE(autoScrollAt(env, {10.0, 60.0}, permissiveGuards()),
             std::optional<double>(-kTotalsLength));
}

void ScrollMathTest::autoScrollDelta_endZone()
{
    // item end 430 > 400 - 30 = 370
    const ScrollEnv env = overflowEnv(600.0, 100.0);
    QCOMPARE(autoScrollAt(env, {370.0, 60.0}, permissiveGuards()),
             std::optional<double>(kTotalsLength));
}

void ScrollMathTest::autoScrollDelta_boundaryEqualitiesBlockTheirZone()
{
    // at the first position the start zone must not fire (Qt5's wheel-at-
    // the-first-task issue), and at the last position the end zone must not
    const ScrollEnv atFirst = overflowEnv(600.0, 0.0);
    QCOMPARE(autoScrollAt(atFirst, {10.0, 60.0}, permissiveGuards()), std::nullopt);

    const ScrollEnv atLast = overflowEnv(600.0, 200.0);
    QCOMPARE(autoScrollAt(atLast, {370.0, 60.0}, permissiveGuards()), std::nullopt);

    // each boundary only blocks its own zone
    QCOMPARE(autoScrollAt(atFirst, {370.0, 60.0}, permissiveGuards()),
             std::optional<double>(kTotalsLength));
    QCOMPARE(autoScrollAt(atLast, {10.0, 60.0}, permissiveGuards()),
             std::optional<double>(-kTotalsLength));
}

void ScrollMathTest::autoScrollDelta_zoneBoundariesAreExclusive()
{
    const ScrollEnv env = overflowEnv(600.0, 100.0);
    // start exactly at the trigger zone is not < it
    QCOMPARE(autoScrollAt(env, {kTriggerZone, 60.0}, permissiveGuards()), std::nullopt);
    // end exactly at viewport - zone is not > it (310 + 60 == 370)
    QCOMPARE(autoScrollAt(env, {310.0, 60.0}, permissiveGuards()), std::nullopt);
}

void ScrollMathTest::autoScrollDelta_startZoneWinsWhenZonesOverlap()
{
    // a wide item inside both zones: Qt5's if/else-if order scrolls toward
    // the start
    const ScrollEnv env{true, 600.0, 50, 100.0}; // 50px viewport, zones overlap
    QCOMPARE(autoScrollDelta(env, {10.0, 60.0}, kTriggerZone, permissiveGuards(),
                             false, kTotalsLength),
             std::optional<double>(-kTotalsLength));
}

void ScrollMathTest::autoScrollDelta_animationRunningScalesTheStep()
{
    // while the scroll animation still runs the step is 3.5 slots, so a
    // held hover keeps pace with the animated position (Qt5 localStep)
    const ScrollEnv env = overflowEnv(600.0, 100.0);
    QCOMPARE(autoScrollAt(env, {10.0, 60.0}, permissiveGuards(), true),
             std::optional<double>(-245.0));
    QCOMPARE(autoScrollAt(env, {370.0, 60.0}, permissiveGuards(), true),
             std::optional<double>(245.0));
}

void ScrollMathTest::boundsCorrection_table()
{
    // pos | extra space via content | correction (nullopt = in bounds)
    QCOMPARE(boundsCorrection(overflowEnv(600.0, 100.0)), std::nullopt);
    QCOMPARE(boundsCorrection(overflowEnv(600.0, 0.0)), std::nullopt);   // at first
    QCOMPARE(boundsCorrection(overflowEnv(600.0, 200.0)), std::nullopt); // at last
    QCOMPARE(boundsCorrection(overflowEnv(600.0, -5.0)), std::optional<double>(0.0));
    QCOMPARE(boundsCorrection(overflowEnv(600.0, 250.0)), std::optional<double>(200.0));
    // the overflow just vanished: anything beyond 0 comes back to 0
    QCOMPARE(boundsCorrection({true, 300.0, kViewport, 150.0}), std::optional<double>(0.0));
    QCOMPARE(boundsCorrection({false, 600.0, kViewport, 150.0}), std::optional<double>(0.0));
}

void ScrollMathTest::wrapper_forwardsAndConvertsNullopt()
{
    const Latte::Tasks::ScrollOverflowMath wrapper;

    QCOMPARE(wrapper.contentsExceed(true, 600.0, kViewport), true);
    QCOMPARE(wrapper.contentsExtraSpace(true, 600.7, kViewport), 200);
    QCOMPARE(wrapper.wheelScrollStep(kTotalsLength), 245.0);
    QCOMPARE(wrapper.steppedPos(true, 600.0, kViewport, 0.0, 245.0), 200.0);

    // an optional-returning function converts nullopt to an invalid
    // QVariant (undefined at the QML boundary) and a value to a double
    const QVariant none = wrapper.focusScrollDelta(true, 600.0, kViewport, 170.0, 60.0, kIconSize);
    QVERIFY(!none.isValid());
    const QVariant some = wrapper.focusScrollDelta(true, 600.0, kViewport, -70.0, 60.0, kIconSize);
    QCOMPARE(some.toDouble(), -94.0);

    QVariantMap facts;
    facts.insert(QStringLiteral("scrollingEnabled"), true);
    facts.insert(QStringLiteral("contentLength"), 600.0);
    facts.insert(QStringLiteral("viewportLength"), kViewport);
    facts.insert(QStringLiteral("currentPos"), 100.0);
    facts.insert(QStringLiteral("itemStart"), 10.0);
    facts.insert(QStringLiteral("itemLength"), 60.0);
    facts.insert(QStringLiteral("triggerZone"), kTriggerZone);
    facts.insert(QStringLiteral("autoScrollTasksEnabled"), true);
    facts.insert(QStringLiteral("duringDragging"), false);
    facts.insert(QStringLiteral("tasksCount"), 5);
    facts.insert(QStringLiteral("hoveredIsLastVisibleItem"), false);
    facts.insert(QStringLiteral("parabolicZoomFactor"), 1.0);
    facts.insert(QStringLiteral("scrollAnimationRunning"), false);
    facts.insert(QStringLiteral("totalsLength"), kTotalsLength);
    QCOMPARE(wrapper.autoScrollDelta(facts).toDouble(), -kTotalsLength);

    const QVariant inBounds = wrapper.boundsCorrection(true, 600.0, kViewport, 100.0);
    QVERIFY(!inBounds.isValid());
    QCOMPARE(wrapper.boundsCorrection(true, 600.0, kViewport, 250.0).toDouble(), 200.0);
}

void ScrollMathTest::wrapper_refusesMissingFacts()
{
    const Latte::Tasks::ScrollOverflowMath wrapper;

    QVariantMap facts; // deliberately missing everything
    facts.insert(QStringLiteral("scrollingEnabled"), true);

    QTest::ignoreMessage(QtCriticalMsg,
                         QRegularExpression(QStringLiteral("autoScrollDelta called without fact")));
    QVERIFY(!wrapper.autoScrollDelta(facts).isValid());
}

void ScrollMathTest::wrapper_refusesJunkNumbers()
{
    const Latte::Tasks::ScrollOverflowMath wrapper;
    const double nan = qQNaN();

    // NaN arrives when QML passes undefined into a double parameter; a
    // negative length has no producer in a sane shell. Both are refused
    // loudly with a safe result instead of computing from junk.
    QTest::ignoreMessage(QtCriticalMsg,
                         QRegularExpression(QStringLiteral("invalid contentLength")));
    QCOMPARE(wrapper.contentsExceed(true, nan, kViewport), false);

    QTest::ignoreMessage(QtCriticalMsg,
                         QRegularExpression(QStringLiteral("invalid viewportLength")));
    QCOMPARE(wrapper.contentsExtraSpace(true, 600.0, -4), 0);

    QTest::ignoreMessage(QtCriticalMsg,
                         QRegularExpression(QStringLiteral("invalid totalsLength")));
    QCOMPARE(wrapper.wheelScrollStep(-1.0), 0.0);

    // a refused step keeps the position untouched
    QTest::ignoreMessage(QtCriticalMsg,
                         QRegularExpression(QStringLiteral("non-finite signedStep")));
    QCOMPARE(wrapper.steppedPos(true, 600.0, kViewport, 120.0, nan), 120.0);

    // a junk currentPos cannot be "kept": the row start is the fallback
    QTest::ignoreMessage(QtCriticalMsg,
                         QRegularExpression(QStringLiteral("non-finite currentPos")));
    QCOMPARE(wrapper.steppedPos(true, 600.0, kViewport, nan, 10.0), 0.0);

    QTest::ignoreMessage(QtCriticalMsg,
                         QRegularExpression(QStringLiteral("invalid margin")));
    QVERIFY(!wrapper.focusScrollDelta(true, 600.0, kViewport, -70.0, 60.0, -1.0).isValid());
}

QTEST_GUILESS_MAIN(ScrollMathTest)
#include "scrollmathtest.moc"
