/*
    SPDX-FileCopyrightText: 2016 Smith AR <audoban@openmailbox.org>
    SPDX-FileCopyrightText: 2016 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "infoview.h"

// local
#include <config-latte.h>
#include "wm/abstractwindowinterface.h"
#include "view/panelshadows_p.h"

// Qt
#include <QQuickItem>
#include <QRandomGenerator>
#include <QQmlContext>
#include <QQmlEngine>
#include <QScreen>

// KDE
#include <KLocalizedContext>
#include <KWindowSystem>

// Plasma
#include <KPackage/Package>

// LayerShellQt
#include <LayerShellQt/Window>

namespace Latte {

InfoView::InfoView(Latte::Corona *corona, QString message, QScreen *screen, QWindow *parent)
    : QQuickView(parent),
      m_corona(corona),
      m_message(message),
      m_screen(screen)
{
    m_id = QString::number(QRandomGenerator::global()->bounded(1000));

    setTitle(validTitle());

    setupWaylandIntegration();

    setResizeMode(QQuickView::SizeViewToRootObject);
    setColor(QColor(Qt::transparent));
    setDefaultAlphaBuffer(true);

    setIcon(qGuiApp->windowIcon());

    setScreen(screen);
    setFlags(wFlags());

    connect(m_corona->wm(), &WindowSystem::AbstractWindowInterface::latteWindowAdded, this, &InfoView::updateWaylandId);

    init();
}

InfoView::~InfoView()
{
    PanelShadows::self()->removeWindow(this);

    qDebug() << "InfoView deleting ...";
}

void InfoView::init()
{
    rootContext()->setContextProperty(QStringLiteral("infoWindow"), this);

    //! KDeclarative's engine setup is gone in KF6; the piece Latte needs from
    //! it is the i18n() context for the QML side
    auto *localizedContext = new KLocalizedContext(engine());
    localizedContext->setTranslationDomain(QStringLiteral("latte-dock"));
    engine()->rootContext()->setContextObject(localizedContext);

    auto source = QUrl::fromLocalFile(m_corona->kPackage().filePath("infoviewui"));
    setSource(source);

    rootObject()->setProperty("message", m_message);

    syncGeometry();
}

QString InfoView::validTitle() const
{
    return "#layoutinfowindow#" + m_id;
}

KSvg::FrameSvg::EnabledBorders InfoView::enabledBorders() const
{
    return m_borders;
}


inline Qt::WindowFlags InfoView::wFlags() const
{
    return (flags() | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint) & ~Qt::WindowDoesNotAcceptFocus;
}

void InfoView::syncGeometry()
{
    const QSize size(rootObject()->width(), rootObject()->height());
    const auto sGeometry = screen()->geometry();

    setMaximumSize(size);
    setMinimumSize(size);
    resize(size);

    QPoint position{sGeometry.center().x() - size.width() / 2, sGeometry.center().y() - size.height() / 2 };

    //! under wayland the surface is unanchored and the compositor centres it
    setPosition(position);
}

void InfoView::showEvent(QShowEvent *ev)
{
    QQuickWindow::showEvent(ev);

    m_corona->wm()->setViewExtraFlags(this);
    setFlags(wFlags());

    m_corona->wm()->enableBlurBehind(*this);

    syncGeometry();

    QTimer::singleShot(400, this, &InfoView::syncGeometry);

    PanelShadows::self()->addWindow(this);
    PanelShadows::self()->setEnabledBorders(this, m_borders);
}

void InfoView::updateWaylandId()
{
    Latte::WindowSystem::WindowId newId = m_corona->wm()->winIdFor("latte-dock", validTitle());

    if (m_trackedWindowId != newId) {
        if (!m_trackedWindowId.isEmpty()) {
            m_corona->wm()->unregisterIgnoredWindow(m_trackedWindowId, this);
        }

        m_trackedWindowId = newId;
        m_corona->wm()->registerIgnoredWindow(m_trackedWindowId, this);
    }
}

void InfoView::setupWaylandIntegration()
{
    if (!KWindowSystem::isPlatformWayland()) {
        return;
    }

    //! the info window is a free-floating centered notification: an
    //! unanchored layer surface is centred by the compositor on its output
    if (LayerShellQt::Window *layer = LayerShellQt::Window::get(this)) {
        layer->setScope(QStringLiteral("dock"));
        layer->setLayer(LayerShellQt::Window::LayerTop);
        layer->setAnchors(LayerShellQt::Window::Anchors());
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityNone);
    }
}

void InfoView::setOnActivities(QStringList activities)
{
    //! STUB: Phase 4 - there is no wayland equivalent of the old WId-based
    //! setOnActivities here yet; the info window shows on all activities
    //! until the window-system backend phase decides whether per-activity
    //! placement needs a wayland implementation
    Q_UNUSED(activities);
}

}
// kate: indent-mode cstyle; indent-width 4; replace-tabs on;
