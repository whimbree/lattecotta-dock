/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "screenspacereservation.h"

// local
#include "../view.h"
#include "../../wm/waylandlayershell.h"

// Qt
#include <QDebug>
#include <QRegion>
#include <QScreen>

namespace Latte {
namespace ViewPart {

ScreenSpaceReservation::ScreenSpaceReservation(Latte::View *view)
    : m_view(view)
{
    Q_ASSERT(m_view);

    setTitle(QStringLiteral("#screen-space-reservation#%1")
                 .arg(m_view->containment() ? m_view->containment()->id() : 0));
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

void ScreenSpaceReservation::publish(const QRect &strutGeometry,
                                     Plasma::Types::Location location)
{
    if (!m_view->screen()) {
        qCritical() << "ScreenSpaceReservation refused a strut without an assigned screen"
                    << "containment=" << (m_view->containment() ? m_view->containment()->id() : 0);
        return;
    }

    const QRect screenGeometry = m_view->screenGeometry();
    m_layerShellWindow = WindowSystem::LayerShell::applyReservationPlacement(
        this, m_view->screen(), location, strutGeometry, screenGeometry);
    if (!m_layerShellWindow) {
        qCritical() << "ScreenSpaceReservation could not publish"
                    << "containment=" << (m_view->containment() ? m_view->containment()->id() : 0)
                    << "strut=" << strutGeometry;
        return;
    }

    m_publishedGeometry = strutGeometry;
    if (!isVisible()) {
        show();
    }
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
