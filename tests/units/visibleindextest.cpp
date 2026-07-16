/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Behavioral tests for the EX-06 VisibleIndexEngine core
//! (declarativeimports/core/units/visibleindex.h).
//!
//! GENERATION METHOD (docs/TESTING.md): every vector marked with a CASE
//! name was recorded by driving the SHIPPED QML twins offscreen - the
//! real containment abilities/Indexer.qml + IndexerPrivate.qml and the
//! real client abilities/client/Indexer.qml wired through a mock bridge,
//! plus verbatim copies of the AppletItem/BasicItem walk bodies - via
//!     scripts/qml-interaction-tests.sh tests/generators/visibleindex
//! at b0e606b2 (harness + raw output notes in that directory's README).
//! Slots without a CASE name are derived from the Qt5 bodies at f0ad7b23
//! (identical to the port's, modulo restoreMode) or pin a documented
//! deviation; each says which.
//!
//! The generator world, reused all over: containment row
//!   startLayout 0 1 2 | mainLayout 3 4(client) | endLayout 5 6
//! with the client twin's tasks row indexed from 0.

#include "../../declarativeimports/core/units/visibleindex.h"

#include <QtTest>

using namespace Latte;
using namespace Latte::VisibleIndex;

namespace {

RowEntry plain(int index)
{
    return {.index = index};
}

RowEntry separator(int index)
{
    return {.index = index, .isSeparator = true};
}

RowEntry hiddenItem(int index)
{
    return {.index = index, .isHidden = true};
}

RowEntry hiddenSeparator(int index)
{
    return {.index = index, .isSeparator = true, .isHidden = true};
}

RowEntry marginsSeparator(int index)
{
    return {.index = index, .isMarginsSeparator = true};
}

RowEntry client(int index, int subItemCount)
{
    return {.index = index, .subItemCount = subItemCount};
}

//! CASE plainRow's containment world with a parameterized client
RowEntries containmentRow(int clientSubItems)
{
    return {plain(0), plain(1), plain(2), plain(3), client(4, clientSubItems), plain(5), plain(6)};
}

} // namespace

class VisibleIndexTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void isWellFormedRow_flagsCollectorContractViolations();

    void countVisibleItemsBefore_expandsSubItemsAndSkipsInvisible();
    void visibleIndexOf_plainRow_matchesShippedTwins();
    void visibleIndexOf_separatorRuns_headMiddleTail();
    void visibleIndexOf_hiddenInterleavedWithSeparators();
    void visibleIndexOf_marginsSeparator_ownsAnIndexYetSkipsInCounting();
    void visibleIndexOf_missingIndex_ownsNothing();

    void clientVisibleIndexOf_composesHostBase();
    void clientVisibleIndexOf_hiddenHostBase_staysShippedFaithful();

    void belongsAt_multiItemApplet_spansItsSubItems();
    void belongsAt_emptyMultiItemApplet_matchesExactBaseOnly();
    void belongsAt_hiddenMultiItemApplet_ownsNothing();
    void belongsAt_negativeVisibleIndex_matchesNothing();

    void firstLastVisibleIndex_boundsSkipInvisible();
    void countVisibleItems_countsEntriesNotSubItems();

    void edgeItemIsSeparator_flagsVisibleSeparatorsOutsideBounds();
    void edgeItemIsSeparator_separatorOnlyRow_keepsQt5Asymmetry();
    void edgeItemIsSeparator_hiddenSeparatorDoesNotCount();

    void hiddenSkippingNeighbor_walksOverHiddenRuns();
    void hiddenSkippingNeighbor_gapsAndEdgesResolveToNothing();
    void neighborIsVisibleSeparator_matchesAppletItemWalks();
    void neighborIsVisibleSeparator_separatorOrMissingSelf_asksNothing();
    void neighborIsSeparator_hiddenSeparatorStopsWalkAndCounts();

    void isInMarginsArea_parityFamily();

    void assignedLayoutIndex_ranksOnlyCountedChildren();

    void twinCoherence_clientAnswersLandInsideTheHostApplet();

    void emptyRow_everyAnswerIsNone();
};

void VisibleIndexTest::isWellFormedRow_flagsCollectorContractViolations()
{
    QVERIFY(isWellFormedRow({}));
    QVERIFY(isWellFormedRow(containmentRow(3)));
    //! the two collector-contract violations (rowentry.h): unindexed
    //! entries and negative sub-item counts
    QVERIFY(!isWellFormedRow({plain(-1)}));
    QVERIFY(!isWellFormedRow({client(0, -1)}));
}

void VisibleIndexTest::countVisibleItemsBefore_expandsSubItemsAndSkipsInvisible()
{
    //! CASE plainRow: entries before 5 are four plain items + the
    //! 3-task client -> 7 (shipped visibleIndex(5) was 8 = 7 + 1)
    QCOMPARE(countVisibleItemsBefore(containmentRow(3), 5), 7);

    //! all invisible kinds skipped: separator, hidden, margins separator
    const RowEntries mixed{plain(0), separator(1), hiddenItem(2), marginsSeparator(3), plain(4)};
    QCOMPARE(countVisibleItemsBefore(mixed, 5), 2);

    //! nothing sits before the row's first index
    QCOMPARE(countVisibleItemsBefore(mixed, 0), 0);
}

void VisibleIndexTest::visibleIndexOf_plainRow_matchesShippedTwins()
{
    //! CASE plainRow vis: {0:1, 1:2, 2:3, 3:4, 4:5, 5:8, 6:9}
    const RowEntries row = containmentRow(3);
    QCOMPARE(visibleIndexOf(row, 0), std::optional<int>(1));
    QCOMPARE(visibleIndexOf(row, 1), std::optional<int>(2));
    QCOMPARE(visibleIndexOf(row, 2), std::optional<int>(3));
    QCOMPARE(visibleIndexOf(row, 3), std::optional<int>(4));
    QCOMPARE(visibleIndexOf(row, 4), std::optional<int>(5));
    QCOMPARE(visibleIndexOf(row, 5), std::optional<int>(8));
    QCOMPARE(visibleIndexOf(row, 6), std::optional<int>(9));
}

void VisibleIndexTest::visibleIndexOf_separatorRuns_headMiddleTail()
{
    //! CASE separatorsHeadMiddleTail vis: {0:-1, 1:1, 2:2, 3:-1, 4:3, 5:6, 6:-1}
    const RowEntries row{separator(0), plain(1), plain(2), separator(3), client(4, 3), plain(5), separator(6)};
    QCOMPARE(visibleIndexOf(row, 0), std::nullopt);
    QCOMPARE(visibleIndexOf(row, 1), std::optional<int>(1));
    QCOMPARE(visibleIndexOf(row, 2), std::optional<int>(2));
    QCOMPARE(visibleIndexOf(row, 3), std::nullopt);
    QCOMPARE(visibleIndexOf(row, 4), std::optional<int>(3));
    QCOMPARE(visibleIndexOf(row, 5), std::optional<int>(6));
    QCOMPARE(visibleIndexOf(row, 6), std::nullopt);
}

void VisibleIndexTest::visibleIndexOf_hiddenInterleavedWithSeparators()
{
    //! CASE hiddenInterleaved vis: {0:1, 1:-1, 2:-1, 3:2, 4:3, 5:-1, 6:6}
    const RowEntries row{plain(0), hiddenItem(1), separator(2), plain(3), client(4, 3), hiddenItem(5), plain(6)};
    QCOMPARE(visibleIndexOf(row, 0), std::optional<int>(1));
    QCOMPARE(visibleIndexOf(row, 1), std::nullopt);
    QCOMPARE(visibleIndexOf(row, 2), std::nullopt);
    QCOMPARE(visibleIndexOf(row, 3), std::optional<int>(2));
    QCOMPARE(visibleIndexOf(row, 4), std::optional<int>(3));
    QCOMPARE(visibleIndexOf(row, 5), std::nullopt);
    QCOMPARE(visibleIndexOf(row, 6), std::optional<int>(6));
}

void VisibleIndexTest::visibleIndexOf_marginsSeparator_ownsAnIndexYetSkipsInCounting()
{
    //! CASE marginsOdd vis: {3:4, 4:4} - the Qt5 asymmetry: the margins
    //! separator at 3 OWNS visible index 4 (visibleIndex vetoes only
    //! separators/hidden) while being skipped when counting others, so
    //! the next visible entry gets the SAME index. Faithful, pinned.
    const RowEntries row{plain(0), plain(1), plain(2), marginsSeparator(3), client(4, 3), plain(5), plain(6)};
    QCOMPARE(visibleIndexOf(row, 3), std::optional<int>(4));
    QCOMPARE(visibleIndexOf(row, 4), std::optional<int>(4));
    QCOMPARE(visibleIndexOf(row, 5), std::optional<int>(7));
}

void VisibleIndexTest::visibleIndexOf_missingIndex_ownsNothing()
{
    //! DELIBERATE DEVIATION from Qt5 (documented in visibleindex.h):
    //! Qt5 answered 1 + countBefore for an index no entry has - e.g. 8
    //! here would have answered 8, a phantom badge slot for an index -1
    //! splitter probe or a transient re-index window. The core answers
    //! "none".
    const RowEntries row = containmentRow(3);
    QCOMPARE(visibleIndexOf(row, 8), std::nullopt);
    QCOMPARE(visibleIndexOf(row, -1), std::nullopt);

    //! CASE gapWalk vis: {100:-1, 101:8} - a real between-layouts gap
    //! world; the gap index answers nothing, the real entries unchanged
    const RowEntries gapRow{plain(0), plain(1), plain(2), plain(3), client(4, 3), hiddenItem(100), plain(101)};
    QCOMPARE(visibleIndexOf(gapRow, 99), std::nullopt);
    QCOMPARE(visibleIndexOf(gapRow, 100), std::nullopt);
    QCOMPARE(visibleIndexOf(gapRow, 101), std::optional<int>(8));
}

void VisibleIndexTest::clientVisibleIndexOf_composesHostBase()
{
    //! CASE plainRow clientVis: {0:5, 1:6, 2:7} with host base 5
    const RowEntries tasks{plain(0), plain(1), plain(2)};
    QCOMPARE(clientVisibleIndexOf(tasks, 0, 5), std::optional<int>(5));
    QCOMPARE(clientVisibleIndexOf(tasks, 1, 5), std::optional<int>(6));
    QCOMPARE(clientVisibleIndexOf(tasks, 2, 5), std::optional<int>(7));

    //! CASE clientMixed clientVis: {0:5, 1:-1, 2:6, 3:7, 4:-1}
    const RowEntries mixed{plain(0), separator(1), plain(2), plain(3), hiddenItem(4)};
    QCOMPARE(clientVisibleIndexOf(mixed, 0, 5), std::optional<int>(5));
    QCOMPARE(clientVisibleIndexOf(mixed, 1, 5), std::nullopt);
    QCOMPARE(clientVisibleIndexOf(mixed, 2, 5), std::optional<int>(6));
    QCOMPARE(clientVisibleIndexOf(mixed, 3, 5), std::optional<int>(7));
    QCOMPARE(clientVisibleIndexOf(mixed, 4, 5), std::nullopt);
}

void VisibleIndexTest::clientVisibleIndexOf_hiddenHostBase_staysShippedFaithful()
{
    //! CASE hiddenClient clientVis: {0:-1, 1:0, 2:1} - the shipped twin
    //! feeds the host's -1 straight into the sum when the applet is
    //! hidden; faithful (the deviation lives in belongsAt, not here)
    const RowEntries tasks{plain(0), plain(1), plain(2)};
    QCOMPARE(clientVisibleIndexOf(tasks, 0, -1), std::optional<int>(-1));
    QCOMPARE(clientVisibleIndexOf(tasks, 1, -1), std::optional<int>(0));
    QCOMPARE(clientVisibleIndexOf(tasks, 2, -1), std::optional<int>(1));
}

void VisibleIndexTest::belongsAt_multiItemApplet_spansItsSubItems()
{
    //! CASE plainRow belongs[4]: true exactly at visible indexes 5,6,7
    const RowEntries row = containmentRow(3);
    QCOMPARE(belongsAtVisibleIndex(row, 4, 4), false);
    QCOMPARE(belongsAtVisibleIndex(row, 4, 5), true);
    QCOMPARE(belongsAtVisibleIndex(row, 4, 6), true);
    QCOMPARE(belongsAtVisibleIndex(row, 4, 7), true);
    QCOMPARE(belongsAtVisibleIndex(row, 4, 8), false);

    //! a plain entry matches only its own index (belongs[5]: true at 8)
    QCOMPARE(belongsAtVisibleIndex(row, 5, 8), true);
    QCOMPARE(belongsAtVisibleIndex(row, 5, 7), false);
    QCOMPARE(belongsAtVisibleIndex(row, 5, 9), false);
}

void VisibleIndexTest::belongsAt_emptyMultiItemApplet_matchesExactBaseOnly()
{
    //! CASE clientEmpty: the 0-task client at 4 and the next entry BOTH
    //! answered visible index 5 (belongs[4][5] and belongs[5][5] true) -
    //! Qt5's exact-match branch fires before the empty range; faithful,
    //! iteration order decides the badge and that stays in QML
    const RowEntries row = containmentRow(0);
    QCOMPARE(belongsAtVisibleIndex(row, 4, 5), true);
    QCOMPARE(belongsAtVisibleIndex(row, 5, 5), true);
    QCOMPARE(belongsAtVisibleIndex(row, 4, 6), false);
    QCOMPARE(belongsAtVisibleIndex(row, 4, 4), false);
}

void VisibleIndexTest::belongsAt_hiddenMultiItemApplet_ownsNothing()
{
    //! DELIBERATE DEVIATION, defect fixed at origin: CASE hiddenClient
    //! recorded the shipped QML answering true at visible indexes 0 and
    //! 1 for a HIDDEN 3-task client (Qt5's -1 base fell into the range
    //! branch: [-1+0, -1+3) clipped to >= 0), letting an invisible
    //! applet steal shortcut activation. The optional base makes that
    //! unrepresentable: an entry owning no visible index matches nothing.
    RowEntries row = containmentRow(3);
    row[4].isHidden = true;

    for (int visible = 0; visible <= 9; ++visible) {
        QCOMPARE(belongsAtVisibleIndex(row, 4, visible), false);
    }

    //! separators own nothing either (belongs[0] all-false in CASE
    //! separatorsHeadMiddleTail)
    const RowEntries seps{separator(0), plain(1)};
    QCOMPARE(belongsAtVisibleIndex(seps, 0, 1), false);
}

void VisibleIndexTest::belongsAt_negativeVisibleIndex_matchesNothing()
{
    //! the Qt5 guard: itemVisibleIndex < 0 never matches, even though a
    //! hidden host's client arithmetic can produce -1 upstream
    const RowEntries row = containmentRow(3);
    QCOMPARE(belongsAtVisibleIndex(row, 4, -1), false);
    QCOMPARE(belongsAtVisibleIndex(row, 0, -1), false);
}

void VisibleIndexTest::firstLastVisibleIndex_boundsSkipInvisible()
{
    //! CASE clientMixed client bounds: firstVisible 0, lastVisible 3
    const RowEntries mixed{plain(0), separator(1), plain(2), plain(3), hiddenItem(4)};
    QCOMPARE(firstVisibleIndex(mixed), std::optional<int>(0));
    QCOMPARE(lastVisibleIndex(mixed), std::optional<int>(3));

    //! CASE clientEdgeSeparators bounds: separators at both edges -> 1/1
    const RowEntries edges{separator(0), plain(1), separator(2)};
    QCOMPARE(firstVisibleIndex(edges), std::optional<int>(1));
    QCOMPARE(lastVisibleIndex(edges), std::optional<int>(1));

    //! CASE separatorOnlyClient bounds: -1/-1 at the QML boundary
    const RowEntries seponly{separator(0)};
    QCOMPARE(firstVisibleIndex(seponly), std::nullopt);
    QCOMPARE(lastVisibleIndex(seponly), std::nullopt);
}

void VisibleIndexTest::countVisibleItems_countsEntriesNotSubItems()
{
    //! CASE clientMixed visibleItemsCount: 3 of 5 (separator + hidden out)
    const RowEntries mixed{plain(0), separator(1), plain(2), plain(3), hiddenItem(4)};
    QCOMPARE(countVisibleItems(mixed), 3);

    //! CASE hiddenSeparatorTask visibleItemsCount: 2 (hidden separator out)
    const RowEntries hiddensep{plain(0), hiddenSeparator(1), plain(2)};
    QCOMPARE(countVisibleItems(hiddensep), 2);

    //! a multi-item entry is ONE visible entry here - the client twin
    //! counts entries, expansion belongs to countVisibleItemsBefore
    QCOMPARE(countVisibleItems({client(0, 5)}), 1);
}

void VisibleIndexTest::edgeItemIsSeparator_flagsVisibleSeparatorsOutsideBounds()
{
    //! CASE clientEdgeSeparators: firstTail true, lastHead true
    const RowEntries edges{separator(0), plain(1), separator(2)};
    QCOMPARE(edgeItemIsSeparator(edges, Direction::Tail), true);
    QCOMPARE(edgeItemIsSeparator(edges, Direction::Head), true);

    //! CASE plainRow: all visible -> false/false
    const RowEntries plainTasks{plain(0), plain(1), plain(2)};
    QCOMPARE(edgeItemIsSeparator(plainTasks, Direction::Tail), false);
    QCOMPARE(edgeItemIsSeparator(plainTasks, Direction::Head), false);

    //! an inner separator is not an edge separator
    const RowEntries inner{plain(0), separator(1), plain(2)};
    QCOMPARE(edgeItemIsSeparator(inner, Direction::Tail), false);
    QCOMPARE(edgeItemIsSeparator(inner, Direction::Head), false);
}

void VisibleIndexTest::edgeItemIsSeparator_separatorOnlyRow_keepsQt5Asymmetry()
{
    //! CASE separatorOnlyClient: firstTail FALSE, lastHead TRUE. With no
    //! visible items both bounds collapse to -1: the tail scan covers
    //! nothing, the head scan covers the whole row. Qt5-faithful.
    const RowEntries seponly{separator(0)};
    QCOMPARE(edgeItemIsSeparator(seponly, Direction::Tail), false);
    QCOMPARE(edgeItemIsSeparator(seponly, Direction::Head), true);
}

void VisibleIndexTest::edgeItemIsSeparator_hiddenSeparatorDoesNotCount()
{
    //! derived from the Qt5 body (separators.contains && !hidden.contains)
    const RowEntries row{hiddenSeparator(0), plain(1), hiddenSeparator(2)};
    QCOMPARE(edgeItemIsSeparator(row, Direction::Tail), false);
    QCOMPARE(edgeItemIsSeparator(row, Direction::Head), false);
}

void VisibleIndexTest::hiddenSkippingNeighbor_walksOverHiddenRuns()
{
    //! CASE hiddenInterleaved world: 0 plain, 1 hidden, 2 sep, 3 plain
    const RowEntries row{plain(0), hiddenItem(1), separator(2), plain(3), client(4, 3), hiddenItem(5), plain(6)};

    //! head from 0 walks over hidden 1 and lands on separator 2
    const auto headOfZero = hiddenSkippingNeighbor(row, 0, Direction::Head);
    QVERIFY(headOfZero.has_value());
    QCOMPARE(headOfZero->index, 2);
    QCOMPARE(headOfZero->isSeparator, true);

    //! tail from 6 walks over hidden 5 and lands on the client at 4 -
    //! the landing the QML shell hands to its live bridge delegation
    const auto tailOfSix = hiddenSkippingNeighbor(row, 6, Direction::Tail);
    QVERIFY(tailOfSix.has_value());
    QCOMPARE(tailOfSix->index, 4);
    QCOMPARE(tailOfSix->subItemCount, 3);
}

void VisibleIndexTest::hiddenSkippingNeighbor_gapsAndEdgesResolveToNothing()
{
    //! CASE gapWalk: tail from 101 steps onto hidden 100, then the gap
    //! at 99 - nothing there (apTail[101] was false)
    const RowEntries gapRow{plain(0), plain(1), plain(2), plain(3), client(4, 3), hiddenItem(100), plain(101)};
    QCOMPARE(hiddenSkippingNeighbor(gapRow, 101, Direction::Tail), std::nullopt);

    //! off both row edges
    QCOMPARE(hiddenSkippingNeighbor(gapRow, 0, Direction::Tail), std::nullopt);
    QCOMPARE(hiddenSkippingNeighbor(gapRow, 101, Direction::Head), std::nullopt);

    //! a hidden run reaching the tail edge walks off it
    const RowEntries hiddenHead{hiddenItem(0), hiddenItem(1), plain(2)};
    QCOMPARE(hiddenSkippingNeighbor(hiddenHead, 2, Direction::Tail), std::nullopt);
}

void VisibleIndexTest::neighborIsVisibleSeparator_matchesAppletItemWalks()
{
    //! CASE separatorsHeadMiddleTail apTail/apHead (non-delegated
    //! entries): apTail {1:true, 4:true}, apHead {2:true, 5:true}
    const RowEntries row{separator(0), plain(1), plain(2), separator(3), client(4, 3), plain(5), separator(6)};
    QCOMPARE(neighborIsVisibleSeparator(row, 1, Direction::Tail), true);
    QCOMPARE(neighborIsVisibleSeparator(row, 2, Direction::Tail), false);
    QCOMPARE(neighborIsVisibleSeparator(row, 4, Direction::Tail), true);
    QCOMPARE(neighborIsVisibleSeparator(row, 2, Direction::Head), true);
    QCOMPARE(neighborIsVisibleSeparator(row, 5, Direction::Head), true);
    QCOMPARE(neighborIsVisibleSeparator(row, 1, Direction::Head), false);

    //! CASE hiddenInterleaved: apTail[3] true through hidden runs is the
    //! walk landing on the separator at 2... the QML walked 3-1=2
    //! directly; from 4 the walk crosses plain 3 and stops (false)
    const RowEntries interleaved{plain(0), hiddenItem(1), separator(2), plain(3), client(4, 3), hiddenItem(5), plain(6)};
    QCOMPARE(neighborIsVisibleSeparator(interleaved, 3, Direction::Tail), true);
    QCOMPARE(neighborIsVisibleSeparator(interleaved, 4, Direction::Tail), false);
    //! apHead {0:true, 1:true}: both land on the separator at 2
    QCOMPARE(neighborIsVisibleSeparator(interleaved, 0, Direction::Head), true);
    QCOMPARE(neighborIsVisibleSeparator(interleaved, 1, Direction::Head), true);
}

void VisibleIndexTest::neighborIsVisibleSeparator_separatorOrMissingSelf_asksNothing()
{
    const RowEntries row{separator(0), plain(1), separator(2)};
    //! the Qt5 self guard: a separator never reports its own neighbors
    QCOMPARE(neighborIsVisibleSeparator(row, 0, Direction::Head), false);
    QCOMPARE(neighborIsVisibleSeparator(row, 2, Direction::Tail), false);
    //! missing self (transient index -1, gap) owns no neighbors
    QCOMPARE(neighborIsVisibleSeparator(row, -1, Direction::Head), false);
    QCOMPARE(neighborIsVisibleSeparator(row, 5, Direction::Tail), false);
}

void VisibleIndexTest::neighborIsSeparator_hiddenSeparatorStopsWalkAndCounts()
{
    //! CASE hiddenSeparatorTask basic: t2 {tail:true, tailVisible:false},
    //! t0 {head:true, headVisible:false} - the BasicItem rule where a
    //! settings-hidden separator still shapes its neighbors' spacing
    const RowEntries row{plain(0), hiddenSeparator(1), plain(2)};
    QCOMPARE(neighborIsSeparator(row, 2, Direction::Tail), true);
    QCOMPARE(neighborIsVisibleSeparator(row, 2, Direction::Tail), false);
    QCOMPARE(neighborIsSeparator(row, 0, Direction::Head), true);
    QCOMPARE(neighborIsVisibleSeparator(row, 0, Direction::Head), false);

    //! a hidden NON-separator is still walked over under both rules
    const RowEntries hiddenRun{plain(0), hiddenItem(1), separator(2), plain(3)};
    QCOMPARE(neighborIsSeparator(hiddenRun, 0, Direction::Head), true);
    QCOMPARE(neighborIsSeparator(hiddenRun, 3, Direction::Tail), true);

    //! edges and gaps answer false like the visible rule
    QCOMPARE(neighborIsSeparator(hiddenRun, 0, Direction::Tail), false);
    QCOMPARE(neighborIsSeparator(hiddenRun, 3, Direction::Head), false);
}

void VisibleIndexTest::isInMarginsArea_parityFamily()
{
    //! CASE marginsPair margins: {0:f, 1:f(self), 2:t, 3:t, 4:t, 5:f(self), 6:f}
    const RowEntries pair{plain(0), marginsSeparator(1), plain(2), plain(3), client(4, 3), marginsSeparator(5), plain(6)};
    QCOMPARE(isInMarginsArea(pair, 0), false);
    QCOMPARE(isInMarginsArea(pair, 1), false);
    QCOMPARE(isInMarginsArea(pair, 2), true);
    QCOMPARE(isInMarginsArea(pair, 3), true);
    QCOMPARE(isInMarginsArea(pair, 4), true);
    QCOMPARE(isInMarginsArea(pair, 5), false);
    QCOMPARE(isInMarginsArea(pair, 6), false);

    //! CASE marginsOdd margins: a single separator at 3 leaves everything
    //! after it inside: {4:t, 5:t, 6:t}
    const RowEntries odd{plain(0), plain(1), plain(2), marginsSeparator(3), client(4, 3), plain(5), plain(6)};
    QCOMPARE(isInMarginsArea(odd, 2), false);
    QCOMPARE(isInMarginsArea(odd, 3), false);
    QCOMPARE(isInMarginsArea(odd, 4), true);
    QCOMPARE(isInMarginsArea(odd, 6), true);

    //! no margins separators at all: nobody is inside (CASE plainRow)
    QCOMPARE(isInMarginsArea(containmentRow(3), 3), false);
}

void VisibleIndexTest::assignedLayoutIndex_ranksOnlyCountedChildren()
{
    //! the fe63a63e checkIndex semantics: edge spacers / internal view
    //! splitters (false flags) do not advance the rank
    const std::vector<bool> counted{false, true, true, false, true};
    QCOMPARE(assignedLayoutIndex(counted, 1, 0), std::optional<int>(0));
    QCOMPARE(assignedLayoutIndex(counted, 2, 0), std::optional<int>(1));
    QCOMPARE(assignedLayoutIndex(counted, 4, 0), std::optional<int>(2));

    //! endLayout's deliberately high beginIndex offsets the rank
    QCOMPARE(assignedLayoutIndex(counted, 2, 100), std::optional<int>(101));

    //! an uncounted self keeps index -1 (nullopt at the core boundary):
    //! Qt5's loop skipped it before the identity check ever matched
    QCOMPARE(assignedLayoutIndex(counted, 0, 0), std::nullopt);
    QCOMPARE(assignedLayoutIndex(counted, 3, 7), std::nullopt);
}

void VisibleIndexTest::twinCoherence_clientAnswersLandInsideTheHostApplet()
{
    //! the drift-regression invariant tying the twins together (the spec's
    //! cross-check): every visible task's client visible index must
    //! belong to the hosting applet in the containment row, with the
    //! host base taken from the containment row itself. CASE plainRow
    //! and CASE clientMixed shapes.
    const RowEntries tasks{plain(0), separator(1), plain(2), plain(3), hiddenItem(4)};
    const int visibleTasks = countVisibleItems(tasks);
    const RowEntries hostRow = containmentRow(visibleTasks);

    const std::optional<int> base = visibleIndexOf(hostRow, 4);
    QVERIFY(base.has_value());

    for (const RowEntry &task : tasks) {
        const std::optional<int> clientAnswer = clientVisibleIndexOf(tasks, task.index, *base);
        if (isVisibleItem(task)) {
            QVERIFY(clientAnswer.has_value());
            QVERIFY(belongsAtVisibleIndex(hostRow, 4, *clientAnswer));
            //! and it belongs to no other entry
            for (const RowEntry &other : hostRow) {
                if (other.index != 4) {
                    QVERIFY(!belongsAtVisibleIndex(hostRow, other.index, *clientAnswer));
                }
            }
        } else {
            QCOMPARE(clientAnswer, std::nullopt);
        }
    }
}

void VisibleIndexTest::emptyRow_everyAnswerIsNone()
{
    const RowEntries empty;
    QCOMPARE(visibleIndexOf(empty, 0), std::nullopt);
    QCOMPARE(countVisibleItemsBefore(empty, 5), 0);
    QCOMPARE(belongsAtVisibleIndex(empty, 0, 0), false);
    QCOMPARE(firstVisibleIndex(empty), std::nullopt);
    QCOMPARE(lastVisibleIndex(empty), std::nullopt);
    QCOMPARE(countVisibleItems(empty), 0);
    QCOMPARE(edgeItemIsSeparator(empty, Direction::Tail), false);
    QCOMPARE(edgeItemIsSeparator(empty, Direction::Head), false);
    QCOMPARE(hiddenSkippingNeighbor(empty, 0, Direction::Head), std::nullopt);
    QCOMPARE(neighborIsSeparator(empty, 0, Direction::Head), false);
    QCOMPARE(neighborIsVisibleSeparator(empty, 0, Direction::Tail), false);
    QCOMPARE(isInMarginsArea(empty, 0), false);
}

QTEST_GUILESS_MAIN(VisibleIndexTest)
#include "visibleindextest.moc"
