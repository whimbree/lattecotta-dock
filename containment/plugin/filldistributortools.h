/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef FILLDISTRIBUTORTOOLS_H
#define FILLDISTRIBUTORTOOLS_H

// Qt
#include <QObject>
#include <QQmlEngine>
#include <QJSEngine>
#include <QVariantList>
#include <QVariantMap>

namespace Latte {

//! Thin QML boundary for the FillLengthDistributor core (EX-05 in
//! docs/tracking/QML_EXTRACTION_PLAN.md, units/filldistributor.h). The containment
//! layouter gathers the per-item records and per-layout aggregates from
//! its live grids, calls distribute() once per update, and applies the
//! returned lengths; every pass and quirk lives in the core.
class FillDistributorTools final : public QObject
{
    Q_OBJECT

public:
    explicit FillDistributorTools(QObject *parent = nullptr);

    //! layout maps carry: items (list of {autoFill, hidden, hasApplet,
    //! splitter, min, pref, max, liveMax, liveMin}), sizeWithNoFill,
    //! fillApplets, shownApplets, gridLength. Returns {start, main, end}
    //! lists index-parallel with the input items; each entry carries a
    //! "max" and/or "min" key only when that pass assigned the item.
    Q_INVOKABLE QVariantMap distribute(const QVariantMap &start, const QVariantMap &main, const QVariantMap &end,
                                       bool justifyAlignment, double minLength, double contentsMaxLength);
};

static QObject *filldistributortools_qobject_singletontype_provider(QQmlEngine *engine, QJSEngine *scriptEngine)
{
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)

    return new FillDistributorTools;
}

}

#endif
