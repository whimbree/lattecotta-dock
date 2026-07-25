/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef FLOATINGINPUTEVENT_H
#define FLOATINGINPUTEVENT_H

#include "floatingpanelgeometry.h"

#include <QMouseEvent>
#include <QWheelEvent>

#include <memory>

namespace Latte::ViewPart::FloatingInputEvent {

struct MouseRoute {
    bool consumed{false};
    std::unique_ptr<QMouseEvent> projected;
};

struct WheelRoute {
    bool consumed{false};
    std::unique_ptr<QWheelEvent> projected;
};

[[nodiscard]] inline MouseRoute routeMouseEvent(
    FloatingPanelGeometry::InputDisposition disposition,
    const QMouseEvent &source,
    const QPointF &projectedPosition,
    const QPointF &projectedGlobalPosition)
{
    using FloatingPanelGeometry::InputDisposition;

    switch (disposition) {
    case InputDisposition::ConsumeWithoutForwarding:
        return {
            .consumed = true,
            .projected = {},
        };
    case InputDisposition::Forward:
        return {};
    case InputDisposition::ProjectToVisibleMask: {
        auto event = std::make_unique<QMouseEvent>(
            source.type(),
            projectedPosition,
            projectedPosition,
            projectedGlobalPosition,
            source.button(),
            source.buttons(),
            source.modifiers(),
            source.source(),
            source.pointingDevice());
        event->setTimestamp(source.timestamp());
        return {.projected = std::move(event)};
    }
    }

    Q_UNREACHABLE();
} // namespace Latte::ViewPart::FloatingInputEvent

[[nodiscard]] inline WheelRoute routeWheelEvent(
    FloatingPanelGeometry::InputDisposition disposition,
    const QWheelEvent &source,
    const QPointF &projectedPosition,
    const QPointF &projectedGlobalPosition)
{
    using FloatingPanelGeometry::InputDisposition;

    switch (disposition) {
    case InputDisposition::ConsumeWithoutForwarding:
        return {
            .consumed = true,
            .projected = {},
        };
    case InputDisposition::Forward:
        return {};
    case InputDisposition::ProjectToVisibleMask: {
        auto event = std::make_unique<QWheelEvent>(
            projectedPosition,
            projectedGlobalPosition,
            source.pixelDelta(),
            source.angleDelta(),
            source.buttons(),
            source.modifiers(),
            source.phase(),
            source.inverted(),
            source.source(),
            source.pointingDevice());
        event->setTimestamp(source.timestamp());
        return {.projected = std::move(event)};
    }
    }

    Q_UNREACHABLE();
}

}

#endif
