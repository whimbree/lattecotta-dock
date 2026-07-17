/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, derived)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef ACTIVITYSETALGEBRA_H
#define ACTIVITYSETALGEBRA_H

// Qt
#include <QString>
#include <QStringList>

namespace Latte {
namespace Layouts {

//! Pure set algebra over activity-id lists, lifted out of Synchronizer so
//! the free/free-running/valid filtering is unit-testable without the live
//! Corona graph (EX-22 in docs/QML_EXTRACTION_PLAN.md). The namespace
//! structure and function signatures are adopted from David Goree's
//! latte-dock-qt6 (app/layouts/activitysetalgebra.h at 941bb7fb,
//! github.com/CaptSilver/latte-dock-qt6); the bodies are re-derived from
//! our Qt5-inherited synchronizer.cpp. Synchronizer keeps gathering the
//! inputs from its live managers and delegates the filtering here; result
//! ORDER is contract in all three functions - switchToLayout takes [0] of
//! the free lists and validActivities feeds ordered activity assignment.
namespace ActivitySetAlgebra {

//! allActivities with every assigned id removed. QStringList::removeAll
//! semantics: every occurrence of an assigned id is dropped, order of the
//! survivors follows allActivities.
inline QStringList freeActivities(const QStringList &allActivities, const QStringList &assignedActivityIds)
{
    QStringList frees = allActivities;

    for (const auto &assigned : assignedActivityIds) {
        frees.removeAll(assigned);
    }

    return frees;
}

//! Running activities that no layout is assigned to, running order preserved.
inline QStringList freeRunningActivities(const QStringList &runningActivities, const QStringList &assignedActivityIds)
{
    QStringList fActivities;

    for (const auto &activity : runningActivities) {
        if (!assignedActivityIds.contains(activity)) {
            fActivities.append(activity);
        }
    }

    return fActivities;
}

//! The subset of a layout's activities that exist in allActivities, layout
//! order preserved. Keeps activity ids recorded by a different machine out
//! of switching decisions without touching the stored list.
inline QStringList validActivities(const QStringList &layoutActivities, const QStringList &allActivities)
{
    QStringList valids;

    for (const auto &activity : layoutActivities) {
        if (allActivities.contains(activity)) {
            valids << activity;
        }
    }

    return valids;
}

}
}
}

#endif
