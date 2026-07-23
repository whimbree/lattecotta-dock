/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "autosizestepper.h"

// Qt
#include <QDebug>

// C++
#include <optional>
#include <type_traits>
#include <variant>

namespace Latte {
namespace Containment {

AutoSizeStepper::AutoSizeStepper(QObject *parent)
    : QObject(parent)
{
}

void AutoSizeStepper::clearHistory()
{
    m_history.clear();
}

QVariantMap AutoSizeStepper::step(double layoutLength, double maxLength, int currentIconSize,
                                  int maxIconSize, int appliedIconSize)
{
    //! AutoSize.qml's maxLength <= 0 no-geometry contract returns before
    //! calling in (ad9b823f), lengths are widths/heights and cannot be
    //! negative, and metrics' icon sizes never sit below the 16px floor or
    //! above their configured ceiling. Arriving here outside those contracts
    //! is a shell bug, never something to search around silently.
    const bool appliedSizeIsValid = appliedIconSize == -1
            || (appliedIconSize >= AutoSizeEngine::minIconSize
                && appliedIconSize <= maxIconSize);
    const bool measurementIsValid = maxLength > 0
            && layoutLength >= 0
            && currentIconSize >= AutoSizeEngine::minIconSize
            && maxIconSize >= AutoSizeEngine::minIconSize
            && currentIconSize <= maxIconSize
            && appliedSizeIsValid;

    if (!measurementIsValid) {
        qCritical() << "AutoSizeStepper.step: invalid measurement, layoutLength" << layoutLength
                    << "maxLength" << maxLength
                    << "currentIconSize" << currentIconSize << "maxIconSize" << maxIconSize
                    << "appliedIconSize" << appliedIconSize << "- refusing to step";
        return QVariantMap();
    }

    AutoSizeEngine::AutoSizeInput input;
    input.layoutLength = layoutLength;
    input.maxLength = maxLength;
    input.currentIconSize = currentIconSize;
    input.maxIconSize = maxIconSize;
    //! the sizer's -1 = automatic sentinel becomes the core's absent state
    input.appliedIconSize = (appliedIconSize == -1) ? std::nullopt
                                                    : std::optional<int>(appliedIconSize);

    const AutoSizeEngine::AutoSizeStep result = AutoSizeEngine::step(input, m_history);

    QVariantMap out;
    //! exhaustive by construction: a new AutoSizeStep alternative fails the
    //! static_assert at compile time instead of misdispatching here
    std::visit([&out](const auto &alternative) {
        using T = std::decay_t<decltype(alternative)>;
        if constexpr (std::is_same_v<T, AutoSizeEngine::KeepCurrent>) {
            out.insert(QStringLiteral("found"), false);
        } else if constexpr (std::is_same_v<T, AutoSizeEngine::ApplySize>) {
            out.insert(QStringLiteral("found"), true);
            out.insert(QStringLiteral("nextIconSize"), alternative.iconSize);
        } else {
            static_assert(std::is_same_v<T, AutoSizeEngine::RestoreAutomaticMax>);
            out.insert(QStringLiteral("found"), true);
            //! the sizer's automatic sentinel
            out.insert(QStringLiteral("nextIconSize"), -1);
        }
    }, result);

    return out;
}

}
}
