/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef WINDOWTOUCHSTATE_H
#define WINDOWTOUCHSTATE_H

#include <QRect>

#include <optional>
#include <span>

namespace Latte::ViewPart {

struct WindowTouchCandidate
{
    QRect geometry;
    bool isWindow{false};
    bool hidden{false};
    bool minimized{false};
};

class StableWindowTouchTrigger final
{
public:
    [[nodiscard]] static std::optional<StableWindowTouchTrigger>
    fromGeometry(const QRect &geometry)
    {
        if (!geometry.isValid()) {
            return std::nullopt;
        }

        return StableWindowTouchTrigger{geometry};
    }

    [[nodiscard]] const QRect &geometry() const
    {
        return m_geometry;
    }

private:
    explicit StableWindowTouchTrigger(const QRect &geometry)
        : m_geometry(geometry)
    {
    }

    QRect m_geometry;
};

[[nodiscard]] inline int countWindowsTouchingTrigger(
    const StableWindowTouchTrigger &trigger,
    const std::span<const WindowTouchCandidate> candidates)
{
    int count{0};

    for (const auto &candidate : candidates) {
        if (candidate.isWindow
                && !candidate.hidden
                && !candidate.minimized
                && candidate.geometry.intersects(trigger.geometry())) {
            ++count;
        }
    }

    return count;
}

} // namespace Latte::ViewPart

#endif
