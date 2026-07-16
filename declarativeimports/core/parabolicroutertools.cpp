/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "parabolicroutertools.h"

// local
#include "units/parabolicrouter.h"

// Qt
#include <QDebug>

namespace Latte {

ParabolicRouterTools::ParabolicRouterTools(QObject *parent)
    : QObject(parent)
{
}

namespace {

QVariantList toVariantStack(const QVector<double> &stack)
{
    QVariantList out;
    out.reserve(stack.size());
    for (double s : stack) {
        out.append(s);
    }
    return out;
}

}

QVariantMap ParabolicRouterTools::route(const QVariantList &row, int entryPos, int step,
                                        const QVariantList &stack, int spreadSteps,
                                        bool spacersAbsorbing)
{
    QVector<ParabolicRouter::RowItem> coreRow;
    coreRow.reserve(row.size());
    for (const QVariant &v : row) {
        bool ok = false;
        const int kind = v.toInt(&ok);
        if (!ok || kind < Normal || kind > DeadStop) {
            //! a malformed row is a shell bug, never something to route
            //! around silently
            qCritical() << "ParabolicRouter.route: invalid row entry" << v
                        << "- refusing to route";
            return QVariantMap();
        }
        ParabolicRouter::RowItem item;
        item.kind = static_cast<ParabolicRouter::ItemKind>(kind);
        item.absorbing = spacersAbsorbing;
        coreRow.append(item);
    }

    QVector<double> coreStack;
    coreStack.reserve(stack.size());
    for (const QVariant &v : stack) {
        coreStack.append(v.toDouble());
    }

    const ParabolicRouter::RouteResult result =
        ParabolicRouter::routeStack(coreRow, entryPos, step, coreStack, spreadSteps);

    QVariantList actions;
    actions.reserve(result.actions.size() + 1);
    for (const auto &action : result.actions) {
        QVariantMap a;
        a.insert(QStringLiteral("pos"), action.pos);
        switch (action.kind) {
        case ParabolicRouter::ActionKind::ApplyScale:
            a.insert(QStringLiteral("kind"), QStringLiteral("emit"));
            a.insert(QStringLiteral("stack"), QVariantList{action.scale});
            break;
        case ParabolicRouter::ActionKind::ClientHandoff:
            a.insert(QStringLiteral("kind"), QStringLiteral("emit"));
            a.insert(QStringLiteral("stack"), toVariantStack(action.stack));
            break;
        case ParabolicRouter::ActionKind::SpacerAbsorb:
            a.insert(QStringLiteral("kind"), QStringLiteral("absorb"));
            a.insert(QStringLiteral("factor"), action.absorbFactor);
            break;
        }
        actions.append(a);
    }

    if (result.clearEmissionPos >= 0) {
        QVariantMap clear;
        clear.insert(QStringLiteral("kind"), QStringLiteral("emit"));
        clear.insert(QStringLiteral("pos"), result.clearEmissionPos);
        clear.insert(QStringLiteral("stack"), QVariantList{1.0});
        actions.append(clear);
    }

    QVariantMap out;
    out.insert(QStringLiteral("actions"), actions);
    out.insert(QStringLiteral("clearEmissionPos"), result.clearEmissionPos);
    out.insert(QStringLiteral("overflow"), toVariantStack(result.overflow));
    return out;
}

}
