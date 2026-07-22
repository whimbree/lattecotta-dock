/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "subwindow.h"

// local
#include "../view.h"
#include "../visibilitymanager.h"
#include "../../wm/waylandlayershell.h"

// Qt
#include <QDebug>
#include <QSurfaceFormat>
#include <QQuickView>
#include <QTimer>

// KDE
#include <KWindowSystem>

namespace Latte {
namespace ViewPart {

SubWindow::SubWindow(Latte::View *view, QString debugType) :
    m_latteView(view)
{
    m_corona = qobject_cast<Latte::Corona *>(view->corona());

    m_debugMode = (qApp->arguments().contains("-d") && qApp->arguments().contains("--kwinedges"));
    m_debugType = debugType;

    m_showColor = QColor(Qt::transparent);
    m_hideColor = QColor(Qt::transparent);

    setTitle(validTitle());
    setColor(m_showColor);
    setDefaultAlphaBuffer(true);

    setFlags(Qt::FramelessWindowHint
             | Qt::WindowStaysOnTopHint
             | Qt::NoDropShadowWindowHint
             | Qt::WindowDoesNotAcceptFocus);

    m_fixGeometryTimer.setSingleShot(true);
    m_fixGeometryTimer.setInterval(500);
    connect(&m_fixGeometryTimer, &QTimer::timeout, this, &SubWindow::fixGeometry);

    connect(this, &QQuickView::xChanged, this, &SubWindow::startGeometryTimer);
    connect(this, &QQuickView::yChanged, this, &SubWindow::startGeometryTimer);
    connect(this, &QQuickView::widthChanged, this, &SubWindow::startGeometryTimer);
    connect(this, &QQuickView::heightChanged, this, &SubWindow::startGeometryTimer);

    connect(this, &SubWindow::calculatedGeometryChanged, this, &SubWindow::fixGeometry);

    connect(m_latteView, &Latte::View::absoluteGeometryChanged, this, &SubWindow::updateGeometry);
    connect(m_latteView, &Latte::View::screenGeometryChanged, this, &SubWindow::updateGeometry);
    connect(m_latteView, &Latte::View::locationChanged, this, &SubWindow::updateGeometry);
    connect(m_latteView, &QQuickView::screenChanged, this, [this]() {
        setScreen(m_latteView->screen());
        updateGeometry();
    });

    setupWaylandIntegration();

    connect(m_corona->wm(), &WindowSystem::AbstractWindowInterface::latteWindowAdded, this, &SubWindow::updateWaylandId);

    setScreen(m_latteView->screen());
    show();
    hideWithMask();
}

SubWindow::~SubWindow()
{
    m_inDelete = true;

    m_corona->wm()->unregisterIgnoredWindow(m_trackedWindowId, this);

    m_latteView = nullptr;
}

int SubWindow::location()
{
    return (int)m_latteView->location();
}

int SubWindow::thickness() const
{
    return m_thickness;
}

QString SubWindow::validTitlePrefix() const
{
    return QString("#subwindow#");
}

QString SubWindow::validTitle() const
{
    return QString(validTitlePrefix() + QString::number(m_latteView->containment()->id()));
}

Latte::View *SubWindow::parentView()
{
    return m_latteView;
}

Latte::WindowSystem::WindowId SubWindow::trackedWindowId()
{
    //! wayland ids are compositor uuids and a uuid never parses as a
    //! number, so the Qt5-era 'toInt() <= 0' (id not yet assigned) test
    //! had become constant-true here: under wayland every call lazily
    //! re-resolves the id. Kept unconditional deliberately - the reshow
    //! hack remaps the surface with a fresh uuid, and the wm only emits
    //! latteWindowAdded for isAcceptableWindow() windows, which latte's
    //! own skip-taskbar subwindows are not, so this lazy re-resolve is
    //! the reliable path. Tightening it to an isEmpty() check needs a
    //! live-session pass first.
    if (KWindowSystem::isPlatformWayland()) {
        updateWaylandId();
    }

    return m_trackedWindowId;
}

void SubWindow::fixGeometry()
{
    if (!m_calculatedGeometry.isEmpty()
            && (m_calculatedGeometry.x() != x() || m_calculatedGeometry.y() != y()
                || m_calculatedGeometry.width() != width() || m_calculatedGeometry.height() != height())) {
        setMinimumSize(m_calculatedGeometry.size());
        setMaximumSize(m_calculatedGeometry.size());
        resize(m_calculatedGeometry.size());
        setPosition(m_calculatedGeometry.x(), m_calculatedGeometry.y());
    }
}

void SubWindow::updateWaylandId()
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

void SubWindow::startGeometryTimer()
{
    m_fixGeometryTimer.start();
}

void SubWindow::setupWaylandIntegration()
{
    if (!KWindowSystem::isPlatformWayland() || !m_latteView || !m_latteView->containment()) {
        return;
    }

    //! edge helper strips sit centered on the dock's edge; they must never
    //! steal keyboard focus from the compositor. windowSpansScreenLength is
    //! false: these strips are sized to a fraction of the screen length (the
    //! dock's own length, not the whole edge), so they keep the single-edge
    //! Center anchoring - the full-length span is only for masked dock windows.
    namespace LS = Latte::WindowSystem::LayerShell;
    LS::configureView(this, m_latteView->screen(), m_latteView->location(), Latte::Types::Center, false);
    LS::setFocusPolicy(this, false);
}

bool SubWindow::event(QEvent *e)
{
    if (e->type() == QEvent::Show) {
        m_corona->wm()->setViewExtraFlags(this);
    }

    return QQuickView::event(e);
}


void SubWindow::hideWithMask()
{
    if (m_debugMode) {
        qDebug() << m_debugType + " :: MASK HIDE...";
    }

    setMask(VisibilityManager::ISHIDDENMASK);

    //! repaint in order to update mask immediately
    setColor(m_hideColor);
}

void SubWindow::showWithMask()
{
    if (m_debugMode) {
        qDebug() << m_debugType + " :: MASK SHOW...";
    }

    setMask(QRegion());

    //! repaint in order to update mask immediately
    setColor(m_showColor);
}

}
}
