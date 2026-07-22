/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// EX-04 AutoSizeEngine (docs/tracking/QML_EXTRACTION_PLAN.md): the automatic
// icon-size search as a pure step function. The ad9b823f incident was this
// feedback loop hanging the GUI thread at 100% CPU because inherited stepped
// equality exits passed their bounds. The same 8px search also left valid
// fitting sizes untested and made results depend on the configured ceiling's
// remainder modulo 8. The tables pin the replacement contract: calculate the
// largest fitting integer pixel size directly, with termination structural.

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
static_assert(projectLengthAtIconSize(32, 64, 1000.0) == 500.0,
              "fixed autosize projections must remain compile-time evaluable");

class AutoSizeEngineTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void shrink_largestFitTable();
    void grow_largestFitTable();
    void shrink_terminatesForEveryIconSize();
    void shrink_staysWithinBounds();
    void shrink_clampsDegenerateInputsToFloor();
    void grow_terminatesAndSaturatesForEveryIconSize();
    void grow_reportsWhenNoLargerPixelFits();
    void history_ringTruncatesToNewestEntries();
    void history_producesEndlessLoop_truthTable();
    void step_shrinksAndRecordsPrediction();
    void step_keepsCurrentInsideZoomedEndSlack();
    void step_growsOnlyFromOwnAppliedSize();
    void step_growToCeilingRestoresAutomatic();
    void step_growMidRangeAppliesSize();
    void step_protectorBlocksRepeatingGrow();
    void step_noFitGrowKeepsCurrentAndHistory();
    void step_zoomDisabledUsesExactLimit();
    void step_zeroLayoutLengthGrowsToCeiling();
    void step_growRightAfterBoundaryShrinkIsRejected();
    void step_shrinkChoosesSameLargestFitAcrossCeilings();
    void step_growUsesSpaceBelowTheNextEightPixelBucket();
    void step_hoverZoomDoesNotReduceRestingFit();
};

namespace {

//! a step() input around one measured layout snapshot; fields mirror the
//! QML shell's reads (see AutoSizeInput's comments)
AutoSizeInput makeInput(double layoutLength, double maxLength, int currentIconSize,
                        int maxIconSize, double zoomFactor,
                        std::optional<int> appliedIconSize)
{
    return AutoSizeInput{layoutLength, maxLength, currentIconSize,
                         maxIconSize, zoomFactor, appliedIconSize};
}

}

void AutoSizeEngineTest::shrink_largestFitTable()
{
    // maxIconSize | currentIconSize | layoutLength | toShrinkLimit
    // | largest fitting iconSize | projected length (12 decimals)
    struct Row { int max; int current; double layout, limit; int iconSize; double projected; };
    const Row table[] = {
        {64, 64, 1000.0, 900.0, 57, 890.625000000000},
        {64, 64, 1000.0, 500.0, 32, 500.000000000000},
        {64, 64, 1000.0, 100.0, 16, 250.000000000000},
        {64, 64, 1000.0, -1.0, 16, 250.000000000000},
        {64, 48, 900.0, 600.0, 32, 600.000000000000},
        {64, 32, 1000.0, 600.0, 19, 593.750000000000},
        {78, 78, 780.0, 400.0, 40, 400.000000000000},
        {78, 78, 780.0, -1.0, 16, 160.000000000000},
        {96, 64, 1200.0, 800.0, 42, 787.500000000000},
        // equality fits in the shrink contract
        {128, 128, 2000.0, 1500.0, 96, 1500.000000000000},
        {48, 48, 500.0, 450.0, 43, 447.916666666667},
        {24, 24, 300.0, 200.0, 16, 200.000000000000},
        {16, 16, 200.0, 100.0, 16, 200.000000000000},
        {30, 30, 433.7, 321.9, 22, 318.046666666667},
        // a ceiling below the floor clamps up to the floor
        {8, 8, 100.0, -1.0, 16, 200.000000000000},
        {64, 64, 0.0, -1.0, 16, 0.000000000000},
    };

    for (const auto &row : table) {
        const ShrinkResult got = shrinkToLargestFittingSize(row.max, row.current,
                                                            row.layout, row.limit);
        QVERIFY2(got.iconSize == row.iconSize && qAbs(got.projectedLength - row.projected) < 1e-9,
                 qPrintable(QStringLiteral("max=%1 current=%2 layout=%3 limit=%4: got (%5, %6) expected (%7, %8)")
                            .arg(row.max).arg(row.current).arg(row.layout).arg(row.limit)
                            .arg(got.iconSize).arg(got.projectedLength, 0, 'f', 12)
                            .arg(row.iconSize).arg(row.projected, 0, 'f', 12)));
    }
}

void AutoSizeEngineTest::grow_largestFitTable()
{
    // maxIconSize | currentIconSize | layoutLength | toGrowLimit
    // | largest fitting size (-1 = none) | its projected length
    struct Row { int max; int current; double layout, limit; int goodSize; double projected; };
    const Row table[] = {
        {64, 32, 500.0, 1e9, 64, 1000.000000000000},
        {64, 32, 500.0, 600.0, 38, 593.750000000000},
        {64, 32, 500.0, 510.0, -1, 0.0},
        {64, 32, 500.0, -1.0, -1, 0.0},
        {78, 78, 780.0, 1e9, 78, 780.000000000000},
        {128, 78, 900.0, 1000.0, 86, 992.307692307692},
        {96, 48, 700.0, 720.0, 49, 714.583333333333},
        {64, 56, 640.0, 700.0, 61, 697.142857142857},
        {24, 16, 200.0, 260.0, 20, 250.000000000000},
        {48, 40, 433.7, 470.25, 43, 466.227500000000},
        {16, 16, 100.0, 1e9, 16, 100.000000000000},
        {64, 64, 500.0, 1e9, 64, 500.000000000000},
    };

    for (const auto &row : table) {
        const std::optional<GrowResult> got = growToLargestFittingSize(row.max, row.current,
                                                                      row.layout, row.limit);
        const int gotGood = got ? got->iconSize : -1;
        const double gotProjection = got ? got->projectedLength : 0.0;
        QVERIFY2(gotGood == row.goodSize && qAbs(gotProjection - row.projected) < 1e-9,
                 qPrintable(QStringLiteral("max=%1 current=%2 layout=%3 limit=%4: got (%5, %6) expected (%7, %8)")
                            .arg(row.max).arg(row.current).arg(row.layout).arg(row.limit)
                            .arg(gotGood).arg(gotProjection, 0, 'f', 12)
                            .arg(row.goodSize).arg(row.projected, 0, 'f', 12)));
    }
}

void AutoSizeEngineTest::shrink_terminatesForEveryIconSize()
{
    // the ad9b823f named regression, driven through step(): a barely
    // positive maxLength (the smallest geometry the shell's <= 0 contract
    // lets through) cannot fit even the floor projection. Every icon
    // size from 16 to 256, including the live 78, must land exactly on the
    // 16px floor and return.
    for (int size = 16; size <= 256; ++size) {
        History history;
        const AutoSizeInput input = makeInput(10.0 * size, /*maxLength*/ 1.0,
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
    // must stay inside [16, maxIconSize - 1] (floor-clamped) for every
    // start size. Mirrors the retired tst_autosize.qml pin.
    for (int size = 17; size <= 256; ++size) {
        // current layout: 8 items at 'size' px; the limit allows roughly half
        const double limit = 4.0 * size;
        const ShrinkResult res = shrinkToLargestFittingSize(size, size, 8.0 * size, limit);

        QVERIFY2(res.iconSize >= minIconSize,
                 qPrintable(QStringLiteral("size %1: result %2 fell below the floor").arg(size).arg(res.iconSize)));
        QVERIFY2(res.iconSize <= qMax(minIconSize, size - 1),
                 qPrintable(QStringLiteral("size %1: a shrink pass must reduce by at least one pixel").arg(size)));

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
    const ShrinkResult belowFloor = shrinkToLargestFittingSize(8, 8, 100.0, -1.0);
    QCOMPARE(belowFloor.iconSize, minIconSize); // a max size below the floor clamps up

    const ShrinkResult zeroLayout = shrinkToLargestFittingSize(64, 64, 0.0, -1.0);
    QCOMPARE(zeroLayout.iconSize, minIconSize); // zero length, negative limit: floor exit
}

void AutoSizeEngineTest::grow_terminatesAndSaturatesForEveryIconSize()
{
    // an always-satisfied grow limit must climb to the ceiling and stop
    // there for every start size and the current == ceiling case
    for (int size = 16; size <= 256; ++size) {
        const std::optional<GrowResult> res = growToLargestFittingSize(256, size, 10.0, 1e18);
        QVERIFY2(res.has_value() && res->iconSize == 256,
                 qPrintable(QStringLiteral("size %1: an unbounded grow limit must saturate at maxIconSize").arg(size)));
    }

    const std::optional<GrowResult> atCeiling = growToLargestFittingSize(78, 78, 10.0, 1e18);
    QVERIFY(atCeiling.has_value());
    QCOMPARE(atCeiling->iconSize, 78);
}

void AutoSizeEngineTest::grow_reportsWhenNoLargerPixelFits()
{
    // when even current + 1 exceeds the limit the pass reports nothing
    for (int size = 16; size <= 256; ++size) {
        const std::optional<GrowResult> res = growToLargestFittingSize(256, size,
                                                                       10.0 * size, -1.0);
        QVERIFY2(!res.has_value(),
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
    // The settled 1000px row overflows a 997.6px budget. Size 63 projects
    // 984.375px and is the largest fitting integer below the 64px ceiling.
    History history;
    const AutoSizeInput input = makeInput(1000.0, 997.6, 64, 64, 1.6, std::nullopt);
    const AutoSizeStep result = step(input, history);

    const auto *applied = std::get_if<ApplySize>(&result);
    QVERIFY(applied);
    QCOMPARE(applied->iconSize, 63);
    QCOMPARE(history.size(), 1);
    QCOMPARE(history.at(0).currentLength, 1000);
    QCOMPARE(history.at(0).predictedLength, 984);
}

void AutoSizeEngineTest::step_keepsCurrentInsideZoomedEndSlack()
{
    // Zoom-enabled growth keeps only the 2px end slack. A settled 999px row
    // is below the 1000px shrink limit but above the strict 998px grow limit,
    // so it stays put without reserving a temporary hovered icon.
    History history;
    const AutoSizeInput input = makeInput(999.0, 1000.0, 64, 64, 1.6, std::optional<int>(64));
    const AutoSizeStep result = step(input, history);

    QVERIFY(std::holds_alternative<KeepCurrent>(result));
    QCOMPARE(history.size(), 0); // an idle pass records nothing
}

void AutoSizeEngineTest::step_growsOnlyFromOwnAppliedSize()
{
    // growable snapshot (layout 500 far under toGrowLimit 1998), but the
    // search never grows from a size it did not apply itself: automatic
    // sizing (absent) and a foreign size must both keep current
    History history;
    const AutoSizeInput automatic = makeInput(500.0, 2000.0, 32, 64, 1.6, std::nullopt);
    QVERIFY(std::holds_alternative<KeepCurrent>(step(automatic, history)));

    const AutoSizeInput foreign = makeInput(500.0, 2000.0, 32, 64, 1.6, std::optional<int>(48));
    QVERIFY(std::holds_alternative<KeepCurrent>(step(foreign, history)));

    QCOMPARE(history.size(), 0);
}

void AutoSizeEngineTest::step_growToCeilingRestoresAutomatic()
{
    // same snapshot grown from its own applied size saturates at the
    // ceiling: the constraint is gone, distinct from applying max as a size
    History history;
    const AutoSizeInput input = makeInput(500.0, 2000.0, 32, 64, 1.6, std::optional<int>(32));
    const AutoSizeStep result = step(input, history);

    QVERIFY(std::holds_alternative<RestoreAutomaticMax>(result));
    QCOMPARE(history.size(), 1);
    QCOMPARE(history.at(0).currentLength, 500);
    QCOMPARE(history.at(0).predictedLength, 1000); // 64/32 * 500
}

void AutoSizeEngineTest::step_growMidRangeAppliesSize()
{
    // With zoom disabled the 900px grow limit is exact. Size 57 projects
    // 890.625 and fits; size 58 projects 906.25 and does not.
    History history;
    const AutoSizeInput input = makeInput(500.0, 900.0, 32, 64, 1.0, std::optional<int>(32));
    const AutoSizeStep result = step(input, history);

    const auto *applied = std::get_if<ApplySize>(&result);
    QVERIFY(applied);
    QCOMPARE(applied->iconSize, 57);
    QCOMPARE(history.size(), 1);
    QCOMPARE(history.at(0).currentLength, 500);
    QCOMPARE(history.at(0).predictedLength, 891);
}

void AutoSizeEngineTest::step_protectorBlocksRepeatingGrow()
{
    // the endless-loop protector across real passes: a grow, the shrink
    // that undoes it, then the identical grow again must be rejected and
    // must leave the history untouched; clearing the history re-arms it
    History history;

    const AutoSizeInput growInput = makeInput(500.0, 900.0, 32, 64, 1.0, std::optional<int>(32));
    QVERIFY(std::holds_alternative<ApplySize>(step(growInput, history))); // records (500, 891)

    // after applying 57 the row measures 890.625 and overflows a tighter
    // geometry: the 800px limit selects size 51, which projects 796.875px.
    // This records (891, 797), a shrink undoing the grow.
    const AutoSizeInput shrinkInput = makeInput(890.625, 800.0, 57, 64,
                                                1.0, std::optional<int>(57));
    const AutoSizeStep shrunk = step(shrinkInput, history);
    const auto *applied = std::get_if<ApplySize>(&shrunk);
    QVERIFY(applied);
    QCOMPARE(applied->iconSize, 51);
    QCOMPARE(history.size(), 2);

    // the same grow prediction (500, 891) two passes later: blocked
    QVERIFY(std::holds_alternative<KeepCurrent>(step(growInput, history)));
    QCOMPARE(history.size(), 2); // a rejected grow records nothing

    history.clear();
    QVERIFY(std::holds_alternative<ApplySize>(step(growInput, history))); // re-armed
}

void AutoSizeEngineTest::step_noFitGrowKeepsCurrentAndHistory()
{
    // grow branch entered (layout 500 < toGrowLimit 510) but even the first
    // candidate 33 projects 515.625 and exceeds the limit: keep current, and
    // the failed attempt records no prediction
    History history;
    const AutoSizeInput input = makeInput(500.0, 510.0, 32, 64, 1.0, std::optional<int>(32));
    const AutoSizeStep result = step(input, history);

    QVERIFY(std::holds_alternative<KeepCurrent>(result));
    QCOMPARE(history.size(), 0);
}

void AutoSizeEngineTest::step_zoomDisabledUsesExactLimit()
{
    // Zoom factor 1 makes both limits equal maxLength: under it grows, over
    // it shrinks, exactly at it keeps. The grow stops at 63 because
    // the ceiling candidate 64 projects exactly 1000, and strict < keeps a
    // projection at the limit out.
    History history;

    const AutoSizeInput under = makeInput(500.0, 1000.0, 32, 64, 1.0, std::optional<int>(32));
    const AutoSizeStep grown = step(under, history);
    const auto *applied = std::get_if<ApplySize>(&grown);
    QVERIFY(applied);
    QCOMPARE(applied->iconSize, 63);

    history.clear();
    const AutoSizeInput over = makeInput(1500.0, 1000.0, 32, 64, 1.0, std::optional<int>(32));
    QVERIFY(std::holds_alternative<ApplySize>(step(over, history)));

    history.clear();
    const AutoSizeInput exact = makeInput(1000.0, 1000.0, 32, 64, 1.0, std::optional<int>(32));
    QVERIFY(std::holds_alternative<KeepCurrent>(step(exact, history)));
}

void AutoSizeEngineTest::step_zeroLayoutLengthGrowsToCeiling()
{
    // an empty row projects 0 at every candidate, so a grow saturates at
    // the ceiling and restores automatic sizing instead of looping
    History history;
    const AutoSizeInput input = makeInput(0.0, 1000.0, 32, 64, 1.6, std::optional<int>(32));
    QVERIFY(std::holds_alternative<RestoreAutomaticMax>(step(input, history)));
}

void AutoSizeEngineTest::step_growRightAfterBoundaryShrinkIsRejected()
{
    // A 1000px row at icon 64 shrinks against a 900px budget to size 57,
    // projecting 890.625px. The re-measure remains above the zoom-enabled
    // 898px grow limit, so the 2px end slack prevents an immediate bounce.
    History history;

    const AutoSizeInput overflow = makeInput(1000.0, 900.0, 64, 64, 1.6, std::nullopt);
    const AutoSizeStep shrunk = step(overflow, history);
    const auto *applied = std::get_if<ApplySize>(&shrunk);
    QVERIFY(applied);
    QCOMPARE(applied->iconSize, 57);

    const AutoSizeInput remeasured = makeInput(890.625, 900.0, 57, 64,
                                               1.6, std::optional<int>(57));
    QVERIFY(std::holds_alternative<KeepCurrent>(step(remeasured, history)));
    QCOMPARE(history.size(), 1); // the rejected grow recorded nothing
}

void AutoSizeEngineTest::step_shrinkChoosesSameLargestFitAcrossCeilings()
{
    // One applet row consumes 21.4px of primary-axis length per icon-size
    // pixel. With a 643px shrink limit, size 30 fits exactly at 642px and
    // size 31 does not. The chosen fit is a property of that geometry, not
    // of the configured ceiling's remainder modulo the retired 8px step.
    for (const int configuredCeiling : {31, 50, 64, 68, 127}) {
        History history;
        const double layoutLength = 21.4 * configuredCeiling;
        const AutoSizeInput input = makeInput(layoutLength,
                                              /*maxLength*/ 643.0,
                                              configuredCeiling,
                                              configuredCeiling,
                                              /*zoomFactor*/ 1.6,
                                              std::nullopt);
        const AutoSizeStep result = step(input, history);
        const auto *applied = std::get_if<ApplySize>(&result);

        QVERIFY2(applied,
                 qPrintable(QStringLiteral("ceiling %1: overflowing geometry must shrink")
                                    .arg(configuredCeiling)));
        QCOMPARE(applied->iconSize, 30);
    }
}

void AutoSizeEngineTest::step_growUsesSpaceBelowTheNextEightPixelBucket()
{
    // Live-shaped settled geometry: at size 44 the row is 965px long and
    // the zoom-enabled grow limit is 1226px after adding incremental hover
    // extent. Size 54 produces 1184.32px of resting row plus 32.4px of zoom
    // growth and fits; size 55 reaches 1239.25px total and does not. A search
    // that tests only eight-pixel jumps must not strand the row at 44.
    History history;
    const AutoSizeInput input = makeInput(/*layoutLength*/ 965.0,
                                          /*maxLength*/ 1228.0,
                                          /*currentIconSize*/ 44,
                                          /*maxIconSize*/ 68,
                                          /*zoomFactor*/ 1.6,
                                          std::optional<int>(44));
    const AutoSizeStep result = step(input, history);
    const auto *applied = std::get_if<ApplySize>(&result);

    QVERIFY(applied);
    QCOMPARE(applied->iconSize, 54);
}

void AutoSizeEngineTest::step_hoverZoomDoesNotReduceRestingFit()
{
    // Live-shaped bottom dock: the resting row is 1114px inside a 1228px
    // Maximum Length budget at size 50. Size 53 projects to 1180.84px at
    // rest, then its 42.4px incremental hover growth reaches 1223.24px.
    // Size 54 would reach 1246.32px. The temporary presentation must not
    // shrink a fitting resting row, and growth must leave only the 2px end
    // slack after accounting for zoom.
    History history;
    const AutoSizeInput input = makeInput(/*layoutLength*/ 1114.0,
                                          /*maxLength*/ 1228.0,
                                          /*currentIconSize*/ 50,
                                          /*maxIconSize*/ 114,
                                          /*zoomFactor*/ 1.8,
                                          /*appliedIconSize*/ 50);
    const AutoSizeStep result = step(input, history);

    const auto *const applied = std::get_if<ApplySize>(&result);
    QVERIFY(applied);
    QCOMPARE(applied->iconSize, 53);
    QCOMPARE(history.size(), 1);
    QCOMPARE(history.at(0).currentLength, 1114);
    QCOMPARE(history.at(0).predictedLength, 1181);
}

QTEST_GUILESS_MAIN(AutoSizeEngineTest)

#include "autosizeenginetest.moc"
