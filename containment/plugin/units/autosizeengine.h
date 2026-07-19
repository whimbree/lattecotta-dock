/*
    SPDX-FileCopyrightText: 2019 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef AUTOSIZEENGINE_H
#define AUTOSIZEENGINE_H

// Qt
#include <QDebug>
#include <QVector>
#include <QtGlobal>

// C++
#include <optional>
#include <variant>

namespace Latte {

//! The automatic icon-size search as a pure step function (EX-04 in
//! docs/tracking/QML_EXTRACTION_PLAN.md), extracted from the containment ability
//! AutoSize.qml and code/autosize.js. One call decides one pass: shrink
//! when the applet row overflows the available length, grow when it fits
//! with room to spare, otherwise keep the current size. The QML shell
//! keeps its timers, gates and property bindings and asks here for
//! decisions only.
//!
//! Termination is the whole point of the extraction: the Qt5 ancestor's
//! loops (f0ad7b23) exited on the equalities nextIconSize !== 16 /
//! !== maxIconSize, so any size not congruent to the bound modulo the
//! step (the live iconSize=78 case) stepped past it and spun forever,
//! starving the GUI thread at 100% CPU. Both loops here carry ad9b823f's
//! deliberate deviation from Qt5: clamp to the bound, exit on inequality.
namespace AutoSizeEngine {

//! step size of the search, from AutoSize.qml's automaticStep
inline constexpr int automaticStep = 8;

//! the search never proposes anything below this floor
inline constexpr int minIconSize = 16;

//! history ring bounds: past historyMaxSize entries the ring is cut back
//! to the newest historyMinSize
inline constexpr int historyMinSize = 4;
inline constexpr int historyMaxSize = 10;
static_assert(0 < historyMinSize && historyMinSize < historyMaxSize,
              "the ring truncates to historyMinSize, which must keep at least "
              "the two entries producesEndlessLoop() reads");

//! one recorded pass: the layout length the pass measured and the length
//! it predicted its proposal would produce
struct Prediction {
    int currentLength;
    int predictedLength;
};

//! Newest-first bounded record of the search's predictions, so repeating
//! shrink/grow cycles are detectable. Value type; the QML shell's wrapper
//! owns one instance per sizer.
class History
{
public:
    void clear()
    {
        m_predictions.clear();
    }

    //! newest entry first; past historyMaxSize the ring is cut back to the
    //! newest historyMinSize entries (AutoSize.qml's unshift + splice)
    void addPrediction(int currentLength, int predictedLength)
    {
        m_predictions.prepend(Prediction{currentLength, predictedLength});

        if (m_predictions.size() > historyMaxSize) {
            m_predictions.resize(historyMinSize);
        }
    }

    //! true when the incoming grow prediction repeats the one from two
    //! passes ago and the pass between them was a shrink undoing it: the
    //! grow would re-arm the same shrink and the pair would cycle forever
    bool producesEndlessLoop(int currentLength, int predictedLength) const
    {
        if (m_predictions.size() < 2) {
            return false;
        }

        const bool repeatsTwoPassesAgo = (m_predictions[1].currentLength == currentLength)
                && (m_predictions[1].predictedLength == predictedLength);

        if (!repeatsTwoPassesAgo) {
            return false;
        }

        const bool lastPassShrank = m_predictions[0].currentLength > m_predictions[0].predictedLength;
        const bool passBeforeGrew = m_predictions[1].currentLength < m_predictions[1].predictedLength;

        if (lastPassShrank && passBeforeGrew) {
            //! upstream's protector announcement, kept verbatim: this firing
            //! is the only trace the suppressed oscillation leaves
            qDebug() << " AUTOMATIC ITEM SIZE PROTECTOR, :: ENDLESS AUTOMATIC SIZE LOOP DETECTED";
            return true;
        }

        return false;
    }

    int size() const
    {
        return static_cast<int>(m_predictions.size());
    }

    const Prediction &at(int i) const
    {
        return m_predictions.at(i);
    }

private:
    QVector<Prediction> m_predictions;
};

//! result of one shrink pass: the size to apply and the layout length it
//! projects (shrink always applies; only the floor can leave the limit
//! unsatisfied)
struct ShrinkResult {
    int iconSize;
    double projectedLength;
};

//! One shrink pass: step candidate sizes down from maxIconSize until the
//! projected layout length fits in toShrinkLimit or the floor is reached.
//! currentIconSize is the size the present layoutLength was measured at,
//! so factor = candidate / currentIconSize projects the length.
inline ShrinkResult shrinkUntilFits(int maxIconSize, int currentIconSize,
                                    double layoutLength, double toShrinkLimit)
{
    Q_ASSERT(maxIconSize >= 1); //! a non-positive ceiling is a caller bug
    Q_ASSERT(currentIconSize >= 1); //! projection divides by it

    int nextIconSize = maxIconSize;
    double nextLength = 0.0;

    do {
        nextIconSize = qMax(minIconSize, nextIconSize - automaticStep);
        const double factor = static_cast<double>(nextIconSize) / currentIconSize;
        nextLength = factor * layoutLength;

    } while ((nextLength > toShrinkLimit) && (nextIconSize > minIconSize));

    return ShrinkResult{nextIconSize, nextLength};
}

//! result of one grow pass: the largest stepped size whose projection
//! stayed under the limit (nothing fit when absent, the shipped JS's -1),
//! and the projection of the last candidate tried
struct GrowResult {
    std::optional<int> goodSize;
    double projectedLength;
};

//! One grow pass: step candidate sizes up from currentIconSize as long as
//! the projected length stays under toGrowLimit, saturating at maxIconSize.
inline GrowResult growWhileFits(int maxIconSize, int currentIconSize,
                                double layoutLength, double toGrowLimit)
{
    Q_ASSERT(maxIconSize >= 1); //! a non-positive ceiling would "apply" size 0
    Q_ASSERT(currentIconSize >= 1); //! projection divides by it

    int nextIconSize = currentIconSize;
    std::optional<int> goodSize;
    double nextLength = 0.0;

    do {
        nextIconSize = qMin(maxIconSize, nextIconSize + automaticStep);
        const double factor = static_cast<double>(nextIconSize) / currentIconSize;
        nextLength = factor * layoutLength;

        if (nextLength < toGrowLimit) {
            goodSize = nextIconSize;
        }

    } while ((nextLength < toGrowLimit) && (nextIconSize < maxIconSize));

    return GrowResult{goodSize, nextLength};
}

//! this pass proposes no new size
struct KeepCurrent {
};

//! this pass proposes a concrete size (a shrink, or a grow short of the
//! ceiling)
struct ApplySize {
    int iconSize;
};

//! a grow reached maxIconSize: the constraint is gone and the sizer
//! returns to automatic sizing (the QML shell's iconSize = -1 sentinel
//! exists only at its boundary, mapped from this alternative)
struct RestoreAutomaticMax {
};

using AutoSizeStep = std::variant<KeepCurrent, ApplySize, RestoreAutomaticMax>;

struct AutoSizeInput {
    //! current laid-out length of the applet rows
    double layoutLength;
    //! available length for the view; must be > 0. The no-geometry <= 0
    //! early-return contract (ad9b823f: early wayland startup calls arrive
    //! before the window is sized) stays in the QML shell, which owns the
    //! onMaxLengthChanged re-run; reaching the core with <= 0 is a shell
    //! bug and asserts.
    double maxLength;
    //! one item's total length at currentIconSize (metrics.totals.length)
    double itemLength;
    //! the size layoutLength was measured at (metrics.iconSize)
    int currentIconSize;
    //! the configured ceiling (metrics.maxIconSize)
    int maxIconSize;
    //! parabolic zoom factor; the limits reserve room for a zoomed item
    double zoomFactor;
    //! the search's own last applied size; absent means automatic sizing
    //! (the shell's -1). Growing is only allowed from a size this search
    //! applied itself, never past automatic.
    std::optional<int> appliedIconSize;
};

//! One pass of the automatic icon-size search over a measured layout
//! snapshot. Shrink when the row overflows the shrink limit (always
//! applies and records its prediction); grow when the row sits under the
//! deliberately tighter grow limit AND the current size is the search's
//! own applied size (applies only when a stepped size fits and the
//! history's endless-loop protector stays quiet); keep the current size
//! otherwise. The band between the two limits is upstream's robustness
//! margin: the grow limit reserves 1.2x the zoomed item so early
//! calculations cannot oscillate between a shrink and the grow that
//! re-arms it.
inline AutoSizeStep step(const AutoSizeInput &input, History &history)
{
    Q_ASSERT(input.maxLength > 0); //! the QML shell's <= 0 contract keeps this out
    Q_ASSERT(input.currentIconSize >= 1); //! projection divides by it
    Q_ASSERT(input.maxIconSize >= 1); //! a non-positive ceiling is a caller bug

    const double toShrinkLimit = input.maxLength - (input.zoomFactor * input.itemLength);
    const double toGrowLimit = input.maxLength - (1.2 * input.zoomFactor * input.itemLength);

    if (input.layoutLength > toShrinkLimit) {
        //! must shrink
        const ShrinkResult shrunk = shrinkUntilFits(input.maxIconSize, input.currentIconSize,
                                                    input.layoutLength, toShrinkLimit);

        history.addPrediction(qRound(input.layoutLength), qRound(shrunk.projectedLength));
        return ApplySize{shrunk.iconSize};
    }

    const bool atOwnAppliedSize = (input.appliedIconSize == input.currentIconSize);

    if (input.layoutLength < toGrowLimit && atOwnAppliedSize) {
        //! must grow probably
        const GrowResult grown = growWhileFits(input.maxIconSize, input.currentIconSize,
                                               input.layoutLength, toGrowLimit);

        //! predictions are recorded rounded; layout lengths are never
        //! negative here, where JS Math.round and qRound agree
        const int intLength = qRound(input.layoutLength);
        const int intNextLength = qRound(grown.projectedLength);

        if (grown.goodSize && !history.producesEndlessLoop(intLength, intNextLength)) {
            history.addPrediction(intLength, intNextLength);

            if (*grown.goodSize == input.maxIconSize) {
                return RestoreAutomaticMax{};
            }

            return ApplySize{*grown.goodSize};
        }
    }

    return KeepCurrent{};
}

}
}

#endif
