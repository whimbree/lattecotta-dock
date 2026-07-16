/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PREVIEWSWITCHENGINE_H
#define PREVIEWSWITCHENGINE_H

// Qt
#include <QVector>
#include <QtGlobal>

// C++
#include <optional>

namespace Latte {
namespace Tasks {

//! The window-previews dialog's decision core (EX-01 in
//! docs/QML_EXTRACTION_PLAN.md): switch vs defer vs settle-adopt, the
//! hide-countdown decisions, and the parked-delegate LRU accounting, as one
//! pure state machine. Latte-authored, NOT part of the plasma-desktop
//! vendored set in plasmoid/plugin.
//!
//! Every mechanism here was earned by the 2026-07-15 hover-lag excavation
//! (c6eeeb20..f1edd103 bodies carry the measurements); the QML shell keeps
//! ownership of timers, delegates and bindings and applies the decisions.
//! Time arrives as a parameter (qint64 ms); the engine never reads a clock.
//! Task identity is an opaque int handed out by the shell.
class PreviewSwitchEngine
{
public:
    enum class SwitchDecision { ShowNow, Defer };
    enum class HideDecision { None, StartCountdown, CancelCountdown };
    enum class Materialize { KeepActive, Revive, Build, BuildEvicting };

    //! the tested thresholds. QML reads its Timer intervals from these
    //! (through the bridge) so the running values cannot drift from the
    //! tested ones. A request this soon after the previous one is a sweep
    //! in flight (4b533b8d); the settle interval must stay BELOW the
    //! threshold or a steady sweep whose crossings are slower than the
    //! timer degenerates back to one rebuild stall per icon.
    static constexpr int switchBurstThresholdMs = 350;
    static constexpr int settleIntervalMs = 250;
    static constexpr int hideCountdownMs = 300;
    static_assert(settleIntervalMs < switchBurstThresholdMs,
                  "4b533b8d: settle interval at or above the burst threshold degenerates sweeps");

    struct SwitchRequest {
        //! a real id from the shell's identity map (ids start at 1); "no
        //! task" is not a representable request
        int taskId = 0;
        qint64 nowMs = 0;
        bool dialogVisible = false;
    };

    struct SwitchResult {
        SwitchDecision decision = SwitchDecision::ShowNow;
        //! 54ed1974: a deferred switch never reaches show(), whose first
        //! act cancels the hide countdown every task exit arms; the Defer
        //! decision carries the cancel so a scrub cannot hide the dialog
        //! under the pointer. ShowNow carries no instruction - show()
        //! itself stops the countdown, unchanged.
        bool cancelHideCountdown = false;
        bool armSettleTimer = false;
    };

    //! the composite the hide countdown reads (main.qml
    //! hidePreviewWinTimer): dialog contains-mouse, active task
    //! contains-mouse, external drag hovering the active task
    struct HoverSnapshot {
        bool dialogVisible = false;
        bool dialogContainsMouse = false;
        bool activeTaskContainsMouse = false;
        bool externalDragOnActiveTask = false;
    };

    struct MaterializeResult {
        Materialize kind = Materialize::Build;
        std::optional<int> evictedTask;
        //! true when the shell must create a fresh delegate and run the
        //! full model-binding pass; false reuses live bindings (rootIndex
        //! refresh only, the QML-side contract)
        bool fresh = false;
    };

    explicit PreviewSwitchEngine(int parkedCapacity = 4)
        : m_parkedCapacity(parkedCapacity)
    {
        //! a negative capacity is representable and would evict on every
        //! materialize, silently degrading the cache to nothing
        Q_ASSERT(parkedCapacity >= 0);
    }

    //! mirrors shouldDeferSwitch() (main.qml): requests while the dialog
    //! is hidden, before anything is shown, or for the shown task itself
    //! are immediate; otherwise a request within switchBurstThresholdMs of
    //! the previous one defers to the settle timer.
    SwitchResult decide(const SwitchRequest &req)
    {
        if (!req.dialogVisible || !m_shownTask || *m_shownTask == req.taskId) {
            m_lastSwitchRequestMs = req.nowMs;
            return {SwitchDecision::ShowNow, false, false};
        }

        const bool inBurst = (req.nowMs - m_lastSwitchRequestMs) < switchBurstThresholdMs;
        m_lastSwitchRequestMs = req.nowMs;

        if (inBurst) {
            m_pendingTask = req.taskId;
            return {SwitchDecision::Defer, true, true};
        }

        return {SwitchDecision::ShowNow, false, false};
    }

    //! the settle-timer decision: adopt the pending task only while the
    //! pointer is still on it and the dialog still shows (the defer ->
    //! exit-all -> settle interleaving must not show over nothing,
    //! d56a26aa family). Deliberately does NOT stamp the request clock:
    //! the settle adoption is not a switch request, or it would re-enter
    //! the burst check and re-defer forever (4b533b8d).
    std::optional<int> settle(bool pendingStillHovered, bool dialogVisible)
    {
        if (!m_pendingTask || !pendingStillHovered || !dialogVisible) {
            return std::nullopt;
        }

        const int task = *m_pendingTask;
        m_pendingTask.reset();
        return task;
    }

    std::optional<int> pendingTask() const
    {
        return m_pendingTask;
    }

    //! show() accepted a task: it becomes the shown task and supersedes
    //! any deferred switch still waiting on the settle timer
    void shown(int taskId)
    {
        m_shownTask = taskId;
        m_pendingTask.reset();
    }

    //! the dialog left the screen (forcePreviewsHiding / visible=false).
    //! Clears the SHOWN task and the pending switch - never the cache
    //! association: the active delegate stays warm so re-hovering the same
    //! task revives instead of rebuilding (the QML predecessor keyed the
    //! cache off the UI's nulled activeItem and paid a full rebuild plus a
    //! null-keyed unreachable parked entry on every hide/re-hover cycle).
    void dialogHidden()
    {
        m_shownTask.reset();
        m_pendingTask.reset();
    }

    //! hover state changed: arm or cancel the hide countdown (main.qml
    //! hide() / onContainsMouseChanged). The countdown timer stays QML.
    HideDecision hoverChanged(const HoverSnapshot &s) const
    {
        if (s.dialogContainsMouse) {
            return HideDecision::CancelCountdown;
        }
        if (!s.dialogVisible) {
            return HideDecision::None;
        }
        return HideDecision::StartCountdown;
    }

    //! the countdown expired: hide unless anything still holds the dialog
    bool shouldHideNow(const HoverSnapshot &s) const
    {
        return !(s.dialogContainsMouse || s.activeTaskContainsMouse || s.externalDragOnActiveTask);
    }

    //! LRU accounting for the delegate cache (f1edd103): which delegate
    //! shows taskId's previews - the active one kept, a parked one revived,
    //! or a fresh build, parking the previous active and evicting the
    //! oldest parked entry beyond capacity. The shell owns the delegate
    //! objects and applies the result.
    MaterializeResult materialize(int taskId)
    {
        if (m_activeCacheTask == taskId) {
            return {Materialize::KeepActive, std::nullopt, false};
        }

        const int parkedIdx = m_parked.indexOf(taskId);
        MaterializeResult result;

        if (parkedIdx >= 0) {
            m_parked.removeAt(parkedIdx);
            result = {Materialize::Revive, std::nullopt, false};
        } else {
            result = {Materialize::Build, std::nullopt, true};
        }

        if (m_activeCacheTask) {
            m_parked.append(*m_activeCacheTask);
            if (m_parked.size() > m_parkedCapacity) {
                result.evictedTask = m_parked.takeFirst();
                if (result.kind == Materialize::Build) {
                    result.kind = Materialize::BuildEvicting;
                }
            }
        }

        m_activeCacheTask = taskId;
        return result;
    }

    //! the destruction contract (f1edd103): a dying task drops its parked
    //! delegate. Faithful to the QML body: only parked entries are
    //! scanned; a dying ACTIVE task is untouched (that arm is a filed
    //! watch item, not silently changed here).
    bool drop(int taskId)
    {
        const int idx = m_parked.indexOf(taskId);
        if (idx < 0) {
            return false;
        }
        m_parked.removeAt(idx);
        return true;
    }

    std::optional<int> activeCacheTask() const
    {
        return m_activeCacheTask;
    }

    QVector<int> parkedTasks() const
    {
        return m_parked;
    }

private:
    int m_parkedCapacity;
    std::optional<int> m_shownTask;
    std::optional<int> m_pendingTask;
    std::optional<int> m_activeCacheTask;
    qint64 m_lastSwitchRequestMs = 0;
    QVector<int> m_parked; //!< oldest first
};

}
}

#endif
