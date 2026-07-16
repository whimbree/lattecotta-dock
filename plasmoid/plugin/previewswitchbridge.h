/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later

    Latte-authored, NOT part of the plasma-desktop vendored set in this
    directory (see plasmoid/plugin/units/README.md).
*/

#ifndef PREVIEWSWITCHBRIDGE_H
#define PREVIEWSWITCHBRIDGE_H

// local
#include "units/previewswitchengine.h"

// Qt
#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QVariantMap>

namespace Latte {
namespace Tasks {

//! Thin QML shell over PreviewSwitchEngine (EX-01): hands out opaque int
//! ids for TaskItem objects, injects the clock, and translates verdicts to
//! QML-friendly shapes. Owns NO timers, NO delegates and NO decisions -
//! the engine decides, the QML applies. The id maps rely on the same
//! destruction contract the delegate cache does (gate rule 8): a dying
//! TaskItem calls dropCachedDelegateFor -> dropTask, which erases its ids;
//! ids are never reused, so a dead task's id cannot alias a new task.
class PreviewSwitchBridge : public QObject
{
    Q_OBJECT
    //! the engine's tested thresholds; QML Timer intervals read these so
    //! the running values cannot drift from the tested ones (gate rule 4's
    //! numeric relation is a static_assert in the engine header)
    Q_PROPERTY(int switchBurstThreshold READ switchBurstThreshold CONSTANT)
    Q_PROPERTY(int settleInterval READ settleInterval CONSTANT)
    Q_PROPERTY(int hideCountdown READ hideCountdown CONSTANT)

public:
    explicit PreviewSwitchBridge(QObject *parent = nullptr);

    int switchBurstThreshold() const;
    int settleInterval() const;
    int hideCountdown() const;

    Q_INVOKABLE bool shouldDeferSwitch(QObject *task, bool dialogVisible);
    Q_INVOKABLE QObject *pendingTask() const;
    Q_INVOKABLE QObject *settlePending(bool pendingStillHovered, bool dialogVisible);
    Q_INVOKABLE void shown(QObject *task);
    Q_INVOKABLE void dialogHidden();

    Q_INVOKABLE bool shouldArmHideCountdown(bool dialogVisible, bool dialogContainsMouse) const;
    Q_INVOKABLE bool shouldHideNow(bool dialogContainsMouse, bool activeTaskContainsMouse,
                                   bool externalDragOnActiveTask) const;

    //! verdict map: changed (false = keep the active delegate), fresh
    //! (create + full binding pass), revive (take the parked delegate),
    //! evict (destroy the OLDEST parked entry - positional, index 0: the
    //! engine's parked list and the QML parkedEntries array append in the
    //! same order, so no object round-trip is needed)
    Q_INVOKABLE QVariantMap materialize(QObject *task);
    Q_INVOKABLE void dropTask(QObject *task);

private:
    int idFor(QObject *task);

    PreviewSwitchEngine m_engine;
    QElapsedTimer m_clock;
    QHash<const QObject *, int> m_idByTask;
    QHash<int, QObject *> m_taskById;
    int m_nextId = 1;
};

}
}

#endif
