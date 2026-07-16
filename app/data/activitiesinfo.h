/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef ACTIVITIESINFO_H
#define ACTIVITIESINFO_H

// local
#include "activitydata.h"

// Qt
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace Latte {

//! KActivities 6 removed Consumer::runningActivities() and Info::State, which
//! Latte needs to assign layouts per activity and to cycle only between live
//! activities. Read the states straight from the activity manager instead
//! (org.kde.ActivityManager.Activities, ListActivitiesWithInformation).
namespace ActivitiesInfo {

//! One activity record, kept in the manager's order. Cycling next/previous
//! relies on a stable order, so ordered lists must not be derived from an
//! unordered map.
struct Record {
    QString id;
    Data::Activity::State state{Data::Activity::Invalid};
};

//! Live, ordered query of every activity and its state.
QVector<Record> list();

//! Live running activity ids in manager order.
QStringList runningActivities();

//! Live id -> state map for per-activity lookups.
QHash<QString, Data::Activity::State> states();

//! Running ids from records, order preserved. Pure, so the running/stopped
//! distinction is testable without a live activity manager.
QStringList runningActivitiesFrom(const QVector<Record> &records);

//! Validate an org.kde.ActivityManager state integer into
//! Data::Activity::State. The enum mirrors the wire values, but unknown or
//! future values must still collapse to Invalid instead of being cast across.
Data::Activity::State stateFromManager(int managerState);

}
}

#endif
