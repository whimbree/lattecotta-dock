/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "launcherlistbridge.h"

// Qt
#include <QDebug>
#include <QVariantMap>

namespace Latte {
namespace Tasks {

LauncherListOps::LauncherListOps(QObject *parent)
    : QObject(parent)
{
}

QStringList LauncherListOps::launchersForActivity(const QStringList &storedRecords,
                                                  const QString &currentActivity) const
{
    const LauncherLists::ParsedStoredList parsed = LauncherLists::parseStoredRecords(storedRecords);

    for (const QString &record : parsed.malformed) {
        // Qt5 mis-read these silently (a tag without its newline showed a
        // garbage launcher, a url containing '[' vanished); refusing them
        // loudly keeps the stored list the single source of truth
        qWarning() << "Launchers: refusing malformed stored launcher record:" << record;
    }

    return LauncherLists::selectLaunchersForActivity(parsed.records, currentActivity);
}

QStringList LauncherListOps::separatorCandidates() const
{
    return LauncherLists::separatorCandidates();
}

QString LauncherListOps::freeSeparatorName(const QStringList &takenNames) const
{
    const std::optional<QString> name = LauncherLists::freeSeparatorName(takenNames);

    if (!name) {
        // Qt5 returned "" without a word and the add silently no-oped;
        // the cap is real (19 separators), hitting it deserves a report
        qWarning() << "Launchers: all" << LauncherLists::MAXSEPARATORS
                   << "internal separator names are taken, refusing to add another separator";
        return QString();
    }

    return *name;
}

bool LauncherListOps::isSeparatorName(const QString &launcherUrl) const
{
    return LauncherLists::isSeparatorName(launcherUrl);
}

QVariant LauncherListOps::planMoves(const QStringList &current, const QStringList &goal) const
{
    const std::optional<QVector<LauncherLists::Move>> plan = LauncherLists::planMoves(current, goal);

    if (!plan) {
        // a membership gap is normal settling (the model is churning and
        // the caller retries); duplicates are a model anomaly and get said
        const auto hasDuplicates = [](QStringList list) {
            std::sort(list.begin(), list.end());
            return std::adjacent_find(list.cbegin(), list.cend()) != list.cend();
        };

        if (hasDuplicates(current) || hasDuplicates(goal)) {
            qWarning() << "Launchers: duplicate launcher urls in the order lists, no move plan -"
                       << "current:" << current << "goal:" << goal;
        }

        return QVariant();
    }

    QVariantList moves;
    moves.reserve(plan->size());

    for (const LauncherLists::Move &move : *plan) {
        moves.append(QVariantMap{{QStringLiteral("from"), move.from},
                                 {QStringLiteral("to"), move.to}});
    }

    return moves;
}

TasksExtendedRegistries::TasksExtendedRegistries(QObject *parent)
    : QObject(parent)
{
}

//! An empty launcher url in a registry is either an inert ghost (substring
//! registries: Qt5's equals() never matched it, but its add still latched
//! the paused-state counts until the 30s GC - a wedge, not a behavior) or
//! a meaningless key (exact registries). Refused loudly at the boundary;
//! deviation from Qt5's silent ghost-push, recorded in the EX-11 spec.
bool TasksExtendedRegistries::refuseEmptyUrl(const QString &launcherUrl, const char *registry) const
{
    if (!launcherUrl.isEmpty()) {
        return false;
    }

    qWarning() << "TasksExtendedRegistries: refusing an empty launcher url for" << registry;
    return true;
}

bool TasksExtendedRegistries::addWaiting(const QString &launcherUrl)
{
    if (refuseEmptyUrl(launcherUrl, "waiting launchers")) {
        return false;
    }

    return m_waiting.add(launcherUrl);
}

bool TasksExtendedRegistries::removeWaiting(const QString &launcherUrl)
{
    return m_waiting.remove(launcherUrl);
}

bool TasksExtendedRegistries::waitingExists(const QString &launcherUrl) const
{
    return m_waiting.contains(launcherUrl);
}

int TasksExtendedRegistries::waitingCount() const
{
    return m_waiting.count();
}

QStringList TasksExtendedRegistries::waitingItems() const
{
    return m_waiting.items();
}

bool TasksExtendedRegistries::addImmediate(const QString &launcherUrl)
{
    if (refuseEmptyUrl(launcherUrl, "immediate launchers")) {
        return false;
    }

    return m_immediate.add(launcherUrl);
}

bool TasksExtendedRegistries::removeImmediate(const QString &launcherUrl)
{
    return m_immediate.remove(launcherUrl);
}

bool TasksExtendedRegistries::immediateExists(const QString &launcherUrl) const
{
    return m_immediate.contains(launcherUrl);
}

QStringList TasksExtendedRegistries::immediateItems() const
{
    return m_immediate.items();
}

bool TasksExtendedRegistries::addToBeAdded(const QString &launcherUrl)
{
    if (refuseEmptyUrl(launcherUrl, "to-be-added launchers")) {
        return false;
    }

    return m_toBeAdded.add(launcherUrl);
}

bool TasksExtendedRegistries::removeToBeAdded(const QString &launcherUrl)
{
    return m_toBeAdded.remove(launcherUrl);
}

bool TasksExtendedRegistries::toBeAddedExists(const QString &launcherUrl) const
{
    return m_toBeAdded.contains(launcherUrl);
}

QStringList TasksExtendedRegistries::toBeAddedItems() const
{
    return m_toBeAdded.items();
}

bool TasksExtendedRegistries::addToBeRemoved(const QString &launcherUrl)
{
    if (refuseEmptyUrl(launcherUrl, "to-be-removed launchers")) {
        return false;
    }

    return m_toBeRemoved.add(launcherUrl);
}

bool TasksExtendedRegistries::removeToBeRemoved(const QString &launcherUrl)
{
    return m_toBeRemoved.remove(launcherUrl);
}

bool TasksExtendedRegistries::toBeRemovedExists(const QString &launcherUrl) const
{
    return m_toBeRemoved.contains(launcherUrl);
}

QStringList TasksExtendedRegistries::toBeRemovedItems() const
{
    return m_toBeRemoved.items();
}

bool TasksExtendedRegistries::addMoveIntent(const QString &launcherUrl, int position)
{
    if (refuseEmptyUrl(launcherUrl, "move intents")) {
        return false;
    }

    if (position < 0) {
        // Qt5 clamped silently (Math.max(0,toPos)); the only live emitter
        // already clamps before signalling, so this path is a caller bug
        // worth hearing about - the clamp keeps the Qt5 outcome
        qWarning() << "TasksExtendedRegistries: negative move-intent position" << position
                   << "for" << launcherUrl << "- clamping to 0";
        position = 0;
    }

    return m_moveIntents.add(launcherUrl, position);
}

bool TasksExtendedRegistries::moveIntentExists(const QString &launcherUrl) const
{
    return m_moveIntents.contains(launcherUrl);
}

QVariant TasksExtendedRegistries::takeMoveIntentPosition(const QString &launcherUrl)
{
    const std::optional<int> position = m_moveIntents.positionOf(launcherUrl);

    if (!position) {
        return QVariant();
    }

    m_moveIntents.remove(launcherUrl);
    return *position;
}

QVariantList TasksExtendedRegistries::moveIntents() const
{
    QVariantList intents;

    const QVector<LauncherLists::MoveIntentSet::Intent> items = m_moveIntents.items();
    intents.reserve(items.size());

    for (const LauncherLists::MoveIntentSet::Intent &intent : items) {
        intents.append(QVariantMap{{QStringLiteral("launcher"), intent.launcher},
                                   {QStringLiteral("pos"), intent.position}});
    }

    return intents;
}

void TasksExtendedRegistries::setFrozenZoom(const QString &taskId, qreal zoom)
{
    if (refuseEmptyUrl(taskId, "frozen zooms")) {
        return;
    }

    m_frozenZooms.setZoom(taskId, zoom);
}

QVariant TasksExtendedRegistries::frozenZoom(const QString &taskId) const
{
    const std::optional<qreal> zoom = m_frozenZooms.zoomOf(taskId);

    if (!zoom) {
        return QVariant();
    }

    return *zoom;
}

bool TasksExtendedRegistries::removeFrozenZoom(const QString &taskId)
{
    return m_frozenZooms.remove(taskId);
}

QVariantList TasksExtendedRegistries::frozenEntries() const
{
    QVariantList entries;

    const QVector<LauncherLists::FrozenZoomSet::Entry> items = m_frozenZooms.items();
    entries.reserve(items.size());

    for (const LauncherLists::FrozenZoomSet::Entry &entry : items) {
        entries.append(QVariantMap{{QStringLiteral("id"), entry.id},
                                   {QStringLiteral("zoom"), entry.zoom}});
    }

    return entries;
}

void TasksExtendedRegistries::clearAll()
{
    m_waiting.clear();
    m_immediate.clear();
    m_toBeAdded.clear();
    m_toBeRemoved.clear();
    m_moveIntents.clear();
    m_frozenZooms.clear();
}

}
}
