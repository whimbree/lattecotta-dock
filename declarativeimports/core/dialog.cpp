/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "dialog.h"
#include "windowresizehandler.h"

// Qt
#include <QDebug>
#include <QEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>

#include <algorithm>
#include <cmath>

// KDE
#include <KConfigGroup>
#include <KWindowEffects>
#include <KWindowSystem>

// Plasma
#include <Plasma/Applet>
#include <plasmaquick/plasmashellwaylandintegration.h>


namespace Latte {
namespace Quick {

Dialog::Dialog(QQuickItem *parent)
    : PlasmaQuick::Dialog(parent)
{
    connect(this, &PlasmaQuick::Dialog::visualParentChanged, this, &Dialog::onVisualParentChanged);

    //! resizable persistent popups: the resize grab zones depend on the
    //! dialog type (AppletPopup only) and on which edge faces the dock
    connect(this, &PlasmaQuick::Dialog::typeChanged, this, &Dialog::updateResizeSupport);
    connect(this, &Dialog::edgeChanged, this, &Dialog::updateResizeSupport);

    //! backstop end-of-resize detection: during a compositor-driven
    //! interactive resize the client receives no mouse events (the
    //! compositor owns the grab), only a stream of Resize events, and no
    //! protocol tells the client the grab ended. The PRIMARY end signals
    //! are the pointer coming back to us after release (Enter/MouseMove/
    //! MouseButtonRelease in event()) and the popup hiding. This timer only
    //! covers a release that lands outside the window with the popup never
    //! hovered again - without it the session would stay open and dock
    //! geometry changes in that window would leave the popup unanchored.
    //! Kept long on purpose: firing during a mid-drag PAUSE (button still
    //! held) persists the paused size early and re-anchors once under the
    //! live grab - recoverable but visible - so the backstop should
    //! essentially never win against a real release.
    m_resizeSettleTimer.setSingleShot(true);
    m_resizeSettleTimer.setInterval(2000);
    connect(&m_resizeSettleTimer, &QTimer::timeout, this, &Dialog::finishSystemResize);

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

QObject *Dialog::applet() const
{
    return m_applet;
}

void Dialog::setApplet(QObject *applet)
{
    Plasma::Applet *plasmaApplet = qobject_cast<Plasma::Applet *>(applet);

    if (applet && !plasmaApplet) {
        qWarning() << "Latte::Quick::Dialog::setApplet expects a Plasma::Applet, got"
                   << applet->metaObject()->className() << "- popup size persistence stays disabled";
    }

    if (m_applet == plasmaApplet) {
        return;
    }

    if (m_applet) {
        m_applet->removeEventFilter(this);
    }

    m_applet = plasmaApplet;

    if (m_applet) {
        loadPersistedPopupSize();

        //! the context menu's "Reset Popup Size" runs in the app binary
        //! while this dialog lives in the QML plugin; the applet object is
        //! what both sides share, so the reset announces itself by bumping
        //! a dynamic property on it (QEvent::DynamicPropertyChange below).
        //! KConfigWatcher was tried first and is structurally unfit here:
        //! kconfig's change notification is a DBus signal whose object
        //! path embeds the config file name, layout files routinely carry
        //! spaces ("My Layout.layout.latte") which are illegal in a DBus
        //! object path, and no ConfigChanged ever reached the bus for them
        //! (verified with dbus-monitor against a notify-flagged write).
        m_applet->installEventFilter(this);
    } else {
        setCustomPopupSize(QSize());
    }

    updateResizeSupport();
    Q_EMIT appletChanged();
}

bool Dialog::eventFilter(QObject *watched, QEvent *e)
{
    if (watched == m_applet && e->type() == QEvent::DynamicPropertyChange) {
        auto *pe = static_cast<QDynamicPropertyChangeEvent *>(e);

        //! the "Reset Popup Size" wake-up, see setApplet(). The config keys
        //! are already deleted by the sender; re-loading finds them gone,
        //! clears customPopupSize and the QML sizing chain returns a pinned
        //! popup to its live hint size in place.
        if (pe->propertyName() == QByteArrayLiteral("_latte_popupSizeReset") && !m_inSystemResize) {
            loadPersistedPopupSize();
        }
    }

    return PlasmaQuick::Dialog::eventFilter(watched, e);
}

QSize Dialog::customPopupSize() const
{
    return m_customPopupSize;
}

void Dialog::setCustomPopupSize(const QSize &size)
{
    if (m_customPopupSize == size) {
        return;
    }

    m_customPopupSize = size;
    Q_EMIT customPopupSizeChanged();
}

QMargins Dialog::frameMargins() const
{
    QMargins frame;

    if (QObject *m = margins()) {
        frame = QMargins(m->property("left").toInt(), m->property("top").toInt(),
                         m->property("right").toInt(), m->property("bottom").toInt());
    }

    return frame;
}

void Dialog::updateResizeSupport()
{
    //! scoped STRICTLY to AppletPopup dialogs with a persistence applet:
    //! hover previews and tooltips must never become resizable
    const bool supported = m_applet && type() == Dialog::AppletPopup;

    if (!m_resizeHandler) {
        if (!supported) {
            return;
        }
        m_resizeHandler = new WindowResizeHandler(this);
        connect(m_resizeHandler, &WindowResizeHandler::resizeStarted, this, &Dialog::onSystemResizeStarted);
    }

    //! grab zones are the frame margins, like plasmashell's padding()-sized
    //! zones; the dock-facing edge is excluded - the popup is anchored
    //! there (its border is hidden for the attached look) and the
    //! compositor would keep that edge fixed anyway
    Qt::Edges edges;

    if (supported) {
        edges = Qt::LeftEdge | Qt::RightEdge | Qt::TopEdge | Qt::BottomEdge;

        switch (m_edge) {
        case Plasma::Types::LeftEdge:
            edges &= ~Qt::Edges(Qt::LeftEdge);
            break;
        case Plasma::Types::RightEdge:
            edges &= ~Qt::Edges(Qt::RightEdge);
            break;
        case Plasma::Types::TopEdge:
            edges &= ~Qt::Edges(Qt::TopEdge);
            break;
        default:
            edges &= ~Qt::Edges(Qt::BottomEdge);
            break;
        }
    }

    m_resizeHandler->setActiveEdges(edges);
    m_resizeHandler->setMargins(frameMargins());
}

void Dialog::onSystemResizeStarted()
{
    m_inSystemResize = true;

    //! freeze the sizing chain on the current content size for the whole
    //! drag: without this a representation whose implicit size reflows
    //! mid-drag (content wrapping as the width changes) re-evaluates the
    //! QML bindings and resizes the window against the compositor's grab
    m_preResizeCustomSize = m_customPopupSize;
    m_resizeStartContentSize = mainItem() ? QSize(mainItem()->width(), mainItem()->height())
                                          : size().shrunkBy(frameMargins());
    setCustomPopupSize(m_resizeStartContentSize);

    m_resizeSettleTimer.start();
}

void Dialog::finishSystemResize()
{
    if (!m_inSystemResize) {
        return;
    }

    m_inSystemResize = false;
    m_resizeSettleTimer.stop();

    const QSize content = size().shrunkBy(frameMargins());

    if (!content.isEmpty() && content != m_resizeStartContentSize) {
        setCustomPopupSize(content);
        persistPopupSize();
    } else {
        //! a grab that never changed the size (press on the border,
        //! release in place) is not a resize: restore the pre-session
        //! state so "has a custom size" keeps meaning the user chose one
        setCustomPopupSize(m_preResizeCustomSize);
    }

    //! re-anchor exactly once, on release; every reposition path was
    //! suppressed while the compositor owned the geometry
    updateGeometry();
}

void Dialog::loadPersistedPopupSize()
{
    const KConfigGroup config = m_applet->config();
    QSize size(config.readEntry("popupWidth", 0), config.readEntry("popupHeight", 0));

    if (size.width() > 0 && size.height() > 0) {
        //! a size persisted on a larger monitor must not overflow this
        //! screen - the same 95% cap plasmashell's AppletPopup enforces
        //! continuously in updateMaxSize()
        if (const QScreen *s = screen()) {
            size.setWidth(std::min(size.width(), int(std::round(s->geometry().width() * 0.95))));
            size.setHeight(std::min(size.height(), int(std::round(s->geometry().height() * 0.95))));
        }
        setCustomPopupSize(size);
    } else {
        setCustomPopupSize(QSize());
    }
}

void Dialog::persistPopupSize()
{
    if (!m_applet) {
        qWarning() << "Latte::Quick::Dialog: cannot persist popup size, no applet is set";
        return;
    }

    //! DELIBERATE deviation from plasmashell's AppletPopup, which saves on
    //! every hideEvent(): persisting only from an actual finished user
    //! resize keeps "has a custom size" meaningful, so the context menu's
    //! "Reset Popup Size" entry can hide itself when there is nothing to
    //! reset. The keys and group are exactly plasmashell's.
    KConfigGroup config = m_applet->config();
    config.writeEntry("popupWidth", m_customPopupSize.width());
    config.writeEntry("popupHeight", m_customPopupSize.height());
    config.sync();
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
    //! the compositor owns the geometry for the whole interactive resize;
    //! any position re-assert now fights the drag. finishSystemResize()
    //! re-anchors once when the grab ends.
    if (m_inSystemResize) {
        return;
    }

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
    //! see syncAnchoredWaylandPosition(): no repositioning under an active
    //! interactive-resize grab
    if (m_inSystemResize) {
        return;
    }

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

    //! AppletPopup dialogs keep location pinned to the dock edge ALWAYS.
    //! The base class derives its compositor slide hint from location and
    //! applies it inside its first-expose handler, IMMEDIATELY before the
    //! call that renders and commits the mapping frame (on the threaded
    //! loop that call blocks until the frame is out) - so whatever
    //! location says at that instant is the slide KWin maps with, and no
    //! re-assert from our side can ever run in between. With location
    //! Floating the base UNSET the slide on every open and the popup faded
    //! in via the generic effects while sliding out correctly from our
    //! later hints (user-reproduced through four ordering attempts; the
    //! full hunt is in the plan). Pinning the edge also hides the popup's
    //! dock-facing border - the Qt5 Latte attached-popup look.
    if (hideEdgeBorder || type() == Dialog::AppletPopup) {
        setLocation(m_edge);
    } else {
        setLocation(Plasma::Types::Floating);
    }

    //! enabled borders decide the frame margins, and the margins are the
    //! resize grab zones - refresh them together
    updateResizeSupport();
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
    if (m_inSystemResize) {
        if (e->type() == QEvent::Resize) {
            //! the compositor is still driving the drag; keep the backstop
            //! timer at bay while geometry keeps flowing
            m_resizeSettleTimer.start();
        } else if (e->type() == QEvent::Enter || e->type() == QEvent::Hide) {
            //! the compositor sends the surface a pointer LEAVE when its
            //! resize grab engages and an ENTER when the grab releases with
            //! the cursor over us again, so Enter is the fast end signal
            //! for the common release-on-the-window case; a hide ends the
            //! session unconditionally. MouseMove and MouseButtonRelease
            //! are deliberately NOT end triggers: input already in flight
            //! between startSystemResize() and the grab actually engaging
            //! is still delivered to us a beat later and ended the session
            //! instantly while the compositor resize carried on (probed
            //! live: the release landed in the same millisecond as the
            //! session start, the window then grew 150px with the session
            //! already dead and nothing was persisted).
            finishSystemResize();
        }
    }

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
