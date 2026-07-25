/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "screenspacereservation.h"

// local
#include "../../wm/waylandlayershell.h"

// Qt
#include <QDebug>
#include <QRegion>
#include <QScreen>

namespace Latte {
namespace ViewPart {

ScreenSpaceReservation::ScreenSpaceReservation(
    const int outputId,
    const Plasma::Types::Location location)
{
    setTitle(QStringLiteral("#screen-space-reservation#output=%1#edge=%2")
                 .arg(outputId)
                 .arg(static_cast<int>(location)));
    setColor(Qt::transparent);
    setDefaultAlphaBuffer(true);
    setFlags(Qt::FramelessWindowHint
             | Qt::WindowStaysOnTopHint
             | Qt::NoDropShadowWindowHint
             | Qt::WindowDoesNotAcceptFocus
             | Qt::WindowTransparentForInput);

    //! An empty mask means infinite input on Qt Wayland. A 1px rectangle
    //! outside the surface expresses a genuinely empty on-surface input area.
    setMask(QRegion(QRect(-1, -1, 1, 1)));
}

ScreenSpaceReservation::~ScreenSpaceReservation()
{
    clear();
    setVisible(false);
}

bool ScreenSpaceReservation::publish(
    QScreen *const screen,
    const QRect &strutGeometry,
    const Plasma::Types::Location location)
{
    if (!screen) {
        qCritical() << "ScreenSpaceReservation refused a strut without an assigned screen"
                    << "surface=" << title();
        return false;
    }

    const QRect screenGeometry = screen->geometry();
    m_layerShellWindow = WindowSystem::LayerShell::applyReservationPlacement(
        this, screen, location, strutGeometry, screenGeometry);
    if (!m_layerShellWindow) {
        qCritical() << "ScreenSpaceReservation could not publish"
                    << "surface=" << title()
                    << "strut=" << strutGeometry;
        return false;
    }

    m_publishedGeometry = strutGeometry;
    if (!isVisible()) {
        show();
    }
    return true;
}

void ScreenSpaceReservation::clear()
{
    if (m_layerShellWindow) {
        WindowSystem::LayerShell::clearReservation(this);
    }
    m_publishedGeometry = QRect();
}

QRect ScreenSpaceReservation::publishedGeometry() const
{
    return m_publishedGeometry;
}

const LayerShellQt::Window *ScreenSpaceReservation::layerShellWindow() const
{
    return m_layerShellWindow;
}

}
}
