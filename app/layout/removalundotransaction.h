/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <concepts>
#include <cstdint>
#include <functional>
#include <utility>

namespace Latte {
namespace Layout {

//! Pure lifecycle authority for one reversible root-view removal. Every Undo
//! is resolved after libplasma finishes its recursive child-state transition.
//! A failed restoration can only proceed through runtime retirement,
//! containment destruction, and the final persistence tombstone in that order.
class RemovalUndoTransaction
{
public:
    using Token = std::uint64_t;

    enum class Phase
    {
        Idle,
        Suspended,
        UndoResolutionQueued,
        ResolvingUndo,
        RemovalFinalizationQueued,
        FinalizingRemoval,
        Restored,
        Removed,
        Unrecoverable,
    };

    enum class UndoResolution
    {
        Restored,
        RemovalRequired,
        Stale,
    };

    enum class FinalizationResult
    {
        Removed,
        RuntimeRetirementFailed,
        DestructionFailed,
        PersistenceFailed,
        Stale,
    };

    [[nodiscard]] constexpr Token beginRemoval()
    {
        m_phase = Phase::Suspended;
        return ++m_generation;
    }

    [[nodiscard]] constexpr Token token() const
    {
        return m_generation;
    }

    [[nodiscard]] constexpr Phase phase() const
    {
        return m_phase;
    }

    [[nodiscard]] constexpr bool queueUndoResolutionIfCurrent(
        const Token token)
    {
        if (token != m_generation
                || m_phase != Phase::Suspended) {
            return false;
        }

        m_phase = Phase::UndoResolutionQueued;
        return true;
    }

    template<typename Restore, typename Resume>
        requires std::convertible_to<
            std::invoke_result_t<Restore>, bool>
            && std::convertible_to<
                std::invoke_result_t<Resume>, bool>
    [[nodiscard]] constexpr UndoResolution resolveUndoOrRequireRemoval(
        const Token token,
        Restore &&restore,
        Resume &&resume)
    {
        if (token != m_generation
                || m_phase
                    != Phase::UndoResolutionQueued) {
            return UndoResolution::Stale;
        }

        m_phase = Phase::ResolvingUndo;
        if (!static_cast<bool>(
                std::invoke(
                    std::forward<Restore>(restore)))
                || !static_cast<bool>(
                    std::invoke(
                        std::forward<Resume>(resume)))) {
            m_phase = Phase::RemovalFinalizationQueued;
            return UndoResolution::RemovalRequired;
        }

        m_phase = Phase::Restored;
        return UndoResolution::Restored;
    }

    [[nodiscard]] constexpr bool queueRemovalFinalizationIfCurrent(
        const Token token)
    {
        if (token != m_generation
                || (m_phase != Phase::Suspended
                    && m_phase
                        != Phase::ResolvingUndo)) {
            return false;
        }

        m_phase = Phase::RemovalFinalizationQueued;
        return true;
    }

    template<typename RetireRuntime, typename DestroyContainment,
             typename CommitPersistence>
        requires std::convertible_to<
            std::invoke_result_t<RetireRuntime>, bool>
            && std::convertible_to<
                std::invoke_result_t<DestroyContainment>, bool>
            && std::convertible_to<
                std::invoke_result_t<CommitPersistence>, bool>
    [[nodiscard]] constexpr FinalizationResult finalizeRemovalIfCurrent(
        const Token token,
        RetireRuntime &&retireRuntime,
        DestroyContainment &&destroyContainment,
        CommitPersistence &&commitPersistence)
    {
        if (token != m_generation
                || m_phase
                    != Phase::RemovalFinalizationQueued) {
            return FinalizationResult::Stale;
        }

        m_phase = Phase::FinalizingRemoval;
        if (!static_cast<bool>(
                std::invoke(
                    std::forward<RetireRuntime>(
                        retireRuntime)))) {
            m_phase = Phase::Unrecoverable;
            return FinalizationResult::
                RuntimeRetirementFailed;
        }
        if (!static_cast<bool>(
                std::invoke(
                    std::forward<DestroyContainment>(
                        destroyContainment)))) {
            m_phase = Phase::Unrecoverable;
            return FinalizationResult::
                DestructionFailed;
        }
        if (!static_cast<bool>(
                std::invoke(
                    std::forward<CommitPersistence>(
                        commitPersistence)))) {
            m_phase = Phase::Unrecoverable;
            return FinalizationResult::
                PersistenceFailed;
        }

        m_phase = Phase::Removed;
        return FinalizationResult::Removed;
    }

private:
    Token m_generation{0};
    Phase m_phase{Phase::Idle};
};

} // namespace Layout
} // namespace Latte
