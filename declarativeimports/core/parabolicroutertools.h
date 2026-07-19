/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef LATTECOREPARABOLICROUTERTOOLS_H
#define LATTECOREPARABOLICROUTERTOOLS_H

// Qt
#include <QJSEngine>
#include <QObject>
#include <QQmlEngine>
#include <QVariantList>
#include <QVariantMap>

namespace Latte {

//! Thin QML shell over the ParabolicRouter pure core (EX-02 in
//! docs/tracking/QML_EXTRACTION_PLAN.md, units/parabolicrouter.h). The shells
//! (containment parabolic ability, plasmoid client ability) feed their
//! row snapshot and apply the returned emission plan through the
//! surviving sglUpdateLower/HigherItemScale application signals.
class ParabolicRouterTools final : public QObject
{
    Q_OBJECT

public:
    //! mirrors ParabolicRouter::ItemKind for the QML row builders
    enum ItemKind {
        Normal = 0,
        Transparent,
        EdgeSpacer,
        BridgeClient,
        DeadStop
    };
    Q_ENUM(ItemKind)

    explicit ParabolicRouterTools(QObject *parent = nullptr);

public Q_SLOTS:
    //! row: ItemKind ints; step: -1 lower / +1 higher; spacersAbsorbing:
    //! the view-global alignment gate (Center/Justify), applied to every
    //! spacer in the row. Returns the emission plan:
    //!   actions: ordered [{kind:"emit", pos, stack} |
    //!                     {kind:"absorb", pos, factor}] - the in-row
    //!            clear-tail [1] emission is already appended
    //!   clearEmissionPos: its position, -1 when none (the plasmoid twin
    //!            exports [1] through the bridge when >= 0)
    //!   overflow: stack that left the row edge (containment re-emits at
    //!            the boundary index, plasmoid exports through the bridge)
    Q_INVOKABLE QVariantMap route(const QVariantList &row, int entryPos, int step,
                                  const QVariantList &stack, int spreadSteps,
                                  bool spacersAbsorbing);
};

static QObject *parabolicroutertools_qobject_singletontype_provider(QQmlEngine *engine, QJSEngine *scriptEngine)
{
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)

// NOTE: QML engine is the owner of this resource
    return new ParabolicRouterTools;
}

}

#endif
