/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "dialog.h"

// Qt
#include <QEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>

// KDE
#include <KWindowEffects>
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

    //! the base class's updateVisibility() derives a slide hint from its
    //! location property and UNSETS the slide (NoEdge) when location is
    //! Floating - which Latte keeps deliberately, location drives border
    //! drawing here. That unset runs from this same visibleChanged signal,
    //! connected in the base constructor, i.e. always BEFORE this slot; by
    //! re-asserting right after it in the same synchronous emission the
    //! hint is current before the first frame can commit. Without this the
    //! popup faded in (compositor fallback) yet slid OUT, because by close
    //! time our event-driven re-asserts had landed (user-reproduced
    //! consistently, fixed by exactly this ordering).
    connect(this, &QWindow::visibleChanged, this, [this](bool shown) {
        if (shown) {
            updateSlideEffect(QRect(position(), pendingContentSize()));
        }
    });
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
    return (type() == Dialog::Normal || type() == Dialog::PopupMenu || type() == Dialog::Tooltip || type() == Dialog::Dock || type() == Dialog::AppletPopup);
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

QSize Dialog::pendingContentSize() const
{
    //! the size the dialog is about to have, not the stale QWindow::size().
    //! When the previews content switches to another task the mainItem has
    //! already resized while the window has not caught up yet; centering on
    //! the anchor with the old width fights the base class's
    //! syncToMainItemSize() placement (which uses the new size) and the
    //! popup visibly bounces between the two spots on every crossing.
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

    return nextSize;
}

void Dialog::updateSlideEffect(const QRect &globalGeometry)
{
    //! Applet popups slide in from the dock's screen edge, exactly like
    //! plasmashell's popups (PopupPlasmaWindow::updateSlideEffect - the
    //! compositor's sliding-popups effect, requested per window through
    //! KWindowEffects). Only the Normal-type dialogs slide: that is the
    //! applet popup class (CompactApplet's popupWindow). Tooltip-flavored
    //! dialogs (task previews, title tooltips, the rearrange modal) do not
    //! slide in plasma either - their windows appear in place.
    if (type() != Dialog::Normal && type() != Dialog::AppletPopup) {
        return;
    }

    KWindowEffects::SlideFromLocation slideLocation = KWindowEffects::NoEdge;

    switch (m_edge) {
    case Plasma::Types::BottomEdge:
        slideLocation = KWindowEffects::BottomEdge;
        break;
    case Plasma::Types::TopEdge:
        slideLocation = KWindowEffects::TopEdge;
        break;
    case Plasma::Types::LeftEdge:
        slideLocation = KWindowEffects::LeftEdge;
        break;
    case Plasma::Types::RightEdge:
        slideLocation = KWindowEffects::RightEdge;
        break;
    default:
        break;
    }

    //! offset ALWAYS -1: the compositor computes the slide origin from the
    //! window's real geometry at animation time (screen edge to window edge,
    //! the same arithmetic we would do). Computing it client-side broke the
    //! slide-in invisibly: the pre-map hint (the only one that reaches the
    //! compositor before the mapping commit) read pre-map garbage geometry,
    //! and the effect clips the sliding window to the region above the
    //! offset line - a garbage offset makes that clip EMPTY, so the whole
    //! slide-in animated invisibly and the popup seemed to pop in, while
    //! slide-out worked from post-map re-asserts with sane values
    //! (wire-dumped: offset 2301 on the pre-map hint, -1 on the late ones).
    KWindowEffects::slideWindow(this, slideLocation, -1);
}

void Dialog::syncAnchoredWaylandPosition()
{
    //! while hidden the base class places the window itself on show
    if (!KWindowSystem::isPlatformWayland() || !visualParent() || !isVisible()) {
        return;
    }

    //! QWindow-level moves (setPosition()/setGeometry()) are NO-OPs for a
    //! visible wayland window: the logged window position stayed frozen
    //! through every call while the previews window switched tasks, so the
    //! popup kept showing new content at the previous task's spot. The
    //! plasmashell surface can be positioned at any time; this is the same
    //! integration the base class uses for its show-time placement.
    //!
    //! The target is recomputed FRESH from the current anchor and content
    //! size on every call, never remembered: a stored position goes stale
    //! the moment the content resizes, and whether the resize handler or
    //! the base class's stale re-send ran last decided where the popup
    //! ended up (user-reproduced: a hover sweep mapped the previews with
    //! the previous task's width, the base recentered correctly on the
    //! resize, then its next re-send reverted the fix and the window sat
    //! parked ~450px off its task). Recomputing makes every event
    //! self-healing regardless of ordering.
    const QSize nextSize = pendingContentSize();
    const QPoint target = popupPosition(visualParent(), nextSize);

    PlasmaShellWaylandIntegration::get(this)->setPosition(target);
    updateSlideEffect(QRect(target, nextSize));
}

void Dialog::updateGeometry()
{
    //! while hidden the base class places the window itself on show
    if (!visualParent() || !isVisible()) {
        return;
    }

    const QSize nextSize = pendingContentSize();
    const QPoint target = popupPosition(visualParent(), nextSize);

    adjustGeometry(QRect(target, nextSize));

    if (KWindowSystem::isPlatformWayland()) {
        PlasmaShellWaylandIntegration::get(this)->setPosition(target);
    }

    updateSlideEffect(QRect(target, nextSize));
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

    if (e->type() == QEvent::PlatformSurface
               && static_cast<QPlatformSurfaceEvent *>(e)->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated) {
        //! Qt destroys the wl_surface on every hide, so every per-surface
        //! state must be re-established on the NEW surface before its first
        //! commit maps the window. The base class re-applies the plasma
        //! role only from applyType() during show handling, which loses the
        //! race against the render thread's mapping commit: KWin then maps
        //! an appletpopup as a role-less window (probe: popupWindow=false at
        //! windowAdded), the open animates with the generic window effects
        //! and the sliding-popups effect never sees it. Re-apply the role
        //! synchronously at surface creation, before any commit can exist.
        if (KWindowSystem::isPlatformWayland() && type() == Dialog::AppletPopup) {
            PlasmaShellWaylandIntegration::get(this)->setRole(QtWayland::org_kde_plasma_surface::role_appletpopup);
            PlasmaShellWaylandIntegration::get(this)->setPanelBehavior(QtWayland::org_kde_plasma_surface::panel_behavior_always_visible);
        }
        //! the slide hint must be CURRENT on the surface before the commit
        //! that maps the window, or the compositor animates the fade effect
        //! instead of the slide (user-reproduced consistently: fade-in,
        //! slide-out - by close time the hint had landed). Under the
        //! threaded render loop the render thread races the gui thread to
        //! the mapping commit, so hints sent from the Show event arrive one
        //! commit late; SurfaceCreated precedes any commit by definition.
        //! The geometry is not final here - offset -1 lets the compositor
        //! compute it, which matches our arithmetic anyway.
        updateSlideEffect(QRect(position(), pendingContentSize()));
    }

    const bool result = PlasmaQuick::Dialog::event(e);

    //! The base class re-sends QWindow::position() to the plasmashell
    //! surface on every Move event and on expose (libplasma dialog.cpp,
    //! QEvent::Move handler and updateVisibility()). On wayland that value
    //! is permanently stuck at the show-time position, so each re-send
    //! teleports the window back and undoes any anchored placement (watched
    //! live in WAYLAND_DEBUG: every one of our set_position requests was
    //! followed by one carrying the stale spot). Re-assert the anchored
    //! position AFTER the base has handled the event, so ours is always the
    //! last request the compositor sees - recomputed fresh, see
    //! syncAnchoredWaylandPosition() for why storing it raced.
    if (e->type() == QEvent::Move || e->type() == QEvent::Expose || e->type() == QEvent::Show) {
        syncAnchoredWaylandPosition();
    }

    return result;
}

}
}
