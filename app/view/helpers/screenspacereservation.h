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
class View;
}

namespace Latte {
namespace ViewPart {

//! A transparent, inputless layer surface that publishes one dock's occupied
//! work-area footprint. The visual View deliberately owns no exclusive zone,
//! so KWin cannot move that larger canvas through another dock's scalar band.
class ScreenSpaceReservation final : public QQuickView
{
public:
    explicit ScreenSpaceReservation(Latte::View *view);
    ~ScreenSpaceReservation() override;

    void publish(const QRect &strutGeometry, Plasma::Types::Location location);
    void clear();

    [[nodiscard]] QRect publishedGeometry() const;
    [[nodiscard]] const LayerShellQt::Window *layerShellWindow() const;

private:
    Latte::View *const m_view;
    LayerShellQt::Window *m_layerShellWindow{nullptr};
    QRect m_publishedGeometry;
};

}
}

#endif
