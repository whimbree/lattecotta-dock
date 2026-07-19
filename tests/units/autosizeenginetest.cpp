/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// EX-04 AutoSizeEngine (docs/tracking/QML_EXTRACTION_PLAN.md): the automatic
// icon-size search as a pure step function. Termination is the headline
// invariant: the ad9b823f incident was this feedback loop hanging the GUI
// thread at 100% CPU on startup because the inherited equality exits
// (nextIconSize !== 16 / !== maxIconSize) stepped past their bound for any
// size not congruent modulo the step - the live case was iconSize=78. A
// regression back to the equality form makes the property loops here spin
// and fails the run via the harness timeout instead of a live dock hang.
//
// Reference-table generation method (per docs/reference/TESTING.md): the rows in
// shrink_equivalenceTable and grow_equivalenceTable were produced by
// driving the SHIPPED containment/package/contents/code/autosize.js
// shrinkStep()/growStep() through qmltestrunner offscreen at HEAD 71737d61
// (a scratch TestCase under tests/generators/ printed 12-decimal values);
// they were NOT derived from the header under test.

#include <QtTest>

// C++
#include <type_traits>
#include <variant>

#include "../../containment/plugin/units/autosizeengine.h"

using namespace Latte::AutoSizeEngine;

// The invalid states the type discipline eliminates, proven unrepresentable
// at compile time (step-2.5 law): a "nothing found" or "restore automatic"
// outcome cannot smuggle a garbage size along (the alternatives carry no
// payload), and the outcome set is closed - the wrapper's std::visit
// dispatch static_asserts exhaustiveness against exactly these three.
static_assert(std::is_empty_v<KeepCurrent>,
              "KeepCurrent must not carry a size for callers to misread");
static_assert(std::is_empty_v<RestoreAutomaticMax>,
              "RestoreAutomaticMax must not carry a size; -1 exists only at the QML boundary");
static_assert(std::variant_size_v<AutoSizeStep> == 3,
              "AutoSizeStep grew an alternative: extend the tests and the wrapper dispatch");

class AutoSizeEngineTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void shrink_equivalenceTable();
    void grow_equivalenceTable();
    void shrink_terminatesForEveryIconSize();
    void shrink_staysWithinBounds();
    void shrink_clampsDegenerateInputsToFloor();
    void grow_terminatesAndSaturatesForEveryIconSize();
    void grow_reportsWhenNoStepFits();
    void history_ringTruncatesToNewestEntries();
    void history_producesEndlessLoop_truthTable();
    void step_shrinksAndRecordsPrediction();
    void step_keepsCurrentInsideRobustnessBand();
    void step_growsOnlyFromOwnAppliedSize();
    void step_growToCeilingRestoresAutomatic();
    void step_growMidRangeAppliesSize();
    void step_protectorBlocksRepeatingGrow();
    void step_noFitGrowKeepsCurrentAndHistory();
    void step_zeroItemLengthCollapsesTheBand();
    void step_zeroLayoutLengthGrowsToCeiling();
    void step_growRightAfterBoundaryShrinkIsRejected();
};

namespace {

//! a step() input around one measured layout snapshot; fields mirror the
//! QML shell's reads (see AutoSizeInput's comments)
AutoSizeInput makeInput(double layoutLength, double maxLength, double itemLength,
                        int currentIconSize, int maxIconSize, double zoomFactor,
                        std::optional<int> appliedIconSize)
{
    return AutoSizeInput{layoutLength, maxLength, itemLength,
                         currentIconSize, maxIconSize, zoomFactor, appliedIconSize};
}

}

void AutoSizeEngineTest::shrink_equivalenceTable()
{
    // maxIconSize | currentIconSize | layoutLength | toShrinkLimit
    // | shipped iconSize | shipped nextLength (12 decimals)
    struct Row { int max; int current; double layout, limit; int iconSize; double projected; };
    const Row table[] = {
        {64, 64, 1000.0, 900.0, 56, 875.000000000000},
        {64, 64, 1000.0, 500.0, 32, 500.000000000000},
        {64, 64, 1000.0, 100.0, 16, 250.000000000000},
        {64, 64, 1000.0, -1.0, 16, 250.000000000000},
        {64, 48, 900.0, 600.0, 32, 600.000000000000},
        {64, 32, 1000.0, 600.0, 16, 500.000000000000},
        {78, 78, 780.0, 400.0, 38, 380.000000000000},
        {78, 78, 780.0, -1.0, 16, 160.000000000000},
        {96, 64, 1200.0, 800.0, 40, 750.000000000000},
        // exits exactly at the limit: the loop condition is strict (>)
        {128, 128, 2000.0, 1500.0, 96, 1500.000000000000},
        {48, 48, 500.0, 450.0, 40, 416.666666666667},
        {24, 24, 300.0, 200.0, 16, 200.000000000000},
        {16, 16, 200.0, 100.0, 16, 200.000000000000},
        {30, 30, 433.7, 321.9, 22, 318.046666666667},
        // a ceiling below the floor clamps up to the floor
        {8, 8, 100.0, -1.0, 16, 200.000000000000},
        {64, 64, 0.0, -1.0, 16, 0.000000000000},
    };

    for (const auto &row : table) {
        const ShrinkResult got = shrinkUntilFits(row.max, row.current, row.layout, row.limit);
        QVERIFY2(got.iconSize == row.iconSize && qAbs(got.projectedLength - row.projected) < 1e-9,
                 qPrintable(QStringLiteral("max=%1 current=%2 layout=%3 limit=%4: got (%5, %6) expected (%7, %8)")
                            .arg(row.max).arg(row.current).arg(row.layout).arg(row.limit)
                            .arg(got.iconSize).arg(got.projectedLength, 0, 'f', 12)
                            .arg(row.iconSize).arg(row.projected, 0, 'f', 12)));
    }
}

void AutoSizeEngineTest::grow_equivalenceTable()
{
    // maxIconSize | currentIconSize | layoutLength | toGrowLimit
    // | shipped foundGoodSize (-1 = none) | shipped nextLength
    struct Row { int max; int current; double layout, limit; int goodSize; double projected; };
    const Row table[] = {
        {64, 32, 500.0, 1e9, 64, 1000.000000000000},
        {64, 32, 500.0, 600.0, -1, 625.000000000000},
        {64, 32, 500.0, 510.0, -1, 625.000000000000},
        {64, 32, 500.0, -1.0, -1, 625.000000000000},
        {78, 78, 780.0, 1e9, 78, 780.000000000000},
        {128, 78, 900.0, 1000.0, 86, 1084.615384615385},
        {96, 48, 700.0, 720.0, -1, 816.666666666667},
        {64, 56, 640.0, 700.0, -1, 731.428571428571},
        {24, 16, 200.0, 260.0, -1, 300.000000000000},
        {48, 40, 433.7, 470.25, -1, 520.440000000000},
        {16, 16, 100.0, 1e9, 16, 100.000000000000},
        {64, 64, 500.0, 1e9, 64, 500.000000000000},
    };

    for (const auto &row : table) {
        const GrowResult got = growWhileFits(row.max, row.current, row.layout, row.limit);
        const int gotGood = got.goodSize.value_or(-1); //! -1 mirrors the shipped JS sentinel
        QVERIFY2(gotGood == row.goodSize && qAbs(got.projectedLength - row.projected) < 1e-9,
                 qPrintable(QStringLiteral("max=%1 current=%2 layout=%3 limit=%4: got (%5, %6) expected (%7, %8)")
                            .arg(row.max).arg(row.current).arg(row.layout).arg(row.limit)
                            .arg(gotGood).arg(got.projectedLength, 0, 'f', 12)
                            .arg(row.goodSize).arg(row.projected, 0, 'f', 12)));
    }
}

void AutoSizeEngineTest::shrink_terminatesForEveryIconSize()
{
    // the ad9b823f named regression, driven through step(): a barely
    // positive maxLength (the smallest geometry the shell's <= 0 contract
    // lets through) makes toShrinkLimit negative, so the length condition
    // can never break the loop and only the floor exit can. Every icon
    // size from 16 to 256 - multiples of the step and non-multiples like
    // the live 78 alike - must land exactly on the 16px floor and return.
    for (int size = 16; size <= 256; ++size) {
        History history;
        const AutoSizeInput input = makeInput(10.0 * size, /*maxLength*/ 1.0, /*itemLength*/ size,
                                              size, size, 1.6, std::nullopt);
        const AutoSizeStep result = step(input, history);

        const auto *applied = std::get_if<ApplySize>(&result);
        QVERIFY2(applied, qPrintable(QStringLiteral("size %1: an overflowing layout must shrink").arg(size)));
        QVERIFY2(applied->iconSize == minIconSize,
                 qPrintable(QStringLiteral("size %1: an unsatisfiable shrink limit must exit at the floor, got %2")
                            .arg(size).arg(applied->iconSize)));
    }
}

void AutoSizeEngineTest::shrink_staysWithinBounds()
{
    // a satisfiable limit exits early on the length condition; the result
    // must stay inside [16, maxIconSize - step] (floor-clamped) for every
    // start size. Mirrors the retired tst_autosize.qml pin.
    for (int size = 17; size <= 256; ++size) {
        // current layout: 8 items at 'size' px; the limit allows roughly half
        const double limit = 4.0 * size;
        const ShrinkResult res = shrinkUntilFits(size, size, 8.0 * size, limit);

        QVERIFY2(res.iconSize >= minIconSize,
                 qPrintable(QStringLiteral("size %1: result %2 fell below the floor").arg(size).arg(res.iconSize)));
        QVERIFY2(res.iconSize <= qMax(minIconSize, size - automaticStep),
                 qPrintable(QStringLiteral("size %1: a shrink pass must step down at least once").arg(size)));

        if (res.iconSize > minIconSize) {
            // only a floor exit is allowed to leave the limit unsatisfied
            QVERIFY2(res.projectedLength <= limit,
                     qPrintable(QStringLiteral("size %1: projected length %2 does not fit the limit")
                                .arg(size).arg(res.projectedLength)));
        }
    }
}

void AutoSizeEngineTest::shrink_clampsDegenerateInputsToFloor()
{
    // inputs below the floor or nonsensical limits must still return
    const ShrinkResult belowFloor = shrinkUntilFits(8, 8, 100.0, -1.0);
    QCOMPARE(belowFloor.iconSize, minIconSize); // a max size below the floor clamps up

    const ShrinkResult zeroLayout = shrinkUntilFits(64, 64, 0.0, -1.0);
    QCOMPARE(zeroLayout.iconSize, minIconSize); // zero length, negative limit: floor exit
}

void AutoSizeEngineTest::grow_terminatesAndSaturatesForEveryIconSize()
{
    // an always-satisfied grow limit must climb to the ceiling and stop
    // there for every start size, including non-multiples of the step and
    // the current == ceiling case (where the inherited equality form spun)
    for (int size = 16; size <= 256; ++size) {
        const GrowResult res = growWhileFits(256, size, 10.0, 1e18);
        QVERIFY2(res.goodSize.has_value() && *res.goodSize == 256,
                 qPrintable(QStringLiteral("size %1: an unbounded grow limit must saturate at maxIconSize").arg(size)));
    }

    const GrowResult atCeiling = growWhileFits(78, 78, 10.0, 1e18);
    QVERIFY(atCeiling.goodSize.has_value());
    QCOMPARE(*atCeiling.goodSize, 78); // already at a non-multiple ceiling: saturates and returns
}

void AutoSizeEngineTest::grow_reportsWhenNoStepFits()
{
    // when even the first candidate exceeds the limit the pass reports
    // nothing (the shipped JS's -1) and must not loop
    for (int size = 16; size <= 256; ++size) {
        const GrowResult res = growWhileFits(256, size, 10.0 * size, -1.0);
        QVERIFY2(!res.goodSize.has_value(),
                 qPrintable(QStringLiteral("size %1: an unsatisfiable grow limit reports no good size").arg(size)));
    }
}

void AutoSizeEngineTest::history_ringTruncatesToNewestEntries()
{
    History history;
    QCOMPARE(history.size(), 0);

    for (int i = 1; i <= 10; ++i) {
        history.addPrediction(i, 1000 + i);
    }
    QCOMPARE(history.size(), historyMaxSize); // exactly full: no truncation yet

    history.addPrediction(11, 1011); // the add that overflows cuts back to the newest 4
    QCOMPARE(history.size(), historyMinSize);
    QCOMPARE(history.at(0).currentLength, 11); // newest first
    QCOMPARE(history.at(1).currentLength, 10);
    QCOMPARE(history.at(2).currentLength, 9);
    QCOMPARE(history.at(3).currentLength, 8);

    history.addPrediction(12, 1012); // grows again until the next overflow
    QCOMPARE(history.size(), historyMinSize + 1);
    QCOMPARE(history.at(0).currentLength, 12);

    history.clear();
    QCOMPARE(history.size(), 0);
}

void AutoSizeEngineTest::history_producesEndlessLoop_truthTable()
{
    // the detector fires only when ALL hold: at least two entries, the
    // incoming prediction equals history[1], history[0] was a shrink
    // (current > predicted) and history[1] was a grow (current < predicted)
    {
        History history;
        QVERIFY(!history.producesEndlessLoop(500, 875)); // empty

        history.addPrediction(500, 875);
        QVERIFY(!history.producesEndlessLoop(500, 875)); // one entry
    }
    {
        History history; // [0]=(875,625) shrink, [1]=(500,875) grow
        history.addPrediction(500, 875);
        history.addPrediction(875, 625);
        QVERIFY(history.producesEndlessLoop(500, 875)); // full match: fires
        QVERIFY(!history.producesEndlessLoop(500, 874)); // predicted differs
        QVERIFY(!history.producesEndlessLoop(501, 875)); // current differs
    }
    {
        History history; // [0] grew: the undo-shrink condition fails
        history.addPrediction(500, 875);
        history.addPrediction(600, 700);
        QVERIFY(!history.producesEndlessLoop(500, 875));
    }
    {
        History history; // [1]=(900,700) shrank: the grew-before condition fails
        history.addPrediction(900, 700);
        history.addPrediction(875, 625);
        QVERIFY(!history.producesEndlessLoop(900, 700));
    }
    {
        History history; // [0] flat (current == predicted): strict > fails
        history.addPrediction(500, 875);
        history.addPrediction(700, 700);
        QVERIFY(!history.producesEndlessLoop(500, 875));
    }
}

void AutoSizeEngineTest::step_shrinksAndRecordsPrediction()
{
    // layout 1000 at icon 64, maxLength 1100, one zoomed 64px item reserved:
    // toShrinkLimit = 1100 - 1.6*64 = 997.6, overflow, candidate 56 projects
    // 875 and fits
    History history;
    const AutoSizeInput input = makeInput(1000.0, 1100.0, 64.0, 64, 64, 1.6, std::nullopt);
    const AutoSizeStep result = step(input, history);

    const auto *applied = std::get_if<ApplySize>(&result);
    QVERIFY(applied);
    QCOMPARE(applied->iconSize, 56);
    QCOMPARE(history.size(), 1);
    QCOMPARE(history.at(0).currentLength, 1000);
    QCOMPARE(history.at(0).predictedLength, 875);
}

void AutoSizeEngineTest::step_keepsCurrentInsideRobustnessBand()
{
    // the asymmetric limits: toShrinkLimit = 1000 - 1.6*64 = 897.6,
    // toGrowLimit = 1000 - 1.2*1.6*64 = 877.12. A layout inside the band
    // must neither shrink nor grow - this margin is exactly what prevents
    // a grow immediately after a shrink at the boundary (upstream's
    // robustness comment; the oscillation the asymmetry prevents)
    History history;
    const AutoSizeInput input = makeInput(890.0, 1000.0, 64.0, 64, 64, 1.6, std::optional<int>(64));
    const AutoSizeStep result = step(input, history);

    QVERIFY(std::holds_alternative<KeepCurrent>(result));
    QCOMPARE(history.size(), 0); // an idle pass records nothing
}

void AutoSizeEngineTest::step_growsOnlyFromOwnAppliedSize()
{
    // growable snapshot (layout 500 far under toGrowLimit 1877.12), but the
    // search never grows from a size it did not apply itself: automatic
    // sizing (absent) and a foreign size must both keep current
    History history;
    const AutoSizeInput automatic = makeInput(500.0, 2000.0, 64.0, 32, 64, 1.6, std::nullopt);
    QVERIFY(std::holds_alternative<KeepCurrent>(step(automatic, history)));

    const AutoSizeInput foreign = makeInput(500.0, 2000.0, 64.0, 32, 64, 1.6, std::optional<int>(48));
    QVERIFY(std::holds_alternative<KeepCurrent>(step(foreign, history)));

    QCOMPARE(history.size(), 0);
}

void AutoSizeEngineTest::step_growToCeilingRestoresAutomatic()
{
    // same snapshot grown from its own applied size saturates at the
    // ceiling: the constraint is gone, distinct from applying max as a size
    History history;
    const AutoSizeInput input = makeInput(500.0, 2000.0, 64.0, 32, 64, 1.6, std::optional<int>(32));
    const AutoSizeStep result = step(input, history);

    QVERIFY(std::holds_alternative<RestoreAutomaticMax>(result));
    QCOMPARE(history.size(), 1);
    QCOMPARE(history.at(0).currentLength, 500);
    QCOMPARE(history.at(0).predictedLength, 1000); // 64/32 * 500
}

void AutoSizeEngineTest::step_growMidRangeAppliesSize()
{
    // toGrowLimit = 1500 - 1.2*1.0*500 = 900: candidates 40/48/56 project
    // 625/750/875 and fit, 64 projects 1000 and does not - the largest
    // fitting size applies
    History history;
    const AutoSizeInput input = makeInput(500.0, 1500.0, 500.0, 32, 64, 1.0, std::optional<int>(32));
    const AutoSizeStep result = step(input, history);

    const auto *applied = std::get_if<ApplySize>(&result);
    QVERIFY(applied);
    QCOMPARE(applied->iconSize, 56);
    QCOMPARE(history.size(), 1);
    QCOMPARE(history.at(0).currentLength, 500);
    // the shipped QML records the LAST candidate's projection (the 1000 of
    // the 64 that overflowed), not the applied size's 875 - preserved
    // as-is, the protector's match keys on these recorded values
    QCOMPARE(history.at(0).predictedLength, 1000);
}

void AutoSizeEngineTest::step_protectorBlocksRepeatingGrow()
{
    // the endless-loop protector across real passes: a grow, the shrink
    // that undoes it, then the identical grow again must be rejected and
    // must leave the history untouched; clearing the history re-arms it
    History history;

    const AutoSizeInput growInput = makeInput(500.0, 1500.0, 500.0, 32, 64, 1.0, std::optional<int>(32));
    QVERIFY(std::holds_alternative<ApplySize>(step(growInput, history))); // records (500, 875)

    // after applying 56 the row measures 875 and overflows a tighter
    // geometry: toShrinkLimit = 800 - 1.0*100 = 700, candidate 40 projects
    // 625 and fits - records (875, 625), a shrink undoing the grow
    const AutoSizeInput shrinkInput = makeInput(875.0, 800.0, 100.0, 56, 64, 1.0, std::optional<int>(56));
    const AutoSizeStep shrunk = step(shrinkInput, history);
    const auto *applied = std::get_if<ApplySize>(&shrunk);
    QVERIFY(applied);
    QCOMPARE(applied->iconSize, 40);
    QCOMPARE(history.size(), 2);

    // the same grow prediction (500, 875) two passes later: blocked
    QVERIFY(std::holds_alternative<KeepCurrent>(step(growInput, history)));
    QCOMPARE(history.size(), 2); // a rejected grow records nothing

    history.clear();
    QVERIFY(std::holds_alternative<ApplySize>(step(growInput, history))); // re-armed
}

void AutoSizeEngineTest::step_noFitGrowKeepsCurrentAndHistory()
{
    // grow branch entered (layout 500 < toGrowLimit 510) but even the first
    // candidate 40 projects 625 and exceeds the limit: keep current, and
    // the failed attempt records no prediction
    History history;
    const AutoSizeInput input = makeInput(500.0, 630.0, 100.0, 32, 64, 1.0, std::optional<int>(32));
    const AutoSizeStep result = step(input, history);

    QVERIFY(std::holds_alternative<KeepCurrent>(result));
    QCOMPARE(history.size(), 0);
}

void AutoSizeEngineTest::step_zeroItemLengthCollapsesTheBand()
{
    // itemLength 0 makes both limits equal maxLength (no zoom reserve to
    // subtract): under it grows, over it shrinks, exactly at it keeps -
    // the degenerate band is empty, not a trap. The grow stops at 56: the
    // ceiling candidate 64 projects exactly 1000, and the strict < keeps
    // a projection AT the limit out
    History history;

    const AutoSizeInput under = makeInput(500.0, 1000.0, 0.0, 32, 64, 1.6, std::optional<int>(32));
    const AutoSizeStep grown = step(under, history);
    const auto *applied = std::get_if<ApplySize>(&grown);
    QVERIFY(applied);
    QCOMPARE(applied->iconSize, 56);

    history.clear();
    const AutoSizeInput over = makeInput(1500.0, 1000.0, 0.0, 32, 64, 1.6, std::optional<int>(32));
    QVERIFY(std::holds_alternative<ApplySize>(step(over, history)));

    history.clear();
    const AutoSizeInput exact = makeInput(1000.0, 1000.0, 0.0, 32, 64, 1.6, std::optional<int>(32));
    QVERIFY(std::holds_alternative<KeepCurrent>(step(exact, history)));
}

void AutoSizeEngineTest::step_zeroLayoutLengthGrowsToCeiling()
{
    // an empty row projects 0 at every candidate, so a grow saturates at
    // the ceiling and restores automatic sizing instead of looping
    History history;
    const AutoSizeInput input = makeInput(0.0, 1000.0, 64.0, 32, 64, 1.6, std::optional<int>(32));
    QVERIFY(std::holds_alternative<RestoreAutomaticMax>(step(input, history)));
}

void AutoSizeEngineTest::step_growRightAfterBoundaryShrinkIsRejected()
{
    // the spec's asymmetric-limits case, driven as consecutive passes: a
    // shrink exits just under the shrink limit (1000 at icon 64 against
    // toShrinkLimit 897.6 applies 56, projecting 875), the re-measure sits
    // inside the grow window (875 < toGrowLimit 877.12) so the grow branch
    // ARMS, but the only candidate 64 projects 1000 again and does not
    // fit - the 1.2x margin leaves no room for a bounce-back grow at the
    // boundary
    History history;

    const AutoSizeInput overflow = makeInput(1000.0, 1000.0, 64.0, 64, 64, 1.6, std::nullopt);
    const AutoSizeStep shrunk = step(overflow, history);
    const auto *applied = std::get_if<ApplySize>(&shrunk);
    QVERIFY(applied);
    QCOMPARE(applied->iconSize, 56);

    const AutoSizeInput remeasured = makeInput(875.0, 1000.0, 64.0, 56, 64, 1.6, std::optional<int>(56));
    QVERIFY(std::holds_alternative<KeepCurrent>(step(remeasured, history)));
    QCOMPARE(history.size(), 1); // the rejected grow recorded nothing
}

QTEST_GUILESS_MAIN(AutoSizeEngineTest)

#include "autosizeenginetest.moc"
