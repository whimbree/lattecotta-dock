/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later

    Latte-authored, NOT part of the plasma-desktop vendored set in this
    directory (see plasmoid/plugin/units/README.md).
*/

#ifndef LAUNCHERLISTBRIDGE_H
#define LAUNCHERLISTBRIDGE_H

// local
#include "units/launcherlistops.h"

// Qt
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>

namespace Latte {
namespace Tasks {

//! Thin QML shells over the LauncherLists core (EX-11). The core decides
//! (units/launcherlistops.h); these wrappers convert QML value traffic and
//! refuse malformed input loudly at the boundary.

//! Stateless ops: the LatteTasks.LauncherListOps singleton the launchers
//! ability and its order validator call.
class LauncherListOps : public QObject
{
    Q_OBJECT

public:
    explicit LauncherListOps(QObject *parent = nullptr);

    //! parses the stored records (loudly refusing malformed ones) and
    //! returns the launcher urls a dock on currentActivity shows, in
    //! stored order
    Q_INVOKABLE QStringList launchersForActivity(const QStringList &storedRecords,
                                                 const QString &currentActivity) const;

    //! the 19 internal separator names in allocation order
    Q_INVOKABLE QStringList separatorCandidates() const;

    //! first candidate not in takenNames; "" plus a loud report when all
    //! 19 are taken (the Qt5 "" contract, kept for the QML callers)
    Q_INVOKABLE QString freeSeparatorName(const QStringList &takenNames) const;

    Q_INVOKABLE bool isSeparatorName(const QString &launcherUrl) const;

    //! ordered {from,to} moves turning current into goal; undefined when
    //! the lists are not duplicate-free permutations of each other (model
    //! still settling - the caller retries; duplicates additionally get a
    //! loud report, they are a model anomaly, not churn)
    Q_INVOKABLE QVariant planMoves(const QStringList &current, const QStringList &goal) const;
};

//! The TasksExtendedManager registries: one stateful instance per tasks
//! applet, owned by TasksExtendedManager.qml, which keeps the state
//! machine around these (signals, the paused-state count latches, the GC
//! timer) while membership lives here.
class TasksExtendedRegistries : public QObject
{
    Q_OBJECT

public:
    explicit TasksExtendedRegistries(QObject *parent = nullptr);

    // waiting launchers (substring equality - see the core's WHY comment)
    Q_INVOKABLE bool addWaiting(const QString &launcherUrl);
    Q_INVOKABLE bool removeWaiting(const QString &launcherUrl);
    Q_INVOKABLE bool waitingExists(const QString &launcherUrl) const;
    Q_INVOKABLE int waitingCount() const;
    Q_INVOKABLE QStringList waitingItems() const;

    // immediate launchers (exact equality)
    Q_INVOKABLE bool addImmediate(const QString &launcherUrl);
    Q_INVOKABLE bool removeImmediate(const QString &launcherUrl);
    Q_INVOKABLE bool immediateExists(const QString &launcherUrl) const;
    Q_INVOKABLE QStringList immediateItems() const;

    // to-be-added launchers (substring equality)
    Q_INVOKABLE bool addToBeAdded(const QString &launcherUrl);
    Q_INVOKABLE bool removeToBeAdded(const QString &launcherUrl);
    Q_INVOKABLE bool toBeAddedExists(const QString &launcherUrl) const;
    Q_INVOKABLE QStringList toBeAddedItems() const;

    // to-be-removed launchers (exact equality)
    Q_INVOKABLE bool addToBeRemoved(const QString &launcherUrl);
    Q_INVOKABLE bool removeToBeRemoved(const QString &launcherUrl);
    Q_INVOKABLE bool toBeRemovedExists(const QString &launcherUrl) const;
    Q_INVOKABLE QStringList toBeRemovedItems() const;

    // move intents (launcher -> target position)
    Q_INVOKABLE bool addMoveIntent(const QString &launcherUrl, int position);
    Q_INVOKABLE bool moveIntentExists(const QString &launcherUrl) const;
    //! returns the intended position AND consumes the intent (one atomic
    //! step, the get-then-remove pair of Qt5's moveLauncherToCorrectPos);
    //! undefined when no intent exists
    Q_INVOKABLE QVariant takeMoveIntentPosition(const QString &launcherUrl);
    Q_INVOKABLE QVariantList moveIntents() const;

    // frozen parabolic zooms (task id -> zoom, upsert-only)
    Q_INVOKABLE void setFrozenZoom(const QString &taskId, qreal zoom);
    Q_INVOKABLE QVariant frozenZoom(const QString &taskId) const;
    Q_INVOKABLE bool removeFrozenZoom(const QString &taskId);
    Q_INVOKABLE QVariantList frozenEntries() const;

    //! the GC sweep (TasksExtendedManager's 30s garbage collector)
    Q_INVOKABLE void clearAll();

private:
    bool refuseEmptyUrl(const QString &launcherUrl, const char *registry) const;

    LauncherLists::LauncherSet m_waiting{LauncherLists::LauncherSet::Equality::SubstringEitherWay};
    LauncherLists::LauncherSet m_immediate{LauncherLists::LauncherSet::Equality::Exact};
    LauncherLists::LauncherSet m_toBeAdded{LauncherLists::LauncherSet::Equality::SubstringEitherWay};
    LauncherLists::LauncherSet m_toBeRemoved{LauncherLists::LauncherSet::Equality::Exact};
    LauncherLists::MoveIntentSet m_moveIntents;
    LauncherLists::FrozenZoomSet m_frozenZooms;
};

}
}

#endif
