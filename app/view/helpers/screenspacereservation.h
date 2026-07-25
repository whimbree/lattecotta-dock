/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef SCREENSPACERESERVATION_H
#define SCREENSPACERESERVATION_H

// Qt
#include <QQuickView>
#include <QRect>

// Plasma
#include <Plasma/Plasma>

namespace LayerShellQt {
class Window;
}

namespace Latte {
namespace ViewPart {

//! A transparent, inputless layer surface that publishes one output-edge
//! work-area reservation. Visual Views deliberately own no exclusive zone,
//! so KWin cannot move their larger canvases through another dock's scalar
//! band.
class ScreenSpaceReservation final : public QQuickView
{
public:
    ScreenSpaceReservation(int outputId, Plasma::Types::Location location);
    ~ScreenSpaceReservation() override;

    [[nodiscard]] bool publish(
        QScreen *screen,
        const QRect &strutGeometry,
        Plasma::Types::Location location);
    void clear();

    [[nodiscard]] QRect publishedGeometry() const;
    [[nodiscard]] const LayerShellQt::Window *layerShellWindow() const;

private:
    LayerShellQt::Window *m_layerShellWindow{nullptr};
    QRect m_publishedGeometry;
};

}
}

#endif
