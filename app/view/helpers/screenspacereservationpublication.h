/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QRect>

#include <Plasma/Plasma>

#include <concepts>
#include <functional>
#include <optional>
#include <utility>

namespace Latte {
namespace ViewPart {

struct ScreenSpaceReservationPublicationTarget
{
    QRect struts;
    int outputId{-1};
    Plasma::Types::Location edge{
        Plasma::Types::Floating};

    bool operator==(
        const ScreenSpaceReservationPublicationTarget &) const =
        default;
};

//! Commits the member-side reservation rectangle only after the coordinator
//! accepts the matching projection. A failed equal-valued reprojection remains
//! dirty so screen migrations with identical geometries cannot be suppressed.
class ScreenSpaceReservationPublicationState final
{
public:
    [[nodiscard]] QRect publishedStruts() const noexcept
    {
        return m_committed
            ? m_committed->struts
            : QRect();
    }

    [[nodiscard]] const std::optional<
        ScreenSpaceReservationPublicationTarget> &
    committedTarget() const noexcept
    {
        return m_committed;
    }

    [[nodiscard]] bool retryRequired() const noexcept
    {
        return m_retryRequired;
    }

    template<typename Publish>
        requires std::predicate<
            Publish,
            const ScreenSpaceReservationPublicationTarget &>
    [[nodiscard]] bool update(
        const ScreenSpaceReservationPublicationTarget
            &candidate,
        const bool force,
        Publish &&publish)
    {
        if (!force
                && !m_retryRequired
                && m_committed
                && *m_committed == candidate) {
            return true;
        }

        if (!std::invoke(
                std::forward<Publish>(publish),
                candidate)) {
            m_retryRequired = true;
            return false;
        }

        m_committed = candidate;
        m_retryRequired = false;
        return true;
    }

    template<typename Remove>
        requires std::predicate<Remove>
    [[nodiscard]] bool remove(Remove &&remove)
    {
        if (!std::invoke(std::forward<Remove>(remove))) {
            m_retryRequired = true;
            return false;
        }

        m_committed.reset();
        m_retryRequired = false;
        return true;
    }

private:
    std::optional<
        ScreenSpaceReservationPublicationTarget> m_committed;
    bool m_retryRequired{false};
};

} // namespace ViewPart
} // namespace Latte
