/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef FLOATINGPOPUPPRESENTATION_H
#define FLOATINGPOPUPPRESENTATION_H

#include "floatingpanelgeometry.h"

#include <QByteArray>
#include <QRectF>
#include <QSize>
#include <QtGlobal>
#include <QtMath>

#include <optional>

namespace Latte::ViewPart::FloatingPopupPresentation {

[[nodiscard]] inline std::optional<int> perpendicularAnchor(
    FloatingPanelGeometry::Edge edge,
    const QRectF &visibleGlobalGeometry,
    const QSize &popupSize,
    int popupMargin)
{
    if (!visibleGlobalGeometry.isValid()
        || visibleGlobalGeometry.isEmpty()
        || popupSize.isEmpty()
        || popupMargin < 0) {
        return std::nullopt;
    }

    switch (edge) {
    case FloatingPanelGeometry::Edge::Left:
        // QRectF::right()/bottom() are x + width / y + height, unlike
        // QRect's inclusive integer endpoints. They are the half-open visible
        // boundary where an outward popup begins.
        return qCeil(visibleGlobalGeometry.right()) + popupMargin;
    case FloatingPanelGeometry::Edge::Right:
        return qFloor(visibleGlobalGeometry.left())
            - popupSize.width() - popupMargin;
    case FloatingPanelGeometry::Edge::Top:
        return qCeil(visibleGlobalGeometry.bottom()) + popupMargin;
    case FloatingPanelGeometry::Edge::Bottom:
        return qFloor(visibleGlobalGeometry.top())
            - popupSize.height() - popupMargin;
    }

    Q_UNREACHABLE();
} // namespace Latte::ViewPart::FloatingPopupPresentation

template<typename Integer>
[[nodiscard]] constexpr Integer displayHintsWithFloatingPreference(
    Integer currentHints,
    Integer floatingHint,
    bool preferFloating)
{
    return preferFloating
        ? static_cast<Integer>(currentHints | floatingHint)
        : static_cast<Integer>(currentHints & ~floatingHint);
}

[[nodiscard]] inline bool isAnchorRevisionProperty(
    const QByteArray &propertyName)
{
    return propertyName
        == QByteArrayLiteral("_floating_anchor_revision");
}

}

#endif
