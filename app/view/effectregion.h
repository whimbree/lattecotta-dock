/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef EFFECTREGION_H
#define EFFECTREGION_H

#include <QRect>
#include <QRegion>

namespace Latte::ViewPart::EffectRegion {

[[nodiscard]] inline QRegion rasterizedTranslatedShape(
    const QRectF &visibleShape,
    const QRegion &localShape)
{
    const QRect paintBounds = visibleShape.toAlignedRect();
    if (!paintBounds.isValid() || paintBounds.isEmpty()) {
        return {};
    }

    QRegion shape = localShape;
    if (shape.isEmpty()) {
        shape = QRegion(QRect(QPoint{},
                              visibleShape.size().toSize()));
    }

    QRegion rasterized;
    for (const QRect &sourceRectangle : shape) {
        const QRectF translated{
            QPointF(sourceRectangle.topLeft())
                + visibleShape.topLeft(),
            QSizeF(sourceRectangle.size())};
        rasterized += translated.toAlignedRect();
    }
    return rasterized & QRegion(paintBounds);
}

} // namespace Latte::ViewPart::EffectRegion

#endif
