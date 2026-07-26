/*
    SPDX-FileCopyrightText: 2019 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "genericlayout.h"

// local
#include "abstractlayout.h"
#include "../apptypes.h"
#include "../lattecorona.h"
#include "../screenpool.h"
#include "../layouts/importer.h"
#include "../layouts/manager.h"
#include "../layouts/storage.h"
#include "../layouts/synchronizer.h"
#include "../shortcuts/shortcutstracker.h"
#include "../templates/templatesmanager.h"
#include "../view/clonedview.h"
#include "../view/originalview.h"
#include "../view/positioner.h"
#include "../view/view.h"

// Qt
#include <QDebug>
#include <QFileInfo>
#include <QScreen>
#include <QSet>
#include <QTimer>

// Plasma
#include <Plasma/Plasma>
#include <Plasma/Applet>
#include <Plasma/Containment>

// KDE
#include <KActionCollection>
#include <KConfigGroup>

// C++
#include <algorithm>
#include <chrono>
#include <iterator>
#include <memory>
#include <utility>

namespace Latte {
namespace Layout {

namespace {
//! libplasma's public removal action exposes a 60-second Undo notification.
//! The contract test pins that duration; this independent timer guarantees
//! the transaction still commits when the notification backend disappears
//! without emitting KNotification::closed.
constexpr auto RemovalUndoWindow = std::chrono::seconds{60};
constexpr auto ConfigurationCloseDelay = std::chrono::milliseconds{350};
constexpr auto RuntimeViewReplacementDelay = std::chrono::milliseconds{250};

QString canonicalConfigPath(const QString &path)
{
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}
}

GenericLayout::GenericLayout(QObject *parent, QString layoutFile, QString assignedName)
    : AbstractLayout (parent, layoutFile, assignedName)
{
}

GenericLayout::~GenericLayout()
{
}

Type GenericLayout::type() const
{
    return Type::Generic;
}

void GenericLayout::unloadContainments()
{
    if (!m_corona) {
        return;
    }

    qDebug() << "Layout - " + name() + " : [unloadContainments]"
             << "containments ::: " << m_containments.size()
             << " ,latteViews in memory ::: " << m_latteViews.size()
             << " ,hidden latteViews in memory :::  " << m_waitingLatteViews.size();

    for (const auto view : m_latteViews) {
        view->disconnectSensitiveSignals();
    }

    for (const auto view : m_waitingLatteViews) {
        view->disconnectSensitiveSignals();
    }

    m_unloadedContainmentsIds.clear();

    QList<Plasma::Containment *> subcontainments;

    //!identify subcontainments and unload them first
    for (const auto containment : m_containments) {
        if (Plasma::Applet *parentApplet = qobject_cast<Plasma::Applet *>(containment->parent())) {
            subcontainments.append(containment);
        }
    }

    while (!subcontainments.isEmpty()) {
        Plasma::Containment *sub = subcontainments.at(0);
        m_unloadedContainmentsIds << QString::number(sub->id());
        subcontainments.removeFirst();
        m_containments.removeAll(sub);
        delete sub;
    }

    while (!m_containments.isEmpty()) {
        Plasma::Containment *containment = m_containments.at(0);
        m_unloadedContainmentsIds << QString::number(containment->id());
        m_containments.removeFirst();
        delete containment;
    }
}

void GenericLayout::unloadLatteViews()
{
    if (!m_corona) {
        return;
    }

    qDebug() << "Layout - " + name() + " : [unloadLatteViews]"
             << "containments ::: " << m_containments.size()
             << " ,latteViews in memory ::: " << m_latteViews.size()
             << " ,hidden latteViews in memory :::  " << m_waitingLatteViews.size();

    //!disconnect signals in order to avoid crashes when the layout is unloading
    disconnect(this, &GenericLayout::viewsCountChanged, m_corona, &Latte::Corona::notifyAvailableScreenGeometriesChanged);
    disconnect(this, &GenericLayout::activitiesChanged, this, &GenericLayout::updateLastUsedActivity);
    disconnect(m_corona->activitiesConsumer(), &KActivities::Consumer::currentActivityChanged, this, &GenericLayout::updateLastUsedActivity);

    for (const auto view : m_latteViews) {
        view->disconnectSensitiveSignals();
    }

    for (const auto view : m_waitingLatteViews) {
        view->disconnectSensitiveSignals();
    }

    qDeleteAll(m_latteViews);
    qDeleteAll(m_waitingLatteViews);
    m_latteViews.clear();
    m_waitingLatteViews.clear();
}

bool GenericLayout::blockAutomaticLatteViewCreation() const
{
    return m_blockAutomaticLatteViewCreation;
}

void GenericLayout::setBlockAutomaticLatteViewCreation(bool block)
{
    if (m_blockAutomaticLatteViewCreation == block) {
        return;
    }

    m_blockAutomaticLatteViewCreation = block;
}

bool GenericLayout::isActive() const
{
    return m_corona && m_hasInitializedContainments && (m_corona->layoutsManager()->synchronizer()->layout(m_layoutName) != nullptr);
}

bool GenericLayout::isCurrent()
{
    if (!m_corona) {
        return false;
    }

    return m_corona->layoutsManager()->currentLayoutsNames().contains(name());
}

bool GenericLayout::hasCorona() const
{
    return (m_corona!=nullptr);
}

void GenericLayout::setCorona(Latte::Corona *corona)
{
    m_corona = corona;
}

QString GenericLayout::background() const
{
    QString colorsPath = m_corona->kPackage().path() + "../../shells/org.kde.latte.shell/contents/images/canvas/";

    if (backgroundStyle() == Layout::PatternBackgroundStyle) {
        if (customBackground().isEmpty()) {

            return colorsPath + "defaultcustomprint.jpg";
        } else {
            return AbstractLayout::customBackground();
        }
    }

    return colorsPath + AbstractLayout::color() + "print.jpg";
}

QString GenericLayout::textColor() const
{
    if (backgroundStyle() == Layout::PatternBackgroundStyle && customBackground().isEmpty() && customTextColor().isEmpty()) {
        return AbstractLayout::defaultCustomTextColor();
    }

    return AbstractLayout::textColor();
}

int GenericLayout::viewsCount(int screen) const
{
    if (!m_corona) {
        return 0;
    }

    QScreen *scr = m_corona->screenPool()->screenForId(screen);

    int views{0};

    for (const auto view : m_latteViews) {
        if (view && view->screen() == scr && !view->containment()->destroyed()) {
            ++views;
        }
    }

    return views;
}

int GenericLayout::viewsCount(QScreen *screen) const
{
    if (!m_corona) {
        return 0;
    }

    int views{0};

    for (const auto view : m_latteViews) {
        if (view && view->screen() == screen && !view->containment()->destroyed()) {
            ++views;
        }
    }

    return views;
}

int GenericLayout::viewsCount() const
{
    if (!m_corona) {
        return 0;
    }

    int views{0};

    for (const auto view : m_latteViews) {
        if (view && view->containment() && !view->containment()->destroyed()) {
            ++views;
        }
    }

    return views;
}

QList<int> GenericLayout::qmlFreeEdges(int screen) const
{
    if (!m_corona) {
        const QList<int> emptyEdges;
        return emptyEdges;
    }

    const auto edges = freeEdges(screen);
    QList<int> edgesInt;

    for (const Plasma::Types::Location &edge : edges) {
        edgesInt.append(static_cast<int>(edge));
    }

    return edgesInt;
}

QList<Plasma::Types::Location> GenericLayout::freeEdges(QScreen *scr) const
{
    using Plasma::Types;
    QList<Types::Location> edges{Types::BottomEdge, Types::LeftEdge,
                Types::TopEdge, Types::RightEdge};

    if (!m_corona) {
        return edges;
    }

    for (const auto view : m_latteViews) {
        if (view && view->positioner()->currentScreenName() == scr->name()) {
            edges.removeOne(view->location());
        }
    }

    return edges;
}

QList<Plasma::Types::Location> GenericLayout::freeEdges(int screen) const
{
    using Plasma::Types;
    QList<Types::Location> edges{Types::BottomEdge, Types::LeftEdge,
                Types::TopEdge, Types::RightEdge};

    if (!m_corona) {
        return edges;
    }

    QScreen *scr = m_corona->screenPool()->screenForId(screen);

    for (const auto view : m_latteViews) {
        if (view && scr && view->positioner()->currentScreenName() == scr->name()) {
            edges.removeOne(view->location());
        }
    }

    return edges;
}

int GenericLayout::viewsWithTasks() const
{
    if (!m_corona) {
        return 0;
    }

    int result = 0;

    for (const auto view : m_latteViews) {
        if (view->extendedInterface()->hasLatteTasks() || view->extendedInterface()->hasPlasmaTasks()) {
            result++;
        }
    }

    return result;
}

QStringList GenericLayout::unloadedContainmentsIds()
{
    return m_unloadedContainmentsIds;
}

Latte::Corona *GenericLayout::corona() const
{
    return m_corona;
}

KSharedConfigPtr GenericLayout::activeSingleLayoutConfig() const
{
    if (!m_corona
            || m_corona->layoutsManager()->memoryUsage()
                != MemoryUsage::SingleLayout) {
        qCritical() << "layout:" << name()
                    << "cannot resolve active persistence outside SingleLayout mode";
        return {};
    }

    const KSharedConfigPtr activeConfig = m_corona->config();
    if (!activeConfig
            || canonicalConfigPath(activeConfig->name())
                != canonicalConfigPath(file())) {
        qCritical() << "layout:" << name()
                    << "refused split persistence authorities; Corona config"
                    << (activeConfig ? activeConfig->name() : QString())
                    << "does not match layout file" << file();
        return {};
    }

    return activeConfig;
}

Types::ViewType GenericLayout::latteViewType(uint containmentId) const
{
    for (const auto view : m_latteViews) {
        if (view->containment() && view->containment()->id() == containmentId) {
            return view->type();
        }
    }

    return Types::DockView;
}

Latte::View *GenericLayout::highestPriorityView()
{
    QList<Latte::View *> views = sortedLatteViews();

    return (views.count() > 0 ? views[0] : nullptr);
}

Latte::View *GenericLayout::lastConfigViewFor()
{
    return m_lastConfigViewFor;
}

void GenericLayout::setLastConfigViewFor(Latte::View *view)
{
    if (m_lastConfigViewFor == view) {
        return;
    }

    m_lastConfigViewFor = view;

    if (view) {
        Q_EMIT lastConfigViewForChanged(view);
    }
}

void GenericLayout::onLastConfigViewChangedFrom(Latte::View *view)
{
    if (!m_latteViews.values().contains(view)) {
        setLastConfigViewFor(nullptr);
    }
}

Latte::View *GenericLayout::viewForContainment(uint id) const
{
    for(auto view : m_latteViews) {
        if (view && view->containment()->id() == id) {
            return view;
        }
    }

    //! Suspended removal Views are not runtime action targets. Plasma's Undo
    //! signal reaches destroyedChanged() through the containment directly;
    //! exposing parked Views here lets D-Bus mutations edit a tombstoned
    //! object outside the active ownership map.
    return nullptr;
}

Plasma::Containment *GenericLayout::containmentForId(uint id) const
{
    for(auto containment : m_containments) {
        if (containment->id() == id) {
            return containment;
        }
    }

    return nullptr;
}

bool GenericLayout::contains(Plasma::Containment *containment) const
{
    return m_containments.contains(containment);
}

int GenericLayout::screenForContainment(Plasma::Containment *containment)
{
    if (!containment) {
        return -1;
    }

    //! there is a pending update
    QString containmentid = QString::number(containment->id());
    if (m_pendingContainmentUpdates.containsId(containmentid)) {
        if (m_corona && m_pendingContainmentUpdates[containmentid].onPrimary) {
            return m_corona->screenPool()->primaryScreenId();
        } else {
            return m_pendingContainmentUpdates[containmentid].screen;
        }
    }

    //! there is a view present
    Latte::View *view{nullptr};

    if (m_latteViews.contains(containment)) {
        view = m_latteViews[containment];
    } else if (m_waitingLatteViews.contains(containment)) {
        view = m_waitingLatteViews[containment];
    }

    if (view && view->screen()) {
        return m_corona->screenPool()->id(view->screen()->name());
    }

    //! fallback scenario
    return containment->lastScreen();
}

bool GenericLayout::containsView(const int &containmentId) const
{
    if (!isActive()) {
        return Layouts::Storage::self()->containsView(file(), containmentId);
    }

    for(auto containment : m_containments) {
        if ((int)containment->id() == containmentId && Layouts::Storage::self()->isLatteContainment(containment)) {
            return true;
        }
    }

    return false;
}

Latte::View *GenericLayout::viewForContainment(Plasma::Containment *containment) const
{
    if (m_containments.contains(containment) && m_latteViews.contains(containment)) {
        return m_latteViews[containment];
    }

    return nullptr;
}

QList<Latte::View *> GenericLayout::latteViews()
{
    return m_latteViews.values();
}

QList<Latte::View *> GenericLayout::onlyOriginalViews()
{
    QList<Latte::View *> viewslist;

    for (const auto v : m_latteViews) {
        if (v->isOriginal()) {
            viewslist << v;
        }
    }

    return viewslist;
}

QList<Latte::View *> GenericLayout::sortedLatteViews()
{
    QScreen *primaryScreen = (m_corona ? m_corona->screenPool()->primaryScreen() : nullptr);
    return sortedLatteViews(latteViews(), primaryScreen);
}

QList<Latte::View *> GenericLayout::sortedLatteViews(QList<Latte::View *> views, QScreen *primaryScreen)
{
    QList<Latte::View *> sortedViews = views;

    qDebug() << " -------- ";

    for (int i = 0; i < sortedViews.count(); ++i) {
        qDebug() << i << ". " << sortedViews[i]->screen()->name() << " - " << sortedViews[i]->location();
    }

    //! sort the views based on screens and edges priorities
    //! views on primary screen have higher priority and
    //! for views in the same screen the priority goes to
    //! Bottom,Left,Top,Right
    for (int i = 0; i < sortedViews.size(); ++i) {
        for (int j = 0; j < sortedViews.size() - i - 1; ++j) {
            if (viewAtLowerScreenPriority(sortedViews[j], sortedViews[j + 1], primaryScreen)
                    || (sortedViews[j]->screen() == sortedViews[j + 1]->screen()
                        && viewAtLowerEdgePriority(sortedViews[j], sortedViews[j + 1]))) {
                Latte::View *temp = sortedViews[j + 1];
                sortedViews[j + 1] = sortedViews[j];
                sortedViews[j] = temp;
            }
        }
    }

    Latte::View *highestPriorityView{nullptr};

    for (int i = 0; i < sortedViews.size(); ++i) {
        if (sortedViews[i]->isPreferredForShortcuts()) {
            highestPriorityView = sortedViews[i];
            sortedViews.removeAt(i);
            break;
        }
    }

    if (highestPriorityView) {
        sortedViews.prepend(highestPriorityView);
    }

    qDebug() << " -------- sorted -----";

    for (int i = 0; i < sortedViews.count(); ++i) {
        qDebug() << i << ". " << sortedViews[i]->isPreferredForShortcuts() << " - " << sortedViews[i]->screen()->name() << " - " << sortedViews[i]->location();
    }

    return sortedViews;
}

bool GenericLayout::viewAtLowerScreenPriority(Latte::View *test, Latte::View *base, QScreen *primaryScreen)
{
    if (!base || ! test) {
        return true;
    }

    if (base->screen() == test->screen()) {
        return false;
    } else if (base->screen() != primaryScreen && test->screen() == primaryScreen) {
        return false;
    } else if (base->screen() == primaryScreen && test->screen() != primaryScreen) {
        return true;
    } else {
        int basePriority = -1;
        int testPriority = -1;

        for (int i = 0; i < qGuiApp->screens().count(); ++i) {
            if (base->screen() == qGuiApp->screens()[i]) {
                basePriority = i;
            }

            if (test->screen() == qGuiApp->screens()[i]) {
                testPriority = i;
            }
        }

        if (testPriority <= basePriority) {
            return true;
        } else {
            return false;
        }

    }

    qDebug() << "viewAtLowerScreenPriority : shouldn't had reached here...";
    return false;
}

bool GenericLayout::viewAtLowerEdgePriority(Latte::View *test, Latte::View *base)
{
    if (!base || ! test) {
        return true;
    }

    QList<Plasma::Types::Location> edges{Plasma::Types::RightEdge, Plasma::Types::TopEdge,
                Plasma::Types::LeftEdge, Plasma::Types::BottomEdge};

    int testPriority = -1;
    int basePriority = -1;

    for (int i = 0; i < edges.count(); ++i) {
        if (edges[i] == base->location()) {
            basePriority = i;
        }

        if (edges[i] == test->location()) {
            testPriority = i;
        }
    }

    if (testPriority < basePriority) {
        return true;
    } else {
        return false;
    }
}

bool GenericLayout::viewDataAtLowerScreenPriority(const Latte::Data::View &test, const Latte::Data::View &base) const
{
    if (test.onPrimary && base.onPrimary) {
        return false;
    } else if (!base.onPrimary && test.onPrimary) {
        return false;
    } else if (base.onPrimary && !test.onPrimary) {
        return true;
    } else {
        return test.screen <= base.screen;
    }
}

bool GenericLayout::viewDataAtLowerStatePriority(const Latte::Data::View &test, const Latte::Data::View &base) const
{
    if (test.isActive == base.isActive) {
        return false;
    } else if (!base.isActive && test.isActive) {
        return false;
    } else if (base.isActive && !test.isActive) {
        return true;
    }

    return false;
}

bool GenericLayout::viewDataAtLowerEdgePriority(const Latte::Data::View &test, const Latte::Data::View &base) const
{
    QList<Plasma::Types::Location> edges{Plasma::Types::RightEdge, Plasma::Types::TopEdge,
                Plasma::Types::LeftEdge, Plasma::Types::BottomEdge};

    int testPriority = -1;
    int basePriority = -1;

    for (int i = 0; i < edges.count(); ++i) {
        if (edges[i] == base.edge) {
            basePriority = i;
        }

        if (edges[i] == test.edge) {
            testPriority = i;
        }
    }

    if (testPriority < basePriority) {
        return true;
    } else {
        return false;
    }
}

QList<Latte::Data::View> GenericLayout::sortedViewsData(const QList<Latte::Data::View> &viewsData)
{
    QList<Latte::Data::View> sortedData = viewsData;

    //! sort the views based on screens and edges priorities
    //! views on primary screen have higher priority and
    //! for views in the same screen the priority goes to
    //! Bottom,Left,Top,Right
    for (int i = 0; i < sortedData.size(); ++i) {
        for (int j = 0; j < sortedData.size() - i - 1; ++j) {
            if (viewDataAtLowerStatePriority(sortedData[j], sortedData[j + 1])
                    || viewDataAtLowerScreenPriority(sortedData[j], sortedData[j + 1])
                    || (!viewDataAtLowerScreenPriority(sortedData[j], sortedData[j + 1])
                        && viewDataAtLowerEdgePriority(sortedData[j], sortedData[j + 1])) ) {
                Latte::Data::View temp = sortedData[j + 1];
                sortedData[j + 1] = sortedData[j];
                sortedData[j] = temp;
            }
        }
    }

    return sortedData;
}


const QList<Plasma::Containment *> *GenericLayout::containments() const
{
    return &m_containments;
}

QList<Latte::View *> GenericLayout::viewsWithPlasmaShortcuts()
{
    QList<Latte::View *> views;

    if (!m_corona) {
        return views;
    }

    QList<uint> appletsWithShortcuts = m_corona->globalShortcuts()->shortcutsTracker()->appletsWithPlasmaShortcuts();

    for (const auto &appletId : appletsWithShortcuts) {
        for (const auto view : m_latteViews) {
            bool found{false};
            for (const auto applet : view->containment()->applets()) {
                if (appletId == applet->id()) {
                    if (!views.contains(view)) {
                        views.append(view);
                        found = true;
                        break;
                    }
                }
            }

            if (found) {
                break;
            }
        }
    }

    return views;
}


//! Containments Actions
void GenericLayout::addContainment(Plasma::Containment *containment)
{
    if (!containment || m_containments.contains(containment)) {
        return;
    }

    bool containmentInLayout{false};

    if (m_corona->layoutsManager()->memoryUsage() == MemoryUsage::SingleLayout) {
        m_containments.append(containment);
        containmentInLayout = true;
    } else if (m_corona->layoutsManager()->memoryUsage() == MemoryUsage::MultipleLayouts) {
        QString layoutId = containment->config().readEntry("layoutId", QString());

        if (!layoutId.isEmpty() && (layoutId == m_layoutName)) {
            m_containments.append(containment);
            containmentInLayout = true;
        }
    }

    if (containmentInLayout) {
        //! Plasma::Containment::restore() loads containmentActions from the
        //! corona config's top-level [ActionPlugins][<id>] group, but Latte
        //! rewrites its per-layout file with only [Containments] on save, so
        //! that binding is serialized away and never re-populated - the dock
        //! then loses its right-click menu (and with it edit mode) after the
        //! first save cycle. Re-assert the default RightButton mapping on the
        //! live containment whenever one is wired to this layout, so it is
        //! always present regardless of on-disk state.
        if (!containment->containmentActions().contains(QStringLiteral("RightButton;NoModifier"))) {
            containment->setContainmentActions(QStringLiteral("RightButton;NoModifier"),
                                               QStringLiteral("org.kde.latte.contextmenu"));
        }

        if (!blockAutomaticLatteViewCreation()) {
            addView(containment);
        } else {
            qDebug() << "delaying LatteView creation for containment :: " << containment->id();
        }

        connect(containment, &QObject::destroyed, this, &GenericLayout::containmentDestroyed);
        if (!Layouts::Storage::self()->isLatteContainment(containment)) {
            //! Root view containments are connected by View::setLayout().
            //! Subcontainments have no View, but their reversible destruction
            //! still changes the persisted projection.
            connect(containment, &Plasma::Applet::destroyedChanged,
                    this, &GenericLayout::destroyedChanged, Qt::UniqueConnection);
        }
    }
}

void GenericLayout::appletCreated(Plasma::Applet *applet)
{
    //! In Multiple Layout the orphaned subcontainments must be assigned to layouts
    //! when the user adds them
    KConfigGroup appletSettings = applet->containment()->config().group("Applets").group(QString::number(applet->id()));

    int subId = Layouts::Storage::self()->subContainmentId(appletSettings);

    if (Layouts::Storage::isValid(subId)) {
        uint sId = (uint)subId;

        for (const auto containment : m_corona->containments()) {
            if (containment->id() == sId) {
                containment->config().writeEntry("layoutId", m_layoutName);
            }

            addContainment(containment);
        }
    }
}

void GenericLayout::containmentDestroyed(QObject *cont)
{
    if (!m_corona) {
        return;
    }

    //! reinterpret_cast, not static_cast: this runs inside the containment's
    //! destroyed(QObject*) handler, where every derived destructor has already
    //! run and the object's dynamic type has decayed to QObject - a static_cast
    //! downcast reads the (now-QObject) vptr to validate the cast and is
    //! undefined behaviour (UBSan -fsanitize=vptr aborts here in the sanitized
    //! build). The pointer is used ONLY for identity below (indexOf, and the
    //! Plasma::Containment*-keyed m_latteViews/m_waitingLatteViews lookups);
    //! pointer identity survives destruction and needs no vtable, so a bit
    //! reinterpretation is both correct and vptr-clean. Same destroyed()-handler
    //! demotion family as the location()-reads-freed-memory note below.
    Plasma::Containment *containment = reinterpret_cast<Plasma::Containment *>(cont);

    if (containment) {
        cancelRemovalCommit(containment);
        m_removalSnapshots.remove(containment);
        m_removalTransactions.remove(containment);
        m_finalizingRemovalContainments.remove(
            containment);

        int containmentIndex = m_containments.indexOf(containment);

        if (containmentIndex >= 0) {
            m_containments.removeAt(containmentIndex);
        }

        qDebug() << "Layout " << name() << " :: containment destroyed!!!!";
        auto view = m_latteViews.take(containment);

        if (!view) {
            view = m_waitingLatteViews.take(containment);
        }

        if (view) {
            view->disconnectSensitiveSignals();
            //! no containment->location() here: this runs inside the
            //! containment's destroyed() handler, where the Plasma::Applet
            //! part is already destructed and location() reads freed memory
            //! (destroyed()-handler demotion, same family as d6d57e61). The
            //! positioner slides out on its cached last known edge instead.
            view->positioner()->slideOutDuringExit();
            view->deleteLater();

            Q_EMIT viewEdgeChanged();
            Q_EMIT viewsCountChanged();
        }
    }
}

void GenericLayout::destroyedChanged(bool destroyed)
{
    if (!m_corona) {
        return;
    }

    qDebug() << "dock containment destroyed changed!!!!";
    auto *const containment = qobject_cast<Plasma::Containment *>(QObject::sender());

    if (!containment) {
        return;
    }

    if (destroyed
            && m_finalizingRemovalContainments
                .contains(containment)) {
        return;
    }

    const auto memoryUsage = m_corona->layoutsManager()->memoryUsage();
    Latte::View *view = destroyed
            ? m_latteViews.value(containment)
            : m_waitingLatteViews.value(containment);
    const QString removalSnapshot =
        m_removalSnapshots.value(containment);

    //! Viewless child containments participate in Plasma's recursive Undo,
    //! but they do not own a dock snapshot or runtime View. Their parent root
    //! transaction restores them. In multiple-layout mode the projection is
    //! delayed until the root and every child have left transient state.
    if (!view) {
        const auto *const parentApplet =
            qobject_cast<const Plasma::Applet *>(
                containment->parent());
        const auto *const rootContainment =
            parentApplet
            ? parentApplet->containment()
            : nullptr;
        const bool rootTransactionOwnsProjection =
            rootContainment
            && m_removalTransactions.contains(
                rootContainment);
        if (destroyed
                && !rootTransactionOwnsProjection) {
            scheduleRemovalCommit(containment);
        } else {
            cancelRemovalCommit(containment);
        }
        if (memoryUsage == MemoryUsage::MultipleLayouts
                && !rootTransactionOwnsProjection) {
            scheduleMultipleLayoutProjectionAfterTransientState(
                destroyed
                    ? "viewless containment removal"
                    : "viewless containment Undo");
        }
        return;
    }

    if (destroyed) {
        auto &transaction =
            m_removalTransactions[containment];
        const auto token =
            transaction.beginRemoval();
        if (view
                && memoryUsage == MemoryUsage::SingleLayout
                && removalSnapshot.isEmpty()) {
            qCritical() << "layout:" << name()
                        << "entered reversible removal for containment"
                        << containment->id()
                        << "without a prepared persistence snapshot";
        }
        if (view && !view->suspendForReversibleRemoval()) {
            qCritical() << "layout:" << name()
                        << "could not suspend containment"
                        << containment->id()
                        << "for reversible removal";
            if (transaction
                    .queueRemovalFinalizationIfCurrent(
                        token)) {
                scheduleRemovalFinalization(
                    containment,
                    token,
                    memoryUsage,
                    memoryUsage
                        == MemoryUsage::SingleLayout
                        ? activeSingleLayoutConfig()
                        : KSharedConfigPtr{},
                    removalSnapshot,
                    "runtime suspension failed");
            }
            return;
        }
        view = m_latteViews.take(containment);
        if (view) {
            m_waitingLatteViews[containment] = view;
        }

        if (view
                && memoryUsage
                    == MemoryUsage::SingleLayout
                && removalSnapshot.isEmpty()) {
            if (transaction
                    .queueRemovalFinalizationIfCurrent(
                        token)) {
                scheduleRemovalFinalization(
                    containment,
                    token,
                    memoryUsage,
                    activeSingleLayoutConfig(),
                    removalSnapshot,
                    "prepared removal snapshot was absent");
            }
        } else {
            scheduleRemovalCommit(containment);
        }
        Q_EMIT m_corona->availableScreenRectChangedFrom(view);
        Q_EMIT m_corona->availableScreenRegionChangedFrom(view);
        Q_EMIT viewEdgeChanged();
        Q_EMIT viewsCountChanged();
        return;
    }

    auto transaction =
        m_removalTransactions.find(
            containment);
    if (transaction
            == m_removalTransactions.end()) {
        qCritical() << "layout:" << name()
                    << "received removal Undo without a live transaction for containment"
                    << containment->id();
        return;
    }
    const auto token =
        transaction->token();
    if (!transaction
            ->queueUndoResolutionIfCurrent(
                token)) {
        qCritical() << "layout:" << name()
                    << "refused stale removal Undo for containment"
                    << containment->id();
        return;
    }

    cancelRemovalCommit(containment);
    scheduleRemovalUndoResolution(
        containment,
        token,
        memoryUsage,
        memoryUsage == MemoryUsage::SingleLayout
            ? activeSingleLayoutConfig()
            : KSharedConfigPtr{},
        removalSnapshot);
}

void GenericLayout::
scheduleMultipleLayoutProjectionAfterTransientState(
    const char *const transition)
{
    const QString transitionDescription =
        QString::fromLatin1(transition);
    QTimer::singleShot(
        0,
        this,
        [this, transitionDescription]() {
            if (!m_corona
                    || m_corona->layoutsManager()
                        ->memoryUsage()
                        != MemoryUsage::MultipleLayouts) {
                return;
            }
            if (!Layouts::Storage::self()
                    ->syncToLayoutFile(this, false)) {
                qFatal(
                    "layout %s could not persist %s after libplasma completed child transient state",
                    qPrintable(name()),
                    qPrintable(transitionDescription));
            }
        });
}

void GenericLayout::scheduleRemovalUndoResolution(
    Plasma::Containment *containment,
    const RemovalUndoTransaction::Token token,
    const MemoryUsage::LayoutsMemory memoryUsage,
    KSharedConfigPtr activeConfig,
    QString snapshot)
{
    Q_ASSERT(containment);
    const auto pending =
        m_removalTransactions.constFind(containment);
    Q_ASSERT(pending
             != m_removalTransactions.cend());
    if (pending
            == m_removalTransactions.cend()) {
        qCritical() << "layout:" << name()
                    << "cannot queue removal Undo resolution without its transaction";
        return;
    }

    const QPointer<Plasma::Containment>
        guardedContainment{containment};
    RemovalUndoTransaction queuedTransaction =
        *pending;

    //! libplasma emits root destroyedChanged(false) before recursively
    //! clearing child transient state. No persistence or runtime owner changes
    //! are permitted until this next-turn boundary.
    QTimer::singleShot(
        0,
        this,
        [this,
         guardedContainment,
         containmentIdentity = containment,
         token,
         memoryUsage,
         activeConfig = std::move(activeConfig),
         snapshot = std::move(snapshot),
         transaction = std::move(
             queuedTransaction)]() mutable {
            if (!guardedContainment) {
                const auto resolution =
                    transaction
                        .resolveUndoOrRequireRemoval(
                            token,
                            []() { return false; },
                            []() { return false; });
                Q_ASSERT(
                    resolution
                    == RemovalUndoTransaction::
                        UndoResolution::
                            RemovalRequired);
                const auto result =
                    transaction
                        .finalizeRemovalIfCurrent(
                            token,
                            []() { return true; },
                            []() { return true; },
                            [this, memoryUsage,
                             &activeConfig,
                             &snapshot]() {
                                return commitRemovalPersistenceAfterDestruction(
                                    memoryUsage,
                                    activeConfig,
                                    snapshot);
                            });
                if (result
                        != RemovalUndoTransaction::
                            FinalizationResult::
                                Removed) {
                    qFatal(
                        "layout %s lost a containment during Undo and could not preserve its removal tombstone",
                        qPrintable(name()));
                }
                return;
            }

            auto liveTransaction =
                m_removalTransactions.find(
                    containmentIdentity);
            if (liveTransaction
                    == m_removalTransactions.end()
                    || liveTransaction->token()
                        != token) {
                return;
            }
            transaction = *liveTransaction;
            View *const waitingView =
                m_waitingLatteViews.value(
                    containmentIdentity);
            const auto resolution =
                transaction
                    .resolveUndoOrRequireRemoval(
                        token,
                        [this, memoryUsage,
                         &activeConfig,
                         &snapshot]() {
                            if (memoryUsage
                                    == MemoryUsage::
                                        MultipleLayouts) {
                                return Layouts::Storage::self()
                                    ->syncToLayoutFile(
                                        this,
                                        false);
                            }
                            return activeConfig
                                && !snapshot.isEmpty()
                                && Layouts::Storage::self()
                                    ->restoreView(
                                        activeConfig,
                                        snapshot);
                        },
                        [waitingView]() {
                            return waitingView
                                && waitingView
                                    ->resumeFromReversibleRemoval();
                        });
            if (resolution
                    == RemovalUndoTransaction::
                        UndoResolution::Stale) {
                return;
            }
            if (resolution
                    == RemovalUndoTransaction::
                        UndoResolution::
                            RemovalRequired) {
                *liveTransaction = transaction;
                scheduleRemovalFinalization(
                    containmentIdentity,
                    token,
                    memoryUsage,
                    activeConfig,
                    snapshot,
                    "persistence restore or runtime resume failed");
                return;
            }

            View *const restoredView =
                m_waitingLatteViews.take(
                    containmentIdentity);
            Q_ASSERT(restoredView
                     == waitingView);
            m_latteViews[containmentIdentity] =
                restoredView;
            m_removalTransactions.erase(
                liveTransaction);
            m_removalSnapshots.remove(
                containmentIdentity);

            Q_EMIT m_corona
                ->availableScreenRectChangedFrom(
                    restoredView);
            Q_EMIT m_corona
                ->availableScreenRegionChangedFrom(
                    restoredView);
            Q_EMIT viewEdgeChanged();
            Q_EMIT viewsCountChanged();
        });
}

bool GenericLayout::prepareViewRemoval(Plasma::Containment *containment)
{
    if (!containment || !m_corona || !contains(containment)) {
        qCritical() << "layout:" << name()
                    << "cannot prepare removal for an unowned containment" << containment;
        return false;
    }

    if (m_corona->layoutsManager()->memoryUsage() != MemoryUsage::SingleLayout
            || !Layouts::Storage::self()->isLatteContainment(containment)
            || m_removalSnapshots.contains(containment)) {
        return true;
    }

    const QString snapshot = Layouts::Storage::self()->storedView(
        this, static_cast<int>(containment->id()));
    if (snapshot.isEmpty()) {
        qCritical() << "layout:" << name()
                    << "refusing removal because containment" << containment->id()
                    << "could not be snapshotted for Undo";
        return false;
    }

    m_removalSnapshots.insert(containment, snapshot);
    return true;
}

bool GenericLayout::commitPreparedViewRemoval(
    Plasma::Containment *containment,
    const RemovalCommitMode mode)
{
    if (!containment || !m_corona || !contains(containment)) {
        qCritical() << "layout:" << name()
                    << "cannot commit removal for an unowned containment"
                    << containment;
        return false;
    }

    if (!containment->destroyed()) {
        qCritical() << "layout:" << name()
                    << "cannot commit removal before containment"
                    << containment->id() << "enters destroyed state";
        m_removalSnapshots.remove(containment);
        return false;
    }

    const auto memoryUsage =
        m_corona->layoutsManager()->memoryUsage();
    const QString snapshot = m_removalSnapshots.value(containment);
    const KSharedConfigPtr activeConfig =
        memoryUsage == MemoryUsage::SingleLayout
        ? activeSingleLayoutConfig()
        : KSharedConfigPtr{};
    const bool persistenceCommitted =
        memoryUsage == MemoryUsage::MultipleLayouts
        ? Layouts::Storage::self()
            ->syncToLayoutFile(this, false)
        : activeConfig
            && !snapshot.isEmpty()
            && Layouts::Storage::self()
                ->tombstoneViewFromSnapshot(
                    activeConfig,
                    snapshot);
    if (!persistenceCommitted) {
        qCritical() << "layout:" << name()
                    << "could not persist removal tombstone for containment"
                    << containment->id();
        auto transaction =
            m_removalTransactions.find(
                containment);
        if (mode
                == RemovalCommitMode::Reversible
                && transaction
                    != m_removalTransactions.end()
                && transaction
                    ->queueRemovalFinalizationIfCurrent(
                        transaction->token())) {
            scheduleRemovalFinalization(
                containment,
                transaction->token(),
                memoryUsage,
                activeConfig,
                snapshot,
                "initial removal tombstone failed");
        }
        return false;
    }

    if (mode == RemovalCommitMode::Permanent) {
        m_removalSnapshots.remove(containment);
    }
    return true;
}

void GenericLayout::cancelRemovalCommit(Plasma::Containment *containment)
{
    if (auto *const timer = m_removalCommitTimers.take(containment)) {
        timer->stop();
        timer->deleteLater();
    }
}

bool GenericLayout::
commitRemovalPersistenceAfterDestruction(
    const MemoryUsage::LayoutsMemory memoryUsage,
    const KSharedConfigPtr &activeConfig,
    const QString &snapshot)
{
    if (memoryUsage
            == MemoryUsage::MultipleLayouts) {
        return Layouts::Storage::self()
            ->syncToLayoutFile(this, false);
    }

    if (!activeConfig || snapshot.isEmpty()) {
        qCritical() << "layout:" << name()
                    << "cannot commit failed-Undo removal without its captured config and snapshot";
        return false;
    }

    return Layouts::Storage::self()
        ->tombstoneViewFromSnapshot(
            activeConfig,
            snapshot);
}

bool GenericLayout::retireRuntimeViewForFailedRemoval(
    Plasma::Containment *containment)
{
    View *view =
        m_latteViews.take(containment);
    if (!view) {
        view = m_waitingLatteViews.take(
            containment);
    }
    if (view) {
        view->disconnectSensitiveSignals();
        view->setVisible(false);
        view->deleteLater();
        Q_EMIT m_corona
            ->availableScreenRectChangedFrom(view);
        Q_EMIT m_corona
            ->availableScreenRegionChangedFrom(view);
        Q_EMIT viewEdgeChanged();
        Q_EMIT viewsCountChanged();
    }

    return !m_latteViews.contains(containment)
        && !m_waitingLatteViews.contains(
            containment);
}

void GenericLayout::scheduleRemovalFinalization(
    Plasma::Containment *containment,
    const RemovalUndoTransaction::Token token,
    const MemoryUsage::LayoutsMemory memoryUsage,
    KSharedConfigPtr activeConfig,
    QString snapshot,
    const char *const failure)
{
    Q_ASSERT(containment);
    const auto pending =
        m_removalTransactions.constFind(containment);
    Q_ASSERT(pending
             != m_removalTransactions.cend());
    if (pending
            == m_removalTransactions.cend()) {
        qCritical() << "layout:" << name()
                    << "cannot queue removal finalization without its transaction";
        return;
    }
    const QPointer<Plasma::Containment>
        guardedContainment{containment};
    RemovalUndoTransaction queuedTransaction =
        *pending;
    const QString failureDescription =
        QString::fromLatin1(failure);

    //! libplasma emits root destroyedChanged(false) before recursively
    //! clearing child transient state. The next event-loop turn is the first
    //! boundary at which permanent destruction and the following tombstone
    //! cannot be overwritten by the remainder of that Undo.
    QTimer::singleShot(
        0,
        this,
        [this,
         guardedContainment,
         containmentIdentity = containment,
         token,
         memoryUsage,
         activeConfig = std::move(activeConfig),
         snapshot = std::move(snapshot),
         failureDescription,
         transaction = std::move(
             queuedTransaction)]() mutable {
            Plasma::Containment *const identity =
                guardedContainment
                ? guardedContainment.data()
                : containmentIdentity;
            if (guardedContainment) {
                const auto liveTransaction =
                    m_removalTransactions.find(
                        identity);
                if (liveTransaction
                        == m_removalTransactions.end()
                        || liveTransaction->token()
                            != token) {
                    return;
                }
                transaction = *liveTransaction;
                m_removalTransactions.erase(
                    liveTransaction);
            }
            cancelRemovalCommit(
                identity);
            m_finalizingRemovalContainments.insert(
                identity);

            const auto result =
                transaction
                    .finalizeRemovalIfCurrent(
                        token,
                        [this, identity]() {
                            return retireRuntimeViewForFailedRemoval(
                                identity);
                        },
                        [this, guardedContainment,
                         identity]() {
                            if (!guardedContainment) {
                                return !m_containments
                                    .contains(identity);
                            }
                            guardedContainment
                                ->setImmutability(
                                    Plasma::Types::
                                        Mutable);
                            //! Applet::destroy(), unlike the public remove
                            //! action, commits transient deletion immediately.
                            //! containmentDestroyed() removes layout ownership
                            //! synchronously before deleteLater() retires the
                            //! QObject itself.
                            guardedContainment->destroy();
                            return !m_containments
                                    .contains(identity)
                                && !m_latteViews
                                    .contains(identity)
                                && !m_waitingLatteViews
                                    .contains(identity);
                        },
                        [this, memoryUsage,
                         &activeConfig,
                         &snapshot]() {
                            return commitRemovalPersistenceAfterDestruction(
                                memoryUsage,
                                activeConfig,
                                snapshot);
                        });
            m_finalizingRemovalContainments.remove(
                identity);
            if (result
                    != RemovalUndoTransaction::
                        FinalizationResult::Removed) {
                qFatal(
                    "layout %s could not converge failed removal (%s), finalization result %d",
                    qPrintable(name()),
                    qPrintable(failureDescription),
                    static_cast<int>(result));
            }

            qCritical() << "layout:" << name()
                        << "completed removal after failed restoration:"
                        << failureDescription;
        });
}

void GenericLayout::scheduleRemovalCommit(Plasma::Containment *containment)
{
    Q_ASSERT(containment);

    cancelRemovalCommit(containment);

    const auto memoryUsage =
        m_corona->layoutsManager()->memoryUsage();
    const KSharedConfigPtr activeConfig =
        memoryUsage == MemoryUsage::SingleLayout
        ? activeSingleLayoutConfig()
        : KSharedConfigPtr{};
    const QString snapshot =
        m_removalSnapshots.value(containment);
    auto *const timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(RemovalUndoWindow);
    m_removalCommitTimers.insert(containment, timer);

    connect(
        timer,
        &QTimer::timeout,
        this,
        [this, containment, timer, memoryUsage,
         activeConfig, snapshot]() {
        const bool ownsCurrentGeneration = m_removalCommitTimers.value(containment) == timer;
        Q_ASSERT(ownsCurrentGeneration);
        if (!ownsCurrentGeneration) {
            qCritical() << "layout:" << name()
                        << "refusing a stale containment-removal commit for" << containment;
            return;
        }

        m_removalCommitTimers.remove(containment);
        timer->deleteLater();

        if (!containment->destroyed()) {
            qCritical() << "layout:" << name()
                        << "discarded an expired removal timer after containment Undo"
                        << containment->id();
            return;
        }

        auto transaction =
            m_removalTransactions.find(
                containment);
        if (transaction
                != m_removalTransactions.end()) {
            const auto token =
                transaction->token();
            if (!transaction
                    ->queueRemovalFinalizationIfCurrent(
                        token)) {
                qFatal(
                    "layout %s could not finalize expired removal for containment %u from transaction phase %d",
                    qPrintable(name()),
                    containment->id(),
                    static_cast<int>(
                        transaction->phase()));
            }
            scheduleRemovalFinalization(
                containment,
                token,
                memoryUsage,
                activeConfig,
                snapshot,
                "removal Undo window expired");
            return;
        }

        //! A viewless containment has no root transaction. Direct destroy
        //! must still prove that libplasma released layout ownership.
        m_finalizingRemovalContainments.insert(
            containment);
        containment->setImmutability(
            Plasma::Types::Mutable);
        containment->destroy();
        m_finalizingRemovalContainments.remove(
            containment);
        if (m_containments.contains(containment)) {
            qFatal(
                "layout %s could not retire expired viewless containment %u",
                qPrintable(name()),
                containment->id());
        }
    });

    timer->start();
}

void GenericLayout::renameLayout(QString newName)
{
    if (!m_corona || m_corona->layoutsManager()->memoryUsage() != MemoryUsage::MultipleLayouts) {
        return;
    }

    if (m_layoutFile != Layouts::Importer::layoutUserFilePath(newName)) {
        setFile(Layouts::Importer::layoutUserFilePath(newName));
    }

    setName(newName);

    for (const auto containment : m_containments) {
        qDebug() << "Cont ID :: " << containment->id();
        containment->config().writeEntry("layoutId", m_layoutName);
    }
}

void GenericLayout::addView(Plasma::Containment *containment)
{
    qDebug().noquote() << "Adding View: Called for layout:" << m_layoutName << "with m_containments.size() ::" << m_containments.size();

    if (!containment || !m_corona || !containment->pluginMetaData().isValid()) {
        qWarning() << "Adding View: The requested containment plugin can not be located or loaded";
        return;
    }

    qDebug() << "Adding View:" << containment->id() << "- Step 1...";

    if (!Layouts::Storage::self()->isLatteContainment(containment)) {
        return;
    }

    qDebug() << "Adding View:" << containment->id() << "- Step 2...";

    if (hasLatteView(containment)) {
        return;
    }

    qDebug() << "Adding View:" << containment->id() << "- Step 3...";

    QScreen *nextScreen{m_corona->screenPool()->primaryScreen()};
    Data::View viewdata = Layouts::Storage::self()->view(this, containment);
    if (!viewdata.isValid()) {
        qCritical() << "GenericLayout::addView refused invalid persisted dock record for containment"
                    << containment->id() << "in layout" << name();
        return;
    }
    viewdata.screen = Layouts::Storage::self()->expectedViewScreenId(m_corona, viewdata);

    QString nextScreenName = m_corona->screenPool()->hasScreenId(viewdata.screen) ? m_corona->screenPool()->connector(viewdata.screen) : "";

    qDebug().noquote() << "Adding View:" << viewdata.id << "-"
                       << "IsClonedFrom:" << viewdata.isClonedFrom
                       << ", NextScreen:" << viewdata.screen << "-" << nextScreenName
                       << ", OnPrimary:" << viewdata.onPrimary
                       << ", Edge:" << viewdata.edge;

    if (!viewdata.onPrimary && Layouts::Storage::isValid(viewdata.screen)) {
        bool foundNextExplicitScreen{false};

        if (m_corona->screenPool()->isScreenActive(viewdata.screen)) {
            foundNextExplicitScreen = true;
            nextScreen = m_corona->screenPool()->screenForId(viewdata.screen);
        }

        if (!foundNextExplicitScreen) {
            qDebug().noquote() << "Adding View:" << viewdata.id << "- Rejected because Screen is not available :: " << nextScreenName;
            return;
        }
    }

    //! it is used to set the correct flag during the creation
    //! of the window... This of course is also used during
    //! recreations of the window between different visibility modes
    auto mode = static_cast<Types::Visibility>(containment->config().readEntry("visibility", static_cast<int>(Types::DodgeActive)));
    bool byPassWM{false};

    if (mode == Types::AlwaysVisible
            || mode == Types::WindowsGoBelow
            || mode == Types::WindowsCanCover
            || mode == Types::WindowsAlwaysCover) {
        byPassWM = false;
    } else {
        byPassWM = containment->config().readEntry("byPassWM", false);
    }

    Latte::View *latteView;

    if (!viewdata.isCloned()) {
        latteView = new Latte::OriginalView(m_corona, nextScreen, byPassWM);
    } else {
        auto view = viewForContainment((uint)viewdata.isClonedFrom);

        if (!containsView(viewdata.isClonedFrom) || !view) {
            qDebug().noquote() << "Adding View:" << viewdata.id << "- Clone did not find OriginalView and as such was stopped!!!";
            return;
        }

        auto *const originalview = qobject_cast<Latte::OriginalView *>(view);
        if (!originalview) {
            qCritical() << "GenericLayout::addView refused linked dock" << viewdata.id
                        << "because persisted root" << viewdata.isClonedFrom
                        << "is itself a linked member";
            return;
        }
        latteView = new Latte::ClonedView(
            m_corona, originalview, viewdata.linkPlacement, nextScreen, byPassWM);
    }

    qDebug().noquote() << "Adding View:" << viewdata.id << "- Passed ALL checks !!!";
    m_latteViews[containment] = latteView;

    latteView->init(containment);
    latteView->setContainment(containment);
    latteView->setLayout(this);

    //! the layer surface can only be configured before the window is first
    //! shown; on X11 this is a no-op
    latteView->setupWaylandLayerShell();

    latteView->show();

    Q_EMIT viewsCountChanged();
}

void GenericLayout::toggleHiddenState(QString viewName, QString screenName, Plasma::Types::Location edge)
{
    if (!m_corona) {
        return;
    }

    QString validScreenName = m_corona->screenPool()->primaryScreen()->name();
    if (!screenName.isEmpty()) {
        validScreenName = screenName;
    }

    int viewsOnEdge{0};

    for(const auto view : latteViews()) {
        if ((viewName.isEmpty() || (!viewName.isEmpty() && viewName == view->name()))
                && view->positioner()->currentScreenName() == validScreenName
                && (edge == Plasma::Types::Floating || ((edge != Plasma::Types::Floating) && view->location() == edge))) {
            viewsOnEdge++;
        }
    }

    if (viewsOnEdge >= 1) {
        for(const auto view : latteViews()) {
            if ((viewName.isEmpty() || (!viewName.isEmpty() && viewName == view->name()))
                    && view->positioner()->currentScreenName() == validScreenName
                    && (edge == Plasma::Types::Floating || ((edge != Plasma::Types::Floating) && view->location() == edge))) {
                view->visibility()->toggleHiddenState();
            }
        }
    }
}

bool GenericLayout::initCorona()
{
    if (!m_corona) {
        return false;
    }

    connect(m_corona, &Plasma::Corona::containmentAdded, this, &GenericLayout::addContainment);

    updateLastUsedActivity();

    //! signals
    connect(this, &GenericLayout::activitiesChanged, this, &GenericLayout::updateLastUsedActivity);
    connect(m_corona->activitiesConsumer(), &KActivities::Consumer::currentActivityChanged, this, &GenericLayout::updateLastUsedActivity);
    connect(m_corona->activitiesConsumer(), &KActivities::Consumer::activitiesChanged, this, &GenericLayout::updateLastUsedActivity);

    connect(this, &GenericLayout::lastConfigViewForChanged, m_corona->layoutsManager(), &Layouts::Manager::lastConfigViewChangedFrom);
    connect(m_corona->layoutsManager(), &Layouts::Manager::lastConfigViewChangedFrom, this, &GenericLayout::onLastConfigViewChangedFrom);

    //!connect signals after adding the containment
    connect(this, &GenericLayout::viewsCountChanged, m_corona, &Latte::Corona::notifyAvailableScreenGeometriesChanged);

    return true;
}

bool GenericLayout::initContainments()
{
    if (!m_corona || m_hasInitializedContainments) {
        return false;
    }

    qDebug() << "Layout ::::: " << name() << " added containments ::: " << m_containments.size();

    //! Views are created one per event loop pass instead of inside this
    //! loop. Every Latte::View::init() blocks the main thread for the
    //! containment's full synchronous applet QML load (upstream PlasmaQuick
    //! design, ~1-1.5s per dock measured), so creating all views back to
    //! back kept the FIRST dock unmapped until the LAST one finished
    //! compiling - ~4.5s to the first painted dock on a three-dock layout.
    //! Staggered, each view maps and paints while the next one constructs.
    //! Trickling views is not a novel state: runtime view addition
    //! (duplicateView, moveViewToLayout, screen changes) already lands one
    //! view at a time through the same guarded addView() path.
    const bool wasblocked = m_blockAutomaticLatteViewCreation;
    m_blockAutomaticLatteViewCreation = true;

    for(int pass=1; pass<=2; ++pass) {
        for (const auto containment : m_corona->containments()) {
            //! in first pass we load subcontainments
            //! in second pass we load main dock and panel containments
            //! this way subcontainments will be always available to find when the layout is activating
            //! for example during startup that clones must be created and subcontainments should be taken into account
            if ((pass==1 && Layouts::Storage::self()->isLatteContainment(containment)
                 || (pass==2 && !Layouts::Storage::self()->isLatteContainment(containment)))) {
                continue;
            }

            if (m_corona->layoutsManager()->memoryUsage() == MemoryUsage::SingleLayout) {
                addContainment(containment);
            } else if (m_corona->layoutsManager()->memoryUsage() == MemoryUsage::MultipleLayouts) {
                QString layoutId = containment->config().readEntry("layoutId", QString());

                if (!layoutId.isEmpty() && (layoutId == m_layoutName)) {
                    addContainment(containment);
                }
            }
        }
    }

    m_blockAutomaticLatteViewCreation = wasblocked;

    QList<QPointer<Plasma::Containment>> pending;

    for (const auto containment : m_containments) {
        if (Layouts::Storage::self()->isLatteContainment(containment) && !hasLatteView(containment)) {
            pending << containment;
        }
    }

    Data::ViewsTable persistedViews;
    for (const auto &containment : std::as_const(pending)) {
        if (containment) {
            persistedViews << Layouts::Storage::self()->view(this, containment);
        }
    }

    const QString relationshipError = persistedViews.relationshipValidationError();
    if (!relationshipError.isEmpty()) {
        qCritical() << "GenericLayout::initContainments refused malformed dock relationship graph in layout"
                    << name() << ":" << relationshipError;
        return false;
    }

    //! A linked member needs its relationship root's runtime coordinator.
    //! Load roots first while preserving the on-disk order within each role.
    //! Screen-group replicas are normally absent here, while explicit linked
    //! members persist and follow their root in the second partition.
    const auto startupRank = [](const QPointer<Plasma::Containment> &containment) {
        if (!containment) {
            return 2;
        }

        return Layouts::Storage::self()->isClonedView(containment) ? 1 : 0;
    };
    std::stable_sort(pending.begin(), pending.end(), [&](const auto &left, const auto &right) {
        return startupRank(left) < startupRank(right);
    });

    addNextStartupView(pending);

    m_hasInitializedContainments = true;
    Q_EMIT viewsCountChanged();
    return true;
}

void GenericLayout::addNextStartupView(QList<QPointer<Plasma::Containment>> pending)
{
    if (pending.isEmpty()) {
        return;
    }

    auto containment = pending.takeFirst();

    //! the QPointer nulls if the containment died between event loop turns;
    //! hasLatteView() covers a view that appeared meanwhile through another
    //! path (e.g. a screen-change sync) - addView() also rechecks both
    if (containment && !blockAutomaticLatteViewCreation() && !hasLatteView(containment)) {
        addView(containment);
    }

    if (!pending.isEmpty()) {
        //! queued on this layout: destruction of the layout cancels the chain
        QMetaObject::invokeMethod(this, [this, pending]() {
            addNextStartupView(pending);
        }, Qt::QueuedConnection);
    }
}

void GenericLayout::updateLastUsedActivity()
{
    if (!m_corona) {
        return;
    }

    QString currentId = m_corona->activitiesConsumer()->currentActivity();
    QStringList appliedActivitiesIds = appliedActivities();

    if (appliedActivitiesIds.contains(Data::Layout::ALLACTIVITIESID)
            || (m_lastUsedActivity != currentId && appliedActivitiesIds.contains(currentId))) {
        m_lastUsedActivity = currentId;
        Q_EMIT lastUsedActivityChanged();
    }
}

void GenericLayout::assignToLayout(Latte::View *latteView, QList<Plasma::Containment *> containments)
{
    if (!m_corona || containments.isEmpty()) {
        return;
    }

    if (latteView) {
        m_latteViews[latteView->containment()] = latteView;
    }

    m_containments << containments;

    for (const auto containment : containments) {
        containment->config().writeEntry("layoutId", name());

        if (!latteView || (latteView && latteView->containment() != containment)) {
            //! assign signals only to subcontainments
            //! the View::setLayout() is responsible for the View::Containment signals
            connect(containment, &QObject::destroyed, this, &GenericLayout::containmentDestroyed);
            connect(containment, &Plasma::Applet::destroyedChanged, this, &GenericLayout::destroyedChanged);
            connect(containment, &Plasma::Containment::appletCreated, this, &GenericLayout::appletCreated);
        }
    }

    if (latteView) {
        latteView->setLayout(this);
    }

    Q_EMIT viewsCountChanged();

    //! sync the original layout file for integrity
    if (m_corona->layoutsManager()->memoryUsage() == MemoryUsage::MultipleLayouts) {
        if (!Layouts::Storage::self()
                ->syncToLayoutFile(this, false)) {
            qCritical() << "layout:" << name()
                        << "could not synchronize assigned containment ownership";
        }
    }
}

QList<Plasma::Containment *> GenericLayout::unassignFromLayout(Plasma::Containment *latteContainment)
{
    QList<Plasma::Containment *> containments;

    if (!m_corona || !latteContainment || !contains(latteContainment)) {
        return containments;
    }

    containments << latteContainment;

    for (const auto containment : m_containments) {
        Plasma::Applet *parentApplet = qobject_cast<Plasma::Applet *>(containment->parent());

        //! add subcontainments from that latteView
        if (parentApplet && parentApplet->containment() && parentApplet->containment() == latteContainment) {
            containments << containment;
            //! unassign signals only to subcontainments
            //! the View::setLayout() is responsible for the View::Containment signals
            disconnect(containment, &QObject::destroyed, this, &GenericLayout::containmentDestroyed);
            disconnect(containment, &Plasma::Applet::destroyedChanged, this, &GenericLayout::destroyedChanged);
            disconnect(containment, &Plasma::Containment::appletCreated, this, &GenericLayout::appletCreated);
        }
    }

    for (const auto containment : containments) {
        m_containments.removeAll(containment);
    }

    if (containments.size() > 0) {
        m_latteViews.remove(latteContainment);
    }

    //! sync the original layout file for integrity
    if (m_corona && m_corona->layoutsManager()->memoryUsage() == MemoryUsage::MultipleLayouts) {
        if (!Layouts::Storage::self()
                ->syncToLayoutFile(this, false)) {
            qCritical() << "layout:" << name()
                        << "could not synchronize unassigned containment ownership";
        }
    }

    return containments;
}

void GenericLayout::recreateView(Plasma::Containment *containment, bool delayed)
{
    if (!m_corona || !containment || !m_latteViews.contains(containment)) {
        return;
    }

    View *const requestedView = m_latteViews.value(containment);
    auto *const relationshipRoot = qobject_cast<OriginalView *>(requestedView);

    QList<QPointer<Plasma::Containment>> containmentsToRecreate{containment};
    if (relationshipRoot) {
        //! Replacing a relationship root also replaces every live member.
        //! ClonedView keeps its root as a QPointer, so leaving member runtimes
        //! alive would strand them on the destroyed generation. Persistent
        //! containment identities remain unchanged and the replacement root
        //! is constructed before its members below.
        for (View *const view : std::as_const(m_latteViews)) {
            if (view && view != relationshipRoot
                    && view->relationshipRootView() == relationshipRoot
                    && view->containment()) {
                containmentsToRecreate << view->containment();
            }
        }
    }

    const bool alreadyRecreating = std::any_of(
        containmentsToRecreate.cbegin(), containmentsToRecreate.cend(),
        [this](const QPointer<Plasma::Containment> &candidate) {
            return candidate && m_viewsToRecreate.contains(candidate.data());
        });
    if (alreadyRecreating) {
        qWarning() << "recreate: refused overlapping runtime-view replacement for containment"
                   << containment->id();
        return;
    }

    QList<const Plasma::Containment *> recreationKeys;
    recreationKeys.reserve(containmentsToRecreate.size());
    for (const auto &candidate : std::as_const(containmentsToRecreate)) {
        Q_ASSERT(candidate);
        const Plasma::Containment *const key = candidate.data();
        recreationKeys << key;
        m_viewsToRecreate.insert(key);
    }

    const bool recreatesRelationship = relationshipRoot != nullptr;
    const auto clearRecreationRecords = [this, recreationKeys]() {
        for (const Plasma::Containment *const key : recreationKeys) {
            m_viewsToRecreate.remove(key);
        }
    };

    const auto delay = delayed ? ConfigurationCloseDelay : std::chrono::milliseconds::zero();

    //! give the time to config window to close itself first and then recreate the dock
    //! step:1 remove the latteview
    QTimer::singleShot(delay, this, [this, containmentsToRecreate,
                                      clearRecreationRecords, recreatesRelationship]() {
        const auto rootContainment = containmentsToRecreate.constFirst();
        if (!rootContainment || !m_latteViews.contains(rootContainment.data())) {
            qWarning() << "recreate - step 1: the requested view disappeared during the recreation delay";
            clearRecreationRecords();
            return;
        }

        QList<View *> viewsToDelete;
        viewsToDelete.reserve(containmentsToRecreate.size());

        //! Members are queued before the root so their destructors can still
        //! unregister from the live root. QObject::deleteLater preserves event
        //! posting order within this thread.
        for (auto it = std::next(containmentsToRecreate.cbegin());
             it != containmentsToRecreate.cend(); ++it) {
            if (*it) {
                if (View *const view = m_latteViews.take(it->data())) {
                    viewsToDelete << view;
                }
            }
        }
        if (View *const rootView = m_latteViews.take(rootContainment.data())) {
            viewsToDelete << rootView;
        }

        if (viewsToDelete.isEmpty()) {
            qCritical() << "recreate - step 1: no runtime views remained for the scheduled replacement";
            clearRecreationRecords();
            return;
        }

        struct DestructionState {
            int remaining{0};
            bool replacementScheduled{false};
        };
        const auto destruction = std::make_shared<DestructionState>();
        destruction->remaining = viewsToDelete.size();

        //! step:2 add the new latteview
        for (View *const view : std::as_const(viewsToDelete)) {
            view->disconnectSensitiveSignals();
            connect(view, &QObject::destroyed, this,
                    [this, containmentsToRecreate, clearRecreationRecords,
                     destruction, recreatesRelationship]() {
                Q_ASSERT(destruction->remaining > 0);
                --destruction->remaining;
                if (destruction->remaining != 0) {
                    return;
                }

                Q_ASSERT(!destruction->replacementScheduled);
                destruction->replacementScheduled = true;
                QTimer::singleShot(RuntimeViewReplacementDelay, this,
                                   [this, containmentsToRecreate,
                                    clearRecreationRecords, recreatesRelationship]() {
                    const Layout::ViewsMap eligibleViews = validViewsMap();
                    bool rootRuntimeReady{!recreatesRelationship};

                    for (int index = 0; index < containmentsToRecreate.size(); ++index) {
                        const auto &candidate = containmentsToRecreate[index];
                        if (!candidate || !m_containments.contains(candidate.data())) {
                            qWarning() << "recreate - step 2: a containment was destroyed during runtime replacement";
                            continue;
                        }
                        if (!mapContainsId(&eligibleViews, candidate->id())) {
                            qWarning() << "recreate - step 2: containment" << candidate->id()
                                       << "is no longer eligible on an active output";
                            continue;
                        }
                        if (recreatesRelationship && index > 0 && !rootRuntimeReady) {
                            qWarning() << "recreate - step 2: linked member" << candidate->id()
                                       << "cannot outlive its runtime root";
                            continue;
                        }
                        if (m_latteViews.contains(candidate.data())) {
                            qWarning() << "recreate - step 2: containment" << candidate->id()
                                       << "already has a runtime view; skipping duplicate construction";
                            if (recreatesRelationship && index == 0) {
                                rootRuntimeReady = true;
                            }
                            continue;
                        }

                        qDebug() << "recreate - step 2: adding dock for containment:" << candidate->id();
                        addView(candidate.data());
                        if (recreatesRelationship && index == 0) {
                            rootRuntimeReady = m_latteViews.contains(candidate.data());
                        }
                    }

                    clearRecreationRecords();

                    //! Initial root synchronization was suppressed while the
                    //! relationship was incomplete. Reconcile derived output
                    //! members only after every preserved member has rebound.
                    const auto rootContainment = containmentsToRecreate.constFirst();
                    if (rootContainment) {
                        if (auto *const root = qobject_cast<OriginalView *>(
                                m_latteViews.value(rootContainment.data()))) {
                            root->synchronizeScreenGroupMembers();
                        }
                    }
                });
            });
            view->deleteLater();
        }
    });
}

bool GenericLayout::isRecreatingView(const Plasma::Containment *containment) const
{
    return containment && m_viewsToRecreate.contains(containment);
}


bool GenericLayout::hasLatteView(Plasma::Containment *containment)
{
    if (!m_corona) {
        return false;
    }

    return m_latteViews.keys().contains(containment);
}

QList<Plasma::Types::Location> GenericLayout::availableEdgesForView(QScreen *scr, Latte::View *forView) const
{
    using Plasma::Types;
    QList<Types::Location> edges{Types::BottomEdge, Types::LeftEdge,
                Types::TopEdge, Types::RightEdge};

    if (!m_corona) {
        return edges;
    }

    for (const auto view : m_latteViews) {
        //! make sure that available edges takes into account only views that should be excluded,
        //! this is why the forView should not be excluded
        if (view && view != forView && view->positioner()->currentScreenName() == scr->name()) {
            edges.removeOne(view->location());
        }
    }

    return edges;
}

bool GenericLayout::explicitDockOccupyEdge(int screen, Plasma::Types::Location location) const
{
    if (!m_corona) {
        return false;
    }

    for (const auto containment : m_containments) {
        if (Layouts::Storage::self()->isLatteContainment(containment)) {
            bool onPrimary = containment->config().readEntry("onPrimary", true);
            int id = containment->lastScreen();
            Plasma::Types::Location contLocation = containment->location();

            if (!onPrimary && id == screen && contLocation == location) {
                return true;
            }
        }
    }

    return false;
}

bool GenericLayout::primaryDockOccupyEdge(Plasma::Types::Location location) const
{
    if (!m_corona) {
        return false;
    }

    for (const auto containment : m_containments) {
        if (Layouts::Storage::self()->isLatteContainment(containment)) {
            bool onPrimary{false};

            if (m_latteViews.contains(containment)) {
                onPrimary = m_latteViews[containment]->onPrimary();
            } else {
                onPrimary = containment->config().readEntry("onPrimary", true);
            }

            Plasma::Types::Location contLocation = containment->location();

            if (onPrimary && contLocation == location) {
                return true;
            }
        }
    }

    return false;
}

bool GenericLayout::mapContainsId(const Layout::ViewsMap *map, uint viewId) const
{
    for(const auto &scr : map->keys()) {
        for(const auto &edge : (*map)[scr].keys()) {
            if ((*map)[scr][edge].contains(viewId)) {
                return true;
            }
        }
    }

    return false;
}

QString GenericLayout::mapScreenName(const ViewsMap *map, uint viewId) const
{
    for(const auto &scr : map->keys()) {
        for(const auto &edge : (*map)[scr].keys()) {
            if ((*map)[scr][edge].contains(viewId)) {
                return scr;
            }
        }
    }

    return QString::number(Latte::ScreenPool::NOSCREENID);
}

//! screen name, location, containmentId
Layout::ViewsMap GenericLayout::validViewsMap()
{
    Layout::ViewsMap map;

    if (!m_corona) {
        return map;
    }

    QString prmScreenName = m_corona->screenPool()->primaryScreen()->name();

    for (const auto containment : m_containments) {
        if (Layouts::Storage::self()->isLatteContainment(containment)
                && !Layouts::Storage::self()->isScreenGroupDerivedView(containment)) {
            const QString containmentId = QString::number(containment->id());
            //! Output removal can make Qt temporarily report the surviving
            //! primary QScreen from a window that still targets the removed
            //! connector. Runtime View::data() therefore cannot own placement:
            //! using it here remapped an explicit linked member to primary and
            //! kept a stale surface alive. Persisted placement is authoritative,
            //! with the explicit pending transaction taking precedence until
            //! Plasma commits the containment screen change.
            Data::View view = m_pendingContainmentUpdates.containsId(containmentId)
                ? m_pendingContainmentUpdates[containmentId]
                : Latte::Layouts::Storage::self()->view(containment->config());
            view.screen = Layouts::Storage::self()->expectedViewScreenId(m_corona, view);

            if (view.onPrimary) {
                map[prmScreenName][view.edge] << containment->id();
            } else {
                QString expScreenName = m_corona->screenPool()->connector(view.screen);

                if (m_corona->screenPool()->isScreenActive(view.screen)) {
                    map[expScreenName][view.edge] << containment->id();
                }
            }
        }
    }

    return map;
}


//! the central functions that updates loading/unloading latteviews
//! concerning screen changed (for multi-screen setups mainly)
void GenericLayout::syncLatteViewsToScreens()
{
    if (!m_corona) {
        return;
    }

    qDebug() << "START of SyncLatteViewsToScreens ....";
    qDebug() << "LAYOUT ::: " << name();
    qDebug() << "screen count changed -+-+ " << qGuiApp->screens().size();

    //! Clear up pendingContainmentUpdates when no-needed any more
    QStringList clearpendings;
    for(int i=0; i<m_pendingContainmentUpdates.rowCount(); ++i) {
        auto viewdata = m_pendingContainmentUpdates[i];
        auto containment = containmentForId(viewdata.id.toUInt());

        if (containment) {
            if ((viewdata.onPrimary && containment->lastScreen() == m_corona->screenPool()->primaryScreenId())
                    || (!viewdata.onPrimary && containment->lastScreen() == viewdata.screen)) {
                clearpendings << viewdata.id;
            }
        }
    }

    for(auto pendingid : clearpendings) {
        m_pendingContainmentUpdates.remove(pendingid);
    }

    if (m_pendingContainmentUpdates.rowCount() > 0) {
        qDebug () << "  Pending View updates still valid : ";
        m_pendingContainmentUpdates.print();
    }

    //! use valid views map based on active screens
    Layout::ViewsMap viewsMap = validViewsMap();

    QString prmScreenName = m_corona->screenPool()->primaryScreen()->name();

    qDebug() << "PRIMARY SCREEN :: " << prmScreenName;
    qDebug() << "LATTEVIEWS MAP :: " << viewsMap;

    //! Add roots before explicit members. A member whose output reconnects in
    //! the same screen event cannot construct until its direct root exists.
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto containment : m_containments) {
            if (hasLatteView(containment) || !mapContainsId(&viewsMap, containment->id())) {
                continue;
            }

            const bool isLinkedMember = Layouts::Storage::self()->isClonedView(containment);
            if ((pass == 0 && isLinkedMember) || (pass == 1 && !isLinkedMember)) {
                continue;
            }

            qDebug() << "syncLatteViewsToScreens: view must be added... for containment:" << containment->id() << "at screen:" << mapScreenName(&viewsMap, containment->id());
            addView(containment);
        }
    }

    //! remove views
    QSet<OriginalView *> rootsToUnload;
    for (auto *const view : std::as_const(m_latteViews)) {
        if (view && view->isOriginal() && view->containment()
                && !mapContainsId(&viewsMap, view->containment()->id())) {
            if (auto *const root = qobject_cast<OriginalView *>(view)) {
                rootsToUnload.insert(root);
            }
        }
    }

    //! An explicit member cannot outlive its runtime root. Derived members are
    //! disposable screen-group projections and are retired here so the root
    //! can regenerate them when its output returns. Explicit member
    //! containments remain persistent and are only parked below.
    for (auto *const root : std::as_const(rootsToUnload)) {
        root->retireScreenGroupDerivedClonesForRuntimeUnload();
    }

    QSet<Plasma::Containment *> viewsToDelete;

    for (auto *const view : std::as_const(m_latteViews)) {
        auto containment = view->containment();
        if (containment && view->ownsOutputPlacement()
                && !mapContainsId(&viewsMap, containment->id())) {
            viewsToDelete.insert(containment);
        }

        if (containment && view->linkPlacement() == Data::View::LinkPlacement::ExplicitTarget
                && rootsToUnload.contains(qobject_cast<OriginalView *>(view->relationshipRootView()))) {
            viewsToDelete.insert(containment);
        }
    }

    for (auto *const containment : std::as_const(viewsToDelete)) {
        auto view = m_latteViews.take(containment);
        qDebug() << "syncLatteViewsToScreens: view must be deleted... for containment:" << containment->id() << " at screen:" << view->positioner()->currentScreenName();
        view->disconnectSensitiveSignals();
        view->deleteLater();
    }

    //! reconsider views
    for (const auto view : m_latteViews) {
        if (view->containment() && view->ownsOutputPlacement()
                && mapContainsId(&viewsMap, view->containment()->id())) {
            //! if the dock will not be deleted its a very good point to reconsider
            //! if the screen in which is running is the correct one
            qDebug() << "syncLatteViewsToScreens: view must consider its screen... for containment:" << view->containment()->id() << " at screen:" << view->positioner()->currentScreenName();
            view->reconsiderScreen();
        }
    }

    qDebug() << "end of, syncLatteViewsToScreens ....";
}

QList<Plasma::Containment *> GenericLayout::subContainmentsOf(uint id) const
{
    QList<Plasma::Containment *> subs;

    auto containment = containmentForId(id);

    if (!containment || !Layouts::Storage::self()->isLatteContainment(containment)) {
        return subs;
    }

    auto applets = containment->config().group("Applets");

    for (const auto &applet : applets.groupList()) {
        int tSubId = Layouts::Storage::self()->subContainmentId(applets.group(applet));

        if (Layouts::Storage::isValid(tSubId)) {
            auto subcontainment = containmentForId(tSubId);

            if (subcontainment) {
                subs << subcontainment;
            }
        }
    }

    return subs;
}

QList<int> GenericLayout::subContainmentsOf(Plasma::Containment *containment) const
{
    QList<int> subs;

    if (Layouts::Storage::self()->isLatteContainment(containment)) {
        auto applets = containment->config().group("Applets");

        for (const auto &applet : applets.groupList()) {
            int tSubId = Layouts::Storage::self()->subContainmentId(applets.group(applet));

            if (Layouts::Storage::isValid(tSubId)) {
                subs << tSubId;
            }
        }
    }

    return subs;
}

QList<int> GenericLayout::viewsExplicitScreens()
{
    Data::ViewsTable views = viewsTable();
    QList<int> screens;

    for (int i=0; i<views.rowCount(); ++i) {
        if (!views[i].onPrimary && !screens.contains(views[i].screen)) {
            screens << views[i].screen;
        }
    }

    return screens;
}

//! STORAGE

bool GenericLayout::isWritable() const
{
    return Layouts::Storage::self()->isWritable(this);
}

void GenericLayout::lock()
{
    Layouts::Storage::self()->lock(this);
}

void GenericLayout::unlock()
{
    Layouts::Storage::self()->unlock(this);
}

void GenericLayout::syncToLayoutFile(bool removeLayoutId)
{
    syncSettings();
    if (!Layouts::Storage::self()
            ->syncToLayoutFile(
                this,
                removeLayoutId)) {
        qCritical() << "layout:" << name()
                    << "could not synchronize its layout file";
    }
}

bool GenericLayout::newView(const QString &templateName)
{
    if (!isActive() || !m_corona->templatesManager()->hasViewTemplate(templateName)) {
        return false;
    }

    QString templatefilepath = m_corona->templatesManager()->viewTemplateFilePath(templateName);
    Data::ViewsTable templateviews = Layouts::Storage::self()->views(templatefilepath);

    if (templateviews.rowCount() <= 0) {
        return false;
    }

    Data::View nextdata = templateviews[0];
    int scrId = m_corona->screenPool()->primaryScreenId();

    QList<Plasma::Types::Location> freeedges = freeEdges(scrId);

    if (!freeedges.contains(nextdata.edge)) {
        nextdata.edge = (freeedges.count() > 0 ? freeedges[0] : Plasma::Types::BottomEdge);
    }

    nextdata.setState(Data::View::OriginFromViewTemplate, templatefilepath);

    newView(nextdata);

    return true;
}

Data::View GenericLayout::newView(const Latte::Data::View &nextViewData)
{
    if (nextViewData.state() == Data::View::IsInvalid) {
        return Data::View();
    }

    Data::View result = Layouts::Storage::self()->newView(this, nextViewData);
    Q_EMIT viewEdgeChanged();

    return result;
}

void GenericLayout::updateView(const Latte::Data::View &viewData)
{
    //! storage -> storage [view scenario]
    if (!isActive()) {
        Layouts::Storage::self()->updateView(this, viewData);
        return;
    }

    //! active -> active [view scenario]
    Latte::View *view = viewForContainment(viewData.id.toUInt());
    bool viewMustBeDeleted = (view && !viewData.onPrimary && !m_corona->screenPool()->isScreenActive(viewData.screen));

    QString nextactivelayoutname = (viewData.state() == Data::View::OriginFromLayout && !viewData.originLayout().isEmpty() ? viewData.originLayout() : QString());

    if (view) {
        if (!viewMustBeDeleted) {
            QString scrName = Latte::Data::Screen::ONPRIMARYNAME;

            if (!viewData.onPrimary) {
                if (m_corona->screenPool()->hasScreenId(viewData.screen)) {
                    scrName = m_corona->screenPool()->connector(viewData.screen);
                } else {
                    scrName = "";
                }
            }

            view->setName(viewData.name);
            view->positioner()->setNextLocation(nextactivelayoutname, viewData.screensGroup, scrName, viewData.edge, viewData.alignment);
            return;
        } else {
            //! viewMustBeDeleted
            m_latteViews.remove(view->containment());
            view->disconnectSensitiveSignals();
            delete view;
        }
    }

    //! inactiveinmemory -> active/inactiveinmemory [viewscenario]
    //! active -> inactiveinmemory                  [viewscenario]
    auto containment = containmentForId(viewData.id.toUInt());
    if (containment) {
        Layouts::Storage::self()->updateView(this, viewData);

        //! by using pendingContainmentUpdates we make sure that when containment->screen() will be
        //! called though reactToScreenChange() the proper screen will be returned
        if (!m_pendingContainmentUpdates.containsId(viewData.id)) {
            m_pendingContainmentUpdates << viewData;
        } else {
            m_pendingContainmentUpdates[viewData.id] = viewData;
        }
        containment->reactToScreenChange();
    }

    if (!nextactivelayoutname.isEmpty()) {
        if (!m_corona->layoutsManager()->moveView(
                name(), viewData.id.toUInt(), nextactivelayoutname)) {
            qCritical() << "GenericLayout: failed to commit move for containment"
                        << viewData.id << "to" << nextactivelayoutname;
        }
    }

    //! complete update circle and inform the others about the changes
    if (viewMustBeDeleted) {
        Q_EMIT viewEdgeChanged();
        Q_EMIT viewsCountChanged();
    }

    syncLatteViewsToScreens();
}

void GenericLayout::removeView(const Latte::Data::View &viewData)
{
    if (!containsView(viewData.id.toInt())) {
        return;
    }

    const Data::ViewsTable currentViews = viewsTable();
    if (currentViews.hasExplicitLinkedMembers(viewData.id)) {
        qCritical() << "layout:" << name() << "refused removal of linked root"
                    << viewData.id
                    << "because member removal is not one reversible Plasma transaction; remove linked members first";
        return;
    }

    if (!isActive()) {
        Layouts::Storage::self()->removeView(file(), viewData);
        return;
    }

    Plasma::Containment *viewcontainment = containmentForId(viewData.id.toUInt());
    if (!prepareViewRemoval(viewcontainment)) {
        return;
    }
    const auto memoryUsage =
        m_corona->layoutsManager()->memoryUsage();
    const QString removalSnapshot =
        m_removalSnapshots.value(
            viewcontainment);
    const KSharedConfigPtr activeConfig =
        memoryUsage == MemoryUsage::SingleLayout
        ? activeSingleLayoutConfig()
        : KSharedConfigPtr{};

    //! Direct removal has no Undo phase. Suppress the reversible transition
    //! callback, finish libplasma's root and child destruction, then commit
    //! the captured snapshot tombstone after layout ownership disappears.
    m_finalizingRemovalContainments.insert(
        viewcontainment);
    destroyContainment(viewcontainment);
    m_finalizingRemovalContainments.remove(
        viewcontainment);
    if (m_containments.contains(viewcontainment)
            || m_latteViews.contains(viewcontainment)
            || m_waitingLatteViews.contains(
                viewcontainment)) {
        qFatal(
            "layout %s could not retire runtime ownership for containment %s; persistent removal was not committed",
            qPrintable(name()),
            qPrintable(viewData.id));
    }
    if (!commitRemovalPersistenceAfterDestruction(
            memoryUsage,
            activeConfig,
            removalSnapshot)) {
        qFatal(
            "layout %s destroyed containment %s but could not commit its permanent persistence tombstone",
            qPrintable(name()),
            qPrintable(viewData.id));
    }
}

void GenericLayout::removeOrphanedSubContainment(const int &containmentId)
{
    Data::ViewsTable views = viewsTable();
    QString cidstr = QString::number(containmentId);

    if (views.hasContainmentId(cidstr)) {
        return;
    }

    if (!isActive()) {
        Layouts::Storage::self()->removeContainment(file(), cidstr);
        return;
    }

    Plasma::Containment *orphanedcontainment = containmentForId(cidstr.toUInt());
    destroyContainment(orphanedcontainment);
}

void GenericLayout::destroyContainment(Plasma::Containment *containment)
{
    if (!containment || !m_corona || !contains(containment)) {
        return;
    }

    //! breadcrumb for the importLayoutFile dangling-containment race: if a
    //! crash or the import guard fires right after this line, this was the
    //! deleter
    qDebug() << "layout:" << name() << "destroying containment:" << containment->id();

    containment->setImmutability(Plasma::Types::Mutable);
    containment->destroy();
}

QString GenericLayout::storedView(const int &containmentId)
{
    return Layouts::Storage::self()->storedView(this, containmentId);
}

void GenericLayout::importToCorona()
{
    Layouts::Storage::self()->importToCorona(this);
}

Data::ErrorsList GenericLayout::errors() const
{
    return Layouts::Storage::self()->errors(this);
}

Data::WarningsList GenericLayout::warnings() const
{
    return Layouts::Storage::self()->warnings(this);
}

Latte::Data::ViewsTable GenericLayout::viewsTable() const
{
    return Layouts::Storage::self()->views(this);
}

}
}
