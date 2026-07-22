/*
    SPDX-FileCopyrightText: 2021 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "clonedview.h"
#include "containmentinterface.h"
#include "visibilitymanager.h"
#include "../data/viewdata.h"
#include "../layouts/storage.h"

// C++
#include <utility>

// Qt
#include <QSet>

namespace Latte {

QStringList ClonedView::CONTAINMENTMANUALSYNCEDPROPERTIES = QStringList()
        << QString("appletOrder")
        << QString("lockedZoomApplets")
        << QString("userBlocksColorizingApplets");  

ClonedView::ClonedView(Plasma::Corona *corona,
                       Latte::OriginalView *originalView,
                       const Latte::Data::View::LinkPlacement linkPlacement,
                       QScreen *targetScreen,
                       bool byPassX11WM)
    : View(corona, targetScreen, byPassX11WM),
      m_originalView(originalView),
      m_linkPlacement(linkPlacement)
{
    m_originalView->addClone(this);
    initSync();
}

ClonedView::~ClonedView()
{
    if (m_originalView) {
        m_originalView->forgetClone(this);
    }
}

void ClonedView::initSync()
{
    connect(m_originalView, &View::containmentChanged, this, &View::groupIdChanged);

    if (m_linkPlacement == Data::View::LinkPlacement::ScreenGroupDerived) {
        //! Screen-group replicas inherit visibility because they are multiple
        //! output surfaces for one placement policy. Explicit linked members
        //! own visibility independently and synchronize applet content only.
        connect(m_originalView->visibility(), &Latte::ViewPart::VisibilityManager::modeChanged, this, [&]() {
            visibility()->setMode(m_originalView->visibility()->mode());
        });

        connect(m_originalView->visibility(), &Latte::ViewPart::VisibilityManager::raiseOnDesktopChanged, this, [&]() {
            visibility()->setRaiseOnDesktop(m_originalView->visibility()->raiseOnDesktop());
        });

        connect(m_originalView->visibility(), &Latte::ViewPart::VisibilityManager::raiseOnActivityChanged, this, [&]() {
            visibility()->setRaiseOnActivity(m_originalView->visibility()->raiseOnActivity());
        });

        connect(m_originalView->visibility(), &Latte::ViewPart::VisibilityManager::enableKWinEdgesChanged, this, [&]() {
            visibility()->setEnableKWinEdges(m_originalView->visibility()->enableKWinEdges());
        });

        connect(m_originalView->visibility(), &Latte::ViewPart::VisibilityManager::timerShowChanged, this, [&]() {
            visibility()->setTimerShow(m_originalView->visibility()->timerShow());
        });

        connect(m_originalView->visibility(), &Latte::ViewPart::VisibilityManager::timerHideChanged, this, [&]() {
            visibility()->setTimerHide(m_originalView->visibility()->timerHide());
        });
    }


    //! Update Applets from Clone -> OriginalView
    //! every point where the ids hash can gain entries goes through
    //! onSyncProgress so deferred original->clone syncs get their retry
    connect(extendedInterface(), &Latte::ViewPart::ContainmentInterface::appletConfigPropertyChanged, this, &ClonedView::updateOriginalAppletConfigProperty);
    connect(extendedInterface(), &Latte::ViewPart::ContainmentInterface::initializationCompleted, this, [&]() {
        m_initializationCompleted = true;
        m_pendingOrderSync = true;
        onSyncProgress();
    });
    connect(extendedInterface(), &Latte::ViewPart::ContainmentInterface::appletsOrderChanged, this, [&]() {
        //! A root structural change updates the member's order before the new
        //! applet's data object is ready. That intermediate member signal is
        //! progress for the pending root transaction, not a member edit.
        const bool rootOrderWasPending = m_pendingOrderSync;
        onSyncProgress();
        if (!rootOrderWasPending) {
            updateOriginalAppletsOrder();
        }
    });
    connect(extendedInterface(), &Latte::ViewPart::ContainmentInterface::appletDataCreated, this, &ClonedView::onSyncProgress);

    //! Update Applets and Containment from OrigalView -> Clone
    if (m_linkPlacement == Data::View::LinkPlacement::ScreenGroupDerived) {
        connect(m_originalView->extendedInterface(), &Latte::ViewPart::ContainmentInterface::containmentConfigPropertyChanged, this, &ClonedView::updateContainmentConfigProperty);
    }
    connect(m_originalView->extendedInterface(), &Latte::ViewPart::ContainmentInterface::appletConfigPropertyChanged, this, &ClonedView::onOriginalAppletConfigPropertyChanged);
    connect(m_originalView->extendedInterface(), &Latte::ViewPart::ContainmentInterface::appletInScheduledDestructionChanged, this, &ClonedView::onOriginalAppletInScheduledDestructionChanged);
    connect(m_originalView->extendedInterface(), &Latte::ViewPart::ContainmentInterface::appletRemoved, this, &ClonedView::onOriginalAppletRemoved);
    connect(m_originalView->extendedInterface(), &Latte::ViewPart::ContainmentInterface::appletsOrderChanged, this, &ClonedView::onOriginalAppletsOrderChanged);
    connect(m_originalView->extendedInterface(), &Latte::ViewPart::ContainmentInterface::appletsInLockedZoomChanged, this, &ClonedView::onOriginalAppletsInLockedZoomChanged);
    connect(m_originalView->extendedInterface(), &Latte::ViewPart::ContainmentInterface::appletsDisabledColoringChanged, this, &ClonedView::onOriginalAppletsDisabledColoringChanged);
    connect(m_originalView->extendedInterface(), &Latte::ViewPart::ContainmentInterface::appletDataCreated, this, &ClonedView::onSyncProgress);

    if (m_linkPlacement == Data::View::LinkPlacement::ScreenGroupDerived) {
        connect(m_originalView, &Latte::View::indicatorChanged, this, &ClonedView::indicatorChanged);
    }
}

bool ClonedView::isSingle() const
{
    return false;
}

bool ClonedView::isOriginal() const
{
    return false;
}

bool ClonedView::isCloned() const
{
    return true;
}

bool ClonedView::isPreferredForShortcuts() const
{
    return false;
}

int ClonedView::groupId() const
{
    if (!m_originalView->containment()) {
        return -1;
    }

    return m_originalView->containment()->id();
}

Latte::Types::ScreensGroup ClonedView::screensGroup() const
{
    return Latte::Types::SingleScreenGroup;
}

Data::View::LinkPlacement ClonedView::linkPlacement() const
{
    return m_linkPlacement;
}

View *ClonedView::configurationTargetView()
{
    if (m_linkPlacement == Data::View::LinkPlacement::ExplicitTarget) {
        return this;
    }

    return m_originalView.data();
}

View *ClonedView::relationshipRootView()
{
    return m_originalView.data();
}

bool ClonedView::addApplet(const QString &pluginId)
{
    if (!m_originalView) {
        qCritical() << "ClonedView: cannot add an applet after its relationship root was destroyed";
        return false;
    }

    return m_originalView->addApplet(pluginId);
}

bool ClonedView::removeApplet(const int appletId)
{
    if (!m_originalView) {
        qCritical() << "ClonedView: cannot remove an applet after its relationship root was destroyed";
        return false;
    }

    updateAppletIdsHash();
    const int originalId = originalAppletId(appletId);
    if (originalId <= 0) {
        qCritical() << "ClonedView: cannot translate applet" << appletId
                    << "from containment" << (containment() ? containment()->id() : 0)
                    << "to its relationship root";
        return false;
    }

    return m_originalView->removeApplet(originalId);
}

void ClonedView::synchronizeDroppedApplet(QObject *mimeData, const int x, const int y)
{
    if (!m_originalView || !containment()) {
        qCritical() << "ClonedView: cannot synchronize a dropped applet without a valid relationship root";
        return;
    }

    //! This member has already processed the drop. The root owns the shared
    //! transaction and creates instances in itself and every other member.
    m_originalView->addApplet(mimeData, x, y, containment()->id());
}

ViewPart::Indicator *ClonedView::indicator() const
{
    return m_linkPlacement == Data::View::LinkPlacement::ExplicitTarget
            ? View::indicator()
            : m_originalView->indicator();
}


bool ClonedView::hasOriginalAppletId(const int &clonedid) const
{
    if (clonedid < 0) {
        return false;
    }

    QHash<int, int>::const_iterator i = m_currentAppletIds.constBegin();
    while (i != m_currentAppletIds.constEnd()) {
        if (i.value() == clonedid) {
            return true;
        }

        ++i;
    }

    return false;
}

int ClonedView::originalAppletId(const int &clonedid) const
{
    if (clonedid < 0) {
        return -1;
    }

    QHash<int, int>::const_iterator i = m_currentAppletIds.constBegin();
    while (i != m_currentAppletIds.constEnd()) {
        if (i.value() == clonedid) {
            return i.key();
        }

        ++i;
    }

    return -1;
}


bool ClonedView::isTranslatableToClonesOrder(const QList<int> &originalOrder) const
{
    for(int i=0; i<originalOrder.count(); ++i) {
        int oid = originalOrder[i];
        if (oid < 0 ) {
            continue;
        }

        if (!m_currentAppletIds.contains(oid)) {
            return false;
        }
    }

    return true;
}

Latte::Data::View ClonedView::data() const
{
    Latte::Data::View vdata = View::data();
    vdata.isClonedFrom = m_originalView->containment()->id();
    vdata.linkPlacement = m_linkPlacement;
    return vdata;
}

void ClonedView::updateAppletIdsHash()
{
    const QList<int> originalIds = m_originalView->extendedInterface()->appletsOrder();
    const QList<int> clonedIds = extendedInterface()->appletsOrder();
    QSet<int> registeredCloneIds;
    registeredCloneIds.reserve(m_currentAppletIds.size());
    for (const int clonedId : std::as_const(m_currentAppletIds)) {
        registeredCloneIds.insert(clonedId);
    }

    //! Applet ids are containment-local. Match each still-unpaired root applet
    //! to the first still-unpaired member applet with the same plugin. Index
    //! matching is invalid once either containment has been reordered, and was
    //! the cause of member-originated order changes losing their new applet.
    for (const int originalId : originalIds) {
        if (originalId < 0 || m_currentAppletIds.contains(originalId)) {
            continue;
        }

        const ViewPart::AppletInterfaceData originalApplet =
            m_originalView->extendedInterface()->appletDataForId(originalId);
        if (originalApplet.id <= 0 || originalApplet.plugin.isEmpty()) {
            continue;
        }

        for (const int clonedId : clonedIds) {
            if (clonedId < 0 || registeredCloneIds.contains(clonedId)) {
                continue;
            }

            const ViewPart::AppletInterfaceData clonedApplet =
                extendedInterface()->appletDataForId(clonedId);
            if (clonedApplet.id > 0 && clonedApplet.plugin == originalApplet.plugin) {
                m_currentAppletIds.insert(originalId, clonedId);
                registeredCloneIds.insert(clonedId);
                break;
            }
        }
    }
}

QList<int> ClonedView::translateToClonesOrder(const QList<int> &originalIds) const
{
    QList<int> ids;

    for (int i=0; i<originalIds.count(); ++i) {
        int originalid = originalIds[i];
        if (originalid < 0 ) {
            ids << originalid;
            continue;
        }

        if (m_currentAppletIds.contains(originalid)) {
            ids << m_currentAppletIds[originalid];
        } else {
            ids << ERRORAPPLETID; //error
        }
    }

    return ids;
}

QList<int> ClonedView::translateToOriginalsOrder(const QList<int> &clonedIds) const
{
    QList<int> ids;
    ids.reserve(clonedIds.size());

    for (const int clonedId : clonedIds) {
        if (clonedId < 0) {
            ids << clonedId;
            continue;
        }

        const int originalId = originalAppletId(clonedId);
        ids << (originalId > 0 ? originalId : ERRORAPPLETID);
    }

    return ids;
}

void ClonedView::showConfigurationInterface(Plasma::Applet *applet)
{
    Plasma::Containment *c = qobject_cast<Plasma::Containment *>(applet);

    if (Layouts::Storage::self()->isLatteContainment(c)) {
        if (m_linkPlacement == Data::View::LinkPlacement::ExplicitTarget) {
            View::showConfigurationInterface(applet);
            return;
        }

        if (auto *target = configurationTargetView()) {
            target->showSettingsWindow();
        } else {
            qWarning() << "ClonedView: cannot edit a clone after its original was destroyed";
        }
    } else {
        View::showConfigurationInterface(applet);
    }
}

void ClonedView::onOriginalAppletRemoved(const int &id)
{
    if (!m_currentAppletIds.contains(id)) {
        return;
    }

    if (!extendedInterface()->destroyAppletImmediately(m_currentAppletIds[id])) {
        qCritical() << "ClonedView: failed to finalize linked applet" << m_currentAppletIds[id]
                    << "after root applet" << id << "expired";
    }
    m_currentAppletIds.remove(id);
}

void ClonedView::onOriginalAppletConfigPropertyChanged(const int &id, const QString &key, const QVariant &value)
{
    if (!m_currentAppletIds.contains(id)) {
        return;
    }

    extendedInterface()->updateAppletConfigProperty(m_currentAppletIds[id], key, value);
}

void ClonedView::onOriginalAppletInScheduledDestructionChanged(const int &id, const bool &enabled)
{
    if (enabled) {
        if (!m_currentAppletIds.contains(id)) {
            qCritical() << "ClonedView: cannot begin linked applet removal without a mapped member instance for root applet"
                        << id;
            return;
        }

        //! The relationship root owns the one reversible Plasma transaction
        //! and notification. Member applets are projections, so retire their
        //! local objects immediately. This makes their persistence tombstones
        //! durable even if the process exits during the root's Undo window.
        m_pendingOrderSync = true;
        const int memberId = m_currentAppletIds.take(id);
        if (!extendedInterface()->destroyAppletImmediately(memberId)) {
            qCritical() << "ClonedView: failed to retire linked member applet" << memberId
                        << "for root applet" << id;
        }
        return;
    }

    if (m_currentAppletIds.contains(id)) {
        qCritical() << "ClonedView: cannot restore root applet" << id
                    << "because a member instance is still mapped";
        return;
    }

    const ViewPart::AppletInterfaceData originalApplet =
        m_originalView->extendedInterface()->appletDataForId(id);
    if (originalApplet.id <= 0 || !originalApplet.applet) {
        qCritical() << "ClonedView: cannot restore linked member for missing root applet" << id;
        return;
    }

    m_pendingOrderSync = true;
    const int memberId = extendedInterface()->restoreAppletFrom(originalApplet.applet);
    if (memberId <= 0) {
        return;
    }

    m_currentAppletIds.insert(id, memberId);
    onOriginalAppletsOrderChanged();
    onOriginalAppletsInLockedZoomChanged(m_originalView->extendedInterface()->appletsInLockedZoom());
    onOriginalAppletsDisabledColoringChanged(m_originalView->extendedInterface()->appletsDisabledColoring());
}

void ClonedView::updateContainmentConfigProperty(const QString &key, const QVariant &value)
{
    if (!CONTAINMENTMANUALSYNCEDPROPERTIES.contains(key)) {
        extendedInterface()->updateContainmentConfigProperty(key, value);
    } else {
        //qDebug() << "org.kde.sync :: containment config value syncing blocked :: " << key;
    }
}

void ClonedView::updateOriginalAppletConfigProperty(const int &clonedid, const QString &key, const QVariant &value)
{
    if (!hasOriginalAppletId(clonedid)) {
        return;
    }

    m_originalView->extendedInterface()->updateAppletConfigProperty(originalAppletId(clonedid), key, value);
}

//! the deferred-sync gap fix (Phase 8): an original->clone sync arriving
//! while the clone still initializes finds ids that cannot be translated
//! yet. The old handlers dropped such syncs silently, and nothing replayed
//! them once the clone finished - a clone whose containment initialization
//! outpaced the guard stayed permanently out of order. Every handler now
//! applies-or-defers, and onSyncProgress retries whatever is pending each
//! time the ids hash gains entries.
void ClonedView::onSyncProgress()
{
    updateAppletIdsHash();
    retryPendingOriginalSyncs();
}

void ClonedView::updateOriginalAppletsOrder()
{
    if (!m_initializationCompleted || !m_originalView) {
        return;
    }

    const QList<int> clonedOrder = extendedInterface()->appletsOrder();
    if (m_expectedOrderFromOriginal) {
        const bool reachedExpectedOrder = clonedOrder == *m_expectedOrderFromOriginal;
        m_expectedOrderFromOriginal.reset();
        if (reachedExpectedOrder) {
            return;
        }
    }

    if (m_applyingOriginalOrder) {
        return;
    }

    if (clonedOrder.size() != m_originalView->extendedInterface()->appletsOrder().size()) {
        //! A coordinated add/drop/remove can expose the member's intermediate
        //! local order before the root has completed the structural change.
        return;
    }

    const QList<int> originalOrder = translateToOriginalsOrder(clonedOrder);
    if (originalOrder.contains(ERRORAPPLETID)) {
        qCritical() << "ClonedView: cannot translate the complete applet order for containment"
                    << (containment() ? containment()->id() : 0)
                    << "member order" << clonedOrder
                    << "root order" << m_originalView->extendedInterface()->appletsOrder()
                    << "root-to-member ids" << m_currentAppletIds;
        return;
    }

    if (originalOrder != m_originalView->extendedInterface()->appletsOrder()) {
        m_originalView->extendedInterface()->setAppletsOrder(originalOrder);
    }
}

void ClonedView::retryPendingOriginalSyncs()
{
    //! clear each flag BEFORE applying: a successful apply emits the clone's
    //! own change signals, which re-enter onSyncProgress synchronously - the
    //! cleared flag makes the nested retry a no-op instead of a re-apply
    if (m_pendingOrderSync) {
        m_pendingOrderSync = false;

        if (applyOriginalAppletsOrder()) {
            qDebug() << "org.kde.sync ::: deferred applets order sync applied to clone" << (containment() ? containment()->id() : -1);
        } else {
            m_pendingOrderSync = true;
        }
    }

    if (m_pendingLockedZoom) {
        const QList<int> payload = *m_pendingLockedZoom;
        m_pendingLockedZoom.reset();

        if (applyOriginalAppletsInLockedZoom(payload)) {
            qDebug() << "org.kde.sync ::: deferred locked-zoom sync applied to clone" << (containment() ? containment()->id() : -1);
        } else {
            m_pendingLockedZoom = payload;
        }
    }

    if (m_pendingDisabledColoring) {
        const QList<int> payload = *m_pendingDisabledColoring;
        m_pendingDisabledColoring.reset();

        if (applyOriginalAppletsDisabledColoring(payload)) {
            qDebug() << "org.kde.sync ::: deferred disabled-coloring sync applied to clone" << (containment() ? containment()->id() : -1);
        } else {
            m_pendingDisabledColoring = payload;
        }
    }
}

bool ClonedView::applyOriginalAppletsOrder()
{
    //! order is re-read from the original at apply time, so a deferred order
    //! sync always applies the CURRENT order, not the one that failed
    const QList<int> originalorder = m_originalView->extendedInterface()->appletsOrder();

    if (originalorder.count() != extendedInterface()->appletsOrder().count()) {
        //probably an applet was removed or added and clone has not been updated yet
        return false;
    }

    if (!isTranslatableToClonesOrder(originalorder)) {
        return false;
    }

    const QList<int> newclonesorder = translateToClonesOrder(originalorder);

    if (newclonesorder.contains(ERRORAPPLETID)) {
        return false;
    }

    m_expectedOrderFromOriginal = newclonesorder;
    m_applyingOriginalOrder = true;
    extendedInterface()->setAppletsOrder(newclonesorder);
    m_applyingOriginalOrder = false;
    return true;
}

bool ClonedView::applyOriginalAppletsInLockedZoom(const QList<int> &originalapplets)
{
    if (!isTranslatableToClonesOrder(originalapplets)) {
        return false;
    }

    QList<int> newclonesorder = translateToClonesOrder(originalapplets);

    if (newclonesorder.contains(ERRORAPPLETID)) {
        return false;
    }

    extendedInterface()->setAppletsInLockedZoom(newclonesorder);
    return true;
}

bool ClonedView::applyOriginalAppletsDisabledColoring(const QList<int> &originalapplets)
{
    if (!isTranslatableToClonesOrder(originalapplets)) {
        return false;
    }

    QList<int> newclonesorder = translateToClonesOrder(originalapplets);

    if (newclonesorder.contains(ERRORAPPLETID)) {
        return false;
    }

    extendedInterface()->setAppletsDisabledColoring(newclonesorder);
    return true;
}

void ClonedView::onOriginalAppletsOrderChanged()
{
    updateAppletIdsHash();

    //! a fresh order change supersedes any pending one (apply reads fresh)
    m_pendingOrderSync = false;

    if (!applyOriginalAppletsOrder()) {
        m_pendingOrderSync = true;
        qDebug() << "org.kde.sync ::: original applets order can not be applied to the clone yet (still initializing), sync deferred";
    }
}

void ClonedView::onOriginalAppletsInLockedZoomChanged(const QList<int> &originalapplets)
{
    m_pendingLockedZoom.reset();

    if (!applyOriginalAppletsInLockedZoom(originalapplets)) {
        m_pendingLockedZoom = originalapplets;
        qDebug() << "org.kde.sync ::: original locked-zoom applets can not be applied to the clone yet (still initializing), sync deferred";
    }
}

void ClonedView::onOriginalAppletsDisabledColoringChanged(const QList<int> &originalapplets)
{
    m_pendingDisabledColoring.reset();

    if (!applyOriginalAppletsDisabledColoring(originalapplets)) {
        m_pendingDisabledColoring = originalapplets;
        qDebug() << "org.kde.sync ::: original disabled-coloring applets can not be applied to the clone yet (still initializing), sync deferred";
    }
}


}
