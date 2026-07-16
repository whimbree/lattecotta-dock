/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "parabolicmathtools.h"

// local
#include "units/parabolicmath.h"

// Qt
#include <QVariantList>

namespace Latte {

ParabolicMathTools::ParabolicMathTools(QObject *parent)
    : QObject(parent)
{
}

QVariantMap ParabolicMathTools::computeScales(double mousePosPercent, int spreadSteps,
                                              double zoomFactor, bool reversed)
{
    const auto stacks = ParabolicMath::computeScales(mousePosPercent, spreadSteps, zoomFactor, reversed);

    QVariantList left;
    left.reserve(stacks.left.size());
    for (double s : stacks.left) {
        left.append(s);
    }

    QVariantList right;
    right.reserve(stacks.right.size());
    for (double s : stacks.right) {
        right.append(s);
    }

    QVariantMap out;
    out.insert(QStringLiteral("left"), left);
    out.insert(QStringLiteral("right"), right);
    return out;
}

}
