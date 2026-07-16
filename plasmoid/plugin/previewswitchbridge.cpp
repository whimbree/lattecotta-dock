/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "previewswitchbridge.h"

// Qt
#include <QDebug>

namespace Latte {
namespace Tasks {

PreviewSwitchBridge::PreviewSwitchBridge(QObject *parent)
    : QObject(parent)
{
    //! monotonic clock instead of the QML predecessor's Date.now(): the
    //! burst gap must never read negative or jump across a wall-clock
    //! adjustment; the engine only ever compares differences
    m_clock.start();
}

int PreviewSwitchBridge::switchBurstThreshold() const
{
    return PreviewSwitchEngine::switchBurstThresholdMs;
}

int PreviewSwitchBridge::settleInterval() const
{
    return PreviewSwitchEngine::settleIntervalMs;
}

int PreviewSwitchBridge::hideCountdown() const
{
    return PreviewSwitchEngine::hideCountdownMs;
}

int PreviewSwitchBridge::idFor(QObject *task)
{
    const auto it = m_idByTask.constFind(task);
    if (it != m_idByTask.constEnd()) {
        return it.value();
    }

    const int id = m_nextId++;
    m_idByTask.insert(task, id);
    m_taskById.insert(id, task);
    return id;
}

bool PreviewSwitchBridge::shouldDeferSwitch(QObject *task, bool dialogVisible)
{
    if (!task) {
        qWarning() << "PreviewSwitchBridge: shouldDeferSwitch called with a null task";
        return false;
    }

    const auto result = m_engine.decide({idFor(task), m_clock.elapsed(), dialogVisible});
    return result.decision == PreviewSwitchEngine::SwitchDecision::Defer;
}

QObject *PreviewSwitchBridge::pendingTask() const
{
    const auto id = m_engine.pendingTask();
    return id ? m_taskById.value(*id, nullptr) : nullptr;
}

QObject *PreviewSwitchBridge::settlePending(bool pendingStillHovered, bool dialogVisible)
{
    const auto id = m_engine.settle(pendingStillHovered, dialogVisible);
    //! a pending task that died before the settle fired has no map entry
    //! anymore (dropTask erased it); returning null makes the timer adopt
    //! nothing, which is the correct outcome for that race
    return id ? m_taskById.value(*id, nullptr) : nullptr;
}

void PreviewSwitchBridge::shown(QObject *task)
{
    if (!task) {
        qWarning() << "PreviewSwitchBridge: shown called with a null task";
        return;
    }

    m_engine.shown(idFor(task));
}

void PreviewSwitchBridge::dialogHidden()
{
    m_engine.dialogHidden();
}

bool PreviewSwitchBridge::shouldArmHideCountdown(bool dialogVisible, bool dialogContainsMouse) const
{
    PreviewSwitchEngine::HoverSnapshot snap;
    snap.dialogVisible = dialogVisible;
    snap.dialogContainsMouse = dialogContainsMouse;
    return m_engine.hoverChanged(snap) == PreviewSwitchEngine::HideDecision::StartCountdown;
}

bool PreviewSwitchBridge::shouldHideNow(bool dialogContainsMouse, bool activeTaskContainsMouse,
                                        bool externalDragOnActiveTask) const
{
    PreviewSwitchEngine::HoverSnapshot snap;
    snap.dialogVisible = true;
    snap.dialogContainsMouse = dialogContainsMouse;
    snap.activeTaskContainsMouse = activeTaskContainsMouse;
    snap.externalDragOnActiveTask = externalDragOnActiveTask;
    return m_engine.shouldHideNow(snap);
}

QVariantMap PreviewSwitchBridge::materialize(QObject *task)
{
    QVariantMap verdict;

    if (!task) {
        qWarning() << "PreviewSwitchBridge: materialize called with a null task";
        verdict.insert(QStringLiteral("changed"), false);
        verdict.insert(QStringLiteral("fresh"), false);
        verdict.insert(QStringLiteral("revive"), false);
        verdict.insert(QStringLiteral("evict"), false);
        return verdict;
    }

    const auto result = m_engine.materialize(idFor(task));
    verdict.insert(QStringLiteral("changed"), result.kind != PreviewSwitchEngine::Materialize::KeepActive);
    verdict.insert(QStringLiteral("fresh"), result.fresh);
    verdict.insert(QStringLiteral("revive"), result.kind == PreviewSwitchEngine::Materialize::Revive);
    verdict.insert(QStringLiteral("evict"), result.evictedTask.has_value());
    return verdict;
}

void PreviewSwitchBridge::dropTask(QObject *task)
{
    const auto it = m_idByTask.constFind(task);
    if (it == m_idByTask.constEnd()) {
        return;
    }

    m_engine.drop(it.value());
    m_taskById.remove(it.value());
    m_idByTask.erase(it);
}

}
}
