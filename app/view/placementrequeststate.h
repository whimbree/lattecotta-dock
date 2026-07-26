/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <QtGlobal>

namespace Latte {
namespace ViewPart {

//! Complete requested placement. Logical output policy and the physical output
//! resolved for that policy form one value, so a request cannot combine a new
//! screen group with an older output assignment.
struct PlacementIntent
{
    std::string layoutName;
    int screensGroup{0};
    std::string logicalOutputName;
    std::string resolvedOutputName;
    bool followsPrimary{false};
    int edge{0};
    int alignment{0};

    friend bool operator==(
        const PlacementIntent &,
        const PlacementIntent &) = default;
};

//! Sparse boundary request. The state core overlays it on the latest complete
//! requested target, never on a partially applied runtime object.
struct PlacementPatch
{
    std::optional<std::string> layoutName;
    std::optional<int> screensGroup;
    std::optional<std::string> logicalOutputName;
    std::optional<std::string> resolvedOutputName;
    std::optional<bool> followsPrimary;
    std::optional<int> edge;
    std::optional<int> alignment;
};

enum class PlacementRequestOutcome
{
    Committed,
    Refused,
    Superseded,
    Abandoned,
};

struct PlacementRequestCompletion
{
    std::uint64_t token{0};
    PlacementRequestOutcome outcome{
        PlacementRequestOutcome::Refused};

    friend bool operator==(
        const PlacementRequestCompletion &,
        const PlacementRequestCompletion &) = default;
};

enum class PlacementSubmissionStatus
{
    Rejected,
    Applied,
    CompletionExpected,
};

struct PlacementSubmission
{
    PlacementSubmissionStatus status{
        PlacementSubmissionStatus::Rejected};
    std::uint64_t token{0};

    [[nodiscard]] constexpr bool accepted() const
    {
        return status
            != PlacementSubmissionStatus::Rejected;
    }

    [[nodiscard]] constexpr bool expectsCompletion() const
    {
        return status
            == PlacementSubmissionStatus::
                CompletionExpected;
    }
};

//! Invalidate only the delayed callback owned by the generation that just
//! completed synchronously. A newer reentrant request may already own the
//! scheduling slot and must remain intact.
[[nodiscard]] constexpr bool
invalidateScheduledPlacementCompletionForGeneration(
    std::optional<std::uint64_t> &scheduledGeneration,
    const std::uint64_t completedGeneration)
{
    if (scheduledGeneration
            != completedGeneration) {
        return false;
    }

    scheduledGeneration.reset();
    return true;
}

//! Owns generation-scoped completion observers independently from the Qt
//! object that drives placement. Removing the observer list before invocation
//! makes completion exactly once even when a handler submits another request.
class PlacementCompletionRegistry
{
public:
    using Token = std::uint64_t;
    using Handler = std::function<
        void(const PlacementRequestCompletion &)>;

    [[nodiscard]] bool watch(
        const Token token,
        Handler handler)
    {
        if (token == 0 || !handler) {
            return false;
        }

        m_handlers[token].push_back(
            std::move(handler));
        return true;
    }

    [[nodiscard]] bool complete(
        const Token token,
        const PlacementRequestOutcome outcome)
    {
        const auto found = m_handlers.find(token);
        if (found == m_handlers.end()) {
            return false;
        }

        std::vector<Handler> handlers =
            std::move(found->second);
        m_handlers.erase(found);
        const PlacementRequestCompletion completion{
            token,
            outcome,
        };
        for (auto &handler : handlers) {
            handler(completion);
        }
        return true;
    }

    void abandonAll()
    {
        auto handlers = std::move(m_handlers);
        m_handlers.clear();
        for (auto &[token, observers] : handlers) {
            const PlacementRequestCompletion completion{
                token,
                PlacementRequestOutcome::Abandoned,
            };
            for (auto &observer : observers) {
                observer(completion);
            }
        }
    }

    [[nodiscard]] bool contains(
        const Token token) const
    {
        return m_handlers.contains(token);
    }

private:
    std::map<Token, std::vector<Handler>> m_handlers;
};

class PlacementRequestState
{
public:
    using Token = std::uint64_t;

    struct Request
    {
        Token token{0};
        PlacementIntent intent;

        friend bool operator==(
            const Request &,
            const Request &) = default;
    };

    struct Submission
    {
        bool accepted{false};
        Request request;
    };

    [[nodiscard]] Submission submit(
        const PlacementIntent &committed,
        const PlacementPatch &patch)
    {
        const PlacementIntent base =
            m_pending.has_value()
            ? m_pending->intent
            : committed;
        PlacementIntent target = base;
        overlay(target, patch);

        if (target == base) {
            return {
                false,
                m_pending.value_or(
                    Request{m_generation, committed})};
        }

        m_pending = Request{++m_generation, std::move(target)};
        return {true, *m_pending};
    }

    [[nodiscard]] const std::optional<Request> &pending() const
    {
        return m_pending;
    }

    [[nodiscard]] bool isCurrent(const Token token) const
    {
        return m_pending.has_value()
            && m_pending->token == token;
    }

    [[nodiscard]] bool completeIfCurrent(const Token token)
    {
        if (!isCurrent(token)) {
            return false;
        }

        m_pending.reset();
        return true;
    }

    //! Preserve the generation while redirecting a failed application to the
    //! last committed placement. The ordinary surface and reservation commit
    //! path still owns reveal and completion for that generation.
    [[nodiscard]] bool cancelToCommittedIfCurrent(
        const Token token,
        const PlacementIntent &committed)
    {
        if (!isCurrent(token)) {
            return false;
        }

        m_pending->intent = committed;
        return true;
    }

private:
    static void overlay(
        PlacementIntent &target,
        const PlacementPatch &patch)
    {
        if (patch.layoutName) {
            target.layoutName = *patch.layoutName;
        }
        if (patch.screensGroup) {
            target.screensGroup = *patch.screensGroup;
        }
        if (patch.logicalOutputName) {
            target.logicalOutputName =
                *patch.logicalOutputName;
        }
        if (patch.resolvedOutputName) {
            target.resolvedOutputName =
                *patch.resolvedOutputName;
        }
        if (patch.followsPrimary) {
            target.followsPrimary =
                *patch.followsPrimary;
        }
        if (patch.edge) {
            target.edge = *patch.edge;
        }
        if (patch.alignment) {
            target.alignment = *patch.alignment;
        }
    }

    Token m_generation{0};
    std::optional<Request> m_pending;
};

} // namespace ViewPart
} // namespace Latte
