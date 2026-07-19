/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef LATTECOREPARABOLICMATHTOOLS_H
#define LATTECOREPARABOLICMATHTOOLS_H

// Qt
#include <QJSEngine>
#include <QObject>
#include <QQmlEngine>
#include <QVariantMap>

namespace Latte {

//! Thin QML shell over the ParabolicMath pure core (EX-03 in
//! docs/tracking/QML_EXTRACTION_PLAN.md, units/parabolicmath.h). Numbers only;
//! the abilities definition keeps its signals and layout-direction read.
class ParabolicMathTools final : public QObject
{
    Q_OBJECT

public:
    explicit ParabolicMathTools(QObject *parent = nullptr);

public Q_SLOTS:
    //! {left: [..], right: [..]} - nearest neighbor first, trailing 1.0
    //! clearing entry in both stacks
    Q_INVOKABLE QVariantMap computeScales(double mousePosPercent, int spreadSteps,
                                          double zoomFactor, bool reversed);
};

static QObject *parabolicmathtools_qobject_singletontype_provider(QQmlEngine *engine, QJSEngine *scriptEngine)
{
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)

// NOTE: QML engine is the owner of this resource
    return new ParabolicMathTools;
}

}

#endif
