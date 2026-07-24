/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef AUTOSIZESTEPPER_H
#define AUTOSIZESTEPPER_H

// local
#include "units/autosizeengine.h"

// Qt
#include <QObject>
#include <QVariantMap>

namespace Latte {
namespace Containment {

//! Thin QML shell over the AutoSizeEngine pure core (EX-04 in
//! docs/tracking/QML_EXTRACTION_PLAN.md, units/autosizeengine.h). The containment
//! ability AutoSize.qml instantiates one stepper per sizer: the stepper
//! owns the prediction history, the QML keeps its timers, gates and
//! property reads and asks here for decisions only.
//! not final: qmlRegisterType instantiates through a QQmlElement subclass
class AutoSizeStepper : public QObject
{
    Q_OBJECT

public:
    explicit AutoSizeStepper(QObject *parent = nullptr);

public Q_SLOTS:
    //! forget recorded predictions (the sizer's isActive flip)
    Q_INVOKABLE void clearHistory();

    //! One pass of the automatic icon-size search over a measured layout
    //! snapshot; returns {found: bool, nextIconSize: int} where a found
    //! nextIconSize of -1 means a grow reached maxIconSize and the sizer
    //! returns to automatic sizing. The -1 = automatic sentinel exists
    //! only at this boundary in both directions: appliedIconSize takes
    //! the sizer's iconSize property verbatim (-1 included), the core
    //! sees std::optional and a distinct variant alternative.
    Q_INVOKABLE QVariantMap step(double layoutLength, double maxLength, int currentIconSize,
                                 int maxIconSize, int appliedIconSize);

private:
    AutoSizeEngine::History m_history;
};

}
}

#endif
