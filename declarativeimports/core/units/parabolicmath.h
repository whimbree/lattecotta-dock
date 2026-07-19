/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PARABOLICMATH_H
#define PARABOLICMATH_H

// Qt
#include <QVector>
#include <QtGlobal>

// C++
#include <utility>

namespace Latte {

//! The parabolic zoom curve as pure functions (EX-03 in
//! docs/tracking/QML_EXTRACTION_PLAN.md), extracted from the abilities definition
//! ParabolicEffect.qml and algebraically identical to the Qt5 ancestor's
//! linearEffect (f0ad7b23). The QML definition keeps its signal emissions
//! and asks here for numbers only; EX-02's router consumes the same
//! stacks.
namespace ParabolicMath {

//! the linear zoom slope y = (zoom - 1) * x
inline double scaleLinear(double zoomFactor, double x)
{
    return (zoomFactor - 1.0) * x;
}

//! scale for one neighbor slice: the x axis is split into itemsCount
//! slices, the slice's own sub-position comes from the mouse percentage,
//! and the scale is 1 + the slope at that x. itemsCount must be >= 1
//! (computeScales' loop never calls it otherwise; asserted because an
//! itemsCount of 0 is representable and would divide silently - IEEE
//! float division never traps, so no sanitizer catches it).
inline double scaleForItem(double zoomFactor, double mousePosPercentage, int itemIndex, int itemsCount)
{
    Q_ASSERT(itemsCount >= 1);
    const double xSliceLength = 1.0 / itemsCount;
    const double minX = (itemIndex - 1) * xSliceLength;
    const double maxX = itemIndex * xSliceLength;
    const double curX = minX + (maxX - minX) * mousePosPercentage;
    return 1.0 + scaleLinear(zoomFactor, curX);
}

//! the two neighbor scale stacks: nearest neighbor first (largest scale),
//! descending outwards, a trailing 1.0 clearing entry that terminates the
//! row propagation at the first untouched item
struct ScaleStacks {
    QVector<double> left;
    QVector<double> right;
};

//! per-item scale stacks from the pointer position within the hovered
//! item. mousePosPercent is clamped into [0,1] here (pointer coordinates
//! can overshoot the item edges); reversed swaps the stacks wholesale
//! (RTL horizontal rows - the caller decides, it owns the layout
//! direction read).
inline ScaleStacks computeScales(double mousePosPercent, int spreadSteps, double zoomFactor, bool reversed)
{
    const double percentage = qBound(0.0, mousePosPercent, 1.0);

    ScaleStacks stacks;
    stacks.left.reserve(spreadSteps + 1);
    stacks.right.reserve(spreadSteps + 1);

    for (int i = spreadSteps; i >= 1; --i) {
        stacks.left.append(scaleForItem(zoomFactor, 1.0 - percentage, i, spreadSteps));
    }
    stacks.left.append(1.0); //! clearing

    for (int j = spreadSteps; j >= 1; --j) {
        stacks.right.append(scaleForItem(zoomFactor, percentage, j, spreadSteps));
    }
    stacks.right.append(1.0); //! clearing

    if (reversed) {
        std::swap(stacks.left, stacks.right);
    }

    return stacks;
}

}
}

#endif
