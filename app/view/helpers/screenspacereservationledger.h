/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace Latte {
namespace ViewPart {

enum class ReservationEdge : std::uint8_t
{
    Top,
    Bottom,
    Left,
    Right,
};

class ReservationOutputId
{
public:
    [[nodiscard]] static constexpr std::optional<ReservationOutputId>
    fromPersistentId(const int value) noexcept
    {
        return value >= 0
                ? std::optional<ReservationOutputId>(ReservationOutputId(value))
                : std::nullopt;
    }

    [[nodiscard]] constexpr int value() const noexcept
    {
        return m_value;
    }

    auto operator<=>(const ReservationOutputId &) const = default;

private:
    explicit constexpr ReservationOutputId(const int value) noexcept
        : m_value(value)
    {
    }

    int m_value;
};

class ReservationMemberId
{
public:
    [[nodiscard]] static constexpr std::optional<ReservationMemberId>
    fromPersistentDockId(const std::uint64_t value) noexcept
    {
        return value > 0
                ? std::optional<ReservationMemberId>(ReservationMemberId(value))
                : std::nullopt;
    }

    [[nodiscard]] constexpr std::uint64_t value() const noexcept
    {
        return m_value;
    }

    auto operator<=>(const ReservationMemberId &) const = default;

private:
    explicit constexpr ReservationMemberId(const std::uint64_t value) noexcept
        : m_value(value)
    {
    }

    std::uint64_t m_value;
};

class ReservationDepth
{
public:
    [[nodiscard]] static constexpr std::optional<ReservationDepth>
    fromPixels(const int value) noexcept
    {
        return value > 0
                ? std::optional<ReservationDepth>(ReservationDepth(value))
                : std::nullopt;
    }

    [[nodiscard]] constexpr int pixels() const noexcept
    {
        return m_pixels;
    }

    auto operator<=>(const ReservationDepth &) const = default;

private:
    explicit constexpr ReservationDepth(const int pixels) noexcept
        : m_pixels(pixels)
    {
    }

    int m_pixels;
};

struct ReservationGroupKey
{
    ReservationOutputId output;
    ReservationEdge edge;

    auto operator<=>(const ReservationGroupKey &) const = default;
};

struct ReservationGroupState
{
    ReservationDepth maximumDepth;
    std::size_t memberCount;
};

struct ReservationLedgerChange
{
    bool changed{false};
    std::vector<ReservationGroupKey> affectedGroups;
};

//! Value-only ownership policy for output-edge work-area reservations.
//! Members may move between groups, but a group exposes only its deepest
//! contribution. Visual ordering and input regions deliberately do not enter
//! this model.
class ScreenSpaceReservationLedger
{
public:
    [[nodiscard]] ReservationLedgerChange updateContribution(
        const ReservationMemberId member,
        const ReservationGroupKey group,
        const ReservationDepth depth)
    {
        ReservationLedgerChange change;
        const auto existing = m_contributions.find(member);
        if (existing != m_contributions.end()
                && existing->second.group == group
                && existing->second.depth == depth) {
            return change;
        }

        change.changed = true;
        if (existing != m_contributions.end()) {
            appendAffectedGroup(change, existing->second.group);
            eraseFromGroup(member, existing->second.group);
            m_contributions.erase(existing);
        }

        appendAffectedGroup(change, group);
        m_contributions.emplace(member, Contribution{group, depth});
        auto groupEntry = m_groups.find(group);
        if (groupEntry == m_groups.end()) {
            groupEntry = m_groups.emplace(group, MemberDepths{}).first;
        }
        groupEntry->second.emplace(member, depth);
        return change;
    }

    [[nodiscard]] ReservationLedgerChange removeContribution(
        const ReservationMemberId member)
    {
        ReservationLedgerChange change;
        const auto existing = m_contributions.find(member);
        if (existing == m_contributions.end()) {
            return change;
        }

        change.changed = true;
        appendAffectedGroup(change, existing->second.group);
        eraseFromGroup(member, existing->second.group);
        m_contributions.erase(existing);
        return change;
    }

    [[nodiscard]] std::optional<ReservationGroupKey> findGroup(
        const ReservationMemberId member) const
    {
        const auto existing = m_contributions.find(member);
        return existing == m_contributions.end()
                ? std::nullopt
                : std::optional<ReservationGroupKey>(existing->second.group);
    }

    [[nodiscard]] std::optional<ReservationDepth> findContributionDepth(
        const ReservationMemberId member) const
    {
        const auto existing = m_contributions.find(member);
        return existing == m_contributions.end()
                ? std::nullopt
                : std::optional<ReservationDepth>(existing->second.depth);
    }

    [[nodiscard]] std::optional<ReservationGroupState> describeGroup(
        const ReservationGroupKey group) const
    {
        const auto existing = m_groups.find(group);
        if (existing == m_groups.end()) {
            return std::nullopt;
        }

        const auto deepest = std::max_element(
            existing->second.cbegin(),
            existing->second.cend(),
            [](const auto &left, const auto &right) {
                return left.second < right.second;
            });

        return ReservationGroupState{deepest->second, existing->second.size()};
    }

    [[nodiscard]] std::size_t memberCount() const noexcept
    {
        return m_contributions.size();
    }

    [[nodiscard]] std::size_t groupCount() const noexcept
    {
        return m_groups.size();
    }

private:
    struct Contribution
    {
        ReservationGroupKey group;
        ReservationDepth depth;
    };

    using MemberDepths = std::map<ReservationMemberId, ReservationDepth>;

    static void appendAffectedGroup(
        ReservationLedgerChange &change,
        const ReservationGroupKey group)
    {
        if (std::ranges::find(change.affectedGroups, group)
                == change.affectedGroups.end()) {
            change.affectedGroups.push_back(group);
        }
    }

    void eraseFromGroup(
        const ReservationMemberId member,
        const ReservationGroupKey group)
    {
        const auto groupEntry = m_groups.find(group);
        if (groupEntry == m_groups.end()) {
            return;
        }

        groupEntry->second.erase(member);
        if (groupEntry->second.empty()) {
            m_groups.erase(groupEntry);
        }
    }

    std::map<ReservationMemberId, Contribution> m_contributions;
    std::map<ReservationGroupKey, MemberDepths> m_groups;
};

} // namespace ViewPart
} // namespace Latte
