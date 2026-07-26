/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <concepts>
#include <functional>
#include <utility>

namespace Latte {
namespace Layout {

//! Pure ordering authority for one durable cross-layout view move. The
//! destination copy is staging until the active configuration records the
//! destination layout. That one persisted owner decides whether recovery
//! rolls the transaction back or forward after any interruption.
class ViewMoveTransaction
{
public:
    enum class PersistentOwner
    {
        Origin,
        Destination,
        Unknown,
    };

    enum class RecoveryAction
    {
        RollBack,
        RollForward,
        Refuse,
    };

    enum class Phase
    {
        Idle,
        Prepared,
        DestinationPublished,
        CommitDecided,
        OriginRetired,
        Rejected,
        RecoveryRequired,
        Complete,
    };

    enum class Result
    {
        Rejected,
        RejectedRecoveryRequired,
        Committed,
        CommittedRecoveryRequired,
    };

    [[nodiscard]] constexpr Phase phase() const noexcept
    {
        return m_phase;
    }

    [[nodiscard]] static constexpr RecoveryAction recoveryAction(
        const PersistentOwner owner) noexcept
    {
        switch (owner) {
        case PersistentOwner::Origin:
            return RecoveryAction::RollBack;
        case PersistentOwner::Destination:
            return RecoveryAction::RollForward;
        case PersistentOwner::Unknown:
            return RecoveryAction::Refuse;
        }

        return RecoveryAction::Refuse;
    }

    template<typename PrepareJournal,
             typename PublishDestination,
             typename PublishCommitDecision,
             typename ObservePersistentOwner,
             typename RollBackDestination,
             typename RetireOrigin>
        requires std::convertible_to<
            std::invoke_result_t<PrepareJournal>, bool>
            && std::convertible_to<
                std::invoke_result_t<PublishDestination>, bool>
            && std::convertible_to<
                std::invoke_result_t<PublishCommitDecision>, bool>
            && std::same_as<
                std::remove_cvref_t<
                    std::invoke_result_t<ObservePersistentOwner>>,
                PersistentOwner>
            && std::convertible_to<
                std::invoke_result_t<RollBackDestination>, bool>
            && std::convertible_to<
                std::invoke_result_t<RetireOrigin>, bool>
    [[nodiscard]] constexpr Result commit(
        PrepareJournal &&prepareJournal,
        PublishDestination &&publishDestination,
        PublishCommitDecision &&publishCommitDecision,
        ObservePersistentOwner &&observePersistentOwner,
        RollBackDestination &&rollBackDestination,
        RetireOrigin &&retireOrigin)
    {
        if (m_phase != Phase::Idle
                || !static_cast<bool>(
                    std::invoke(
                        std::forward<PrepareJournal>(
                            prepareJournal)))) {
            m_phase = Phase::Rejected;
            return Result::Rejected;
        }
        m_phase = Phase::Prepared;

        if (!static_cast<bool>(
                std::invoke(
                    std::forward<PublishDestination>(
                        publishDestination)))) {
            return rejectOrRequireRecovery(
                std::forward<RollBackDestination>(
                    rollBackDestination));
        }
        m_phase = Phase::DestinationPublished;

        const bool decisionWriteReportedSuccess =
            static_cast<bool>(
                std::invoke(
                    std::forward<PublishCommitDecision>(
                        publishCommitDecision)));
        const PersistentOwner owner =
            std::invoke(
                std::forward<ObservePersistentOwner>(
                    observePersistentOwner));
        if (!decisionWriteReportedSuccess
                && owner
                    != PersistentOwner::Destination) {
            if (owner != PersistentOwner::Origin) {
                m_phase = Phase::RecoveryRequired;
                return Result::RejectedRecoveryRequired;
            }
            return rejectOrRequireRecovery(
                std::forward<RollBackDestination>(
                    rollBackDestination));
        }
        if (owner != PersistentOwner::Destination) {
            if (owner == PersistentOwner::Origin) {
                return rejectOrRequireRecovery(
                    std::forward<RollBackDestination>(
                        rollBackDestination));
            }
            m_phase = Phase::RecoveryRequired;
            return Result::RejectedRecoveryRequired;
        }
        m_phase = Phase::CommitDecided;

        if (!static_cast<bool>(
                std::invoke(
                    std::forward<RetireOrigin>(
                        retireOrigin)))) {
            m_phase = Phase::RecoveryRequired;
            return Result::CommittedRecoveryRequired;
        }
        m_phase = Phase::OriginRetired;
        m_phase = Phase::Complete;
        return Result::Committed;
    }

private:
    template<typename RollBackDestination>
        requires std::convertible_to<
            std::invoke_result_t<RollBackDestination>, bool>
    [[nodiscard]] constexpr Result rejectOrRequireRecovery(
        RollBackDestination &&rollBackDestination)
    {
        if (!static_cast<bool>(
                std::invoke(
                    std::forward<RollBackDestination>(
                        rollBackDestination)))) {
            m_phase = Phase::RecoveryRequired;
            return Result::RejectedRecoveryRequired;
        }

        m_phase = Phase::Rejected;
        return Result::Rejected;
    }

    Phase m_phase{Phase::Idle};
};

static_assert(
    ViewMoveTransaction::recoveryAction(
        ViewMoveTransaction::PersistentOwner::Origin)
    == ViewMoveTransaction::RecoveryAction::RollBack);
static_assert(
    ViewMoveTransaction::recoveryAction(
        ViewMoveTransaction::PersistentOwner::Destination)
    == ViewMoveTransaction::RecoveryAction::RollForward);
static_assert(
    ViewMoveTransaction::recoveryAction(
        ViewMoveTransaction::PersistentOwner::Unknown)
    == ViewMoveTransaction::RecoveryAction::Refuse);

} // namespace Layout
} // namespace Latte
