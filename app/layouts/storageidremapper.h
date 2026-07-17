/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, derived)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef STORAGEIDREMAPPER_H
#define STORAGEIDREMAPPER_H

// Qt
#include <QHash>
#include <QString>
#include <QStringList>
#include <QtGlobal>

namespace Latte {
namespace Layouts {

//! The id-assignment math of layout import/duplicate (EX-07 in
//! docs/QML_EXTRACTION_PLAN.md), lifted out of Storage::newUniqueIdsFile's
//! KConfig surgery so it is unit-testable without files or a corona.
//! Ported from the Qt5-inherited body (f0ad7b23 storage.cpp, verified
//! identical to ours before extraction) with capt's interface shape
//! (latte-dock-qt6 73f64383) as the starting point.
//!
//! Id domain facts the reader needs: ids are KConfig GROUP NAMES, so they
//! are strings of decimal integers; containments allocate from 12 up,
//! applets from 40 up, and an id survives the import unchanged only when
//! it is at or above its base and free in both the destination and the
//! running assignment. The trailing "PROBLEM APPEARED" pass undoes
//! 2-cycles the greedy walk can create (a<->b swapped assignments would
//! make both point at each other's old group).
namespace StorageIdRemapper {

constexpr int containmentIdBase = 12;
constexpr int appletIdBase = 40;
//! exhaustion cap, Qt5-inherited: ids at or above this are never assigned
constexpr int maxId = 32000;

struct IdRemapInput {
    //! ids already taken in the destination (corona containment+applet
    //! ids, or file-derived for corona-less layouts)
    QStringList usedIds;
    //! the origin file's containment ids, in groupList() order
    QStringList containmentIds;
    //! the origin file's applet ids, in order
    QStringList appletIds;
};

struct IdRemap {
    //! old id -> new id, for every input containment and applet id
    QHash<QString, QString> assigned;
    //! true when the id space ran out and at least one assignment is
    //! empty; the caller must fail loudly instead of writing "" group
    //! names (the step-2.5 loud-exhaustion divergence from capt/Qt5)
    bool exhausted = false;

    //! passthrough for unknown keys (an id the remap never saw maps to
    //! itself). Order-list rewriting deliberately does NOT use this:
    //! unknown order tokens map to empty there, preserving the shipped
    //! behavior (assigned.value(token) with a default-constructed value).
    QString mapped(const QString &oldId) const
    {
        return assigned.value(oldId, oldId);
    }
};

//! exact port of Storage::availableId: the lowest i >= base (i < maxId)
//! whose decimal string is in NEITHER all NOR assigned; "" on exhaustion
inline QString availableId(const QStringList &all, const QStringList &assigned, int base)
{
    Q_ASSERT(base >= 0);

    for (int i = base; i < maxId; ++i) {
        const QString iStr = QString::number(i);

        if (!all.contains(iStr) && !assigned.contains(iStr)) {
            return iStr;
        }
    }

    return QString("");
}

//! one allocation pass: keep the old id when it is at or above base and
//! free everywhere, otherwise take the first free id at or above base
inline void assignIds(const QStringList &oldIds, int base,
                      const QStringList &usedIds, QStringList &assignedIds,
                      IdRemap &remap)
{
    for (const auto &oldId : oldIds) {
        QString newId;

        if (oldId.toInt() >= base && !usedIds.contains(oldId) && !assignedIds.contains(oldId)) {
            newId = oldId;
        } else {
            newId = availableId(usedIds, assignedIds, base);
        }

        if (newId.isEmpty()) {
            remap.exhausted = true;
        }

        assignedIds << newId;
        remap.assigned[oldId] = newId;
    }
}

//! the "PROBLEM APPEARED" 2-cycle fix, Qt5-inherited: when a and b were
//! assigned each other's old ids, both revert to their own id (the walk
//! order guarantees both are free by construction of the collision)
inline void undoTwoCycles(const QStringList &oldIds, IdRemap &remap)
{
    for (const auto &oldId : oldIds) {
        const QString value = remap.assigned.value(oldId);

        if (remap.assigned.contains(value)) {
            const QString value2 = remap.assigned.value(value);

            if (oldId != remap.assigned.value(oldId) && !value2.isEmpty() && oldId == value2) {
                remap.assigned[oldId] = oldId;
                remap.assigned[value] = value;
            }
        }
    }
}

//! the full assignment: containments at containmentIdBase, then applets
//! at appletIdBase, sharing ONE running assigned-set so the ranges never
//! collide; then the 2-cycle fix over both id families
inline IdRemap remap(const IdRemapInput &input)
{
    IdRemap result;
    QStringList assignedIds;

    assignIds(input.containmentIds, containmentIdBase, input.usedIds, assignedIds, result);
    assignIds(input.appletIds, appletIdBase, input.usedIds, assignedIds, result);

    undoTwoCycles(input.containmentIds, result);
    undoTwoCycles(input.appletIds, result);

    return result;
}

}
}
}

#endif
