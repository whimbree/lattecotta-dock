/*
    SPDX-FileCopyrightText: 2021 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "originalview.h"
#include "clonedview.h"
#include "positioner.h"
#include "../lattecorona.h"
#include "../screenpool.h"
#include "../layouts/storage.h"

// C++
#include <algorithm>
#include <utility>

// KDE
#include <KLocalizedString>

namespace Latte {
OriginalView::OriginalView(Plasma::Corona *corona, QScreen *targetScreen, bool byPassX11WM)
    : View(corona, targetScreen, byPassX11WM)
{
    connect(this, &View::inEditModeChanged, this, &OriginalView::updateLinkedEditHighlights);
    //! A recreated runtime can attach an existing containment whose edit bit is
    //! already true. Recompute from the level after attachment because no new
    //! userConfiguringChanged edge is required in that state.
    connect(this, &View::containmentChanged, this, &OriginalView::updateLinkedEditHighlights);

    connect(this, &View::containmentChanged, this, [&]() {
        if (!this->containment()) {
            return;
        }

        connect(containment(), &Plasma::Applet::destroyedChanged, this, &OriginalView::synchronizeScreenGroupMembers);
        restoreConfig();
    });

    connect(this, &View::layoutChanged, this, &OriginalView::synchronizeScreenGroupMembers);
    connect(this, &View::layoutChanged, this, [this]() {
        QObject::disconnect(m_relationshipTableConnection);
        if (layout()) {
            m_relationshipTableConnection = connect(
                layout(), &Layout::GenericLayout::viewsCountChanged,
                this, &View::canRemoveChanged);
        }
        Q_EMIT canRemoveChanged();
    });
    connect(this, &OriginalView::screensGroupChanged, this, &OriginalView::synchronizeScreenGroupMembers);
    connect(this, &OriginalView::screensGroupChanged, this, &OriginalView::saveConfig);
}

OriginalView::~OriginalView() = default;

bool OriginalView::isSingle() const
{
    return m_screensGroup == Latte::Types::SingleScreenGroup && clonesCount() == 0;
}

bool OriginalView::isOriginal() const
{
    return true;
}

bool OriginalView::isCloned() const
{
    return !isOriginal();
}

int OriginalView::clonesCount() const
{
    return std::count_if(m_clones.cbegin(), m_clones.cend(), [](const auto &clone) {
        return !clone.isNull();
    });
}

bool OriginalView::canRemove() const
{
    if (!View::canRemove() || !layout() || !containment()) {
        return false;
    }

    return !layout()->viewsTable().hasExplicitLinkedMembers(QString::number(containment()->id()));
}

bool OriginalView::canMoveToLayout() const
{
    if (!View::canMoveToLayout() || !layout() || !containment()) {
        return false;
    }

    return layout()->viewsTable().allowsMoveToAnotherLayout(QString::number(containment()->id()));
}

int OriginalView::expectedScreenIdFromScreenGroup(const Latte::Types::ScreensGroup &nextScreensGroup) const
{
    Data::View view = data();
    view.screensGroup = nextScreensGroup;
    return Latte::Layouts::Storage::self()->expectedViewScreenId(m_corona, view);
}

Latte::Types::ScreensGroup OriginalView::screensGroup() const
{
    return m_screensGroup;
}

Data::View::LinkPlacement OriginalView::linkPlacement() const
{
    return Data::View::LinkPlacement::ScreenGroupDerived;
}

void OriginalView::setScreensGroup(const Latte::Types::ScreensGroup &group)
{
    if (m_screensGroup == group) {
        return;
    }

    m_screensGroup = group;
    Q_EMIT screensGroupChanged();
}

void OriginalView::addClone(Latte::ClonedView *view)
{
    if (!view || std::any_of(m_clones.cbegin(), m_clones.cend(), [view](const auto &clone) {
        return clone.data() == view;
    })) {
        return;
    }

    m_clones << view;
    const LinkedEditHighlightConnections highlightConnections{
        connect(view, &View::inEditModeChanged,
                this, &OriginalView::updateLinkedEditHighlights),
        //! ClonedView joins before GenericLayout assigns its containment. The
        //! attached containment may already be configuring during recreation,
        //! so its current level must be observed as well as later edges.
        connect(view, &View::containmentChanged,
                this, &OriginalView::updateLinkedEditHighlights),
        //! A runtime root can be replaced while its old linked members are still
        //! alive. QObject emits destroyed after this class's members are gone, so
        //! this receiver-scoped connection clears each surviving member without
        //! consulting the dismantled coordinator.
        connect(this, &QObject::destroyed, view, [view]() {
            view->setLinkedEditHighlight(false);
        })
    };
    m_linkedEditHighlightConnections.insert(view, highlightConnections);
    updateLinkedEditHighlights();
    Q_EMIT canRemoveChanged();
    if (view->linkPlacement() == Data::View::LinkPlacement::ScreenGroupDerived) {
        m_waitingCreation.removeAll(view->positioner()->currentScreenId());
    }
}

void OriginalView::forgetClone(Latte::ClonedView *view)
{
    if (view) {
        const auto connections = m_linkedEditHighlightConnections.find(view);
        Q_ASSERT(connections != m_linkedEditHighlightConnections.end());
        if (connections != m_linkedEditHighlightConnections.end()) {
            QObject::disconnect(connections->editModeChanged);
            QObject::disconnect(connections->containmentChanged);
            QObject::disconnect(connections->rootDestroyed);
            m_linkedEditHighlightConnections.erase(connections);
        }
        view->setLinkedEditHighlight(false);
    }

    const int previousCount = m_clones.count();
    m_clones.removeIf([view](const auto &clone) {
        return clone.isNull() || clone.data() == view;
    });
    if (m_clones.count() != previousCount) {
        Q_EMIT canRemoveChanged();
    }
    updateLinkedEditHighlights();
}

void OriginalView::updateLinkedEditHighlights()
{
    const bool hasLinkedMembers = std::any_of(
        m_clones.cbegin(), m_clones.cend(), [](const auto &clone) {
            return !clone.isNull();
        });
    const bool relationshipIsEditing = inEditMode()
        || std::any_of(m_clones.cbegin(), m_clones.cend(), [](const auto &clone) {
            return clone && clone->inEditMode();
        });

    setLinkedEditHighlight(hasLinkedMembers && relationshipIsEditing && !inEditMode());
    for (const auto &clone : std::as_const(m_clones)) {
        if (clone) {
            clone->setLinkedEditHighlight(relationshipIsEditing && !clone->inEditMode());
        }
    }
}

void OriginalView::retireScreenGroupDerivedClonesForRuntimeUnload()
{
    cleanScreenGroupClones();
}

void OriginalView::removeClone(Latte::ClonedView *view)
{
    if (!view || std::none_of(m_clones.cbegin(), m_clones.cend(), [view](const auto &clone) {
        return clone.data() == view;
    })) {
        return;
    }

    forgetClone(view);

    if (!view->layout()) {
        qWarning() << "OriginalView: clone was unregistered without a layout to remove its containment";
        return;
    }
    view->positioner()->slideOutDuringExit();
    view->layout()->removeView(view->data());
}

void OriginalView::createClone(int screenId)
{
    if (!layout() || !containment()) {
        return;
    }

    QString templateFile = layout()->storedView(containment()->id());

    if (templateFile.isEmpty()) {
        return;
    }

    Data::ViewsTable templateviews = Layouts::Storage::self()->views(templateFile);

    if (templateviews.rowCount() <= 0) {
        return;
    }

    Data::View nextdata = templateviews[0];
    nextdata.name = i18nc("clone of original dock panel, name","Clone of %1", name());
    nextdata.onPrimary = false;
    nextdata.screensGroup = Latte::Types::SingleScreenGroup;
    nextdata.isClonedFrom = containment()->id();
    nextdata.linkPlacement = Data::View::LinkPlacement::ScreenGroupDerived;
    nextdata.screen = screenId;

    nextdata.setState(Data::View::OriginFromViewTemplate, templateFile);

    if (!m_waitingCreation.contains(screenId)) {
        m_waitingCreation << screenId;
        layout()->newView(nextdata);
    }
}

void OriginalView::cleanClones()
{
    if (m_clones.count()==0) {
        return;
    }

    while(!m_clones.isEmpty()) {
        auto clone = m_clones.constFirst();
        if (!clone) {
            m_clones.removeFirst();
            qWarning() << "OriginalView: pruned a destroyed clone from the membership list";
            continue;
        }

        removeClone(clone.data());
    }
}

void OriginalView::cleanScreenGroupClones()
{
    const auto clones = m_clones;
    for (const auto &clone : clones) {
        if (!clone) {
            forgetClone(nullptr);
            qWarning() << "OriginalView: pruned a destroyed clone from the membership list";
        } else if (clone->linkPlacement() == Data::View::LinkPlacement::ScreenGroupDerived) {
            removeClone(clone.data());
        }
    }
}

void OriginalView::reconsiderScreen()
{
    View::reconsiderScreen();
    synchronizeScreenGroupMembers();
}

void OriginalView::setNextLocationForClones(const QString layoutName, int edge, int alignment)
{
    if (m_clones.count()==0) {
        return;
    }

    for (const auto &clone : m_clones) {
        if (!clone) {
            qWarning() << "OriginalView: skipped a destroyed clone while moving the clone set";
            continue;
        }

        if (clone->linkPlacement() == Data::View::LinkPlacement::ScreenGroupDerived) {
            clone->positioner()->setNextLocation(layoutName, Latte::Types::SingleScreenGroup, "", edge, alignment);
        }
    }
}

bool OriginalView::addApplet(const QString &pluginId)
{
    if (!extendedInterface()->addApplet(pluginId)) {
        qCritical() << "OriginalView: failed to add applet" << pluginId
                    << "to relationship root" << (containment() ? containment()->id() : 0);
        return false;
    }

    bool addedToEveryMember{true};

    for (const auto &clone : std::as_const(m_clones)) {
        if (!clone) {
            qWarning() << "OriginalView: skipped a destroyed clone while adding an applet";
            continue;
        }

        if (!clone->extendedInterface()->addApplet(pluginId)) {
            qCritical() << "OriginalView: failed to add linked applet" << pluginId
                        << "to containment" << clone->containment()->id();
            addedToEveryMember = false;
        }
    }

    return addedToEveryMember;
}

bool OriginalView::removeApplet(const int appletId)
{
    return extendedInterface()->removeApplet(appletId);
}

void OriginalView::synchronizeDroppedApplet(QObject *mimeData, const int x, const int y)
{
    //! The root containment has already processed the drop. Only the linked
    //! members need local applet instances.
    for (const auto &clone : std::as_const(m_clones)) {
        if (!clone) {
            qWarning() << "OriginalView: skipped a destroyed clone while mirroring a dropped applet";
            continue;
        }

        clone->extendedInterface()->addApplet(mimeData, x, y);
    }
}

void OriginalView::addApplet(QObject *mimedata, const int x, const int y, const uint excludedContainmentId)
{
    if (m_clones.count() == 0) {
        return;
    }

    // add applet in original view
    extendedInterface()->addApplet(mimedata, x, y);

    // add applet in clones and exclude the one that probably produced this triggering
    for (const auto &clone : std::as_const(m_clones)) {
        if (!clone) {
            qWarning() << "OriginalView: skipped a destroyed clone while dropping an applet";
            continue;
        }

        if (clone->containment()->id() == excludedContainmentId) {
            // this way we make sure that an applet will not be double added
            continue;
        }

        clone->extendedInterface()->addApplet(mimedata, x, y);
    }
}

void OriginalView::synchronizeScreenGroupMembers()
{
    if (layout() && containment() && layout()->isRecreatingView(containment())) {
        return;
    }

    //! Startup trickles views in one per event-loop pass with relationship
    //! roots ordered before persisted linked members (initContainments).
    //! Reconciling before those members register would treat their screens as
    //! uncovered, generate fresh clones there, and the surplus pass below
    //! would then destroy the persisted replicas (D283). The layout reruns
    //! this synchronization once its startup views have landed.
    if (layout() && layout()->hasPendingStartupViews()) {
        return;
    }

    if (containment() && containment()->destroyed()) {
        cleanClones();
        return;
    }

    if (m_screensGroup == Latte::Types::SingleScreenGroup) {
        cleanScreenGroupClones();
        return;
    }

    QList<int> secondaryscreens = m_corona->screenPool()->secondaryScreenIds();

    for (const auto scrid : secondaryscreens) {
        if (m_waitingCreation.contains(scrid)) {
            secondaryscreens.removeAll(scrid);
        }
    }

    if (m_screensGroup == Latte::Types::AllSecondaryScreensGroup) {
        //! occupied screen from original view in "allsecondaryscreensgroup" must be ignored
        secondaryscreens.removeAll(expectedScreenIdFromScreenGroup(m_screensGroup));
    }

    QList<Latte::ClonedView *> removable;

    for (const auto &clone : m_clones) {
        if (!clone) {
            qWarning() << "OriginalView: found a destroyed clone during screen synchronization";
            continue;
        }

        if (clone->linkPlacement() == Data::View::LinkPlacement::ExplicitTarget) {
            continue;
        }

        if (secondaryscreens.contains(clone->positioner()->currentScreenId())) {
            // do nothing valid clone
            secondaryscreens.removeAll(clone->positioner()->currentScreenId());
        } else {
            // must be removed the screen is not active
            removable << clone;
        }
    }

    forgetClone(nullptr);

    for (const auto scrid : secondaryscreens) {
        if (removable.count() > 0) {
            //! move deprecated and available clone to valid secondary screen
            auto clone = removable.takeFirst();
            clone->positioner()->setScreenToFollow(m_corona->screenPool()->screenForId(scrid));
        } else {
            //! create a new clone
            createClone(scrid);
        }
    }

    for (auto removableclone : removable) {
        //! remove deprecated clones
        removeClone(removableclone);
    }
}

void OriginalView::saveConfig()
{

    if (!this->containment()) {
        return;
    }

    auto config = this->containment()->config();
    config.writeEntry("screensGroup", (int)m_screensGroup);
}

void OriginalView::restoreConfig()
{
    if (!this->containment()) {
        return;
    }

    auto config = this->containment()->config();
    m_screensGroup = static_cast<Latte::Types::ScreensGroup>(config.readEntry("screensGroup", (int)Latte::Types::SingleScreenGroup));

    //! Send changed signals at the end in order to be sure that saveConfig
    //! wont rewrite default/invalid values
    Q_EMIT screensGroupChanged();
}

}
