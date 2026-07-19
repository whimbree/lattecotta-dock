/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef FILLDISTRIBUTOR_H
#define FILLDISTRIBUTOR_H

// Qt
#include <QVector>
#include <QtGlobal>

// C++
#include <algorithm>
#include <cmath>
#include <optional>

namespace Latte {

//! The two-pass distribution of free length among fill applets
//! (EX-05 in docs/tracking/QML_EXTRACTION_PLAN.md), extracted from the containment
//! abilities definition LayouterPrivate.qml and algebraically identical to
//! the Qt5 ancestor (f0ad7b23; the extraction source was byte-identical to
//! it). The QML layouter keeps gathering inputs from live items and
//! applying the results; the passes live here.
//!
//! Semantics the code cannot show (Qt5-inherited, pinned by
//! tests/units/filldistributortest.cpp against vectors recorded from the
//! shipped QML by tests/qml/tst_filldistribution.qml in record mode):
//!
//! - The whole run happens twice per update: a MaxLength pass distributing
//!   contentsMaxLength writes maxAutoFillLength, then a MinLength pass
//!   distributing root.minLength writes minAutoFillLength. The passes are
//!   NOT independent: step 1 subtracts the applet's MAX-pass value from
//!   the budget even during the MIN pass (the shipped line reads
//!   curApplet.maxAutoFillLength unconditionally), so a most-demanding
//!   bonus granted in the max pass starves the min pass's budget. The
//!   twostep_min_pass_subtracts_max_value_quirk vector pins this.
//! - Results are written into AppletItem's int properties and read back
//!   mid-run, so the QML double->int property coercion is part of the
//!   algorithm's arithmetic (coerceToQmlInt below).
//! - Step 2's most-demanding branch hands ALL remaining space to the
//!   biggest step-1 winner - deliberately allowed to exceed that applet's
//!   own maximum (Qt5 behavior; onestep_two_fills vector pins 740 against
//!   a max of 60). "No assignment above max" holds only on the step-1 and
//!   fair-share paths.
namespace FillDistributor {

enum class Alignment {
    Justify,
    NonJustify
};

//! which of the two dependent passes is running (replaces the shipped
//! inMaxAutoFillCalculations bool threaded through every function)
enum class FillPass {
    MaxLength,
    MinLength
};

//! step 2's dispatch, replacing the shipped noOfApplets sentinel where 0
//! meant "everyone was served in step 1, hand the remainder to the most
//! demanding applet" and -1/positive meant "share the per-applet size with
//! whoever step 1 left unserved" (the count itself was never used there)
enum class Step2Mode {
    ShareToUnassigned,
    RemainderToMostDemanding
};

//! one grid child as the passes see it. Metrics are the RAW values of the
//! AppletItem int properties (appletMinimumLength/-Preferred-/-Maximum-):
//! step 1 normalizes them (normalizeMetricsForFill) but step 2's neutral
//! predicate reads them raw, so both forms are needed and the raw one is
//! the input. The live*FillLength fields are the current values of the
//! item's maxAutoFillLength/minAutoFillLength properties - step 1's budget
//! subtraction and step 2's most-demanding scan read them for items this
//! run has not (yet) assigned.
struct FillItem {
    double minLength = -1;
    double prefLength = -1;
    double maxLength = -1;
    bool autoFill = false;          //! isAutoFillApplet
    bool hidden = false;            //! isHidden
    bool hasApplet = false;         //! applet !== null
    bool internalSplitter = false;  //! isInternalViewSplitter
    double liveMaxFillLength = -1;  //! maxAutoFillLength before this run
    double liveMinFillLength = -1;  //! minAutoFillLength before this run
};

//! one of the three grids plus the aggregates the sibling QML counter
//! component (abilities/privates/layouter/AppletsContainer.qml) computes
//! from the same children - the counters stay in QML because they are the
//! reactive triggers and have other consumers; they arrive here as a
//! snapshot. gridLength is grid.length at call time: positioner geometry
//! updates at polish, never inside the synchronous update call, so a
//! before-the-passes snapshot is exactly what the shipped mid-pass reads
//! observed.
struct LayoutSnapshot {
    QVector<FillItem> items;
    double sizeWithNoFillApplets = 0;
    int fillApplets = 0;
    int shownApplets = 0;
    double gridLength = 0;
};

struct Snapshot {
    LayoutSnapshot start;
    LayoutSnapshot main;
    LayoutSnapshot end;
    Alignment alignment = Alignment::NonJustify;
    double minLength = 0;          //! root.minLength
    double contentsMaxLength = 0;  //! root.maxLength - background paddings
};

//! per-item result. An empty optional means the pass did not touch the
//! item and its property keeps its previous value - exactly the shipped
//! behavior, where untouched items were simply never written to.
struct ItemAssignment {
    std::optional<int> maxFillLength;
    std::optional<int> minFillLength;
};

//! index-parallel with the corresponding Snapshot's item vectors
struct Assignments {
    QVector<ItemAssignment> start;
    QVector<ItemAssignment> main;
    QVector<ItemAssignment> end;
};

//! QML writes the results into AppletItem's int properties and the passes
//! read them back mid-run, so the coercion IS part of the arithmetic.
//! Verified by the harness probe (tst_filldistribution.qml's intCoercion
//! test): double -> int property writes follow ECMAScript ToInt32 -
//! truncation toward zero, non-finite values to 0. ToInt32's modulo-2^32
//! wrap is unreachable for on-screen lengths, so plain truncation is the
//! faithful mirror.
inline int coerceToQmlInt(double value)
{
    if (!std::isfinite(value)) {
        return 0;
    }
    return static_cast<int>(std::trunc(value));
}

namespace detail {

//! step 1 / step 2 participation (the shipped per-item predicate). Step
//! 2's share branch checks a weaker predicate - see shareWithUnassigned.
inline bool participatesInFillPass(const FillItem &item)
{
    return item.autoFill && !item.hidden && (item.hasApplet || item.internalSplitter);
}

//! the shipped [availableSpace, sizePerApplet, noOfApplets] positional
//! array, named
struct DistributionBudget {
    double availableSpace = 0;
    double sizePerApplet = 0;
    int applets = 0;
};

//! -1 sentinels end here: absent metrics become empty optionals
struct NormalizedMetrics {
    std::optional<double> min;
    std::optional<double> pref;
    std::optional<double> max;
};

//! Mirrors the shipped step-1 normalization. Qt5 quirks preserved:
//! - pref's validity is gated on MIN's validity (the shipped line reads
//!   `minSize>=0 && prefSize!==Infinity`), so a valid preferred length is
//!   discarded whenever the minimum is invalid (pinned by the
//!   onestep_min_gates_pref_quirk vector);
//! - max<=0 is invalid, not just max<0: Qt hands maximumLength=0 to
//!   applets that set the fill flag (bug #445869, mediacontroller_plus);
//! - negative prefs fold into "absent": every downstream read treats them
//!   identically to the shipped kept-negative value (systemDecides tests
//!   pref<0; boundPreferredLength's Math.max(min, pref) discards any
//!   negative against the then-guaranteed min>=0).
//! The Infinity checks mirror the shipped lines but are dead against the
//! real gatherer: AppletItem's metric properties are int, so Infinity
//! already collapsed to 0 at that boundary (probe-verified; the
//! onestep_infinity_metrics vector pins the collapsed behavior).
inline NormalizedMetrics normalizeMetricsForFill(const FillItem &item)
{
    NormalizedMetrics metrics;
    if (item.minLength >= 0 && !std::isinf(item.minLength)) {
        metrics.min = item.minLength;
    }
    if (metrics.min && item.prefLength >= 0 && !std::isinf(item.prefLength)) {
        metrics.pref = item.prefLength;
    }
    if (item.maxLength > 0 && !std::isinf(item.maxLength)) {
        metrics.max = item.maxLength;
    }
    return metrics;
}

//! min==max applets provide an exact size even without a preference
inline bool providesStaticSize(const NormalizedMetrics &metrics)
{
    return metrics.min && metrics.max && *metrics.max == *metrics.min;
}

//! no usable preference at all: the system decides the size in step 2
inline bool systemDecidesSize(const NormalizedMetrics &metrics)
{
    return !metrics.pref && !providesStaticSize(metrics);
}

//! the shipped appletPreferredLength qBound variant. An absent max falls
//! back to the preference (or the minimum when that is absent too); an
//! absent pref deliberately does NOT fall back - the shipped -1 flows into
//! Math.max(min, -1) and the minimum wins. Callers guarantee min is
//! present: systemDecidesSize gates every call, and !systemDecides implies
//! a present pref (which requires a present min) or a static size (ditto).
inline double boundPreferredLength(double min, const std::optional<double> &pref, const std::optional<double> &max)
{
    Q_ASSERT(min >= 0);
    const double effectiveMax = max ? *max : pref.value_or(min);
    const double effectivePref = pref.value_or(-1.0);
    return std::min(std::max(min, effectivePref), effectiveMax);
}

//! the last-applet cap: the shipped Math.min(maxSize, sizePerApplet) on
//! the -1-encoded max. When max is absent the shipped code computes
//! min(-1, sizePerApplet), which stays -1 (absent) for any non-negative
//! share and becomes a negative "present" max only when the share is
//! below -1 - but a negative share can never satisfy the
//! `applied >= 0 && applied <= sizePerApplet` assignment gate either way,
//! so absent-stays-absent is behaviorally identical (the
//! twostep_negative_space vector drives the negative-share path).
inline std::optional<double> capMaxAtFairShare(const std::optional<double> &max, double sizePerApplet)
{
    if (max) {
        return std::min(*max, sizePerApplet);
    }
    return std::nullopt;
}

//! per-run working state layered over one layout's snapshot
struct WorkingItem {
    const FillItem *source = nullptr;
    ItemAssignment *out = nullptr;
    bool inFillCalculations = false;
    double currentMaxFillLength = -1;  //! live value, updated as the max pass assigns
    double currentMinFillLength = -1;
};

using WorkingLayout = QVector<WorkingItem>;

inline double currentFillLength(const WorkingItem &item, FillPass pass)
{
    return pass == FillPass::MaxLength ? item.currentMaxFillLength : item.currentMinFillLength;
}

//! the single write point: coerces like the QML int property did and keeps
//! the working copy in sync so mid-run read-backs see what QML saw
inline void assignFillLength(WorkingItem &item, FillPass pass, double value)
{
    const int coerced = coerceToQmlInt(value);
    if (pass == FillPass::MaxLength) {
        item.out->maxFillLength = coerced;
        item.currentMaxFillLength = coerced;
    } else {
        item.out->minFillLength = coerced;
        item.currentMinFillLength = coerced;
    }
}

//! initialize the "waiting for a computed size" flags (the shipped
//! initLayoutForFillsCalculations: the flag is set for every autofill
//! child, hidden or not - step 2's share branch re-checks hidden)
inline void initLayoutForFillsCalculations(WorkingLayout &layout)
{
    for (WorkingItem &item : layout) {
        if (item.source->autoFill) {
            item.inFillCalculations = true;
        }
    }
}

//! step 1: every applet that provides valid metrics gains its preferred
//! space as long as that fits the current fair share; the budget shrinks
//! and the share is redivided after every grant
inline DistributionBudget computeStep1ForLayout(WorkingLayout &layout, DistributionBudget budget, FillPass pass)
{
    for (WorkingItem &item : layout) {
        const FillItem &source = *item.source;
        if (!participatesInFillPass(source)) {
            continue;
        }

        const NormalizedMetrics metrics = normalizeMetricsForFill(source);
        if (systemDecidesSize(metrics)) {
            //! no usable metrics: this applet is served in step 2, after
            //! the applets with real opinions took their space
            continue;
        }

        Q_ASSERT(metrics.min); //! guaranteed by !systemDecidesSize, see boundPreferredLength

        std::optional<double> applied;
        if (budget.applets > 1) {
            applied = boundPreferredLength(*metrics.min, metrics.pref, metrics.max);
        } else if (budget.applets == 1) {
            //! the last applet's maximum is capped at the whole remaining
            //! share so it cannot draw outside the boundaries
            applied = boundPreferredLength(*metrics.min, metrics.pref, capMaxAtFairShare(metrics.max, budget.sizePerApplet));
        }

        //! a fit above the fair share is not granted here; that applet
        //! gets its space in step 2, fairly among everyone left
        if (applied && *applied >= 0 && *applied <= budget.sizePerApplet) {
            assignFillLength(item, pass, std::min(*applied, budget.availableSpace));
            item.inFillCalculations = false;
            //! Qt5 quirk (see the header block): the budget subtracts the
            //! MAX-pass value even during the min pass
            budget.availableSpace = std::max(0.0, budget.availableSpace - item.currentMaxFillLength);
            budget.applets -= 1;
            budget.sizePerApplet = budget.applets > 1 ? std::floor(budget.availableSpace / budget.applets) : budget.availableSpace;
        }
    }

    return budget;
}

//! step 2's remainder branch: all applets were served in step 1 but space
//! is left over. The most demanding applet - biggest step-1 grant among
//! those with real metrics - takes all of it (deliberately past its own
//! maximum, see the header block); with only "neutral" no-opinion applets
//! present the leftover is split equally among them instead.
inline void handRemainderToMostDemanding(WorkingLayout &layout, double sizePerApplet, FillPass pass)
{
    double mostDemandingSize = 0;
    WorkingItem *mostDemanding = nullptr;
    QVector<WorkingItem *> neutralApplets;

    for (WorkingItem &item : layout) {
        const FillItem &source = *item.source;
        if (!participatesInFillPass(source)) {
            continue;
        }

        //! RAW metrics by design (the shipped scan reads the properties
        //! unnormalized): a 0/absent minimum plus 0/absent preference is
        //! "no strong opinion"
        const bool isNeutral = source.minLength <= 0 && source.prefLength <= 0;
        const double current = currentFillLength(item, pass);

        if (!isNeutral && current > mostDemandingSize) {
            mostDemanding = &item;
            mostDemandingSize = current;
        } else if (isNeutral) {
            neutralApplets.append(&item);
        }
    }

    if (mostDemanding) {
        assignFillLength(*mostDemanding, pass, currentFillLength(*mostDemanding, pass) + sizePerApplet);
    } else if (!neutralApplets.isEmpty()) {
        const double share = sizePerApplet / neutralApplets.size();
        for (WorkingItem *item : neutralApplets) {
            assignFillLength(*item, pass, currentFillLength(*item, pass) + share);
        }
    }
}

//! step 2's share branch: whoever step 1 left unserved gets the fair
//! share, floored at the applet's RAW minimum (the shipped line reads the
//! property unnormalized). Weaker predicate than step 1 by design: the
//! shipped branch does not re-check hasApplet/internalSplitter, so an
//! autofill item without an applet is still served here
//! (onestep_fill_without_applet_quirk pins it).
inline void shareWithUnassigned(WorkingLayout &layout, double sizePerApplet, FillPass pass)
{
    for (WorkingItem &item : layout) {
        const FillItem &source = *item.source;
        if (source.autoFill && !source.hidden && item.inFillCalculations) {
            assignFillLength(item, pass, std::max(source.minLength, sizePerApplet));
            item.inFillCalculations = false;
        }
    }
}

inline void computeStep2ForLayout(WorkingLayout &layout, double sizePerApplet, Step2Mode mode, FillPass pass)
{
    if (sizePerApplet < 0) {
        //! a negative share means this layout has no room at all; its
        //! unserved applets keep their previous values (shipped gate)
        return;
    }

    switch (mode) {
    case Step2Mode::RemainderToMostDemanding:
        handRemainderToMostDemanding(layout, sizePerApplet, pass);
        break;
    case Step2Mode::ShareToUnassigned:
        shareWithUnassigned(layout, sizePerApplet, pass);
        break;
    }
}

inline Step2Mode step2ModeForRemaining(int remainingApplets)
{
    return remainingApplets == 0 ? Step2Mode::RemainderToMostDemanding : Step2Mode::ShareToUnassigned;
}

//! the one-pool variant: used outside Justify alignment (everything lives
//! in the main grid then) and for Justify with an empty main grid
inline void updateFillAppletsWithOneStep(const Snapshot &snapshot, WorkingLayout &start, WorkingLayout &main, WorkingLayout &end, FillPass pass)
{
    const double maxLength = pass == FillPass::MaxLength ? snapshot.contentsMaxLength : snapshot.minLength;
    const int fillApplets = snapshot.start.fillApplets + snapshot.main.fillApplets + snapshot.end.fillApplets;
    Q_ASSERT(fillApplets > 0); //! dispatcher guard

    const double availableSpace = std::max(0.0, maxLength
                                           - snapshot.start.sizeWithNoFillApplets
                                           - snapshot.main.sizeWithNoFillApplets
                                           - snapshot.end.sizeWithNoFillApplets);

    //! initialization phase: side grids only participate under Justify
    if (snapshot.alignment == Alignment::Justify) {
        initLayoutForFillsCalculations(start);
        initLayoutForFillsCalculations(end);
    }
    initLayoutForFillsCalculations(main);

    DistributionBudget budget{availableSpace, availableSpace / fillApplets, fillApplets};
    budget = computeStep1ForLayout(main, budget, pass);
    if (snapshot.alignment == Alignment::Justify) {
        budget = computeStep1ForLayout(start, budget, pass);
        budget = computeStep1ForLayout(end, budget, pass);
    }

    //! when step 1 served everyone and space remains, exactly one layout -
    //! first of start, then end, then main, the shipped order - runs the
    //! remainder branch so the leftover is not lost
    const bool remainedSpace = budget.applets == 0 && budget.sizePerApplet > 0;

    Step2Mode startMode = Step2Mode::ShareToUnassigned;
    Step2Mode mainMode = Step2Mode::ShareToUnassigned;
    Step2Mode endMode = Step2Mode::ShareToUnassigned;
    if (remainedSpace) {
        if (snapshot.start.fillApplets > 0) {
            startMode = Step2Mode::RemainderToMostDemanding;
        } else if (snapshot.end.fillApplets > 0) {
            endMode = Step2Mode::RemainderToMostDemanding;
        } else if (snapshot.main.fillApplets > 0) {
            mainMode = Step2Mode::RemainderToMostDemanding;
        }
    }

    computeStep2ForLayout(start, budget.sizePerApplet, startMode, pass);
    computeStep2ForLayout(main, budget.sizePerApplet, mainMode, pass);
    computeStep2ForLayout(end, budget.sizePerApplet, endMode, pass);
}

//! the Justify two-sided variant: the free space is computed per side
//! around the centered main grid, step 1 runs main-then-sides, and the
//! second pass re-derives each side's share from the main grid's on-screen
//! length so the sides cannot overlap the center
inline void updateFillAppletsWithTwoSteps(const Snapshot &snapshot, WorkingLayout &start, WorkingLayout &main, WorkingLayout &end, FillPass pass)
{
    const double maxLength = pass == FillPass::MaxLength ? snapshot.contentsMaxLength : snapshot.minLength;
    const int fillApplets = snapshot.start.fillApplets + snapshot.main.fillApplets + snapshot.end.fillApplets;
    Q_ASSERT(fillApplets > 0); //! dispatcher guard

    const double halfMainLayout = snapshot.main.sizeWithNoFillApplets / 2.0;
    double availableSpaceStart = std::max(0.0, maxLength / 2.0 - snapshot.start.sizeWithNoFillApplets - halfMainLayout);
    double availableSpaceEnd = std::max(0.0, maxLength / 2.0 - snapshot.end.sizeWithNoFillApplets - halfMainLayout);

    //! no fill applets in main OR an alignment where all applets are in
    //! main: pool both sides; otherwise use twice the smaller side so the
    //! centered main cannot be overlapped
    double availableSpace = 0;
    if (snapshot.main.fillApplets == 0 || (snapshot.start.shownApplets == 0 && snapshot.end.shownApplets == 0)) {
        availableSpace = availableSpaceStart + availableSpaceEnd - snapshot.main.sizeWithNoFillApplets;
    } else {
        availableSpace = 2.0 * std::min(availableSpaceStart, availableSpaceEnd) - snapshot.main.sizeWithNoFillApplets;
    }

    double sizePerAppletMain = snapshot.main.fillApplets > 0 ? availableSpace / fillApplets : 0;

    int startRemaining = snapshot.start.fillApplets;
    int mainRemaining = snapshot.main.fillApplets;
    int endRemaining = snapshot.end.fillApplets;

    initLayoutForFillsCalculations(start);
    initLayoutForFillsCalculations(main);
    initLayoutForFillsCalculations(end);

    //! first pass
    if (snapshot.main.fillApplets > 0) {
        const DistributionBudget mainBudget = computeStep1ForLayout(main, {availableSpace, sizePerAppletMain, mainRemaining}, pass);
        sizePerAppletMain = mainBudget.sizePerApplet;
        mainRemaining = mainBudget.applets;
        const double dif = (availableSpace - mainBudget.availableSpace) / 2.0;
        availableSpaceStart -= dif;
        availableSpaceEnd -= dif;
    }

    double sizePerAppletStart = snapshot.start.fillApplets > 0 ? availableSpaceStart / startRemaining : 0;
    double sizePerAppletEnd = snapshot.end.fillApplets > 0 ? availableSpaceEnd / endRemaining : 0;

    if (snapshot.start.fillApplets > 0) {
        const DistributionBudget budget = computeStep1ForLayout(start, {availableSpaceStart, sizePerAppletStart, startRemaining}, pass);
        sizePerAppletStart = budget.sizePerApplet;
        startRemaining = budget.applets;
    }
    if (snapshot.end.fillApplets > 0) {
        const DistributionBudget budget = computeStep1ForLayout(end, {availableSpaceEnd, sizePerAppletEnd, endRemaining}, pass);
        sizePerAppletEnd = budget.sizePerApplet;
        endRemaining = budget.applets;
    }

    //! second pass
    if (snapshot.start.fillApplets > 0) {
        if (snapshot.main.fillApplets > 0) {
            //! adjust ALL start fill applets to the space left beside the
            //! main grid's final length
            startRemaining = snapshot.start.fillApplets;
            sizePerAppletStart = (maxLength / 2.0 - snapshot.main.gridLength / 2.0 - snapshot.start.sizeWithNoFillApplets) / startRemaining;
        }
        computeStep2ForLayout(start, sizePerAppletStart, step2ModeForRemaining(startRemaining), pass);
    }

    if (snapshot.end.fillApplets > 0) {
        if (snapshot.main.fillApplets > 0) {
            endRemaining = snapshot.end.fillApplets;
            sizePerAppletEnd = (maxLength / 2.0 - snapshot.main.gridLength / 2.0 - snapshot.end.sizeWithNoFillApplets) / endRemaining;
        }
        computeStep2ForLayout(end, sizePerAppletEnd, step2ModeForRemaining(endRemaining), pass);
    }

    if (snapshot.main.fillApplets > 0) {
        const double halfRemained = maxLength / 2.0 - snapshot.main.gridLength / 2.0;
        const double freeSpaceAfterStart = halfRemained - snapshot.start.gridLength;
        const double freeSpaceBeforeEnd = halfRemained - snapshot.end.gridLength;

        if (freeSpaceAfterStart > 0 && freeSpaceBeforeEnd > 0) {
            const double minimumHalfPossible = std::min(freeSpaceAfterStart, freeSpaceBeforeEnd);
            sizePerAppletMain = std::max(0.0, (minimumHalfPossible * 2.0) / snapshot.main.fillApplets);
            computeStep2ForLayout(main, sizePerAppletMain, step2ModeForRemaining(mainRemaining), pass);
        }
        //! else: the sides already cover the center; main's unserved fill
        //! applets keep their previous values (shipped behavior, pinned by
        //! twostep_main_skip_keeps_previous_values)
    }
}

inline WorkingLayout buildWorkingLayout(const LayoutSnapshot &snapshot, QVector<ItemAssignment> &out)
{
    out.resize(snapshot.items.size());
    WorkingLayout layout;
    layout.reserve(snapshot.items.size());
    for (int i = 0; i < snapshot.items.size(); ++i) {
        WorkingItem item;
        item.source = &snapshot.items[i];
        item.out = &out[i];
        item.currentMaxFillLength = snapshot.items[i].liveMaxFillLength;
        item.currentMinFillLength = snapshot.items[i].liveMinFillLength;
        layout.append(item);
    }
    return layout;
}

} // namespace detail

//! Runs both dependent passes (max then min - the order matters, see the
//! header block) over one consistent snapshot and returns what to write
//! into each item's maxAutoFillLength/minAutoFillLength. Items with empty
//! optionals were untouched and keep their previous values.
inline Assignments distributeFillLengths(const Snapshot &snapshot)
{
    Q_ASSERT(snapshot.start.fillApplets >= 0 && snapshot.main.fillApplets >= 0 && snapshot.end.fillApplets >= 0);

    Assignments assignments;
    detail::WorkingLayout start = detail::buildWorkingLayout(snapshot.start, assignments.start);
    detail::WorkingLayout main = detail::buildWorkingLayout(snapshot.main, assignments.main);
    detail::WorkingLayout end = detail::buildWorkingLayout(snapshot.end, assignments.end);

    const int fillApplets = snapshot.start.fillApplets + snapshot.main.fillApplets + snapshot.end.fillApplets;
    if (fillApplets == 0) {
        return assignments;
    }

    //! Justify with a populated main grid distributes two-sided; every
    //! other configuration is a single pool
    const bool onePool = snapshot.main.shownApplets == 0 || snapshot.alignment != Alignment::Justify;

    if (onePool) {
        detail::updateFillAppletsWithOneStep(snapshot, start, main, end, FillPass::MaxLength);
        detail::updateFillAppletsWithOneStep(snapshot, start, main, end, FillPass::MinLength);
    } else {
        detail::updateFillAppletsWithTwoSteps(snapshot, start, main, end, FillPass::MaxLength);
        detail::updateFillAppletsWithTwoSteps(snapshot, start, main, end, FillPass::MinLength);
    }

    return assignments;
}

} // namespace FillDistributor
} // namespace Latte

#endif
