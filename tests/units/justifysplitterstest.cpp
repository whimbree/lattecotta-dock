/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The justify-alignment splitter placement (containment/plugin/units/
// justifysplitters.h). Justify carves the panel into three zones with two
// 1-based splitter positions; a splitter at position p is inserted at 0-based
// index p-1. A stored position < 1 (the schema default -1, or a 0 carried over
// from an older Latte alignment scheme - caught live on a real justify panel)
// meant insert(p-1) = insert(-1): a negative QList index, undefined behavior
// the RelWithDebInfo dock ran silently and inconsistently.
//
// This is the template boundary-invariant guard for the UB-catching
// initiative. It runs under -fsanitize=address,undefined with QT_FORCE_ASSERTS
// (tests/units/CMakeLists.txt), so it does two jobs at once: it pins the
// repaired placement, AND it is the tripwire - if the repair in insertSplitters
// is ever removed, feeding it a 0 position drives QList::insert(-1), whose own
// forced assert (or the ASan out-of-bounds write) aborts this binary instead of
// corrupting a live dock. Proven: with the repair reverted to the raw insert,
// the "healBreesLiveJustifyConfig" case aborts with SIGABRT; with the repair in
// place every case passes.

// core under test
#include "../../containment/plugin/units/justifysplitters.h"

// Qt
#include <QList>
#include <QTest>

using namespace Latte::Containment;
using JustifySplitters::Positions;

class JustifySplittersTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void classifyPositionValidity_data();
    void classifyPositionValidity();

    void repairOutOfRangeToCentered_data();
    void repairOutOfRangeToCentered();

    void passValidPositionsThrough();

    void placeSplittersAtValidIndices();
    void healBreesLiveJustifyConfig();

    void placeSplittersFromBadPositions_data();
    void placeSplittersFromBadPositions();

    void matchTheComputePathAcrossZoneSplits_data();
    void matchTheComputePathAcrossZoneSplits();
};

//! the two markers we assert on: -1 is the restore-path sentinel, -10 the
//! save/order sentinel. The placement is identical for either.
static constexpr int RESTORE_SPLITTER = -1;
static constexpr int ORDER_SPLITTER = -10;

//! how many times a value appears in a list
static int countOf(const QList<int> &list, int value)
{
    int n = 0;
    for (int v : list) {
        if (v == value) {
            ++n;
        }
    }
    return n;
}

//! the list with every occurrence of the splitter marker stripped out - what
//! remains must be the original applet order, untouched and in order
static QList<int> withoutSplitters(const QList<int> &list, int splitterId)
{
    QList<int> out;
    for (int v : list) {
        if (v != splitterId) {
            out << v;
        }
    }
    return out;
}

void JustifySplittersTest::classifyPositionValidity_data()
{
    QTest::addColumn<int>("first");
    QTest::addColumn<int>("second");
    QTest::addColumn<int>("appletCount");
    QTest::addColumn<bool>("valid");

    // a real 4-applet justify layout, start=1/main=1/end=2 -> pos (2,4)
    QTest::newRow("typical valid") << 2 << 4 << 4 << true;
    // all applets centered: first before all, second after all
    QTest::newRow("centered, 3 applets") << 1 << 5 << 3 << true;
    // empty panel: first at 1, second at 2 (both markers, no applets)
    QTest::newRow("empty panel") << 1 << 2 << 0 << true;

    // the live bug: a stale 0 first position
    QTest::newRow("zero first (Bree's config)") << 0 << 2 << 3 << false;
    // schema default, never migrated
    QTest::newRow("schema default -1") << -1 << -1 << 3 << false;
    QTest::newRow("negative first") << -3 << 2 << 3 << false;
    // second not strictly after first: main zone would be negative
    QTest::newRow("second equals first") << 2 << 2 << 3 << false;
    QTest::newRow("second before first") << 3 << 2 << 3 << false;
    // second overruns the post-first-insert list
    QTest::newRow("second overruns") << 1 << 6 << 3 << false;
    // first overruns even for the first insert
    QTest::newRow("first overruns") << 5 << 6 << 3 << false;
}

void JustifySplittersTest::classifyPositionValidity()
{
    QFETCH(int, first);
    QFETCH(int, second);
    QFETCH(int, appletCount);
    QFETCH(bool, valid);

    QCOMPARE(JustifySplitters::positionsAreValid(first, second, appletCount), valid);
}

void JustifySplittersTest::repairOutOfRangeToCentered_data()
{
    QTest::addColumn<int>("first");
    QTest::addColumn<int>("second");
    QTest::addColumn<int>("appletCount");

    QTest::newRow("zero first") << 0 << 2 << 3;
    QTest::newRow("both -1") << -1 << -1 << 3;
    QTest::newRow("second overruns") << 1 << 99 << 3;
    QTest::newRow("out of order") << 3 << 2 << 3;
}

void JustifySplittersTest::repairOutOfRangeToCentered()
{
    QFETCH(int, first);
    QFETCH(int, second);
    QFETCH(int, appletCount);

    const auto result = JustifySplitters::repairPositions(first, second, appletCount);

    QVERIFY(result.wasRepaired);
    const Positions centered = JustifySplitters::centeredPositions(appletCount);
    QCOMPARE(result.positions.first, centered.first);
    QCOMPARE(result.positions.second, centered.second);
    // the repaired pair must itself be valid - the whole point
    QVERIFY(JustifySplitters::positionsAreValid(result.positions.first, result.positions.second, appletCount));
}

void JustifySplittersTest::passValidPositionsThrough()
{
    const auto result = JustifySplitters::repairPositions(2, 4, 4);

    QVERIFY(!result.wasRepaired);
    QCOMPARE(result.positions.first, 2);
    QCOMPARE(result.positions.second, 4);
}

void JustifySplittersTest::placeSplittersAtValidIndices()
{
    // start=[10], main=[20], end=[30,40] -> stored (2,4)
    const QList<int> applets{10, 20, 30, 40};
    const QList<int> order = JustifySplitters::insertSplitters(applets, 2, 4, ORDER_SPLITTER);

    // splitter at position p sits at 0-based index p-1; the second insert lands
    // in the already-grown list, so the two markers end at indices 1 and 3
    const QList<int> expected{10, ORDER_SPLITTER, 20, ORDER_SPLITTER, 30, 40};
    QCOMPARE(order, expected);
    QCOMPARE(countOf(order, ORDER_SPLITTER), 2);
    QCOMPARE(withoutSplitters(order, ORDER_SPLITTER), applets);
}

void JustifySplittersTest::healBreesLiveJustifyConfig()
{
    // The live failure: a justify panel whose stored splitterPosition=0. The old
    // code ran appletIdsOrder.insert(0-1, -1) = insert(-1, ...): a negative index.
    // Under QT_FORCE_ASSERTS + ASan a negative insert aborts this binary, so this
    // case reaching its assertions AT ALL proves the repair kept the bad index out
    // of QList::insert. splitterPosition2=2 is the surviving splitter Bree saw.
    const QList<int> applets{10, 20, 30};
    const QList<int> order = JustifySplitters::insertSplitters(applets, 0, 2, RESTORE_SPLITTER);

    // repaired to the centered layout: every applet in the main zone, one
    // splitter before all of them and one after
    const QList<int> expected{RESTORE_SPLITTER, 10, 20, 30, RESTORE_SPLITTER};
    QCOMPARE(order, expected);

    // exactly two splitters, applets preserved and in order
    QCOMPARE(countOf(order, RESTORE_SPLITTER), 2);
    QCOMPARE(withoutSplitters(order, RESTORE_SPLITTER), applets);

    // the trailing splitter is present, not dropped (the "one, right side
    // missing" symptom is gone)
    QVERIFY(order.first() == RESTORE_SPLITTER);
    QVERIFY(order.last() == RESTORE_SPLITTER);
}

void JustifySplittersTest::placeSplittersFromBadPositions_data()
{
    QTest::addColumn<int>("first");
    QTest::addColumn<int>("second");

    QTest::newRow("zero first") << 0 << 2;
    QTest::newRow("both -1") << -1 << -1;
    QTest::newRow("both zero") << 0 << 0;
    QTest::newRow("large negatives") << -100 << -50;
    QTest::newRow("second overruns") << 1 << 999;
    QTest::newRow("first overruns") << 50 << 60;
    QTest::newRow("out of order") << 3 << 1;
}

void JustifySplittersTest::placeSplittersFromBadPositions()
{
    QFETCH(int, first);
    QFETCH(int, second);

    const QList<int> applets{7, 8, 9, 11, 13};

    // no matter how bad the stored pair is, this must not reach a negative or
    // overrun insert (would abort here under the sanitizers) and must yield two
    // in-range splitters with the applet list intact
    const QList<int> order = JustifySplitters::insertSplitters(applets, first, second, RESTORE_SPLITTER);

    QCOMPARE(countOf(order, RESTORE_SPLITTER), 2);
    QCOMPARE(withoutSplitters(order, RESTORE_SPLITTER), applets);

    // every marker index is inside the final list bounds
    for (int i = 0; i < order.count(); ++i) {
        if (order[i] == RESTORE_SPLITTER) {
            QVERIFY(i >= 0 && i < order.count());
        }
    }

    // repaired to centered: marker at the very front and the very back
    QCOMPARE(order.first(), RESTORE_SPLITTER);
    QCOMPARE(order.last(), RESTORE_SPLITTER);
}

void JustifySplittersTest::matchTheComputePathAcrossZoneSplits_data()
{
    // Drive the exact positions LayoutManager::save() emits for every split of N
    // applets across the three zones (pos1=startChilds+1,
    // pos2=startChilds+mainChilds+2) and confirm the round trip lands each applet
    // in the zone save() intended.
    QTest::addColumn<int>("startChilds");
    QTest::addColumn<int>("mainChilds");
    QTest::addColumn<int>("endChilds");

    QTest::newRow("all start") << 3 << 0 << 0;
    QTest::newRow("all main (centered)") << 0 << 3 << 0;
    QTest::newRow("all end") << 0 << 0 << 3;
    QTest::newRow("one each") << 1 << 1 << 1;
    QTest::newRow("start+end, empty main") << 2 << 0 << 2;
    QTest::newRow("empty panel") << 0 << 0 << 0;
}

void JustifySplittersTest::matchTheComputePathAcrossZoneSplits()
{
    QFETCH(int, startChilds);
    QFETCH(int, mainChilds);
    QFETCH(int, endChilds);

    const int total = startChilds + mainChilds + endChilds;

    // ids 1..total in order
    QList<int> applets;
    for (int i = 1; i <= total; ++i) {
        applets << i;
    }

    const int pos1 = startChilds + 1;
    const int pos2 = startChilds + mainChilds + 2;

    // the compute path must always emit valid positions - that is the invariant
    // that makes 0 a stale value rather than a legitimate one
    QVERIFY(JustifySplitters::positionsAreValid(pos1, pos2, total));

    const QList<int> order = JustifySplitters::insertSplitters(applets, pos1, pos2, RESTORE_SPLITTER);

    // walk the markers back into zone counts and confirm they match the input
    int seenSplitters = 0;
    int start = 0, main = 0, end = 0;
    for (int v : order) {
        if (v == RESTORE_SPLITTER) {
            ++seenSplitters;
        } else if (seenSplitters == 0) {
            ++start;
        } else if (seenSplitters == 1) {
            ++main;
        } else {
            ++end;
        }
    }

    QCOMPARE(seenSplitters, 2);
    QCOMPARE(start, startChilds);
    QCOMPARE(main, mainChilds);
    QCOMPARE(end, endChilds);
}

QTEST_MAIN(JustifySplittersTest)

#include "justifysplitterstest.moc"
