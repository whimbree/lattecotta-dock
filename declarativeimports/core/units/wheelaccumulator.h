/*
    SPDX-FileCopyrightText: 2018 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef WHEELACCUMULATOR_H
#define WHEELACCUMULATOR_H

// Qt
#include <QPoint>
#include <QtGlobal>

// C++
#include <cstdlib>
#include <variant>

namespace Latte {

//! Wheel delta accumulation/threshold semantics (EX-15 in
//! docs/QML_EXTRACTION_PLAN.md): one tested accumulator replacing four
//! hand-rolled onWheel bodies - AudioStream.qml (plasma-pa accumulate,
//! 299a241b), TaskMouseArea.qml (dominant-axis threshold),
//! EnvironmentActions.qml (signed-extreme threshold) and
//! RulerMouseArea.qml (vertical threshold). The QML shells keep their
//! timers, guards and action dispatch; the delta arithmetic lives here.
//!
//! Unit domain: everything is in QWheelEvent angleDelta units (120 = one
//! physical notch). The Qt5 handlers compared angle = angleDelta/8
//! against 12 or 10 in JS floats; angleDelta components are ints, so
//! angle > 12 is exactly angleDelta > 96 and angle > 10 exactly > 80 -
//! the thresholds below stay in the integer domain with no float math.

//! which component of angleDelta carries the wheel's intent - one
//! strategy per historical call site, preserved exactly
enum class WheelAxisPick {
    //! plasma-pa's read (299a241b): vertical wins whenever nonzero; a
    //! pure horizontal wheel maps to the OPPOSITE sign (tilting the
    //! wheel right scrolls "down")
    VerticalElseNegatedHorizontal,
    //! the component with the strictly larger magnitude, used as-is;
    //! equal magnitudes resolve to the horizontal component (Qt5's
    //! strict abs(y) > abs(x))
    DominantAxis,
    //! the Qt5 EnvironmentActions quirk, preserved (Qt5-faithful): both
    //! components non-negative -> max(y, x), otherwise min(y, x) - so
    //! ANY negative component forces the pick negative, even against a
    //! strong positive vertical
    SignedExtreme,
    //! vertical only; the horizontal component is ignored
    VerticalOnly,
};

//! fires one step per full stepSize of travel, however many fit in the
//! event, immediately - no lockout; the sub-step residue carries across
//! events (high-resolution wheels reach a step over several events) and
//! optionally drops on direction reversal so the first step of the new
//! direction needs the same travel as every other step
struct AccumulateEveryStep {
    int stepSize; //!< angleDelta units per fired step; must be > 0
    bool resetOnReversal;
};

//! fires exactly one step when the picked delta STRICTLY exceeds the
//! threshold, however large the event; nothing carries across events -
//! this mode has no residue by construction
struct FireOncePastThreshold {
    int threshold; //!< angleDelta units, strict >; must be >= 0
};

using WheelFiring = std::variant<AccumulateEveryStep, FireOncePastThreshold>;

class WheelAccumulator
{
public:
    WheelAccumulator(WheelAxisPick axisPick, WheelFiring firing)
        : m_axisPick(axisPick)
        , m_state(stateFor(firing))
    {
    }

    //! strict abs(y) > abs(x) - the single authority for the
    //! dominant-axis predicate (TaskMouseArea's parallel-scroll read
    //! shares it with the DominantAxis pick, so the two can never drift)
    static bool verticalIsDominant(QPoint angleDelta)
    {
        return std::abs(angleDelta.y()) > std::abs(angleDelta.x());
    }

    //! the signed fired step count for one wheel event; 0 = below
    //! threshold / not enough travel yet. inverted is the event's
    //! natural-scrolling flag: a sign flip applied before everything
    //! else (only the audio badge honors it - the Qt5 handlers of the
    //! other sites ignored it, and fidelity keeps them that way)
    int add(QPoint angleDelta, bool inverted = false)
    {
        const int picked = pickAxisDelta(angleDelta);
        const int delta = inverted ? -picked : picked;

        return std::visit([delta](auto &state) { return fire(state, delta); }, m_state);
    }

private:
    //! accumulate mode is the only stateful one: the residue lives in
    //! its variant alternative, so "residue in threshold mode" is not
    //! representable
    struct Accumulating {
        int stepSize;
        bool resetOnReversal;
        int residue = 0;
    };
    struct Thresholding {
        int threshold;
    };

    static std::variant<Accumulating, Thresholding> stateFor(const WheelFiring &firing)
    {
        if (const auto *accumulate = std::get_if<AccumulateEveryStep>(&firing)) {
            //! stepSize 0 would fire forever on any travel; negative is nonsense
            Q_ASSERT(accumulate->stepSize > 0);
            return Accumulating{accumulate->stepSize, accumulate->resetOnReversal};
        }
        const auto &threshold = std::get<FireOncePastThreshold>(firing);
        //! a negative threshold would fire on zero-delta events
        Q_ASSERT(threshold.threshold >= 0);
        return Thresholding{threshold.threshold};
    }

    int pickAxisDelta(QPoint angleDelta) const
    {
        switch (m_axisPick) {
        case WheelAxisPick::VerticalElseNegatedHorizontal:
            return angleDelta.y() != 0 ? angleDelta.y() : -angleDelta.x();
        case WheelAxisPick::DominantAxis:
            return verticalIsDominant(angleDelta) ? angleDelta.y() : angleDelta.x();
        case WheelAxisPick::SignedExtreme:
            if (angleDelta.y() >= 0 && angleDelta.x() >= 0) {
                return std::max(angleDelta.y(), angleDelta.x());
            }
            return std::min(angleDelta.y(), angleDelta.x());
        case WheelAxisPick::VerticalOnly:
            return angleDelta.y();
        }
        Q_UNREACHABLE_RETURN(0);
    }

    static int fire(Accumulating &state, int delta)
    {
        const bool reverses = (state.residue > 0 && delta < 0) || (state.residue < 0 && delta > 0);
        if (state.resetOnReversal && reverses) {
            state.residue = 0;
        }
        state.residue += delta;

        //! integer division truncates toward zero, which is exactly the
        //! QML's pair of while loops (subtract stepSize per step until
        //! |residue| < stepSize) for both signs
        const int steps = state.residue / state.stepSize;
        state.residue -= steps * state.stepSize;
        return steps;
    }

    static int fire(const Thresholding &state, int delta)
    {
        if (delta > state.threshold) {
            return 1;
        }
        if (delta < -state.threshold) {
            return -1;
        }
        return 0;
    }

    WheelAxisPick m_axisPick;
    std::variant<Accumulating, Thresholding> m_state;
};

}

#endif
