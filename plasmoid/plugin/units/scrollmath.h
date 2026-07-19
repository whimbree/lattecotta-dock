/*
    SPDX-FileCopyrightText: 2019 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef SCROLLMATH_H
#define SCROLLMATH_H

// Qt
#include <QtGlobal>

// C++
#include <cmath>
#include <optional>

namespace Latte {
namespace ScrollMath {

//! Task-row overflow scrolling math (EX-21 in docs/tracking/QML_EXTRACTION_PLAN.md):
//! the overflow predicate, the clamped scroll positions, the scroll-into-view
//! distance and the autoscroll trigger-zone decision that lived as JS bodies
//! in ScrollableList.qml. Latte-authored, NOT part of the plasma-desktop
//! vendored set in plasmoid/plugin (the QML ancestor is Michail's Qt5
//! ScrollableList, hence his line above).
//!
//! One axis: the shell resolves the orientation (contentX vs contentY,
//! point.x vs point.y, width vs height) and hands the core axis-scalar
//! values, so none of the functions branch on vertical - the Qt5 bodies'
//! orientation twins were coordinate selection around identical math.
//! Positions and deltas are SIGNED along the scroll axis: positive scrolls
//! toward the row end (Qt5 increasePosWithStep), negative toward the row
//! start (decreasePosWithStep). "No scroll" is std::nullopt, never a zero
//! delta - the QML boundary alone converts nullopt to undefined.

//! One axis of the scrollable task row, snapshotted by the shell.
struct ScrollEnv {
    //! the scrollTasksEnabled setting; overflow is only ever acted on when
    //! scrolling is enabled (Qt5 gated contentsExceed on it)
    bool scrollingEnabled = false;
    //! root.tasksLength: the full task row's length (real - it tracks the
    //! layout's live width/height)
    double contentLength = 0.0;
    //! the flickable's scroll-axis extent. Qt5 read `length` for the clamp
    //! bounds and scrollableList.width/height for the focus/trigger
    //! comparisons; main.qml binds the scroll-axis extent FROM length
    //! (width: !root.vertical ? length : thickness, height mirrored), so
    //! the two reads are one value and the core takes it once. An int
    //! because the QML `length` property is int.
    int viewportLength = 0;
    //! contentX or contentY, resolved by the shell
    double currentPos = 0.0;
};

//! Whether the task row overflows the viewport. Math.floor kept the Qt5
//! comparison in the int world (a fractional sliver beyond the viewport is
//! not overflow); std::floor pins that exactly.
inline bool contentsExceed(const ScrollEnv &env)
{
    Q_ASSERT(env.contentLength >= 0.0);
    Q_ASSERT(env.viewportLength >= 0);

    return env.scrollingEnabled && std::floor(env.contentLength) > env.viewportLength;
}

//! How much content lies beyond the viewport - the scrollable range, and
//! therefore the last valid scroll position (Qt5 scrollLastPos). Qt5
//! assigned the real difference to an int QML property: ECMAScript ToInt32
//! truncation toward zero, pinned here as the int cast. When the row
//! overflows the difference is >= 1 (floor(content) > viewport implies
//! content >= viewport + 1), so the result is never negative.
inline int contentsExtraSpace(const ScrollEnv &env)
{
    return contentsExceed(env) ? static_cast<int>(env.contentLength - env.viewportLength) : 0;
}

//! The wheel-scroll step (Qt5 scrollStep: 3.5 icon slots per wheel tick).
//! The same factor is the autoscroll step multiple while a scroll animation
//! is already running (autoScrollDelta below) - it lives once here.
inline constexpr double kScrollStepLengthFactor = 3.5;

inline double wheelScrollStep(double totalsLength)
{
    Q_ASSERT(totalsLength >= 0.0);

    return kScrollStepLengthFactor * totalsLength;
}

//! The scroll position after a signed step, clamped Qt5's asymmetric way:
//! a positive (toward-end) step clamps at the last position ONLY and a
//! negative (toward-start) step at the first position ONLY - exactly the
//! increasePosWithStep/decreasePosWithStep pair, where each direction never
//! clamped the other bound.
inline double steppedPos(const ScrollEnv &env, double signedStep)
{
    const double lastPos = contentsExtraSpace(env);

    return signedStep >= 0.0 ? std::min(lastPos, env.currentPos + signedStep)
                             : std::max(0.0, env.currentPos + signedStep);
}

//! Where an item sits along the scroll axis, in viewport coordinates (the
//! shell's task.mapToItem(scrollableList, 0, 0) read).
struct ItemSpan {
    double start = 0.0;
    double length = 0.0;
};

//! The signed step that brings an off-screen item into view with `margin`
//! (the icon size) of breathing room, or nullopt when the item is already
//! visible or the row does not overflow (Qt5 focusOn). The caller applies
//! it through steppedPos, which clamps at the bounds like Qt5's
//! increase/decreasePosWithStep calls did.
inline std::optional<double> focusScrollDelta(const ScrollEnv &env, const ItemSpan &item, double margin)
{
    Q_ASSERT(item.length >= 0.0);
    Q_ASSERT(margin >= 0.0);

    if (!contentsExceed(env)) {
        return std::nullopt;
    }

    if (item.start < 0.0) {
        return -std::abs(item.start - margin);
    }

    if (item.start + item.length > env.viewportLength) {
        return std::abs(item.start - env.viewportLength + item.length + margin);
    }

    return std::nullopt;
}

//! The hover-time facts that can block autoscrolling (Qt5 autoScrollFor's
//! guard row).
struct AutoScrollGuards {
    //! the autoScrollTasksEnabled setting; a drag in progress overrides it
    bool autoScrollTasksEnabled = false;
    bool duringDragging = false;
    int tasksCount = 0;
    //! last task with parabolic effect breaks the autoscrolling behavior
    //! (Qt5's comment): hovering the last visible item while zoomed blocks
    bool hoveredIsLastVisibleItem = false;
    double parabolicZoomFactor = 1.0;
};

//! The signed autoscroll step when the hovered item sits inside a trigger
//! zone at either viewport edge, or nullopt when blocked, not overflowing,
//! outside the zones, or already at the boundary the zone points to.
//!
//! Qt5 carried this body with two IN-QUESTION issues its initial checks may
//! already solve: (1) wheeling at the first/last task, autoscroll could
//! forcefully return the view to the boundary; (2) parabolic zoom plus
//! autoscroll at the boundaries could break each other's animations. The
//! boundary equalities below (currentPos vs first/last) are those checks.
//!
//! The exact float equalities are deliberate and Qt5-faithful (!== in JS):
//! currentPos only ever reaches the boundary values by being clamped to
//! exactly them (steppedPos/boundsCorrection), so exact comparison is the
//! meaningful "already at the boundary" test, not a float-comparison bug.
//!
//! While a scroll animation is still running the step is 3.5 slots instead
//! of one, so a held hover keeps pace with the animated position.
inline std::optional<double> autoScrollDelta(const ScrollEnv &env, const ItemSpan &item,
                                             double triggerZone, const AutoScrollGuards &guards,
                                             bool scrollAnimationRunning, double totalsLength)
{
    Q_ASSERT(item.length >= 0.0);
    Q_ASSERT(triggerZone >= 0.0);
    Q_ASSERT(totalsLength >= 0.0);
    Q_ASSERT(guards.tasksCount >= 0);

    const bool blocked = !guards.autoScrollTasksEnabled && !guards.duringDragging;

    if (blocked || !contentsExceed(env) || guards.tasksCount < 3
            || (guards.hoveredIsLastVisibleItem && guards.parabolicZoomFactor > 1.0)) {
        return std::nullopt;
    }

    const double step = scrollAnimationRunning ? kScrollStepLengthFactor * totalsLength
                                               : totalsLength;
    const double lastPos = contentsExtraSpace(env);

    if (env.currentPos != 0.0 && item.start < triggerZone) {
        return -step;
    }

    if (env.currentPos != lastPos && item.start + item.length > env.viewportLength - triggerZone) {
        return step;
    }

    return std::nullopt;
}

//! The corrected position when the current one fell out of the (possibly
//! just shrunk) valid range, or nullopt when it is already in bounds - so
//! the shell only writes contentX/contentY when Qt5 wrote (an in-bounds
//! rewrite would needlessly run the write through the Behavior animation).
//! This is Qt5's onContentsExtraSpaceChanged clamp.
inline std::optional<double> boundsCorrection(const ScrollEnv &env)
{
    const double lastPos = contentsExtraSpace(env);

    if (env.currentPos < 0.0) {
        return 0.0;
    }

    if (env.currentPos > lastPos) {
        return lastPos;
    }

    return std::nullopt;
}

}
}

#endif
