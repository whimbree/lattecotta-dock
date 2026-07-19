/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// EX-05 FillLengthDistributor (docs/tracking/QML_EXTRACTION_PLAN.md): pins the
// two-pass fill-length distribution against the shipped QML behavior.
//
// Generation method (docs/reference/TESTING.md): every scenario's expected values
// were recorded by driving the SHIPPED pre-cutover implementation
// (containment abilities/privates/LayouterPrivate.qml at 71737d61, itself
// byte-identical to Qt5 f0ad7b23) offscreen through the equality harness
// tests/qml/tst_filldistribution.qml in record mode; the post-cutover
// shell+wrapper+core run printed byte-identical vectors before they were
// baked in there and transcribed here. The recording carries the input
// snapshots too (including the aggregates the real counter Bindings
// computed from the same mock items), so the inputs below are exactly
// what the shipped gatherer produced. The harness's intCoercion probe
// pins the QML double->int property write semantics coerceToQmlInt
// mirrors.

#include "../../containment/plugin/units/filldistributor.h"

#include <QtTest>

using namespace Latte::FillDistributor;

namespace {

FillItem fillApplet(double min, double pref, double max)
{
    FillItem item;
    item.autoFill = true;
    item.hidden = false;
    item.hasApplet = true;
    item.minLength = min;
    item.prefLength = pref;
    item.maxLength = max;
    return item;
}

FillItem fixedApplet()
{
    FillItem item;
    item.hidden = false;
    item.hasApplet = true;
    return item;
}

FillItem hiddenFillApplet()
{
    FillItem item = fillApplet(-1, -1, -1);
    item.hidden = true;
    return item;
}

FillItem fillWithoutApplet()
{
    FillItem item = fillApplet(-1, -1, -1);
    item.hasApplet = false;
    return item;
}

//! metrics as the AppletItem int boundary delivers them for internal view
//! splitters: min=pref=maxJustifySplitterSize(64), max=Infinity collapsed
//! to 0 by the int property (probe-verified)
FillItem splitterItem(bool autoFill = false)
{
    FillItem item;
    item.hidden = false;
    item.internalSplitter = true;
    item.autoFill = autoFill;
    item.minLength = 64;
    item.prefLength = 64;
    item.maxLength = 0;
    return item;
}

LayoutSnapshot layoutOf(QVector<FillItem> items, double sizeWithNoFill, int fillApplets, int shownApplets, double gridLength)
{
    LayoutSnapshot layout;
    layout.items = std::move(items);
    layout.sizeWithNoFillApplets = sizeWithNoFill;
    layout.fillApplets = fillApplets;
    layout.shownApplets = shownApplets;
    layout.gridLength = gridLength;
    return layout;
}

Snapshot snapshotOf(LayoutSnapshot start, LayoutSnapshot main, LayoutSnapshot end, Alignment alignment, double minLength = 600, double contentsMaxLength = 780)
{
    Snapshot snapshot;
    snapshot.start = std::move(start);
    snapshot.main = std::move(main);
    snapshot.end = std::move(end);
    snapshot.alignment = alignment;
    snapshot.minLength = minLength;
    snapshot.contentsMaxLength = contentsMaxLength;
    return snapshot;
}

LayoutSnapshot emptyLayout()
{
    return layoutOf({}, 0, 0, 0, 0);
}

//! the post-run property values: assigned value, or the previous live one
//! when the pass left the item untouched (what the recording observed)
QVector<QPair<int, int>> appliedView(const LayoutSnapshot &snapshot, const QVector<ItemAssignment> &assignments)
{
    QVector<QPair<int, int>> view;
    for (int i = 0; i < snapshot.items.size(); ++i) {
        const FillItem &item = snapshot.items[i];
        const ItemAssignment &assignment = assignments[i];
        view.append({assignment.maxFillLength.value_or(static_cast<int>(item.liveMaxFillLength)),
                     assignment.minFillLength.value_or(static_cast<int>(item.liveMinFillLength))});
    }
    return view;
}

void compareLayout(const LayoutSnapshot &snapshot, const QVector<ItemAssignment> &assignments, const QVector<QPair<int, int>> &expected)
{
    QCOMPARE(assignments.size(), snapshot.items.size());
    QCOMPARE(appliedView(snapshot, assignments), expected);
}

} // namespace

class FillDistributorTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // ---- the recorded scenario table (one pool / non-Justify) ----

    void distribute_singleNeutralFill_takesAllFreeSpace()
    {
        const Snapshot s = snapshotOf(emptyLayout(),
                                      layoutOf({fillApplet(-1, -1, -1)}, 0, 1, 1, 0),
                                      emptyLayout(), Alignment::NonJustify);
        const Assignments a = distributeFillLengths(s);
        compareLayout(s.main, a.main, {{780, 600}});
    }

    void distribute_leftoverGoesToMostDemanding_pastItsOwnMax()
    {
        //! both applets fit in step 1 (50 and 40), the 690 leftover lands
        //! wholesale on the bigger winner - 740 deliberately exceeds its
        //! max of 60 (Qt5 remainder semantics, see the core header). The
        //! min pass then shows the cross-pass budget quirk: 600 minus the
        //! max-pass 740 exhausts the budget, so the second applet gets 0.
        const Snapshot s = snapshotOf(emptyLayout(),
                                      layoutOf({fillApplet(10, 50, 60), fillApplet(10, 40, 60)}, 0, 2, 2, 0),
                                      emptyLayout(), Alignment::NonJustify);
        const Assignments a = distributeFillLengths(s);
        compareLayout(s.main, a.main, {{740, 50}, {40, 0}});
    }

    void distribute_mixedFillAndFixed_freeSpaceExcludesFixed()
    {
        const Snapshot s = snapshotOf(emptyLayout(),
                                      layoutOf({fixedApplet(), fillApplet(-1, -1, -1), fixedApplet()}, 180, 1, 3, 180),
                                      emptyLayout(), Alignment::NonJustify);
        const Assignments a = distributeFillLengths(s);
        compareLayout(s.main, a.main, {{-1, -1}, {600, 420}, {-1, -1}});
        //! fixed neighbors are never assigned
        QVERIFY(!a.main[0].maxFillLength && !a.main[0].minFillLength);
        QVERIFY(!a.main[2].maxFillLength && !a.main[2].minFillLength);
    }

    void distribute_zeroFreeSpace_assignsZeroNotNegative()
    {
        const Snapshot s = snapshotOf(emptyLayout(),
                                      layoutOf({fixedApplet(), fixedApplet(), fillApplet(-1, -1, -1)}, 220, 1, 3, 220),
                                      emptyLayout(), Alignment::NonJustify, 150, 180);
        const Assignments a = distributeFillLengths(s);
        compareLayout(s.main, a.main, {{-1, -1}, {-1, -1}, {0, 0}});
    }

    void distribute_runawayPreferredLength_isCappedAtFairShare()
    {
        //! the ng 30637c1cd collapse family's distributor-level shape: an
        //! applet reporting a runaway preferred length (their live case: a
        //! 2931px clock label) in a constrained row is deferred by step 1
        //! and gets only the fair share in step 2 - the sum never exceeds
        //! the available space
        const Snapshot s = snapshotOf(emptyLayout(),
                                      layoutOf({fillApplet(0, 2931, -1), fillApplet(-1, -1, -1), fixedApplet()}, 100, 2, 3, 100),
                                      emptyLayout(), Alignment::NonJustify);
        const Assignments a = distributeFillLengths(s);
        compareLayout(s.main, a.main, {{340, 250}, {340, 250}, {-1, -1}});
        QVERIFY(*a.main[0].maxFillLength + *a.main[1].maxFillLength <= 780 - 100);
        QVERIFY(*a.main[0].minFillLength + *a.main[1].minFillLength <= 600 - 100);
    }

    void distribute_runawayPreferredLength_singleFill_isCappedAtAvailableSpace()
    {
        const Snapshot s = snapshotOf(emptyLayout(),
                                      layoutOf({fillApplet(0, 2931, -1), fixedApplet()}, 100, 1, 2, 100),
                                      emptyLayout(), Alignment::NonJustify);
        const Assignments a = distributeFillLengths(s);
        compareLayout(s.main, a.main, {{680, 500}, {-1, -1}});
    }

    void distribute_staticSize_getsExactlyItsMinimum()
    {
        const Snapshot s = snapshotOf(emptyLayout(),
                                      layoutOf({fillApplet(80, -1, 80), fillApplet(-1, -1, -1)}, 0, 2, 2, 0),
                                      emptyLayout(), Alignment::NonJustify);
        const Assignments a = distributeFillLengths(s);
        compareLayout(s.main, a.main, {{80, 80}, {700, 520}});
    }

    void distribute_invalidMinDiscardsValidPref_qt5Quirk()
    {
        //! the shipped normalization gates pref validity on MIN validity:
        //! a valid pref of 200 with min=-1 is discarded and the applet is
        //! served the step-2 share instead; the sibling applet with real
        //! metrics gets its bounded preference
        const Snapshot s = snapshotOf(emptyLayout(),
                                      layoutOf({fillApplet(-1, 200, 300), fillApplet(50, 60, 70)}, 0, 2, 2, 0),
                                      emptyLayout(), Alignment::NonJustify);
        const Assignments a = distributeFillLengths(s);
        compareLayout(s.main, a.main, {{720, 540}, {60, 60}});
        //! the bounded neighbor stays inside its own [min, max]
        QVERIFY(*a.main[1].maxFillLength >= 50 && *a.main[1].maxFillLength <= 70);
        QVERIFY(*a.main[1].minFillLength >= 50 && *a.main[1].minFillLength <= 70);
    }

    void distribute_maxZero_isIgnored_bug445869()
    {
        //! Qt hands maximumLength=0 to fill-flagged applets (bug #445869);
        //! the normalization treats it as absent, so the preference of 30
        //! wins step 1 and the remainder lands on top in the max pass
        const Snapshot s = snapshotOf(emptyLayout(),
                                      layoutOf({fillApplet(0, 30, 0)}, 0, 1, 1, 0),
                                      emptyLayout(), Alignment::NonJustify);
        const Assignments a = distributeFillLengths(s);
        compareLayout(s.main, a.main, {{780, 30}});
    }

    void distribute_noFillApplets_returnsUntouched()
    {
        const Snapshot s = snapshotOf(emptyLayout(),
                                      layoutOf({fixedApplet()}, 100, 0, 1, 100),
                                      emptyLayout(), Alignment::NonJustify);
        const Assignments a = distributeFillLengths(s);
        QCOMPARE(a.main.size(), 1);
        QVERIFY(!a.main[0].maxFillLength && !a.main[0].minFillLength);
    }

    void distribute_allFill_oddTotals_truncatedShares()
    {
        //! 781/3 and 601/3: the shares are fractional and the QML int
        //! property write truncates (probe-verified), never rounds up
        const Snapshot s = snapshotOf(emptyLayout(),
                                      layoutOf({fillApplet(-1, -1, -1), fillApplet(-1, -1, -1), fillApplet(-1, -1, -1)}, 0, 3, 3, 0),
                                      emptyLayout(), Alignment::NonJustify, 601, 781);
        const Assignments a = distributeFillLengths(s);
        compareLayout(s.main, a.main, {{260, 200}, {260, 200}, {260, 200}});
        QVERIFY(3 * *a.main[0].maxFillLength <= 781);
        QVERIFY(3 * *a.main[0].minFillLength <= 601);
    }

    void distribute_onlyNeutralApplets_splitLeftoverEqually()
    {
        //! min=0/pref=0 applets are granted 0 in step 1 (so the remainder
        //! branch runs) yet classify as neutral there: the leftover splits
        //! equally instead of going to a most demanding applet. The min
        //! pass shows the cross-pass quirk again: 600 - 390 - 390 leaves
        //! nothing to redistribute, so the min values stay at step 1's 0.
        const Snapshot s = snapshotOf(emptyLayout(),
                                      layoutOf({fillApplet(0, 0, -1), fillApplet(0, 0, -1)}, 0, 2, 2, 0),
                                      emptyLayout(), Alignment::NonJustify);
        const Assignments a = distributeFillLengths(s);
        compareLayout(s.main, a.main, {{390, 0}, {390, 0}});
    }

    void distribute_infinityMetricsCollapsedByIntBoundary()
    {
        //! Infinity metrics never reach the core in production: the
        //! AppletItem int properties collapse them to 0 (probe-verified).
        //! This is that collapsed shape: a 0/0/0 applet behaves as a
        //! zero-preference applet, the sibling takes everything.
        const Snapshot s = snapshotOf(emptyLayout(),
                                      layoutOf({fillApplet(0, 0, 0), fillApplet(-1, -1, -1)}, 0, 2, 2, 0),
                                      emptyLayout(), Alignment::NonJustify);
        const Assignments a = distributeFillLengths(s);
        compareLayout(s.main, a.main, {{0, 0}, {780, 600}});
    }

    void distribute_hiddenFillApplet_untouched()
    {
        const Snapshot s = snapshotOf(emptyLayout(),
                                      layoutOf({hiddenFillApplet(), fillApplet(-1, -1, -1)}, 0, 1, 1, 0),
                                      emptyLayout(), Alignment::NonJustify);
        const Assignments a = distributeFillLengths(s);
        compareLayout(s.main, a.main, {{-1, -1}, {780, 600}});
        QVERIFY(!a.main[0].maxFillLength && !a.main[0].minFillLength);
    }

    void distribute_fillWithoutApplet_servedByStep2Share_qt5Quirk()
    {
        //! step 1 requires an applet (or splitter) but step 2's share
        //! branch deliberately does not - an autofill item whose applet is
        //! still null gets the share (shipped predicate asymmetry)
        const Snapshot s = snapshotOf(emptyLayout(),
                                      layoutOf({fillWithoutApplet(), fixedApplet()}, 100, 1, 1, 100),
                                      emptyLayout(), Alignment::NonJustify);
        const Assignments a = distributeFillLengths(s);
        compareLayout(s.main, a.main, {{680, 500}, {-1, -1}});
    }

    // ---- the recorded scenario table (Justify) ----

    void distribute_justifyEmptyMain_usesOnePool()
    {
        const Snapshot s = snapshotOf(layoutOf({fillApplet(-1, -1, -1)}, 0, 1, 1, 0),
                                      emptyLayout(),
                                      layoutOf({fillApplet(20, 30, 40)}, 0, 1, 1, 0),
                                      Alignment::Justify);
        const Assignments a = distributeFillLengths(s);
        compareLayout(s.start, a.start, {{750, 570}});
        compareLayout(s.end, a.end, {{30, 30}});
        //! the metric-providing applet stays inside its own bounds on the
        //! fair-share path
        QVERIFY(*a.end[0].maxFillLength >= 20 && *a.end[0].maxFillLength <= 40);
    }

    void distribute_justifyRemainder_prefersStartOverEnd()
    {
        //! identical twins in start and end, both served in step 1: the
        //! leftover goes to START's most demanding applet (the shipped
        //! selection order is start, end, main)
        const Snapshot s = snapshotOf(layoutOf({fillApplet(10, 20, 30)}, 0, 1, 1, 0),
                                      emptyLayout(),
                                      layoutOf({fillApplet(10, 20, 30)}, 0, 1, 1, 0),
                                      Alignment::Justify);
        const Assignments a = distributeFillLengths(s);
        compareLayout(s.start, a.start, {{760, 20}});
        compareLayout(s.end, a.end, {{20, 0}});
    }

    void distribute_twoStep_sideFills_fixedMain()
    {
        const Snapshot s = snapshotOf(layoutOf({fillApplet(-1, -1, -1), fixedApplet()}, 50, 1, 2, 50),
                                      layoutOf({fixedApplet()}, 100, 0, 1, 100),
                                      layoutOf({fillApplet(-1, -1, -1)}, 0, 1, 1, 0),
                                      Alignment::Justify);
        const Assignments a = distributeFillLengths(s);
        compareLayout(s.start, a.start, {{290, 200}, {-1, -1}});
        compareLayout(s.main, a.main, {{-1, -1}});
        compareLayout(s.end, a.end, {{340, 250}});
    }

    void distribute_twoStep_mainFill_secondPassDerivesFromGridLengths()
    {
        const Snapshot s = snapshotOf(layoutOf({fillApplet(-1, -1, -1)}, 0, 1, 1, 0),
                                      layoutOf({fillApplet(-1, -1, -1), fixedApplet()}, 100, 1, 2, 100),
                                      layoutOf({fixedApplet()}, 50, 0, 1, 50),
                                      Alignment::Justify);
        const Assignments a = distributeFillLengths(s);
        compareLayout(s.start, a.start, {{340, 250}});
        compareLayout(s.main, a.main, {{580, 400}, {-1, -1}});
        compareLayout(s.end, a.end, {{-1, -1}});
    }

    void distribute_twoStep_oddTotals_halvingFractionsTruncate()
    {
        const Snapshot s = snapshotOf(layoutOf({fillApplet(-1, -1, -1)}, 0, 1, 1, 0),
                                      layoutOf({fixedApplet()}, 101, 0, 1, 101),
                                      layoutOf({fillApplet(-1, -1, -1)}, 0, 1, 1, 0),
                                      Alignment::Justify, 601, 781);
        const Assignments a = distributeFillLengths(s);
        compareLayout(s.start, a.start, {{340, 250}});
        compareLayout(s.end, a.end, {{340, 250}});
    }

    void distribute_twoStep_splittersReduceTheSideBudgets()
    {
        //! the counters (which stay in QML and arrive as snapshot inputs)
        //! charge each internal splitter at maxJustifySplitterSize=64;
        //! the side fills get what is left of each half
        const Snapshot s = snapshotOf(layoutOf({splitterItem(), fillApplet(-1, -1, -1)}, 64, 1, 1, 64),
                                      layoutOf({fixedApplet()}, 100, 0, 1, 100),
                                      layoutOf({fillApplet(-1, -1, -1), splitterItem()}, 64, 1, 1, 64),
                                      Alignment::Justify);
        const Assignments a = distributeFillLengths(s);
        compareLayout(s.start, a.start, {{-1, -1}, {276, 186}});
        compareLayout(s.end, a.end, {{276, 186}, {-1, -1}});
        //! non-fill splitters are never assigned
        QVERIFY(!a.start[0].maxFillLength && !a.end[1].maxFillLength);
    }

    void distribute_autoFillSplitter_participatesInStep1_latentBranch()
    {
        //! the shipped step-1 predicate carries `|| isInternalViewSplitter`
        //! although shipped splitters never set the autofill flag; the
        //! branch is latent, pinned here so a future splitter change lands
        //! on decided behavior: the splitter's static-ish 64 wins step 1
        //! in the min pass, the max pass tops it up as most demanding
        const Snapshot s = snapshotOf(layoutOf({splitterItem(true)}, 0, 1, 0, 0),
                                      layoutOf({fixedApplet()}, 100, 0, 1, 100),
                                      layoutOf({fillApplet(-1, -1, -1)}, 0, 1, 1, 0),
                                      Alignment::Justify);
        const Assignments a = distributeFillLengths(s);
        compareLayout(s.start, a.start, {{340, 64}});
        compareLayout(s.end, a.end, {{340, 250}});
    }

    void distribute_twoStep_mainSkipped_keepsPreviousValues()
    {
        //! when the sides already cover the center (freeSpaceAfterStart
        //! <= 0) the main grid's step 2 never runs and its unserved fill
        //! applets keep their previous values - the shipped
        //! write-nothing behavior, expressed here as empty optionals
        FillItem presetFill = fillApplet(-1, -1, -1);
        presetFill.liveMaxFillLength = 333;
        presetFill.liveMinFillLength = 222;
        const Snapshot s = snapshotOf(layoutOf({fillApplet(-1, -1, -1)}, 0, 1, 1, 500),
                                      layoutOf({presetFill, fixedApplet()}, 100, 1, 2, 100),
                                      layoutOf({fixedApplet()}, 40, 0, 1, 40),
                                      Alignment::Justify);
        const Assignments a = distributeFillLengths(s);
        compareLayout(s.start, a.start, {{340, 250}});
        compareLayout(s.main, a.main, {{333, 222}, {-1, -1}});
        QVERIFY(!a.main[0].maxFillLength); //! untouched, not re-assigned to 333
        QVERIFY(!a.main[0].minFillLength);
    }

    void distribute_minPassSubtractsMaxPassValue_qt5Quirk()
    {
        //! the discriminating construction for the cross-pass budget
        //! quirk: the max pass grants main 100 in step 1 plus the 320
        //! remainder (420 total); the min pass then subtracts 420 - not
        //! the 100 the min pass itself granted - from its budget, which
        //! pushes the start applet's preference of 115 past its shrunken
        //! share, defers it to step 2 and lands it at 300. Without the
        //! quirk it would keep its 115.
        const Snapshot s = snapshotOf(layoutOf({fillApplet(0, 115, -1)}, 0, 1, 1, 0),
                                      layoutOf({fillApplet(0, 100, -1), fixedApplet()}, 60, 1, 2, 100),
                                      layoutOf({fixedApplet()}, 40, 0, 1, 40),
                                      Alignment::Justify, 700, 500);
        const Assignments a = distributeFillLengths(s);
        compareLayout(s.start, a.start, {{115, 300}});
        compareLayout(s.main, a.main, {{420, 620}, {-1, -1}});
    }

    void distribute_twoStep_allInMain_sumBranch()
    {
        //! empty sides select the pooled-space formula even under Justify
        const Snapshot s = snapshotOf(emptyLayout(),
                                      layoutOf({fillApplet(-1, -1, -1), fixedApplet()}, 100, 1, 2, 100),
                                      emptyLayout(), Alignment::Justify);
        const Assignments a = distributeFillLengths(s);
        compareLayout(s.main, a.main, {{680, 500}, {-1, -1}});
    }

    void distribute_negativeSpace_nothingAssigned()
    {
        //! oversized fixed content drives every derived share negative;
        //! the step-2 sizePerApplet>=0 gate refuses and every fill applet
        //! keeps its previous value - no negative or wrapped lengths ever
        //! escape
        const Snapshot s = snapshotOf(layoutOf({fixedApplet(), fillApplet(-1, -1, -1)}, 400, 1, 2, 400),
                                      layoutOf({fixedApplet(), fillApplet(-1, -1, -1)}, 600, 1, 2, 600),
                                      layoutOf({fixedApplet()}, 30, 0, 1, 30),
                                      Alignment::Justify);
        const Assignments a = distributeFillLengths(s);
        for (const auto &layoutAssignments : {a.start, a.main, a.end}) {
            for (const ItemAssignment &assignment : layoutAssignments) {
                QVERIFY(!assignment.maxFillLength);
                QVERIFY(!assignment.minFillLength);
            }
        }
    }

    // ---- degenerate inputs and building blocks ----

    void distribute_emptySnapshot_returnsEmpty()
    {
        const Snapshot s = snapshotOf(emptyLayout(), emptyLayout(), emptyLayout(), Alignment::Justify);
        const Assignments a = distributeFillLengths(s);
        QVERIFY(a.start.isEmpty() && a.main.isEmpty() && a.end.isEmpty());
    }

    void coerceToQmlInt_matchesRecordedProbe()
    {
        //! PROBE|intCoercion at 71737d61:
        //! [0.4,0.5,0.6,1.5,2.5,-0.5,-1.5,33.333,260.333...,390.5,Inf,-Inf]
        //! -> [0,0,0,1,2,0,-1,33,260,390,0,0]
        QCOMPARE(coerceToQmlInt(0.4), 0);
        QCOMPARE(coerceToQmlInt(0.5), 0);
        QCOMPARE(coerceToQmlInt(0.6), 0);
        QCOMPARE(coerceToQmlInt(1.5), 1);
        QCOMPARE(coerceToQmlInt(2.5), 2);
        QCOMPARE(coerceToQmlInt(-0.5), 0);
        QCOMPARE(coerceToQmlInt(-1.5), -1);
        QCOMPARE(coerceToQmlInt(33.333), 33);
        QCOMPARE(coerceToQmlInt(260.33333333333334), 260);
        QCOMPARE(coerceToQmlInt(390.5), 390);
        QCOMPARE(coerceToQmlInt(std::numeric_limits<double>::infinity()), 0);
        QCOMPARE(coerceToQmlInt(-std::numeric_limits<double>::infinity()), 0);
        QCOMPARE(coerceToQmlInt(std::numeric_limits<double>::quiet_NaN()), 0);
    }

    void normalizeMetrics_minGatesPref()
    {
        const auto m = detail::normalizeMetricsForFill(fillApplet(-1, 200, 300));
        QVERIFY(!m.min);
        QVERIFY(!m.pref); //! valid pref discarded: min is invalid (Qt5 quirk)
        QVERIFY(m.max && *m.max == 300);
    }

    void normalizeMetrics_maxZeroAndNegativesAbsent()
    {
        const auto m = detail::normalizeMetricsForFill(fillApplet(0, -5, 0));
        QVERIFY(m.min && *m.min == 0);
        QVERIFY(!m.pref); //! negative pref folds into absent (equivalence note in the core)
        QVERIFY(!m.max);  //! max=0 is invalid (bug #445869)
    }

    void normalizeMetrics_infinityAbsent()
    {
        const double inf = std::numeric_limits<double>::infinity();
        const auto m = detail::normalizeMetricsForFill(fillApplet(inf, inf, inf));
        QVERIFY(!m.min && !m.pref && !m.max);
    }

    void boundPreferredLength_fallbackChain()
    {
        //! absent max falls back to pref, then to min; absent pref does
        //! NOT fall back - the min wins the Math.max
        QCOMPARE(detail::boundPreferredLength(5, 50, std::nullopt), 50.0);
        QCOMPARE(detail::boundPreferredLength(5, std::nullopt, std::nullopt), 5.0);
        QCOMPARE(detail::boundPreferredLength(5, std::nullopt, 10.0), 5.0);
        QCOMPARE(detail::boundPreferredLength(5, 50, 10.0), 10.0);
        QCOMPARE(detail::boundPreferredLength(5, 0, std::nullopt), 0.0); //! pref 0 caps at 0 (JS max:=pref)
    }
};

QTEST_GUILESS_MAIN(FillDistributorTest)
#include "filldistributortest.moc"
