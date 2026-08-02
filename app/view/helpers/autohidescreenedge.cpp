/*
    SPDX-FileCopyrightText: 2023 Vlad Zahorodnii <vlad.zahorodnii@kde.org>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "autohidescreenedge.h"

// local
#include "../view.h"

// generated Wayland protocol
#include "qwayland-kde-screen-edge-v1.h"

// Qt
#include <QEvent>
#include <QWindow>
#include <QtWaylandClient/QWaylandClientExtension>

// KDE
#include <KWayland/Client/surface.h>

namespace Latte {
namespace ViewPart {

class WaylandScreenEdgeManagerV1 final
    : public QWaylandClientExtensionTemplate<WaylandScreenEdgeManagerV1>
    , public QtWayland::kde_screen_edge_manager_v1
{
public:
    WaylandScreenEdgeManagerV1()
        : QWaylandClientExtensionTemplate(1)
    {
        initialize();
    }

    ~WaylandScreenEdgeManagerV1() override
    {
        if (isInitialized()) {
            destroy();
        }
    }
};

class WaylandAutoHideScreenEdgeV1 final
    : public QtWayland::kde_auto_hide_screen_edge_v1
{
public:
    WaylandAutoHideScreenEdgeV1(
        const Plasma::Types::Location location,
        ::kde_auto_hide_screen_edge_v1 *edge)
        : QtWayland::kde_auto_hide_screen_edge_v1(edge)
        , location(location)
    {
    }

    ~WaylandAutoHideScreenEdgeV1()
    {
        destroy();
    }

    const Plasma::Types::Location location;
};

AutoHideScreenEdge::AutoHideScreenEdge(Latte::View *view, QObject *parent)
    : QObject(parent)
    , m_view(view)
    , m_manager(std::make_unique<WaylandScreenEdgeManagerV1>())
{
    Q_ASSERT(view);

    view->installEventFilter(this);

    connect(view, &Latte::View::locationChanged,
            this, &AutoHideScreenEdge::refreshRegistration);
    connect(m_manager.get(), &QWaylandClientExtension::activeChanged,
            this, [this]() {
        destroyEdge();
        Q_EMIT supportedChanged();
        refreshRegistration();
    });
}

AutoHideScreenEdge::~AutoHideScreenEdge()
{
    if (m_view) {
        m_view->removeEventFilter(this);
    }

    m_armed = false;
    destroyEdge();
}

bool AutoHideScreenEdge::isArmed() const
{
    return m_armed;
}

bool AutoHideScreenEdge::isRegistered() const
{
    return m_edge != nullptr;
}

bool AutoHideScreenEdge::isSupported() const
{
    return m_manager && m_manager->isActive();
}

void AutoHideScreenEdge::setArmed(const bool armed)
{
    if (m_armed == armed) {
        refreshRegistration();
        return;
    }

    m_armed = armed;
    Q_EMIT armedChanged();

    if (!m_armed) {
        if (m_edge) {
            m_edge->deactivate();
        }
        return;
    }

    if (!isSupported() && !m_reportedUnsupported) {
        m_reportedUnsupported = true;
        qWarning() << "KWin does not advertise kde_screen_edge_manager_v1;"
                      " falling back to the client edge window";
    }

    refreshRegistration();
}

bool AutoHideScreenEdge::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_view) {
        return false;
    }

    if (event->type() == QEvent::Expose) {
        refreshRegistration();
    } else if (event->type() == QEvent::Hide) {
        destroyEdge();
    }

    return false;
}

bool AutoHideScreenEdge::createEdge()
{
    Q_ASSERT(!m_edge);

    if (!m_view || !isSupported()) {
        return false;
    }

    auto *const surface = KWayland::Client::Surface::fromWindow(m_view);
    if (!surface) {
        qWarning() << "Cannot register the dock screen edge before its"
                      " Wayland surface exists";
        return false;
    }

    uint32_t border{0};
    switch (m_view->location()) {
    case Plasma::Types::LeftEdge:
        border = QtWayland::kde_screen_edge_manager_v1::border_left;
        break;
    case Plasma::Types::RightEdge:
        border = QtWayland::kde_screen_edge_manager_v1::border_right;
        break;
    case Plasma::Types::TopEdge:
        border = QtWayland::kde_screen_edge_manager_v1::border_top;
        break;
    case Plasma::Types::BottomEdge:
        border = QtWayland::kde_screen_edge_manager_v1::border_bottom;
        break;
    default:
        qCritical() << "Cannot register a dock screen edge for location"
                    << static_cast<int>(m_view->location());
        return false;
    }

    auto *const edge = m_manager->get_auto_hide_screen_edge(border, *surface);
    if (!edge) {
        qCritical() << "KWin returned no auto-hide screen-edge object for"
                    << m_view->validTitle();
        return false;
    }

    m_edge = std::make_unique<WaylandAutoHideScreenEdgeV1>(
        m_view->location(), edge);
    Q_EMIT registeredChanged();
    return true;
}

void AutoHideScreenEdge::destroyEdge()
{
    if (!m_edge) {
        return;
    }

    m_edge->deactivate();
    m_edge.reset();
    Q_EMIT registeredChanged();
}

void AutoHideScreenEdge::refreshRegistration()
{
    if (!m_view) {
        destroyEdge();
        return;
    }

    if (m_edge && m_edge->location != m_view->location()) {
        destroyEdge();
    }

    if (!m_armed || !isSupported() || !m_view->isExposed()) {
        return;
    }

    if (!m_edge && !createEdge()) {
        return;
    }

    m_edge->activate();
}

}
}
