/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef LATTECOREWHEELSTEPPER_H
#define LATTECOREWHEELSTEPPER_H

// local
#include "units/wheelaccumulator.h"

// Qt
#include <QObject>
#include <QPointF>

// C++
#include <optional>

namespace Latte {

//! Thin QML boundary over the WheelAccumulator pure core (EX-15 in
//! docs/tracking/QML_EXTRACTION_PLAN.md, units/wheelaccumulator.h). Each wheel
//! site instantiates its own stepper - the accumulating mode carries a
//! per-instance residue (every audio badge owns one), so this cannot be
//! a singleton. The QML shells keep their timers, guards and action
//! dispatch and ask here for fired steps only.
//!
//! Configuration is exactly one of:
//!   stepSize > 0  (+ optionally resetOnReversal) - accumulate mode
//!   fireThreshold >= 0                           - threshold mode
//! Anything else - both set, neither set, resetOnReversal next to a
//! threshold - is refused loudly at add() with qCritical and 0 steps:
//! this is the boundary where bad configuration can arrive from QML,
//! so it refuses at runtime instead of asserting. The 0/-1 "unset"
//! sentinels live only here; the core sees a std::variant.
class WheelStepper : public QObject
{
    Q_OBJECT
    //! REQUIRED: a stepper without an explicit axis read is a config
    //! bug, and the engine refuses to instantiate one - earlier and
    //! louder than any runtime check
    Q_PROPERTY(Latte::WheelStepper::AxisPick axisPick READ axisPick WRITE setAxisPick NOTIFY axisPickChanged REQUIRED)
    Q_PROPERTY(int stepSize READ stepSize WRITE setStepSize NOTIFY stepSizeChanged)
    Q_PROPERTY(bool resetOnReversal READ resetOnReversal WRITE setResetOnReversal NOTIFY resetOnReversalChanged)
    Q_PROPERTY(int fireThreshold READ fireThreshold WRITE setFireThreshold NOTIFY fireThresholdChanged)

public:
    //! mirrors Latte::WheelAxisPick (units/wheelaccumulator.h, where
    //! each strategy is documented); kept in sync by static_asserts in
    //! wheelstepper.cpp
    enum AxisPick {
        VerticalElseNegatedHorizontal = 0,
        DominantAxis,
        SignedExtreme,
        VerticalOnly,
    };
    Q_ENUM(AxisPick)

    explicit WheelStepper(QObject *parent = nullptr);

    AxisPick axisPick() const;
    void setAxisPick(AxisPick axisPick);

    int stepSize() const;
    void setStepSize(int stepSize);

    bool resetOnReversal() const;
    void setResetOnReversal(bool resetOnReversal);

    int fireThreshold() const;
    void setFireThreshold(int fireThreshold);

    //! the signed fired step count for one wheel event (see
    //! WheelAccumulator::add). angleDelta arrives as the QML point
    //! value (QPointF); wheel deltas are integer-valued, so toPoint()
    //! is the identity on real events
    Q_INVOKABLE int add(QPointF angleDelta, bool inverted);

    //! strict abs(y) > abs(x) - TaskMouseArea's parallel-scroll read,
    //! shared with the DominantAxis pick so the two cannot drift
    Q_INVOKABLE bool verticalIsDominant(QPointF angleDelta) const;

Q_SIGNALS:
    void axisPickChanged();
    void stepSizeChanged();
    void resetOnReversalChanged();
    void fireThresholdChanged();

private:
    bool rebuildAccumulator();

    AxisPick m_axisPick{VerticalOnly}; //! unreachable default: REQUIRED forces QML to set it
    int m_stepSize{0}; //! 0 = unset (boundary sentinel)
    bool m_resetOnReversal{false};
    int m_fireThreshold{-1}; //! -1 = unset (boundary sentinel)

    //! rebuilt lazily from the properties at first add(); a
    //! reconfiguration mid-flight drops any residue (no site
    //! reconfigures - the properties are static per shell)
    std::optional<WheelAccumulator> m_accumulator;
};

}

#endif
