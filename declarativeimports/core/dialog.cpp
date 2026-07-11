/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "dialog.h"

// Qt
#include <QScreen>
#include <QWindow>

// KDE
#include <KWindowSystem>

// Plasma
#include <plasmaquick/plasmashellwaylandintegration.h>


namespace Latte {
namespace Quick {

Dialog::Dialog(QQuickItem *parent)
    : PlasmaQuick::Dialog(parent)
{
    connect(this, &PlasmaQuick::Dialog::visualParentChanged, this, &Dialog::onVisualParentChanged);

    //! reposition when the content resizes. The base class is not reliable
    //! here on wayland during rapid content switches, and a stale placement
    //! computed for the previous content width then survives the resize
    //! (previews window observed sitting half its width off its task).
    connect(this, &PlasmaQuick::Dialog::mainItemChanged, this, &Dialog::onMainItemChanged);
    onMainItemChanged();
}

void Dialog::onMainItemChanged()
{
    for (auto &c : m_mainItemConnections) {
        disconnect(c);
    }

    if (mainItem()) {
        m_mainItemConnections[0] = connect(mainItem(), &QQuickItem::widthChanged, this, &Dialog::updateGeometry);
        m_mainItemConnections[1] = connect(mainItem(), &QQuickItem::heightChanged, this, &Dialog::updateGeometry);
    }
}

bool Dialog::containsMouse() const
{
    return m_containsMouse;
}

void Dialog::setContainsMouse(bool contains)
{
    if (m_containsMouse == contains) {
        return;
    }

    m_containsMouse = contains;
    Q_EMIT containsMouseChanged();
}

Plasma::Types::Location Dialog::edge() const
{
    return m_edge;
}

void Dialog::setEdge(const Plasma::Types::Location &edge)
{
    if (m_edge == edge) {
        return;
    }

    m_edge = edge;
    Q_EMIT edgeChanged();
}

bool Dialog::respectsAppletsLayoutGeometry() const
{
    return m_respectsAppletsLayoutGeometry;
}

void Dialog::setRespectsAppletsLayoutGeometry(bool respects)
{
    if (m_respectsAppletsLayoutGeometry == respects) {
        return;
    }

    m_respectsAppletsLayoutGeometry = respects;
    Q_EMIT respectsAppletsLayoutGeometryChanged();
}

bool Dialog::isRespectingAppletsLayoutGeometry() const
{
    if (!m_respectsAppletsLayoutGeometry) {
        return false;
    }

    //! As it appears plasma applets popups are defining their popups to Normal window.
    //! Dock type is needed from wayland scenario. In wayland after a while popups from Normal become Dock types
    return (type() == Dialog::Normal || type() == Dialog::PopupMenu || type() == Dialog::Tooltip || type() == Dialog::Dock);
}

QRect Dialog::appletsLayoutGeometryFromContainment() const
{
    QVariant geom = visualParent() && visualParent()->window() ? visualParent()->window()->property("_applets_layout_geometry") : QVariant();
    return geom.isValid() ? geom.toRect() : QRect();
}

int Dialog::appletsPopUpMargin() const
{
    QVariant margin = visualParent() && visualParent()->window() ? visualParent()->window()->property("_applets_popup_margin") : QVariant();
    return margin.isValid() ? margin.toInt() : -1;
}

void Dialog::onVisualParentChanged()
{
    // clear mode
    for (auto &c : m_visualParentConnections) {
        disconnect(c);
    }

    if (!visualParent() || !flags().testFlag(Qt::ToolTip) || !visualParent()->metaObject())  {
        return;
    }

    bool hassignal = (visualParent()->metaObject()->indexOfSignal(QMetaObject::normalizedSignature("anchoredTooltipPositionChanged()")) != -1);

    if (hassignal) {
        m_visualParentConnections[0] = connect(visualParent(), SIGNAL(anchoredTooltipPositionChanged()) , this, SLOT(updateGeometry()));
    }

    //! one placement per anchor change. Anchors without the follow signal
    //! (the previews' resting anchor) would otherwise only be honored on the
    //! next resize, so switching between two same-sized contents would leave
    //! the window sitting over the previous anchor.
    if (isVisible()) {
        updateGeometry();
    }
}

void Dialog::updateGeometry()
{
    //! while hidden the base class places the window itself on show
    if (!visualParent() || !isVisible()) {
        return;
    }

    //! Position with the size the dialog is about to have, not the stale
    //! QWindow::size(). When the previews content switches to another task
    //! the mainItem has already resized while the window has not caught up
    //! yet; centering on the anchor with the old width fights the base
    //! class's syncToMainItemSize() placement (which uses the new size) and
    //! the popup visibly bounces between the two spots on every crossing.
    QSize nextSize = size();

    if (mainItem()) {
        QSizeF pending(mainItem()->width(), mainItem()->height());

        if (QObject *frameMargins = margins()) {
            pending += QSizeF(frameMargins->property("left").toReal() + frameMargins->property("right").toReal(),
                              frameMargins->property("top").toReal() + frameMargins->property("bottom").toReal());
        }

        if (!pending.isEmpty()) {
            nextSize = pending.toSize();
        }
    }

    const QPoint target = popupPosition(visualParent(), nextSize);

    adjustGeometry(QRect(target, nextSize));

    //! QWindow-level moves (setPosition()/setGeometry()) are NO-OPs for a
    //! visible wayland window: the logged window position stayed frozen
    //! through every call while the previews window switched tasks, so the
    //! popup kept showing new content at the previous task's spot. The
    //! plasmashell surface can be positioned at any time; this is the same
    //! integration the base class uses for its show-time placement.
    if (KWindowSystem::isPlatformWayland()) {
        m_pendingWaylandPosition = target;
        m_hasPendingWaylandPosition = true;
        PlasmaShellWaylandIntegration::get(this)->setPosition(target);
    }
}

void Dialog::updatePopUpEnabledBorders()
{
    QRect appletslayoutgeometry = appletsLayoutGeometryFromContainment();
    int appletspopupmargin = appletsPopUpMargin();

    //! Plasma Scenario
    bool hideEdgeBorder = isRespectingAppletsLayoutGeometry() && !appletslayoutgeometry.isEmpty() && appletspopupmargin==-1;

    if (hideEdgeBorder) {
        setLocation(m_edge);
    } else {
        setLocation(Plasma::Types::Floating);
    }
}

QPoint Dialog::popupPosition(QQuickItem *item, const QSize &size)
{
    auto visualparent = item;

    if (visualparent && visualparent->window() && visualparent->window()->screen()) {
        updatePopUpEnabledBorders();

        QPointF parenttopleftf = visualparent->mapToGlobal(QPointF(0, 0));
        QPoint parenttopleft = parenttopleftf.toPoint();
        QScreen *screen = visualparent->window()->screen();
        QRect screengeometry = screen->geometry();

        int x = 0;
        int y = 0;

        int popupmargin = qMax(0, appletsPopUpMargin());

        if (m_edge == Plasma::Types::LeftEdge || m_edge == Plasma::Types::RightEdge) {
            //! vertical scenario
            screengeometry -= QMargins(0, popupmargin, 0, popupmargin);
            y = parenttopleft.y() + (visualparent->height()/2) - (size.height()/2);
        } else {
            //! horizontal scenario
            screengeometry -= QMargins(popupmargin, 0, popupmargin, 0);
            x = parenttopleft.x() + (visualparent->width()/2) - (size.width()/2);
        }

        if (m_edge == Plasma::Types::LeftEdge) {
            x = parenttopleft.x() + visualparent->width() + popupmargin;
        } else if (m_edge == Plasma::Types::RightEdge) {
            x = parenttopleft.x() - size.width() - popupmargin;
        } else if (m_edge == Plasma::Types::TopEdge) {
            y = parenttopleft.y() + visualparent->height() + popupmargin;
        } else { // bottom case
            y = parenttopleft.y() - size.height() - popupmargin;
        }

        x = qBound(screengeometry.x(), x, screengeometry.right() - size.width() + 1);
        y = qBound(screengeometry.y(), y, screengeometry.bottom() - size.height() + 1);

        QRect appletslayoutgeometry = appletsLayoutGeometryFromContainment();



        if (isRespectingAppletsLayoutGeometry() && !appletslayoutgeometry.isEmpty()) {
            QPoint appletsglobaltopleft = visualparent->window()->mapToGlobal(appletslayoutgeometry.topLeft());

            QRect appletsglobalgeometry(appletsglobaltopleft.x(), appletsglobaltopleft.y(), appletslayoutgeometry.width(), appletslayoutgeometry.height());

            if (m_edge == Plasma::Types::LeftEdge || m_edge == Plasma::Types::RightEdge) {
                int bottomy = appletsglobalgeometry.bottom()-size.height();

                if (appletsglobalgeometry.height() >= size.height()) {
                    y = qBound(appletsglobalgeometry.y(), y, bottomy + 1);
                }
            } else {
                int rightx = appletsglobalgeometry.right()-size.width();

                if (appletsglobalgeometry.width() >= size.width()) {
                    x = qBound(appletsglobalgeometry.x(), x, rightx + 1);
                }
            }
        }

        return QPoint(x,y);
    }

    return PlasmaQuick::Dialog::popupPosition(item, size);
}

/*
void Dialog::adjustGeometry(const QRect &geom)
{
    auto visualparent = visualParent();

    if (visualparent && visualparent->window() && visualparent->window()->screen()) {
        updatePopUpEnabledBorders();

        QPointF parenttopleftf = visualparent->mapToGlobal(QPointF(0, 0));
        QPoint parenttopleft = parenttopleftf.toPoint();
        QScreen *screen = visualparent->window()->screen();
        QRect screengeometry = screen->geometry();

        int x = 0;
        int y = 0;

        if (m_edge == Plasma::Types::LeftEdge || m_edge == Plasma::Types::RightEdge) {
            y = parenttopleft.y() + (visualparent->height()/2) - (geom.height()/2);
        } else {
            x = parenttopleft.x() + (visualparent->width()/2) - (geom.width()/2);
        }

        int popupmargin = qMax(0, appletsPopUpMargin());

        if (m_edge == Plasma::Types::LeftEdge) {
            x = parenttopleft.x() + visualparent->width() + popupmargin;
        } else if (m_edge == Plasma::Types::RightEdge) {
            x = parenttopleft.x() - geom.width() - popupmargin;
        } else if (m_edge == Plasma::Types::TopEdge) {
            y = parenttopleft.y() + visualparent->height() + popupmargin;
        } else { // bottom case
            y = parenttopleft.y() - geom.height() - popupmargin;
        }

        x = qBound(screengeometry.x(), x, screengeometry.right()-1);
        y = qBound(screengeometry.y(), y, screengeometry.bottom()-1);

        QRect appletslayoutgeometry = appletsLayoutGeometryFromContainment();

        if (isRespectingAppletsLayoutGeometry() && !appletslayoutgeometry.isEmpty()) {
            QPoint appletsglobaltopleft = visualparent->window()->mapToGlobal(appletslayoutgeometry.topLeft());

            QRect appletsglobalgeometry(appletsglobaltopleft.x(), appletsglobaltopleft.y(), appletslayoutgeometry.width(), appletslayoutgeometry.height());

            if (m_edge == Plasma::Types::LeftEdge || m_edge == Plasma::Types::RightEdge) {
                int bottomy = appletsglobalgeometry.bottom()-geom.height();

                if (appletsglobalgeometry.height() >= geom.height()) {
                    y = qBound(appletsglobalgeometry.y(), y, bottomy + 1);
                }
            } else {
                int rightx = appletsglobalgeometry.right()-geom.width();

                if (appletsglobalgeometry.width() >= geom.width()) {
                    x = qBound(appletsglobalgeometry.x(), x, rightx + 1);
                }
            }
        }

        QRect repositionedrect(x, y, geom.width(), geom.height());
        setGeometry(repositionedrect);
        return;
    }

    PlasmaQuick::Dialog::adjustGeometry(geom);
}
*/

bool Dialog::event(QEvent *e)
{
    if (e->type() == QEvent::Enter) {
        setContainsMouse(true);
    } else if (e->type() == QEvent::Leave
               || e->type() == QEvent::Hide) {
        setContainsMouse(false);
    }

    const bool result = PlasmaQuick::Dialog::event(e);

    //! The base class re-sends QWindow::position() to the plasmashell
    //! surface on every Move event and on expose (libplasma dialog.cpp,
    //! QEvent::Move handler and updateVisibility()). On wayland that value
    //! is permanently stuck at the show-time position, so each re-send
    //! teleports the window back and undoes updateGeometry()'s anchored
    //! placement (watched live in WAYLAND_DEBUG: every one of our
    //! set_position requests was followed by one carrying the stale spot).
    //! Re-assert the anchored position AFTER the base has handled the
    //! event, so ours is always the last request the compositor sees.
    if (m_hasPendingWaylandPosition
            && isVisible()
            && (e->type() == QEvent::Move || e->type() == QEvent::Expose)) {
        PlasmaShellWaylandIntegration::get(this)->setPosition(m_pendingWaylandPosition);
    }

    if (e->type() == QEvent::Hide) {
        //! next show is placed fresh by the base class; a stale anchored
        //! position must not override it
        m_hasPendingWaylandPosition = false;
    }

    return result;
}

}
}
