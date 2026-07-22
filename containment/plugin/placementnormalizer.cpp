/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "placementnormalizer.h"

// local
#include "../../app/settings/placementstate.h"
#include <coretypes.h>

// Qt
#include <QDebug>

// Plasma
#include <Plasma/Plasma>

// C++
#include <cmath>
#include <optional>

namespace Latte {
namespace Containment {

namespace {

using namespace Latte::Settings::PlacementState;

std::optional<OutputEdge> outputEdgeOf(int location)
{
    switch (location) {
    case Plasma::Types::TopEdge:
        return OutputEdge::Top;
    case Plasma::Types::RightEdge:
        return OutputEdge::Right;
    case Plasma::Types::BottomEdge:
        return OutputEdge::Bottom;
    case Plasma::Types::LeftEdge:
        return OutputEdge::Left;
    default:
        return std::nullopt;
    }
}

std::optional<PhysicalAlignment> physicalAlignmentOf(int alignment)
{
    switch (alignment) {
    case Latte::Types::Left:
        return PhysicalAlignment::Left;
    case Latte::Types::Right:
        return PhysicalAlignment::Right;
    case Latte::Types::Top:
        return PhysicalAlignment::Top;
    case Latte::Types::Bottom:
        return PhysicalAlignment::Bottom;
    case Latte::Types::Center:
        return PhysicalAlignment::Center;
    case Latte::Types::Justify:
        return PhysicalAlignment::Justify;
    default:
        return std::nullopt;
    }
}

int latteAlignmentOf(PhysicalAlignment alignment)
{
    switch (alignment) {
    case PhysicalAlignment::Left:
        return Latte::Types::Left;
    case PhysicalAlignment::Right:
        return Latte::Types::Right;
    case PhysicalAlignment::Top:
        return Latte::Types::Top;
    case PhysicalAlignment::Bottom:
        return Latte::Types::Bottom;
    case PhysicalAlignment::Center:
        return Latte::Types::Center;
    case PhysicalAlignment::Justify:
        return Latte::Types::Justify;
    }

    Q_UNREACHABLE_RETURN(Latte::Types::Center);
}

QVariantMap unchangedMap(int alignment, double minLength, double maxLength, double offset)
{
    return {
        {QStringLiteral("accepted"), false},
        {QStringLiteral("alignment"), alignment},
        {QStringLiteral("minLength"), minLength},
        {QStringLiteral("maxLength"), maxLength},
        {QStringLiteral("offset"), offset},
    };
}

} // namespace

PlacementNormalizer::PlacementNormalizer(QObject *parent)
    : QObject(parent)
{
}

QVariantMap PlacementNormalizer::normalize(int location, int alignment,
                                           double minLength, double maxLength,
                                           double offset) const
{
    const std::optional<OutputEdge> edge = outputEdgeOf(location);
    const std::optional<PhysicalAlignment> physicalAlignment = physicalAlignmentOf(alignment);
    const bool lengthsAreFinite = std::isfinite(minLength)
            && std::isfinite(maxLength) && std::isfinite(offset);

    if (!edge || !physicalAlignment || !lengthsAreFinite) {
        qCritical() << "PlacementNormalizer.normalize: refused invalid placement, location"
                    << location << "alignment" << alignment
                    << "min/max/offset" << minLength << maxLength << offset;
        return unchangedMap(alignment, minLength, maxLength, offset);
    }

    const Alignment semanticAlignment = semanticAlignmentOf(*physicalAlignment);
    const auto placement = Latte::Settings::PlacementState::normalize({
        *edge,
        semanticAlignment,
        minLength,
        maxLength,
        offset,
    });

    return {
        {QStringLiteral("accepted"), true},
        {QStringLiteral("alignment"),
         latteAlignmentOf(physicalAlignmentFor(*edge, semanticAlignment))},
        {QStringLiteral("minLength"), placement.minLengthPercent()},
        {QStringLiteral("maxLength"), placement.maxLengthPercent()},
        {QStringLiteral("offset"), placement.offsetPercent()},
    };
}

} // namespace Containment
} // namespace Latte
