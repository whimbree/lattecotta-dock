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
#include <algorithm>
#include <cmath>
#include <optional>
#include <variant>

namespace Latte {

//! The automatic icon-size search as a pure step function (EX-04 in
//! docs/tracking/QML_EXTRACTION_PLAN.md), extracted from the containment ability
//! AutoSize.qml and code/autosize.js. One call decides one pass: shrink
//! when the settled applet row overflows the available length, grow when the
//! row plus one icon's incremental hover growth fits, otherwise keep the
//! current size. Temporary hover zoom does not make a fitting resting row
//! shrink. The QML shell
//! keeps its timers, gates and property bindings and asks here for
//! decisions only.
//!
//! The Qt5 ancestor searched in 8px steps. Besides its historical
//! non-termination defect, that made the result depend on the configured
//! ceiling modulo 8 and could leave seven usable pixels of icon size
//! untested. The current solver calculates the largest fitting integer size
//! directly. This makes termination structural and gives identical geometry
//! the same result regardless of the configured ceiling.
namespace AutoSizeEngine {

//! the search never proposes anything below this floor
inline constexpr int minIconSize = 16;

//! Keep one logical pixel clear at each primary-axis end after accounting
//! for incremental hover growth. This absorbs rounding at the strict grow
//! boundary without double-counting the icon already present in the row.
inline constexpr double zoomedGrowSlack = 2.0;
static_assert(zoomedGrowSlack > 0.0,
              "zoomed growth needs a positive boundary-rounding margin");

constexpr double zoomedGrowLimit(double maxLength, double zoomFactor) noexcept
{
    return maxLength - (zoomFactor > 1.0 ? zoomedGrowSlack : 0.0);
}
static_assert(zoomedGrowLimit(1228.0, 1.8) == 1226.0,
              "zoomed growth must reserve only the two-pixel end slack");

//! The resting row already contains the base icon. Only the extra extent
//! introduced by hover zoom belongs in the growth fit calculation.
constexpr double incrementalZoomLengthAtIconSize(int iconSize, double zoomFactor) noexcept
{
    return static_cast<double>(iconSize) * (zoomFactor - 1.0);
}
static_assert(incrementalZoomLengthAtIconSize(50, 1.8) > 39.99
              && incrementalZoomLengthAtIconSize(50, 1.8) < 40.01,
              "hover occupancy must count only growth beyond the base icon");

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

//! Project a measured layout linearly to an integer candidate icon size.
//! This is constexpr so table tests and later callers can verify fixed
//! geometry at compile time when all inputs are constants.
constexpr double projectLengthAtIconSize(int candidateIconSize, int currentIconSize,
                                         double layoutLength) noexcept
{
    return (static_cast<double>(candidateIconSize) / currentIconSize) * layoutLength;
}

//! One shrink pass: calculate the largest integer size whose projection is
//! at or below toShrinkLimit. A floor exit may remain over an unsatisfiable
//! limit. currentIconSize is the size at which layoutLength was measured.
inline ShrinkResult shrinkToLargestFittingSize(int maxIconSize, int currentIconSize,
                                               double layoutLength, double toShrinkLimit)
{
    Q_ASSERT(maxIconSize >= 1); //! a non-positive ceiling is a caller bug
    Q_ASSERT(currentIconSize >= 1); //! projection divides by it

    const int largestCandidate = std::max(minIconSize, maxIconSize - 1);
    int fittingSize = minIconSize;

    if (layoutLength > 0.0 && toShrinkLimit >= 0.0) {
        const double continuousFit = (toShrinkLimit * currentIconSize) / layoutLength;
        const double boundedFit = std::clamp(continuousFit,
                                             static_cast<double>(minIconSize),
                                             static_cast<double>(largestCandidate));
        fittingSize = static_cast<int>(boundedFit);

        //! The quotient can land a few ulps beside an integer boundary.
        //! Correct against the actual projection so <= remains the contract.
        if (fittingSize < largestCandidate
            && projectLengthAtIconSize(fittingSize + 1, currentIconSize, layoutLength)
                    <= toShrinkLimit) {
            ++fittingSize;
        } else if (fittingSize > minIconSize
                   && projectLengthAtIconSize(fittingSize, currentIconSize, layoutLength)
                           > toShrinkLimit) {
            --fittingSize;
        }

        Q_ASSERT(fittingSize == minIconSize
                 || projectLengthAtIconSize(fittingSize, currentIconSize, layoutLength)
                         <= toShrinkLimit);
        Q_ASSERT(fittingSize == largestCandidate
                 || projectLengthAtIconSize(fittingSize + 1, currentIconSize, layoutLength)
                         > toShrinkLimit);
    }

    return ShrinkResult{fittingSize,
                        projectLengthAtIconSize(fittingSize, currentIconSize, layoutLength)};
}

//! result of one grow pass: the largest fitting size and its projection
struct GrowResult {
    int iconSize;
    double projectedLength;
};

//! One grow pass: calculate the largest integer size whose projection stays
//! strictly below toGrowLimit. Absence means no size above currentIconSize
//! fits. Reaching maxIconSize is represented by a present result so step()
//! can restore automatic sizing.
inline std::optional<GrowResult> growToLargestFittingSize(int maxIconSize, int currentIconSize,
                                                         double layoutLength, double toGrowLimit)
{
    Q_ASSERT(maxIconSize >= 1); //! a non-positive ceiling would "apply" size 0
    Q_ASSERT(currentIconSize >= 1); //! projection divides by it

    const double ceilingProjection = projectLengthAtIconSize(maxIconSize,
                                                             currentIconSize,
                                                             layoutLength);
    if (ceilingProjection < toGrowLimit) {
        return GrowResult{maxIconSize, ceilingProjection};
    }

    if (layoutLength <= 0.0 || toGrowLimit <= 0.0 || maxIconSize <= currentIconSize) {
        return std::nullopt;
    }

    const double exclusiveContinuousFit = (toGrowLimit * currentIconSize) / layoutLength;
    const double boundedFit = std::clamp(std::ceil(exclusiveContinuousFit) - 1.0,
                                         static_cast<double>(currentIconSize),
                                         static_cast<double>(maxIconSize));
    int fittingSize = static_cast<int>(boundedFit);

    //! As in shrinkToLargestFittingSize(), correct quotient rounding against
    //! the actual strict comparison rather than trusting the division.
    if (fittingSize > currentIconSize
        && projectLengthAtIconSize(fittingSize, currentIconSize, layoutLength)
                >= toGrowLimit) {
        --fittingSize;
    } else if (fittingSize < maxIconSize
               && projectLengthAtIconSize(fittingSize + 1, currentIconSize, layoutLength)
                       < toGrowLimit) {
        ++fittingSize;
    }

    if (fittingSize <= currentIconSize) {
        return std::nullopt;
    }

    Q_ASSERT(projectLengthAtIconSize(fittingSize, currentIconSize, layoutLength)
             < toGrowLimit);
    Q_ASSERT(fittingSize == maxIconSize
             || projectLengthAtIconSize(fittingSize + 1, currentIconSize, layoutLength)
                     >= toGrowLimit);

    return GrowResult{fittingSize,
                      projectLengthAtIconSize(fittingSize, currentIconSize, layoutLength)};
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
    //! the size layoutLength was measured at (metrics.iconSize)
    int currentIconSize;
    //! the configured ceiling (metrics.maxIconSize)
    int maxIconSize;
    //! parabolic zoom factor; growth reserves only the icon's incremental
    //! zoom extent plus the small end slack, not the full hovered item
    double zoomFactor;
    //! the search's own last applied size; absent means automatic sizing
    //! (the shell's -1). Growing is only allowed from a size this search
    //! applied itself, never past automatic.
    std::optional<int> appliedIconSize;
};

//! One pass of the automatic icon-size search over a measured layout
//! snapshot. Shrink when the row overflows the shrink limit (always
//! applies and records its prediction); grow when the row sits under the
//! slightly tighter grow limit AND the current size is the search's
//! own applied size (applies only when a larger integer size fits and the
//! history's endless-loop protector stays quiet); keep the current size
//! otherwise. Shrinking uses the full settled-row budget, so a transient
//! hover cannot make the persistent layout smaller. Growth accounts for the
//! incremental zoom extent and leaves one logical pixel at each end. The
//! prediction history remains the structural protection against a measured
//! shrink/grow cycle.
inline AutoSizeStep step(const AutoSizeInput &input, History &history)
{
    Q_ASSERT(input.maxLength > 0); //! the QML shell's <= 0 contract keeps this out
    Q_ASSERT(input.currentIconSize >= 1); //! projection divides by it
    Q_ASSERT(input.maxIconSize >= 1); //! a non-positive ceiling is a caller bug

    const double toShrinkLimit = input.maxLength;
    const double toGrowLimit = zoomedGrowLimit(input.maxLength, input.zoomFactor);

    if (input.layoutLength > toShrinkLimit) {
        //! must shrink
        const ShrinkResult shrunk = shrinkToLargestFittingSize(input.maxIconSize,
                                                               input.currentIconSize,
                                                               input.layoutLength,
                                                               toShrinkLimit);

        history.addPrediction(qRound(input.layoutLength), qRound(shrunk.projectedLength));
        return ApplySize{shrunk.iconSize};
    }

    const bool atOwnAppliedSize = (input.appliedIconSize == input.currentIconSize);

    if (input.layoutLength < toGrowLimit && atOwnAppliedSize) {
        //! must grow probably
        const double zoomedLengthAtCurrentSize = input.layoutLength
                + incrementalZoomLengthAtIconSize(input.currentIconSize, input.zoomFactor);
        const std::optional<GrowResult> grown = growToLargestFittingSize(input.maxIconSize,
                                                                         input.currentIconSize,
                                                                         zoomedLengthAtCurrentSize,
                                                                         toGrowLimit);

        if (!grown) {
            return KeepCurrent{};
        }

        //! predictions are recorded rounded; layout lengths are never
        //! negative here, where JS Math.round and qRound agree
        const int intLength = qRound(input.layoutLength);
        const int intNextLength = qRound(projectLengthAtIconSize(grown->iconSize,
                                                                 input.currentIconSize,
                                                                 input.layoutLength));

        if (!history.producesEndlessLoop(intLength, intNextLength)) {
            history.addPrediction(intLength, intNextLength);

            if (grown->iconSize == input.maxIconSize) {
                return RestoreAutomaticMax{};
            }

            return ApplySize{grown->iconSize};
        }
    }

    return KeepCurrent{};
}

}
}

#endif
