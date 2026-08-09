/*
    SPDX-FileCopyrightText: 2022 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "contextmenulayerquickitem.h"

// local
#include "../lattecorona.h"
#include "../layouts/storage.h"
#include "../view/view.h"

// Qt
#include <QMouseEvent>
#include <QPointer>
#include <QVersionNumber>
#include <QLatin1String>

// KDE
#include <KAcceleratorManager>
#include <KActionCollection>
#include <KAuthorized>
#include <KConfigGroup>
#include <KLocalizedString>

// Plasma
#include <Plasma/Applet>
#include <Plasma/Containment>
#include <Plasma/ContainmentActions>
#include <Plasma/Corona>
#include <PlasmaQuick/AppletQuickItem>

namespace Latte {

ContextMenuLayerQuickItem::ContextMenuLayerQuickItem(QQuickItem *parent) :
    QQuickItem(parent)
{
    setAcceptedMouseButtons(Qt::AllButtons);
}

ContextMenuLayerQuickItem::~ContextMenuLayerQuickItem()
{
}

bool ContextMenuLayerQuickItem::menuIsShown() const
{
    return m_contextMenu != nullptr;
}

QObject *ContextMenuLayerQuickItem::view() const
{
    return m_latteView;
}

void ContextMenuLayerQuickItem::setView(QObject *view)
{
    if (m_latteView == view) {
        return;
    }

    m_latteView = qobject_cast<Latte::View *>(view);
    Q_EMIT viewChanged();
}

void ContextMenuLayerQuickItem::onMenuAboutToHide()
{
    if (!m_latteView) {
        return;
    }

    m_latteView->containment()->setStatus(m_lastContainmentStatus);
    m_contextMenu = nullptr;
    Q_EMIT menuChanged();
}

QPoint ContextMenuLayerQuickItem::popUpRelevantToParent(const QRect &parentItem, const QRect popUpRect)
{
    QPoint resultPoint;

    if (!m_latteView) {
        return resultPoint;
    }

    if (m_latteView->location() == Plasma::Types::TopEdge) {
        resultPoint.setX(parentItem.left());
        resultPoint.setY(parentItem.bottom());
    } else if (m_latteView->location() == Plasma::Types::BottomEdge) {
        resultPoint.setX(parentItem.left());
        resultPoint.setY(parentItem.top() - popUpRect.height() - 1);
    } else if (m_latteView->location() == Plasma::Types::LeftEdge) {
        resultPoint.setX(parentItem.right());
        resultPoint.setY(parentItem.top());
    } else if (m_latteView->location() == Plasma::Types::RightEdge) {
        resultPoint.setX(parentItem.left() - popUpRect.width());
        resultPoint.setY(parentItem.top());
    }

    return resultPoint;
}

QPoint ContextMenuLayerQuickItem::popUpRelevantToGlobalPoint(const QRect &parentItem, const QRect popUpRect)
{
    QPoint resultPoint;

    if (!m_latteView) {
        return resultPoint;
    }

    if (m_latteView->location() == Plasma::Types::TopEdge) {
        resultPoint.setX(popUpRect.x());
        resultPoint.setY(popUpRect.y() + 1);
    } else if (m_latteView->location() == Plasma::Types::BottomEdge) {
        resultPoint.setX(popUpRect.x());
        resultPoint.setY(popUpRect.y() - popUpRect.height() - 1);
    } else if (m_latteView->location() == Plasma::Types::LeftEdge) {
        resultPoint.setX(popUpRect.x() + 1);
        resultPoint.setY(popUpRect.y());
    } else if (m_latteView->location() == Plasma::Types::RightEdge) {
        resultPoint.setX(popUpRect.x() - popUpRect.width() - 1);
        resultPoint.setY(popUpRect.y());
    }

    return resultPoint;
}

QPoint ContextMenuLayerQuickItem::popUpTopLeft(Plasma::Applet *applet, const QRect popUpRect)
{
    //! Plasma 6: the graphic item is resolved through AppletQuickItem, the
    //! _plasma_graphicObject property is not reliably set anymore
    PlasmaQuick::AppletQuickItem *ai = PlasmaQuick::AppletQuickItem::itemForApplet(applet);

    QRect globalItemRect = m_latteView->absoluteGeometry();

    if (ai && applet != m_latteView->containment()) {
        QPointF appletGlobalTopLeft = ai->mapToGlobal(QPointF(ai->x(), ai->y()));
        globalItemRect = QRect(appletGlobalTopLeft.x(), appletGlobalTopLeft.y(), ai->width(), ai->height());
    }

    int itemLength = (m_latteView->formFactor() == Plasma::Types::Horizontal ? globalItemRect.width() : globalItemRect.height());
    int menuLength = (m_latteView->formFactor() == Plasma::Types::Horizontal ? popUpRect.width() : popUpRect.height());

    if ((itemLength > menuLength)
            || (applet == m_latteView->containment())
            || (m_latteView && Layouts::Storage::self()->isSubContainment(m_latteView->corona(), applet)) ) {
        return popUpRelevantToGlobalPoint(globalItemRect, popUpRect);
    } else {
        return popUpRelevantToParent(globalItemRect, popUpRect);
    }
}


void ContextMenuLayerQuickItem::mouseReleaseEvent(QMouseEvent *event)
{
    if (!event || !m_latteView) {
        return;
    }

    event->setAccepted(m_latteView->containment()->containmentActions().contains(Plasma::ContainmentActions::eventToString(event)));
    Q_EMIT menuChanged();
}

void ContextMenuLayerQuickItem::mousePressEvent(QMouseEvent *event)
{
    //qDebug() << "Step -1 ...";

    if (!event || !m_latteView || !m_latteView->containment()) {
        return;
    }

    //qDebug() << "Step 0...";

    //even if the menu is executed synchronously, other events may be processed
    //by the qml incubator when plasma is loading, so we need to guard there
    if (m_contextMenu) {
        //qDebug() << "Step 0.5 ...";
        m_contextMenu->close();
        m_contextMenu = nullptr;
        return;
    }

    //qDebug() << "1 ...";
    const QString trigger = Plasma::ContainmentActions::eventToString(event);
    Plasma::ContainmentActions *plugin = m_latteView->containment()->containmentActions().value(trigger);

    if (!plugin || plugin->contextualActions().isEmpty()) {
        event->setAccepted(false);
        return;
    }

    // the plugin can be a single action or a context menu
    // Don't have an action list? execute as single action
    // and set the event position as action data
    if (plugin->contextualActions().length() == 1) {
        QAction *action = plugin->contextualActions().at(0);
        action->setData(event->pos());
        action->trigger();
        event->accept();
        return;
    }

    //qDebug() << "2 ...";
    //the plugin can be a single action or a context menu
    //Don't have an action list? execute as single action
    //and set the event position as action data
    /*if (plugin->contextualActions().length() == 1) {
            QAction *action = plugin->contextualActions().at(0);
            action->setData(event->pos());
            action->trigger();
            event->accept();
            return;
        }*/
    //FIXME: very inefficient appletAt() implementation
    Plasma::Applet *applet = nullptr;

    //! Applet resolution is only meaningful when this layer lives in the
    //! view's own window: appletContainsPos expects containment-root
    //! coordinates, which our event positions match only there. The canvas
    //! window's instance overlays the blueprint margin AROUND the dock
    //! (its input region excludes the dock strip), so no applet can be
    //! under its clicks; resolving with canvas-local coordinates there
    //! produced false matches.
    const bool appletResolutionPossible = (window() == m_latteView);

    //! initialize the appletContainsMethod on the first right click
    if (appletResolutionPossible && !m_appletContainsMethod.isValid()) {
        updateAppletContainsMethod();
    }

    if (appletResolutionPossible) {
        for (const Plasma::Applet *appletTemp : m_latteView->containment()->applets()) {
            PlasmaQuick::AppletQuickItem *ai = PlasmaQuick::AppletQuickItem::itemForApplet(const_cast<Plasma::Applet *>(appletTemp));

            //! ai before any use: applets can exist without a graphic item
            //! (the old code dereferenced ai before its null check)
            if (!ai || !ai->isVisible()) {
                continue;
            }

            bool appletContainsMouse = false;

            if (m_appletContainsMethod.isValid()) {
                QVariant retVal;

                if (!m_appletContainsMethod.invoke(m_appletContainsMethodItem, Qt::DirectConnection, Q_RETURN_ARG(QVariant, retVal)
                                                   , Q_ARG(QVariant, appletTemp->id()), Q_ARG(QVariant, event->pos()))) {
                    //! never swallow a failed invoke: a broken lookup here is
                    //! exactly how the applet menus died silently for months
                    qWarning() << "ContextMenuLayer: appletContainsPos invoke FAILED on" << m_appletContainsMethodItem;
                }

                appletContainsMouse = retVal.toBool();
            } else {
                appletContainsMouse = ai->contains(ai->mapFromItem(this, event->pos()));
            }

            if (appletContainsMouse) {
                applet = ai->applet();
                break;
            }
        }
    }

    if (!applet) {
        applet = m_latteView->containment();
    }

    //qDebug() << "3 ...";

    QMenu *desktopMenu = new QMenu;

    //this is a workaround where Qt now creates the menu widget
    //in .exec before oxygen can polish it and set the following attribute
    desktopMenu->setAttribute(Qt::WA_TranslucentBackground);
    //end workaround

    if (desktopMenu->winId()) {
        desktopMenu->windowHandle()->setTransientParent(window());
    }

    desktopMenu->setAttribute(Qt::WA_DeleteOnClose);
    m_contextMenu = desktopMenu;
    Q_EMIT menuChanged();

    //end workaround
    //!end of plasma official code(workaround)

    //qDebug() << "5 ...";

    Q_EMIT m_latteView->containment()->contextualActionsAboutToShow();

    if (applet && applet != m_latteView->containment()) {
        //qDebug() << "5.3 ...";
        Q_EMIT applet->contextualActionsAboutToShow();
        addAppletActions(desktopMenu, applet, event);
    } else {
        //qDebug() << "5.6 ...";
        addContainmentActions(desktopMenu, event);
    }

    //!plasma official code
    //this is a workaround where Qt will fail to realize a mouse has been released

    // this happens if a window which does not accept focus spawns a new window that takes focus and X grab
    // whilst the mouse is depressed
    // https://bugreports.qt.io/browse/QTBUG-59044
    // this causes the next click to go missing

    //by releasing manually we avoid that situation
    auto ungrabMouseHack = [this]() {
        if (window() && window()->mouseGrabberItem()) {
            window()->mouseGrabberItem()->ungrabMouse();
        }
    };

    //post 5.8.0 QQuickWindow code is sendEvent(item, mouseEvent); item->grabMouse()
    QTimer::singleShot(0, this, ungrabMouseHack);

    //this is a workaround where Qt now creates the menu widget
    //in .exec before oxygen can polish it and set the following attribute
    desktopMenu->setAttribute(Qt::WA_TranslucentBackground);
    //end workaround
    QPoint globalPos = event->globalPosition().toPoint();
    desktopMenu->adjustSize();

    QRect popUpRect(globalPos.x(), globalPos.y(), desktopMenu->width(), desktopMenu->height());

    if (applet) {
        globalPos = popUpTopLeft(applet, popUpRect);
    } else {
        globalPos = popUpRelevantToGlobalPoint(QRect(0,0,0,0), popUpRect);
    }

    //qDebug() << "7...";

    if (desktopMenu->isEmpty()) {
        //qDebug() << "7.5 ...";
        delete desktopMenu;
        event->accept();
        return;
    }

    // Bug 344205 keep panel visible while menu is open
    m_lastContainmentStatus = m_latteView->containment()->status();
    m_latteView->containment()->setStatus(Plasma::Types::RequiresAttentionStatus);

    connect(desktopMenu, &QMenu::aboutToHide, this, &ContextMenuLayerQuickItem::onMenuAboutToHide);

    KAcceleratorManager::manage(desktopMenu);

    for (auto action : desktopMenu->actions()) {
        if (action->menu()) {
            connect(action->menu(), &QMenu::aboutToShow, desktopMenu, [action, desktopMenu] {
                if (action->menu()->windowHandle()) {
                    // Need to add the transient parent otherwise Qt will create a new toplevel
                    action->menu()->windowHandle()->setTransientParent(desktopMenu->windowHandle());
                }
            });
        }
    }

    //qDebug() << "8 ...";
    desktopMenu->popup(globalPos);
    event->setAccepted(true);
}

//! update the appletContainsPos method from Panel view
void ContextMenuLayerQuickItem::updateAppletContainsMethod()
{
    if (!m_latteView) {
        return;
    }

    updateAppletContainsMethodIn(m_latteView->contentItem(), 0);
}

void ContextMenuLayerQuickItem::updateAppletContainsMethodIn(QQuickItem *root, int depth)
{
    //! Plasma 6 inserted wrapper layers (ContainmentItem and friends) between
    //! the view's contentItem and the containment root that carries
    //! appletContainsPos, so a direct-children scan never found it and applet
    //! resolution silently fell back to broken geometry mapping. Walk the
    //! subtree; the containment root sits only a few levels down, the depth
    //! cap guards against pathological trees.
    if (!root || depth > 6 || m_appletContainsMethod.isValid()) {
        return;
    }

    for (QQuickItem *item : root->childItems()) {
        if (auto *metaObject = item->metaObject()) {
            // not using QMetaObject::invokeMethod to avoid warnings when calling
            // this on applets that don't have it or other child items since this
            // is pretty much trial and error.
            // Also, "var" arguments are treated as QVariant in QMetaObject

            int methodIndex = metaObject->indexOfMethod("appletContainsPos(QVariant,QVariant)");

            if (methodIndex != -1) {
                m_appletContainsMethod = metaObject->method(methodIndex);
                m_appletContainsMethodItem = item;
                return;
            }
        }

        updateAppletContainsMethodIn(item, depth + 1);

        if (m_appletContainsMethod.isValid()) {
            return;
        }
    }
}

void ContextMenuLayerQuickItem::addAppletActions(QMenu *desktopMenu, Plasma::Applet *applet, QEvent *event)
{
    if (!m_latteView || !m_latteView->containment()) {
        return;
    }

    desktopMenu->addSection(applet->pluginMetaData().name());

    for (QAction *action : applet->contextualActions()) {
        if (action) {
            desktopMenu->addAction(action);
        }
    }

    if (!applet->failedToLaunch()) {
        QAction *runAssociatedApplication = applet->internalAction(QStringLiteral("run associated application"));

        if (runAssociatedApplication && runAssociatedApplication->isEnabled()) {
            desktopMenu->addAction(runAssociatedApplication);
        }

        QAction *configureApplet = applet->internalAction(QStringLiteral("configure"));

        if (configureApplet && configureApplet->isEnabled()) {
            desktopMenu->addAction(configureApplet);
        }

        //! resizable persistent popups (continuation feature): popupWidth/
        //! popupHeight land in the applet's config group only after an
        //! actual edge-drag resize, so the entry showing at all means there
        //! is something to reset. After deleting the keys the applet object
        //! gets a dynamic-property bump - the popup dialog (lattecoreplugin)
        //! event-filters its applet for exactly this and re-sizes a pinned
        //! popup back to its hint size live. In-process hand-off on purpose:
        //! kconfig's DBus change notification cannot address layout files
        //! ("My Layout.layout.latte" - spaces are illegal in a DBus object
        //! path), see Latte::Quick::Dialog::setApplet.
        const KConfigGroup appletConfig = applet->config();

        if (appletConfig.hasKey("popupWidth") || appletConfig.hasKey("popupHeight")) {
            QAction *resetPopupSize = new QAction(i18n("Reset Popup Size"), desktopMenu);
            QPointer<Plasma::Applet> appletGuard = applet;
            connect(resetPopupSize, &QAction::triggered, this, [appletGuard]() {
                if (!appletGuard) {
                    return;
                }
                KConfigGroup config = appletGuard->config();
                config.deleteEntry("popupWidth");
                config.deleteEntry("popupHeight");
                config.sync();

                const int token = appletGuard->property("_latte_popupSizeReset").toInt();
                appletGuard->setProperty("_latte_popupSizeReset", token + 1);
            });
            desktopMenu->addAction(resetPopupSize);
        }

        QAction *appletAlternatives = applet->internalAction(QStringLiteral("alternatives"));

        if (appletAlternatives && appletAlternatives->isEnabled() && m_latteView->containment()->isUserConfiguring()) {
            desktopMenu->addAction(appletAlternatives);
        }
    }

    QAction *containmentAction = desktopMenu->menuAction();
    containmentAction->setText(i18nc("%1 is the name of the containment", "%1 Options", m_latteView->containment()->title()));

    if (desktopMenu->actions().count()>1) { /*take into account the Applet Name Section*/
        addContainmentActions(containmentAction->menu(), event);
    }

    if (!containmentAction->menu()->isEmpty()) {
        int enabled = 0;
        //count number of real actions
        QListIterator<QAction *> actionsIt(containmentAction->menu()->actions());

        while (enabled < 3 && actionsIt.hasNext()) {
            QAction *action = actionsIt.next();

            if (action->isVisible() && !action->isSeparator()) {
                ++enabled;
            }
        }

        desktopMenu->addSeparator();

        if (enabled) {
            //if there is only one, don't create a submenu
            // if (enabled < 2) {
            for (QAction *action : containmentAction->menu()->actions()) {
                if (action && action->isVisible()) {
                    desktopMenu->addAction(action);
                }
            }

            // } else {
            //     desktopMenu->addMenu(containmentMenu);
            // }
        }
    }

    if (m_latteView->containment()->immutability() == Plasma::Types::Mutable &&
            (m_latteView->containment()->containmentType() != Plasma::Containment::Type::Panel || m_latteView->containment()->isUserConfiguring())) {
        const QAction *const closeApplet = applet->internalAction(QStringLiteral("remove"));

        //qDebug() << "checking for removal" << closeApplet;
        if (closeApplet) {
            if (!desktopMenu->isEmpty()) {
                desktopMenu->addSeparator();
            }

            //qDebug() << "adding close action" << closeApplet->isEnabled() << closeApplet->isVisible();
            auto *const relationshipAwareRemove = new QAction(closeApplet->icon(), closeApplet->text(), desktopMenu);
            relationshipAwareRemove->setEnabled(closeApplet->isEnabled());
            relationshipAwareRemove->setVisible(closeApplet->isVisible());
            const int appletId = static_cast<int>(applet->id());
            connect(relationshipAwareRemove, &QAction::triggered, this, [this, appletId]() {
                if (!m_latteView || !m_latteView->removeApplet(appletId)) {
                    qCritical() << "ContextMenuLayerQuickItem: relationship-aware removal failed for applet"
                                << appletId;
                }
            });
            desktopMenu->addAction(relationshipAwareRemove);
        }
    }
}

void ContextMenuLayerQuickItem::addContainmentActions(QMenu *desktopMenu, QEvent *event)
{
    if (!m_latteView || !m_latteView->containment()) {
        return;
    }

    if (m_latteView->containment()->corona()->immutability() != Plasma::Types::Mutable &&
            !KAuthorized::authorizeAction(QStringLiteral("plasma/containment_actions"))) {
        //qDebug() << "immutability";
        return;
    }

    //this is what ContainmentPrivate::prepareContainmentActions was
    const QString trigger = Plasma::ContainmentActions::eventToString(event);
    //"RightButton;NoModifier"
    Plasma::ContainmentActions *plugin = m_latteView->containment()->containmentActions().value(trigger);

    if (!plugin) {
        return;
    }

    if (plugin->containment() != m_latteView->containment()) {
        plugin->setContainment(m_latteView->containment());
        // now configure it
        KConfigGroup cfg(m_latteView->containment()->corona()->config(), "ActionPlugins");
        cfg = KConfigGroup(&cfg, QString::number(m_latteView->containment()->containmentType()));
        KConfigGroup pluginConfig = KConfigGroup(&cfg, trigger);
        plugin->restore(pluginConfig);
    }

    QList<QAction *> actions = plugin->contextualActions();

    /*   for (const QAction *act : actions) {
        if (act->menu()) {
            //this is a workaround where Qt now creates the menu widget
            //in .exec before oxygen can polish it and set the following attribute
            act->menu()->setAttribute(Qt::WA_TranslucentBackground);
            //end workaround

            if (act->menu()->winId()) {
                act->menu()->windowHandle()->setTransientParent(m_latteView);
            }
        }
    }*/

    desktopMenu->addActions(actions);
}

Plasma::Containment *ContextMenuLayerQuickItem::containmentById(uint id)
{
    if (!m_latteView) {
        return nullptr;
    }

    for (const auto containment : m_latteView->corona()->containments()) {
        if (id == containment->id()) {
            return containment;
        }
    }

    return nullptr;
}

}
