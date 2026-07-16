/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "parabolicroutertools.h"

// local
#include "units/parabolicrouter.h"

// Qt
#include <QDebug>

// C++
#include <type_traits>
#include <variant>

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
        //! exhaustive by construction: a new Action alternative fails the
        //! static_assert at compile time instead of misdispatching here
        std::visit([&a](const auto &act) {
            using T = std::decay_t<decltype(act)>;
            a.insert(QStringLiteral("pos"), act.pos);
            if constexpr (std::is_same_v<T, ParabolicRouter::ApplyScale>) {
                a.insert(QStringLiteral("kind"), QStringLiteral("emit"));
                a.insert(QStringLiteral("stack"), QVariantList{act.scale});
            } else if constexpr (std::is_same_v<T, ParabolicRouter::ClientHandoff>) {
                a.insert(QStringLiteral("kind"), QStringLiteral("emit"));
                a.insert(QStringLiteral("stack"), toVariantStack(act.stack));
            } else {
                static_assert(std::is_same_v<T, ParabolicRouter::SpacerAbsorb>);
                a.insert(QStringLiteral("kind"), QStringLiteral("absorb"));
                a.insert(QStringLiteral("factor"), act.factor);
            }
        }, action);
        actions.append(a);
    }

    if (result.clearEmissionPos) {
        QVariantMap clear;
        clear.insert(QStringLiteral("kind"), QStringLiteral("emit"));
        clear.insert(QStringLiteral("pos"), *result.clearEmissionPos);
        clear.insert(QStringLiteral("stack"), QVariantList{1.0});
        actions.append(clear);
    }

    QVariantMap out;
    out.insert(QStringLiteral("actions"), actions);
    //! QML keeps the -1-means-none convention at the boundary only
    out.insert(QStringLiteral("clearEmissionPos"), result.clearEmissionPos.value_or(-1));
    out.insert(QStringLiteral("overflow"), toVariantStack(result.overflow));
    return out;
}

}
