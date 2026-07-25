/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef FLOATINGPANELGEOMETRY_H
#define FLOATINGPANELGEOMETRY_H

#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QtGlobal>

#include <limits>
#include <optional>

namespace Latte::ViewPart::FloatingPanelGeometry {

enum class Edge {
    Top,
    Right,
    Bottom,
    Left,
};

enum class PrimaryAxisAlignment {
    Start,
    Center,
    End,
};

struct StablePrimaryAxisSpan {
    int start{0};
    int length{0};

    friend constexpr bool operator==(const StablePrimaryAxisSpan &,
                                     const StablePrimaryAxisSpan &) = default;
};

struct AttachedRectangle {
    QRect value;
};

struct FloatedRectangle {
    QRect value;
};

struct StableEnvelope {
    QRect value;
};

struct TriggerRectangle {
    QRect value;
};

struct VisibleMaskRectangle {
    QRectF value;
};

struct FittsBridgeRectangle {
    QRectF value;
};

struct AppletMeasurementBounds {
    QRect value;
};

struct Solution {
    AttachedRectangle attached;
    FloatedRectangle floated;
    StableEnvelope envelope;
    TriggerRectangle trigger;
    AppletMeasurementBounds appletMeasurementBounds;
    StablePrimaryAxisSpan primaryAxisSpan;
    int reservationDepth{0};

    [[nodiscard]] VisibleMaskRectangle visibleMask(qreal floatingness) const
    {
        Q_ASSERT(floatingness >= 0.0 && floatingness <= 1.0);

        const QPointF attachedTopLeft{attached.value.topLeft()};
        const QPointF floatedTopLeft{floated.value.topLeft()};
        const QPointF topLeft =
            attachedTopLeft + ((floatedTopLeft - attachedTopLeft) * floatingness);

        return {QRectF(topLeft, QSizeF(attached.value.size()))};
    }

    [[nodiscard]] FittsBridgeRectangle fittsBridge(qreal floatingness) const
    {
        const QRectF visible = visibleMask(floatingness).value;
        return {visible.united(QRectF(attached.value))};
    }

    [[nodiscard]] QPointF contentTranslation(qreal floatingness) const
    {
        return visibleMask(floatingness).value.topLeft() - floated.value.topLeft();
    }
};

struct Inputs {
    QRect outputGeometry;
    Edge edge{Edge::Bottom};
    StablePrimaryAxisSpan primaryAxisSpan;
    int panelDepth{0};
    int floatingGap{0};
};

struct PlacementInputs {
    QRect outputGeometry;
    QRect availablePrimaryGeometry;
    Edge edge{Edge::Bottom};
    PrimaryAxisAlignment alignment{PrimaryAxisAlignment::Center};
    float maxLength{1.0F};
    float offset{0.0F};
    int panelDepth{0};
    int floatingGap{0};
};

[[nodiscard]] constexpr bool isHorizontal(Edge edge)
{
    return edge == Edge::Top || edge == Edge::Bottom;
}

[[nodiscard]] inline std::optional<int> truncateToRepresentableInt(qreal value)
{
    constexpr qreal lowest =
        static_cast<qreal>(std::numeric_limits<int>::lowest());
    constexpr qreal highest =
        static_cast<qreal>(std::numeric_limits<int>::max());

    // Floating-to-integer conversion is undefined outside the destination
    // range. Check every floating product at this boundary so hand-edited
    // finite config values are refused deterministically.
    if (!qIsFinite(value) || value < lowest || value > highest) {
        return std::nullopt;
    }

    return static_cast<int>(value);
}

[[nodiscard]] inline bool hasValidGeometry(const Inputs &in)
{
    if (!in.outputGeometry.isValid() || in.panelDepth <= 0 || in.floatingGap < 0
        || in.primaryAxisSpan.length <= 0) {
        return false;
    }

    const int outputStart =
        isHorizontal(in.edge) ? in.outputGeometry.left() : in.outputGeometry.top();
    const int outputLength =
        isHorizontal(in.edge) ? in.outputGeometry.width() : in.outputGeometry.height();
    const qint64 spanEnd = qint64(in.primaryAxisSpan.start) + in.primaryAxisSpan.length;
    const qint64 outputEnd = qint64(outputStart) + outputLength;
    const qint64 envelopeDepth = qint64(in.panelDepth) + in.floatingGap;
    const qint64 triggerDepth = qint64(in.panelDepth) + 1;
    const int outputDepth =
        isHorizontal(in.edge) ? in.outputGeometry.height()
                              : in.outputGeometry.width();

    return in.primaryAxisSpan.start >= outputStart && spanEnd <= outputEnd
        && envelopeDepth <= outputDepth && triggerDepth <= outputDepth;
}

[[nodiscard]] inline std::optional<Solution> solve(const Inputs &in)
{
    if (!hasValidGeometry(in)) {
        return std::nullopt;
    }

    const int envelopeDepth = in.panelDepth + in.floatingGap;
    QRect envelope;
    QRect attached;
    QRect floated;

    switch (in.edge) {
    case Edge::Top:
        envelope = {in.primaryAxisSpan.start,
                    in.outputGeometry.top(),
                    in.primaryAxisSpan.length,
                    envelopeDepth};
        attached = {0, 0, in.primaryAxisSpan.length, in.panelDepth};
        floated = {0, in.floatingGap, in.primaryAxisSpan.length, in.panelDepth};
        break;
    case Edge::Right:
        envelope = {in.outputGeometry.right() - envelopeDepth + 1,
                    in.primaryAxisSpan.start,
                    envelopeDepth,
                    in.primaryAxisSpan.length};
        attached = {in.floatingGap, 0, in.panelDepth, in.primaryAxisSpan.length};
        floated = {0, 0, in.panelDepth, in.primaryAxisSpan.length};
        break;
    case Edge::Bottom:
        envelope = {in.primaryAxisSpan.start,
                    in.outputGeometry.bottom() - envelopeDepth + 1,
                    in.primaryAxisSpan.length,
                    envelopeDepth};
        attached = {0, in.floatingGap, in.primaryAxisSpan.length, in.panelDepth};
        floated = {0, 0, in.primaryAxisSpan.length, in.panelDepth};
        break;
    case Edge::Left:
        envelope = {in.outputGeometry.left(),
                    in.primaryAxisSpan.start,
                    envelopeDepth,
                    in.primaryAxisSpan.length};
        attached = {0, 0, in.panelDepth, in.primaryAxisSpan.length};
        floated = {in.floatingGap, 0, in.panelDepth, in.primaryAxisSpan.length};
        break;
    }

    QRect trigger = attached.translated(envelope.topLeft());
    switch (in.edge) {
    case Edge::Top:
        // The one logical pixel is for overlap detection at the inward edge.
        trigger.setBottom(trigger.bottom() + 1);
        break;
    case Edge::Right:
        // The one logical pixel is for overlap detection at the inward edge.
        trigger.setLeft(trigger.left() - 1);
        break;
    case Edge::Bottom:
        // The one logical pixel is for overlap detection at the inward edge.
        trigger.setTop(trigger.top() - 1);
        break;
    case Edge::Left:
        // The one logical pixel is for overlap detection at the inward edge.
        trigger.setRight(trigger.right() + 1);
        break;
    }

    const Solution solution{
        .attached = {attached},
        .floated = {floated},
        .envelope = {envelope},
        .trigger = {trigger},
        .appletMeasurementBounds = {
            QRect(QPoint(0, 0), attached.size()),
        },
        .primaryAxisSpan = in.primaryAxisSpan,
        .reservationDepth = in.panelDepth,
    };

    Q_ASSERT(in.outputGeometry.contains(solution.envelope.value));
    Q_ASSERT(solution.envelope.value.contains(
        solution.attached.value.translated(solution.envelope.value.topLeft())));
    Q_ASSERT(solution.envelope.value.contains(
        solution.floated.value.translated(solution.envelope.value.topLeft())));
    Q_ASSERT(in.outputGeometry.contains(solution.trigger.value));

    return solution;
}

[[nodiscard]] inline std::optional<Solution> solvePlacement(const PlacementInputs &in)
{
    if (!in.outputGeometry.isValid() || !in.availablePrimaryGeometry.isValid()
        || !qIsFinite(in.maxLength) || in.maxLength <= 0.0F || in.maxLength > 1.0F
        || !qIsFinite(in.offset)) {
        return std::nullopt;
    }

    const int outputStart =
        isHorizontal(in.edge) ? in.outputGeometry.left() : in.outputGeometry.top();
    const int outputLength =
        isHorizontal(in.edge) ? in.outputGeometry.width() : in.outputGeometry.height();
    const int availableStart =
        isHorizontal(in.edge) ? in.availablePrimaryGeometry.left()
                              : in.availablePrimaryGeometry.top();
    const int availableLength =
        isHorizontal(in.edge) ? in.availablePrimaryGeometry.width()
                              : in.availablePrimaryGeometry.height();
    const qint64 availableEnd = qint64(availableStart) + availableLength;
    const qint64 outputEnd = qint64(outputStart) + outputLength;
    if (availableStart < outputStart || availableEnd > outputEnd) {
        return std::nullopt;
    }

    // Preserve the shipped float truncation order for ordinary placement.
    // Each product remains floating-point until representability is proven.
    const float availableLengthValue = static_cast<float>(availableLength);
    const auto panelLength =
        truncateToRepresentableInt(availableLengthValue * in.maxLength);
    const auto offset =
        truncateToRepresentableInt(availableLengthValue * in.offset);
    if (!panelLength.has_value() || !offset.has_value()) {
        return std::nullopt;
    }

    std::optional<int> panelStartDelta;

    switch (in.alignment) {
    case PrimaryAxisAlignment::Start:
        panelStartDelta = *offset;
        break;
    case PrimaryAxisAlignment::Center:
        panelStartDelta = truncateToRepresentableInt(
            availableLengthValue * ((1.0F - in.maxLength) / 2.0F)
            + availableLengthValue * in.offset);
        break;
    case PrimaryAxisAlignment::End:
        panelStartDelta = truncateToRepresentableInt(
            availableLengthValue
            - (availableLengthValue * in.maxLength)
            - static_cast<float>(*offset));
        break;
    }

    if (!panelStartDelta.has_value()) {
        return std::nullopt;
    }

    const qint64 panelStartValue =
        qint64(availableStart) + *panelStartDelta;
    if (panelStartValue < std::numeric_limits<int>::lowest()
        || panelStartValue > std::numeric_limits<int>::max()) {
        return std::nullopt;
    }
    const int panelStart = static_cast<int>(panelStartValue);

    return solve({
        .outputGeometry = in.outputGeometry,
        .edge = in.edge,
        .primaryAxisSpan = {panelStart, *panelLength},
        .panelDepth = in.panelDepth,
        .floatingGap = in.floatingGap,
    });
}

} // namespace Latte::ViewPart::FloatingPanelGeometry

#endif
