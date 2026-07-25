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

struct RectangleMetrics {
    int left{0};
    int top{0};
    int right{0};
    int bottom{0};
    int width{0};
    int height{0};
};

[[nodiscard]] constexpr bool containsRectangle(
    const RectangleMetrics &outer,
    const RectangleMetrics &inner)
{
    return inner.left >= outer.left && inner.top >= outer.top
        && inner.right <= outer.right && inner.bottom <= outer.bottom;
}

[[nodiscard]] constexpr bool isSupportedEdge(Edge edge)
{
    switch (edge) {
    case Edge::Top:
    case Edge::Right:
    case Edge::Bottom:
    case Edge::Left:
        return true;
    }

    return false;
}

[[nodiscard]] constexpr bool isSupportedAlignment(PrimaryAxisAlignment alignment)
{
    switch (alignment) {
    case PrimaryAxisAlignment::Start:
    case PrimaryAxisAlignment::Center:
    case PrimaryAxisAlignment::End:
        return true;
    }

    return false;
}

[[nodiscard]] constexpr bool isHorizontal(Edge edge)
{
    return edge == Edge::Top || edge == Edge::Bottom;
}

[[nodiscard]] inline std::optional<RectangleMetrics> validateRectangle(
    const QRect &rectangle)
{
    const int left = rectangle.left();
    const int top = rectangle.top();
    const int right = rectangle.right();
    const int bottom = rectangle.bottom();
    const qint64 width = qint64(right) - left + 1;
    const qint64 height = qint64(bottom) - top + 1;

    // QRect can store endpoint pairs whose inclusive span exceeds int even
    // though isValid() is true. Prove the span before width() or height()
    // reaches Qt's checked integer arithmetic.
    if (width <= 0 || width > std::numeric_limits<int>::max()
        || height <= 0 || height > std::numeric_limits<int>::max()) {
        return std::nullopt;
    }

    return RectangleMetrics{
        .left = left,
        .top = top,
        .right = right,
        .bottom = bottom,
        .width = static_cast<int>(width),
        .height = static_cast<int>(height),
    };
}

[[nodiscard]] constexpr std::optional<int> narrowToRepresentableInt(qint64 value)
{
    if (value < std::numeric_limits<int>::lowest()
        || value > std::numeric_limits<int>::max()) {
        return std::nullopt;
    }

    return static_cast<int>(value);
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
    const auto output = validateRectangle(in.outputGeometry);
    if (!output.has_value() || !isSupportedEdge(in.edge) || in.panelDepth <= 0
        || in.floatingGap < 0 || in.primaryAxisSpan.length <= 0) {
        return false;
    }

    const int outputStart =
        isHorizontal(in.edge) ? output->left : output->top;
    const int outputLength =
        isHorizontal(in.edge) ? output->width : output->height;
    const qint64 spanEnd = qint64(in.primaryAxisSpan.start) + in.primaryAxisSpan.length;
    const qint64 outputEnd = qint64(outputStart) + outputLength;
    const qint64 envelopeDepth = qint64(in.panelDepth) + in.floatingGap;
    const qint64 triggerDepth = qint64(in.panelDepth) + 1;
    const int outputDepth =
        isHorizontal(in.edge) ? output->height : output->width;

    return in.primaryAxisSpan.start >= outputStart && spanEnd <= outputEnd
        && envelopeDepth <= outputDepth && triggerDepth <= outputDepth;
}

[[nodiscard]] inline std::optional<Solution> solve(const Inputs &in)
{
    if (!hasValidGeometry(in)) {
        return std::nullopt;
    }

    const RectangleMetrics output = *validateRectangle(in.outputGeometry);
    const int envelopeDepth = in.panelDepth + in.floatingGap;
    const auto primaryAxisEnd = narrowToRepresentableInt(
        qint64(in.primaryAxisSpan.start) + in.primaryAxisSpan.length - 1);
    if (!primaryAxisEnd.has_value()) {
        return std::nullopt;
    }

    QRect envelope;
    QRect attached;
    QRect floated;

    switch (in.edge) {
    case Edge::Top: {
        const auto envelopeBottom = narrowToRepresentableInt(
            qint64(output.top) + envelopeDepth - 1);
        if (!envelopeBottom.has_value()) {
            return std::nullopt;
        }
        envelope.setCoords(in.primaryAxisSpan.start,
                           output.top,
                           *primaryAxisEnd,
                           *envelopeBottom);
        attached = {0, 0, in.primaryAxisSpan.length, in.panelDepth};
        floated = {0, in.floatingGap, in.primaryAxisSpan.length, in.panelDepth};
        break;
    }
    case Edge::Right: {
        const auto envelopeLeft = narrowToRepresentableInt(
            qint64(output.right) - envelopeDepth + 1);
        if (!envelopeLeft.has_value()) {
            return std::nullopt;
        }
        envelope.setCoords(*envelopeLeft,
                           in.primaryAxisSpan.start,
                           output.right,
                           *primaryAxisEnd);
        attached = {in.floatingGap, 0, in.panelDepth, in.primaryAxisSpan.length};
        floated = {0, 0, in.panelDepth, in.primaryAxisSpan.length};
        break;
    }
    case Edge::Bottom: {
        const auto envelopeTop = narrowToRepresentableInt(
            qint64(output.bottom) - envelopeDepth + 1);
        if (!envelopeTop.has_value()) {
            return std::nullopt;
        }
        envelope.setCoords(in.primaryAxisSpan.start,
                           *envelopeTop,
                           *primaryAxisEnd,
                           output.bottom);
        attached = {0, in.floatingGap, in.primaryAxisSpan.length, in.panelDepth};
        floated = {0, 0, in.primaryAxisSpan.length, in.panelDepth};
        break;
    }
    case Edge::Left: {
        const auto envelopeRight = narrowToRepresentableInt(
            qint64(output.left) + envelopeDepth - 1);
        if (!envelopeRight.has_value()) {
            return std::nullopt;
        }
        envelope.setCoords(output.left,
                           in.primaryAxisSpan.start,
                           *envelopeRight,
                           *primaryAxisEnd);
        attached = {0, 0, in.panelDepth, in.primaryAxisSpan.length};
        floated = {in.floatingGap, 0, in.panelDepth, in.primaryAxisSpan.length};
        break;
    }
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

    const auto envelopeMetrics = validateRectangle(solution.envelope.value);
    const auto attachedMetrics = validateRectangle(solution.attached.value);
    const auto floatedMetrics = validateRectangle(solution.floated.value);
    const auto triggerMetrics = validateRectangle(solution.trigger.value);
    Q_ASSERT(envelopeMetrics.has_value());
    Q_ASSERT(attachedMetrics.has_value());
    Q_ASSERT(floatedMetrics.has_value());
    Q_ASSERT(triggerMetrics.has_value());
    Q_ASSERT(containsRectangle(output, *envelopeMetrics));
    Q_ASSERT(attachedMetrics->left >= 0 && attachedMetrics->top >= 0
             && attachedMetrics->right < envelopeMetrics->width
             && attachedMetrics->bottom < envelopeMetrics->height);
    Q_ASSERT(floatedMetrics->left >= 0 && floatedMetrics->top >= 0
             && floatedMetrics->right < envelopeMetrics->width
             && floatedMetrics->bottom < envelopeMetrics->height);
    Q_ASSERT(containsRectangle(output, *triggerMetrics));

    return solution;
}

[[nodiscard]] inline std::optional<Solution> solvePlacement(const PlacementInputs &in)
{
    const auto output = validateRectangle(in.outputGeometry);
    const auto available = validateRectangle(in.availablePrimaryGeometry);
    if (!output.has_value() || !available.has_value()
        || !isSupportedEdge(in.edge) || !isSupportedAlignment(in.alignment)
        || !qIsFinite(in.maxLength) || in.maxLength <= 0.0F
        || in.maxLength > 1.0F || !qIsFinite(in.offset)) {
        return std::nullopt;
    }

    const int outputStart =
        isHorizontal(in.edge) ? output->left : output->top;
    const int outputLength =
        isHorizontal(in.edge) ? output->width : output->height;
    const int availableStart =
        isHorizontal(in.edge) ? available->left : available->top;
    const int availableLength =
        isHorizontal(in.edge) ? available->width : available->height;
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

    const auto panelStart =
        narrowToRepresentableInt(qint64(availableStart) + *panelStartDelta);
    if (!panelStart.has_value()) {
        return std::nullopt;
    }

    return solve({
        .outputGeometry = in.outputGeometry,
        .edge = in.edge,
        .primaryAxisSpan = {*panelStart, *panelLength},
        .panelDepth = in.panelDepth,
        .floatingGap = in.floatingGap,
    });
}

} // namespace Latte::ViewPart::FloatingPanelGeometry

#endif
