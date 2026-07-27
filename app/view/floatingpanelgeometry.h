/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef FLOATINGPANELGEOMETRY_H
#define FLOATINGPANELGEOMETRY_H

#include <QPointF>
#include <QMargins>
#include <QRect>
#include <QRectF>
#include <QtGlobal>

#include <algorithm>
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

enum class InputDisposition {
    ConsumeWithoutForwarding,
    Forward,
    ProjectToVisibleMask,
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

struct PaintMaskRectangle {
    QRect value;
};

struct InputBridgeRectangle {
    QRect value;
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

    [[nodiscard]] PaintMaskRectangle paintMask(qreal floatingness) const
    {
        // QRectF is the geometric truth. Effects consume integer pixels, so
        // rasterize outward to cover every pixel touched by a subpixel shape.
        // QRectF::toRect() rounds and can drop an animated outer row or column.
        return {visibleMask(floatingness).value.toAlignedRect()};
    }

    [[nodiscard]] InputBridgeRectangle inputBridge(qreal floatingness) const
    {
        // Input follows the same outward policy as paint. The primary span
        // stays exact because floating motion is perpendicular to that axis.
        return {fittsBridge(floatingness).value.toAlignedRect()};
    }

    [[nodiscard]] QPointF contentTranslation(qreal floatingness) const
    {
        return visibleMask(floatingness).value.topLeft() - floated.value.topLeft();
    }

    [[nodiscard]] QMargins shadowPaddingOffsets(qreal floatingness) const
    {
        const QRect paint = paintMask(floatingness).value;
        const int rightInset =
            envelope.value.width() - (paint.x() + paint.width());
        const int bottomInset =
            envelope.value.height() - (paint.y() + paint.height());

        // KWindowShadow attaches to the stable QWindow. Negative extra
        // padding moves each tile inward to the outward-rasterized paint
        // mask, keeping the shadow and effects on one pixel authority.
        return {
            -paint.x(),
            -paint.y(),
            -rightInset,
            -bottomInset,
        };
    }

    [[nodiscard]] bool visibleMaskContains(qreal floatingness,
                                           const QPointF &position) const
    {
        const QRectF visible = visibleMask(floatingness).value;
        // Input item bounds are half-open. QRectF::contains includes its
        // right and bottom edges, which belong to the next logical pixel.
        return position.x() >= visible.x()
            && position.x() < visible.x() + visible.width()
            && position.y() >= visible.y()
            && position.y() < visible.y() + visible.height();
    }

    [[nodiscard]] bool fittsBridgeContains(qreal floatingness,
                                           const QPointF &position) const
    {
        const QRectF bridge = fittsBridge(floatingness).value;
        // The bridge follows the same half-open item bounds as the visible
        // mask. An inclusive primary-axis endpoint would leak input into the
        // neighboring logical pixel outside a partial panel span.
        return position.x() >= bridge.x()
            && position.x() < bridge.x() + bridge.width()
            && position.y() >= bridge.y()
            && position.y() < bridge.y() + bridge.height();
    }

    [[nodiscard]] QPointF positionAdjustedForVisibleMask(
        qreal floatingness,
        const QPointF &position) const
    {
        const QRectF visible = visibleMask(floatingness).value;
        return {
            std::clamp(position.x(),
                       visible.x(),
                       visible.x() + visible.width() - 1.0),
            std::clamp(position.y(),
                       visible.y(),
                       visible.y() + visible.height() - 1.0),
        };
    }

    [[nodiscard]] InputDisposition classifyInput(
        qreal floatingness,
        const QPointF &position) const
    {
        if (!fittsBridgeContains(floatingness, position)) {
            return InputDisposition::ConsumeWithoutForwarding;
        }

        return visibleMaskContains(floatingness, position)
            ? InputDisposition::Forward
            : InputDisposition::ProjectToVisibleMask;
    }

    [[nodiscard]] bool screenEdgeBorderVisible(qreal floatingness) const
    {
        Q_ASSERT(floatingness >= 0.0 && floatingness <= 1.0);
        return attached.value != floated.value && floatingness != 0.0;
    }

    [[nodiscard]] bool floatingCornersVisible(qreal floatingness) const
    {
        return screenEdgeBorderVisible(floatingness);
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

struct StableWindowTouchTriggerInputs {
    QRect outputGeometry;
    Edge edge{Edge::Bottom};
    StableEnvelope envelope;
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
    const int outputDepth =
        isHorizontal(in.edge) ? output->height : output->width;

    return in.primaryAxisSpan.start >= outputStart && spanEnd <= outputEnd
        && envelopeDepth <= outputDepth;
}

[[nodiscard]] inline std::optional<TriggerRectangle>
solveStableWindowTouchTrigger(const StableWindowTouchTriggerInputs &in)
{
    const auto output = validateRectangle(in.outputGeometry);
    const auto envelope = validateRectangle(in.envelope.value);
    if (!output || !envelope || !isSupportedEdge(in.edge)
        || !containsRectangle(*output, *envelope)) {
        return std::nullopt;
    }

    //! Plasma tests the complete stable floating envelope after translating
    //! it one logical pixel toward the workspace. This is a translation, not
    //! an expansion of the attached background: the floating gap participates
    //! while the output clip removes the row shifted beyond the opposite edge.
    qint64 horizontalTranslation{0};
    qint64 verticalTranslation{0};
    switch (in.edge) {
    case Edge::Top:
        verticalTranslation = 1;
        break;
    case Edge::Right:
        horizontalTranslation = -1;
        break;
    case Edge::Bottom:
        verticalTranslation = -1;
        break;
    case Edge::Left:
        horizontalTranslation = 1;
        break;
    }

    const qint64 left = std::max(
        qint64(output->left),
        qint64(envelope->left) + horizontalTranslation);
    const qint64 top = std::max(
        qint64(output->top),
        qint64(envelope->top) + verticalTranslation);
    const qint64 right = std::min(
        qint64(output->right),
        qint64(envelope->right) + horizontalTranslation);
    const qint64 bottom = std::min(
        qint64(output->bottom),
        qint64(envelope->bottom) + verticalTranslation);
    const auto narrowedLeft = narrowToRepresentableInt(left);
    const auto narrowedTop = narrowToRepresentableInt(top);
    const auto narrowedRight = narrowToRepresentableInt(right);
    const auto narrowedBottom = narrowToRepresentableInt(bottom);
    if (left > right || top > bottom
        || !narrowedLeft || !narrowedTop
        || !narrowedRight || !narrowedBottom) {
        return std::nullopt;
    }

    QRect trigger;
    trigger.setCoords(
        *narrowedLeft,
        *narrowedTop,
        *narrowedRight,
        *narrowedBottom);
    return TriggerRectangle{trigger};
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

    const auto trigger = solveStableWindowTouchTrigger({
        .outputGeometry = in.outputGeometry,
        .edge = in.edge,
        .envelope = {envelope},
    });
    if (!trigger) {
        return std::nullopt;
    }

    const Solution solution{
        .attached = {attached},
        .floated = {floated},
        .envelope = {envelope},
        .trigger = *trigger,
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
