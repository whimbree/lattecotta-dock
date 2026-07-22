/*
    SPDX-FileCopyrightText: 2018 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef LENGTHOFFSETCLAMP_H
#define LENGTHOFFSETCLAMP_H

// local
#include "placementstate.h"

// Qt
#include <QtGlobal>

// C++
#include <algorithm>
#include <cmath>

namespace Latte {
namespace Settings {

//! The mutual maxLength/minLength/offset clamp that keeps a dock
//! on-screen (EX-18 in docs/tracking/QML_EXTRACTION_PLAN.md), deduplicated out of
//! two Qt5-inherited QML copies that edited the same three config keys
//! and had to agree or corrupt them: the maxLength ruler
//! (shell .../canvas/maxlength/RulerMouseArea.qml) and the Appearance
//! page's slider cluster (shell .../pages/AppearanceConfig.qml). Both
//! f0ad7b23 ancestors were read before extraction and carried identical
//! math (docs/agent-logs/EX-18.md is the arbitration record); equal
//! behavior wins, one authority remains here.
//!
//! Units: everything is in the percent units the config keys use -
//! maxLength/minLength as percent of the screen edge, offset as percent
//! displacement from the alignment anchor (sign carries the direction
//! for centered alignments).
//!
//! Preconditions: inputs must be finite. Q_ASSERT tripwires here fail at
//! the defect under test builds (QT_FORCE_ASSERTS); the QML boundary
//! that can receive a hand-edited config refuses non-finite input loudly
//! at runtime in LengthOffsetClampBridge instead - an assert is never
//! the input handling.
namespace LengthOffsetClamp {

//! the clamp math distinguishes three alignment kinds; WHICH edge is
//! deliberately unrepresentable here because no branch reads it. Center
//! and Justify share the same on-screen GEOMETRY (a symmetric offset that
//! can push either half past the screen middle), so the offset math treats
//! them alike; Justify is a distinct value only because its Maximum is NOT
//! floored by minLength (D17, maximumIsFlooredByMinimum below). The bridge
//! maps the raw Latte::Types::Alignment int totally: Justify -> Justify,
//! Center -> Centered, every other value -> Edge (the same predicate the
//! shipped QML applied with ===, split so the floor can tell Justify apart).
enum class Alignment {
    Edge,
    Centered,
    Justify
};

//! Center and Justify share on-screen GEOMETRY: every offset/overflow
//! branch treats them identically. Only the minLength floor tells them
//! apart, so the geometry predicate is named separately from the enum
//! comparison to keep that single distinction legible at each call site.
constexpr bool hasCenteredGeometry(Alignment alignment)
{
    return alignment == Alignment::Centered || alignment == Alignment::Justify;
}

//! the stored Minimum length floors the Maximum for every alignment EXCEPT
//! Justify. A Justify dock has no independent minimum (its Minimum slider
//! is disabled for Justify in AppearanceConfig.qml), so flooring Maximum by
//! a frozen, un-editable leftover minLength strands Maximum above it (D17).
//!
//! DEVIATION from Qt5, recorded deliberately (CLAUDE.md Qt5-faithful rule):
//! Qt5 Latte floored Maximum by minLength UNCONDITIONALLY - the ruler body's
//! `value = Math.max(minLength, value)` and the slider's
//! `Math.max(value, minLength, localMinValue)` are both alignment-blind
//! (verified in latte-dock-qt6 RulerMouseArea.qml:47 and AppearanceConfig
//! .qml:274, 2026-07-18) - so a Justify dock there could not lower Maximum
//! below a stale minLength either. This maintained continuation treats that
//! as an upstream defect and diverges: Justify's effective minimum is 0. The
//! Minimum-slider-disabled-for-Justify half stays Qt5-faithful; only the
//! floor becomes alignment-aware.
constexpr bool maximumIsFlooredByMinimum(Alignment alignment)
{
    return alignment != Alignment::Justify;
}

struct LengthState {
    double maxLength;
    double minLength;
    double offset;
};

struct OffsetBounds {
    double from;
    double to;
};

//! clampOffset's result: the offset slider never touches minLength, so
//! its result type cannot claim it did
struct OffsetClamp {
    double maxLength;
    double offset;
};

inline PlacementState::Alignment placementAlignmentOf(Alignment alignment)
{
    switch (alignment) {
    case Alignment::Edge:
        return PlacementState::Alignment::Start;
    case Alignment::Centered:
        return PlacementState::Alignment::Center;
    case Alignment::Justify:
        return PlacementState::Alignment::Justify;
    }

    Q_UNREACHABLE_RETURN(PlacementState::Alignment::Center);
}

//! Route a complete length state through the placement invariant. Bottom is a
//! representative edge here because percent bounds depend on semantic
//! alignment, not axis; the containment boundary supplies the real edge when
//! translating Start/End back to physical alignment names.
inline LengthState normalizePlacement(LengthState state, Alignment alignment)
{
    const auto placement = PlacementState::normalize({
        PlacementState::OutputEdge::Bottom,
        placementAlignmentOf(alignment),
        state.minLength,
        state.maxLength,
        state.offset,
    });

    return {placement.maxLengthPercent(), placement.minLengthPercent(),
            placement.offsetPercent()};
}

//! Compatibility entry for the deduplicated block that ran after maxLength
//! changed. The original QML only pulled offset back on-screen; all callers now
//! enter the complete placement transaction so a startup or external state is
//! normalized identically. This existed byte-identically in
//! both QML copies ("centered and justify alignments based on offset and
//! get out of the screen in some cases", per the inherited comment);
//! divergence between them was the config-corruption class this unit
//! exists to end.
inline LengthState pullOffsetOnScreen(LengthState state, Alignment alignment)
{
    Q_ASSERT(std::isfinite(state.maxLength) && std::isfinite(state.minLength)
             && std::isfinite(state.offset));

    return normalizePlacement(state, alignment);
}

//! the maxLength ruler's wheel step (RulerMouseArea.qml body): a coupled
//! max==min pair moves as one, the step clamps to the ruler's 30..100
//! rails, minLength stays a floor the wheel cannot cross (except under
//! Justify, D17), then the off-screen correction runs
inline LengthState clampMaxLengthByStep(LengthState state, double step, Alignment alignment)
{
    Q_ASSERT(std::isfinite(state.maxLength) && std::isfinite(state.minLength)
             && std::isfinite(state.offset) && std::isfinite(step));

    //! exact equality on purpose: both values come from the same config
    //! keys, so a coupled pair was written as exactly-equal doubles (the
    //! shipped QML compared with ===). The coupling stays alignment-blind
    //! and Qt5-faithful (ACCEPTED, D15): it keeps a fixed-length dock
    //! (max==min) fixed as the ruler scrolls.
    const bool minimumIsCoupled = (state.maxLength == state.minLength);

    double value = std::max(std::min(state.maxLength + step, 100.0), 30.0);

    if (minimumIsCoupled) {
        state.minLength = std::max(30.0, value);
    }

    //! the stored minimum floors the wheel for every alignment but Justify,
    //! whose Maximum is not bounded below by a disabled Minimum slider (D17)
    if (maximumIsFlooredByMinimum(alignment)) {
        value = std::max(state.minLength, value);
    }
    state.maxLength = value;

    return pullOffsetOnScreen(state, alignment);
}

//! the Appearance page's Maximum slider (maxLengthSlider.updateMaxLength
//! body): an absolute request floored by minLength (except under Justify,
//! D17) and the slider's localMinValue of 1, then the off-screen
//! correction. The current maxLength is never read by this path, so it is
//! not a parameter.
//!
//! DEVIATION from Qt5, recorded in the spec and docs/agent-logs/EX-18.md:
//! the correction runs on the EFFECTIVE (floored) maxLength where Qt5
//! used the raw slider value. The two disagree only when the request is
//! below the floor - a state the slider wiring makes unreachable (its
//! pressed branch snaps the handle up to the floor before release) - and
//! the raw form broke the on-screen guarantee and idempotence there
//! (counterexample in toValue_correctionUsesEffectiveMaxLength).
inline LengthState clampMaxLengthToValue(double requestedMaxLength, double minLength,
                                         double offset, Alignment alignment)
{
    Q_ASSERT(std::isfinite(requestedMaxLength) && std::isfinite(minLength)
             && std::isfinite(offset));

    //! Justify's effective minimum is 0: its Minimum slider is disabled, so
    //! flooring by the stored minLength would strand Maximum (D17). The
    //! localMinValue floor of 1 still applies to every alignment - a dock
    //! is never zero-length.
    const double effectiveMinimum = maximumIsFlooredByMinimum(alignment) ? minLength : 0.0;
    LengthState state{std::max({requestedMaxLength, effectiveMinimum, 1.0}), minLength, offset};

    return pullOffsetOnScreen(state, alignment);
}

//! the offset slider's reachable range for a given maxLength (the
//! shipped fromValue/toValue/screenLengthMaxFactor cluster): centered
//! alignments swing +-factor around the middle, edge alignments get the
//! same width one-sided
inline OffsetBounds offsetSliderBounds(double maxLength, Alignment alignment)
{
    Q_ASSERT(std::isfinite(maxLength));

    //! trunc, not round: the shipped slider computed this through
    //! `readonly property int screenLengthMaxFactor: (100 - maxLength)/2`
    //! and QML int property bindings truncate toward zero (probed on the
    //! pinned qtdeclarative 6.11.0, qmltestrunner offscreen, 2026-07-16:
    //! 10.5 -> 10, -10.5 -> -10, 10.6 -> 10). qRound here would widen
    //! the bounds by one for odd lengths.
    const double factor = std::trunc((100.0 - maxLength) / 2.0);

    if (hasCenteredGeometry(alignment)) {
        return {-factor, factor};
    }

    return {0.0, 2.0 * factor};
}

//! the offset slider's write path (offsetSlider.updateOffset body):
//! bound the requested offset, then shrink maxLength if the pair still
//! leaves the screen
inline OffsetClamp clampOffset(double maxLength, double requestedOffset, Alignment alignment)
{
    Q_ASSERT(std::isfinite(maxLength) && std::isfinite(requestedOffset));

    //! Normalize maximum before deriving the slider rail. Deriving the rail
    //! from an invalid maximum and healing maximum afterwards made a second
    //! application move offset again; one transaction must converge at once.
    const LengthState initial = normalizePlacement({maxLength, 0.0, requestedOffset}, alignment);
    const OffsetBounds bounds = offsetSliderBounds(initial.maxLength, alignment);
    //! min(max(from, x), to) exactly as the shipped QML composed it, NOT
    //! std::clamp: a hand-edited config can carry maxLength > 100, which
    //! inverts the bounds (from > to), and this composition order defines
    //! the result (= to) where std::clamp would be undefined behavior
    const double offset = std::min(std::max(bounds.from, initial.offset), bounds.to);

    //! minLength is deliberately 0: the offset control does not own that key.
    //! The common placement transaction still clamps maximum and offset in one
    //! application, including a hand-edited maximum outside the percent domain.
    const LengthState normalized = normalizePlacement({initial.maxLength, 0.0, offset}, alignment);
    return {normalized.maxLength, normalized.offset};
}

} // namespace LengthOffsetClamp
} // namespace Settings
} // namespace Latte

#endif
