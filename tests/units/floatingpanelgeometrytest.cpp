/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../../app/view/floatingpanelgeometry.h"

#include <QRect>
#include <QtTest>

#include <limits>

using namespace Latte::ViewPart::FloatingPanelGeometry;

Q_DECLARE_METATYPE(Inputs)
Q_DECLARE_METATYPE(PlacementInputs)
Q_DECLARE_METATYPE(Edge)
Q_DECLARE_METATYPE(PrimaryAxisAlignment)

class FloatingPanelGeometryTest : public QObject
{
    Q_OBJECT

private:
    static QRect rectangleFromCoordinates(int left,
                                          int top,
                                          int right,
                                          int bottom)
    {
        QRect rectangle;
        rectangle.setCoords(left, top, right, bottom);
        return rectangle;
    }

    static Inputs inputs(Edge edge)
    {
        return {
            .outputGeometry = QRect(1920, -200, 1600, 900),
            .edge = edge,
            .primaryAxisSpan = isHorizontal(edge) ? StablePrimaryAxisSpan{2120, 800}
                                                  : StablePrimaryAxisSpan{-100, 500},
            .panelDepth = 48,
            .floatingGap = 12,
        };
    }

private Q_SLOTS:
    void solvesStableEnvelopeForEveryEdge_data();
    void solvesStableEnvelopeForEveryEdge();
    void interpolatesVisibleMaskWithoutChangingStableGeometry();
    void bridgesOnlyTheExactPartialSpan();
    void rasterizesFractionalPresentationOutwardForEveryEdge_data();
    void rasterizesFractionalPresentationOutwardForEveryEdge();
    void derivesAsymmetricShadowOffsetsFromVisibleShape_data();
    void derivesAsymmetricShadowOffsetsFromVisibleShape();
    void fittsBridgeUsesHalfOpenPrimarySpan_data();
    void fittsBridgeUsesHalfOpenPrimarySpan();
    void rejectsOutwardRasterFringeAtFractionalProgress_data();
    void rejectsOutwardRasterFringeAtFractionalProgress();
    void projectsEdgeInputIntoVisibleShape_data();
    void projectsEdgeInputIntoVisibleShape();
    void keepsFloatingBordersAndCornersForEveryNonzeroProgress();
    void triggerOverlapsOneLogicalPixelInward_data();
    void triggerOverlapsOneLogicalPixelInward();
    void solvesExtremeOriginGeometryWithoutIntermediateOverflow_data();
    void solvesExtremeOriginGeometryWithoutIntermediateOverflow();
    void solvesAlignedPartialPlacement_data();
    void solvesAlignedPartialPlacement();
    void fullSpanEndPlacementStaysInsideEveryOutputEdge_data();
    void fullSpanEndPlacementStaysInsideEveryOutputEdge();
    void rejectsInvalidBoundaryGeometry_data();
    void rejectsInvalidBoundaryGeometry();
    void rejectsInvalidPlacement_data();
    void rejectsInvalidPlacement();
};

void FloatingPanelGeometryTest::solvesStableEnvelopeForEveryEdge_data()
{
    QTest::addColumn<Edge>("edge");
    QTest::addColumn<QRect>("envelope");
    QTest::addColumn<QRect>("attached");
    QTest::addColumn<QRect>("floated");

    QTest::newRow("top") << Edge::Top << QRect(2120, -200, 800, 60)
                         << QRect(0, 0, 800, 48) << QRect(0, 12, 800, 48);
    QTest::newRow("right") << Edge::Right << QRect(3460, -100, 60, 500)
                           << QRect(12, 0, 48, 500) << QRect(0, 0, 48, 500);
    QTest::newRow("bottom") << Edge::Bottom << QRect(2120, 640, 800, 60)
                            << QRect(0, 12, 800, 48) << QRect(0, 0, 800, 48);
    QTest::newRow("left") << Edge::Left << QRect(1920, -100, 60, 500)
                          << QRect(0, 0, 48, 500) << QRect(12, 0, 48, 500);
}

void FloatingPanelGeometryTest::solvesStableEnvelopeForEveryEdge()
{
    QFETCH(Edge, edge);
    QFETCH(QRect, envelope);
    QFETCH(QRect, attached);
    QFETCH(QRect, floated);

    const auto solution = solve(inputs(edge));
    QVERIFY(solution.has_value());
    QCOMPARE(solution->envelope.value, envelope);
    QCOMPARE(solution->attached.value, attached);
    QCOMPARE(solution->floated.value, floated);
    QCOMPARE(solution->appletMeasurementBounds.value,
             QRect(QPoint(0, 0), attached.size()));
}

void FloatingPanelGeometryTest::interpolatesVisibleMaskWithoutChangingStableGeometry()
{
    const auto solution = solve(inputs(Edge::Bottom));
    QVERIFY(solution.has_value());
    const QRect stableEnvelope = solution->envelope.value;
    const QRect stableAppletBounds = solution->appletMeasurementBounds.value;
    const StablePrimaryAxisSpan stableSpan = solution->primaryAxisSpan;

    QCOMPARE(solution->visibleMask(0.0).value, QRectF(0, 12, 800, 48));
    QCOMPARE(solution->visibleMask(0.25).value, QRectF(0, 9, 800, 48));
    QCOMPARE(solution->visibleMask(0.5).value, QRectF(0, 6, 800, 48));
    QCOMPARE(solution->visibleMask(1.0).value, QRectF(0, 0, 800, 48));
    QCOMPARE(solution->contentTranslation(0.25), QPointF(0, 9));

    QCOMPARE(solution->envelope.value, stableEnvelope);
    QCOMPARE(solution->appletMeasurementBounds.value, stableAppletBounds);
    QCOMPARE(solution->primaryAxisSpan, stableSpan);
}

void FloatingPanelGeometryTest::bridgesOnlyTheExactPartialSpan()
{
    const auto solution = solve(inputs(Edge::Bottom));
    QVERIFY(solution.has_value());

    QCOMPARE(solution->fittsBridge(0.0).value, QRectF(0, 12, 800, 48));
    QCOMPARE(solution->fittsBridge(0.5).value, QRectF(0, 6, 800, 54));
    QCOMPARE(solution->fittsBridge(1.0).value, QRectF(0, 0, 800, 60));

    QCOMPARE(solution->envelope.value.left(), 2120);
    QCOMPARE(solution->envelope.value.width(), 800);
}

void FloatingPanelGeometryTest::rasterizesFractionalPresentationOutwardForEveryEdge_data()
{
    QTest::addColumn<Edge>("edge");
    QTest::addColumn<qreal>("progress");
    QTest::addColumn<QRectF>("visible");
    QTest::addColumn<QRect>("paint");
    QTest::addColumn<QRect>("input");

    QTest::newRow("top-quarter")
        << Edge::Top << 0.25 << QRectF(0, 2.75, 800, 48)
        << QRect(0, 2, 800, 49) << QRect(0, 0, 800, 51);
    QTest::newRow("top-half")
        << Edge::Top << 0.5 << QRectF(0, 5.5, 800, 48)
        << QRect(0, 5, 800, 49) << QRect(0, 0, 800, 54);
    QTest::newRow("top-three-quarters")
        << Edge::Top << 0.75 << QRectF(0, 8.25, 800, 48)
        << QRect(0, 8, 800, 49) << QRect(0, 0, 800, 57);

    QTest::newRow("right-quarter")
        << Edge::Right << 0.25 << QRectF(8.25, 0, 48, 500)
        << QRect(8, 0, 49, 500) << QRect(8, 0, 51, 500);
    QTest::newRow("right-half")
        << Edge::Right << 0.5 << QRectF(5.5, 0, 48, 500)
        << QRect(5, 0, 49, 500) << QRect(5, 0, 54, 500);
    QTest::newRow("right-three-quarters")
        << Edge::Right << 0.75 << QRectF(2.75, 0, 48, 500)
        << QRect(2, 0, 49, 500) << QRect(2, 0, 57, 500);

    QTest::newRow("bottom-quarter")
        << Edge::Bottom << 0.25 << QRectF(0, 8.25, 800, 48)
        << QRect(0, 8, 800, 49) << QRect(0, 8, 800, 51);
    QTest::newRow("bottom-half")
        << Edge::Bottom << 0.5 << QRectF(0, 5.5, 800, 48)
        << QRect(0, 5, 800, 49) << QRect(0, 5, 800, 54);
    QTest::newRow("bottom-three-quarters")
        << Edge::Bottom << 0.75 << QRectF(0, 2.75, 800, 48)
        << QRect(0, 2, 800, 49) << QRect(0, 2, 800, 57);

    QTest::newRow("left-quarter")
        << Edge::Left << 0.25 << QRectF(2.75, 0, 48, 500)
        << QRect(2, 0, 49, 500) << QRect(0, 0, 51, 500);
    QTest::newRow("left-half")
        << Edge::Left << 0.5 << QRectF(5.5, 0, 48, 500)
        << QRect(5, 0, 49, 500) << QRect(0, 0, 54, 500);
    QTest::newRow("left-three-quarters")
        << Edge::Left << 0.75 << QRectF(8.25, 0, 48, 500)
        << QRect(8, 0, 49, 500) << QRect(0, 0, 57, 500);
}

void FloatingPanelGeometryTest::rasterizesFractionalPresentationOutwardForEveryEdge()
{
    QFETCH(Edge, edge);
    QFETCH(qreal, progress);
    QFETCH(QRectF, visible);
    QFETCH(QRect, paint);
    QFETCH(QRect, input);

    Inputs in = inputs(edge);
    in.floatingGap = 11;
    const auto solution = solve(in);
    QVERIFY(solution.has_value());

    QCOMPARE(solution->visibleMask(progress).value, visible);
    QCOMPARE(solution->paintMask(progress).value, paint);
    QCOMPARE(solution->inputBridge(progress).value, input);

    const QRect stableCanvas{QPoint(0, 0), solution->envelope.value.size()};
    const QMargins baseTilePadding{3, 5, 7, 11};
    const QMargins appliedTilePadding =
        baseTilePadding + solution->shadowPaddingOffsets(progress);
    const QMargins recoveredOffsets = appliedTilePadding - baseTilePadding;
    QCOMPARE(stableCanvas.adjusted(-recoveredOffsets.left(),
                                   -recoveredOffsets.top(),
                                   recoveredOffsets.right(),
                                   recoveredOffsets.bottom()),
             paint);
}

void FloatingPanelGeometryTest::derivesAsymmetricShadowOffsetsFromVisibleShape_data()
{
    QTest::addColumn<Edge>("edge");
    QTest::addColumn<qreal>("progress");
    QTest::addColumn<QMargins>("offsets");

    QTest::newRow("top-quarter") << Edge::Top << 0.25
                                  << QMargins(0, -2, 0, -8);
    QTest::newRow("right-quarter") << Edge::Right << 0.25
                                    << QMargins(-8, 0, -2, 0);
    QTest::newRow("bottom-quarter") << Edge::Bottom << 0.25
                                     << QMargins(0, -8, 0, -2);
    QTest::newRow("left-quarter") << Edge::Left << 0.25
                                   << QMargins(-2, 0, -8, 0);
    QTest::newRow("top-half") << Edge::Top << 0.5
                               << QMargins(0, -5, 0, -5);
    QTest::newRow("right-three-quarters") << Edge::Right << 0.75
                                           << QMargins(-2, 0, -8, 0);
}

void FloatingPanelGeometryTest::derivesAsymmetricShadowOffsetsFromVisibleShape()
{
    QFETCH(Edge, edge);
    QFETCH(qreal, progress);
    QFETCH(QMargins, offsets);

    Inputs in = inputs(edge);
    in.floatingGap = 11;
    const auto solution = solve(in);
    QVERIFY(solution.has_value());

    QCOMPARE(solution->shadowPaddingOffsets(progress), offsets);
}

void FloatingPanelGeometryTest::fittsBridgeUsesHalfOpenPrimarySpan_data()
{
    QTest::addColumn<Edge>("edge");
    QTest::addColumn<QPointF>("beforeStart");
    QTest::addColumn<QPointF>("atStart");
    QTest::addColumn<QPointF>("beforeEnd");
    QTest::addColumn<QPointF>("atEnd");

    constexpr qreal epsilon = 0.01;
    QTest::newRow("top")
        << Edge::Top << QPointF(-epsilon, 20) << QPointF(0, 20)
        << QPointF(800 - epsilon, 20) << QPointF(800, 20);
    QTest::newRow("right")
        << Edge::Right << QPointF(20, -epsilon) << QPointF(20, 0)
        << QPointF(20, 500 - epsilon) << QPointF(20, 500);
    QTest::newRow("bottom")
        << Edge::Bottom << QPointF(-epsilon, 20) << QPointF(0, 20)
        << QPointF(800 - epsilon, 20) << QPointF(800, 20);
    QTest::newRow("left")
        << Edge::Left << QPointF(20, -epsilon) << QPointF(20, 0)
        << QPointF(20, 500 - epsilon) << QPointF(20, 500);
}

void FloatingPanelGeometryTest::fittsBridgeUsesHalfOpenPrimarySpan()
{
    QFETCH(Edge, edge);
    QFETCH(QPointF, beforeStart);
    QFETCH(QPointF, atStart);
    QFETCH(QPointF, beforeEnd);
    QFETCH(QPointF, atEnd);

    const auto solution = solve(inputs(edge));
    QVERIFY(solution.has_value());

    QVERIFY(!solution->fittsBridgeContains(0.5, beforeStart));
    QVERIFY(solution->fittsBridgeContains(0.5, atStart));
    QVERIFY(solution->fittsBridgeContains(0.5, beforeEnd));
    QVERIFY(!solution->fittsBridgeContains(0.5, atEnd));
}

void FloatingPanelGeometryTest::
    rejectsOutwardRasterFringeAtFractionalProgress_data()
{
    QTest::addColumn<Edge>("edge");
    QTest::newRow("top") << Edge::Top;
    QTest::newRow("right") << Edge::Right;
    QTest::newRow("bottom") << Edge::Bottom;
    QTest::newRow("left") << Edge::Left;
}

void FloatingPanelGeometryTest::rejectsOutwardRasterFringeAtFractionalProgress()
{
    QFETCH(Edge, edge);

    Inputs in = inputs(edge);
    in.floatingGap = 11;
    const auto solution = solve(in);
    QVERIFY(solution.has_value());

    constexpr qreal progress{0.25};
    constexpr qreal epsilon{0.01};
    const QRectF visible =
        solution->visibleMask(progress).value;
    const QRectF bridge =
        solution->fittsBridge(progress).value;
    QPointF insideBridge = visible.center();
    QPointF atBridgeEnd = bridge.center();
    QPointF insideRasterFringe = visible.center();
    if (edge == Edge::Top || edge == Edge::Left) {
        if (isHorizontal(edge)) {
            insideBridge.setY(visible.bottom() - epsilon);
            insideRasterFringe.setY(
                solution->paintMask(progress).value.bottom()
                + 1.0 - epsilon);
            atBridgeEnd.setY(bridge.bottom());
        } else {
            insideBridge.setX(visible.right() - epsilon);
            insideRasterFringe.setX(
                solution->paintMask(progress).value.right()
                + 1.0 - epsilon);
            atBridgeEnd.setX(bridge.right());
        }
    } else if (isHorizontal(edge)) {
        insideBridge.setY(visible.top() + epsilon);
        insideRasterFringe.setY(
            solution->paintMask(progress).value.top()
            + epsilon);
        atBridgeEnd.setY(bridge.bottom());
    } else {
        insideBridge.setX(visible.left() + epsilon);
        insideRasterFringe.setX(
            solution->paintMask(progress).value.left()
            + epsilon);
        atBridgeEnd.setX(bridge.right());
    }

    QCOMPARE(solution->classifyInput(0.25, insideBridge),
             InputDisposition::Forward);
    QCOMPARE(solution->classifyInput(0.25, atBridgeEnd),
             InputDisposition::ConsumeWithoutForwarding);
    QCOMPARE(solution->classifyInput(0.25, insideRasterFringe),
             InputDisposition::ConsumeWithoutForwarding);
    QVERIFY(QRectF(solution->inputBridge(progress).value).contains(
        insideRasterFringe));
}

void FloatingPanelGeometryTest::projectsEdgeInputIntoVisibleShape_data()
{
    QTest::addColumn<Edge>("edge");
    QTest::addColumn<QPointF>("edgePoint");
    QTest::addColumn<QPointF>("adjustedPoint");

    QTest::newRow("top")
        << Edge::Top << QPointF(127.25, 0) << QPointF(127.25, 5.5);
    QTest::newRow("right")
        << Edge::Right << QPointF(58.5, 127.25) << QPointF(52.5, 127.25);
    QTest::newRow("bottom")
        << Edge::Bottom << QPointF(127.25, 58.5) << QPointF(127.25, 52.5);
    QTest::newRow("left")
        << Edge::Left << QPointF(0, 127.25) << QPointF(5.5, 127.25);
}

void FloatingPanelGeometryTest::projectsEdgeInputIntoVisibleShape()
{
    QFETCH(Edge, edge);
    QFETCH(QPointF, edgePoint);
    QFETCH(QPointF, adjustedPoint);

    Inputs in = inputs(edge);
    in.floatingGap = 11;
    const auto solution = solve(in);
    QVERIFY(solution.has_value());

    QVERIFY(solution->fittsBridgeContains(0.5, edgePoint));
    QVERIFY(!solution->visibleMaskContains(0.5, edgePoint));
    QCOMPARE(solution->classifyInput(0.5, edgePoint),
             InputDisposition::ProjectToVisibleMask);
    QCOMPARE(solution->positionAdjustedForVisibleMask(0.5, edgePoint),
             adjustedPoint);
    QVERIFY(solution->visibleMaskContains(0.5, adjustedPoint));

    const QPointF alreadyVisible = solution->visibleMask(0.5).value.center();
    QCOMPARE(solution->positionAdjustedForVisibleMask(0.5, alreadyVisible),
             alreadyVisible);
}

void FloatingPanelGeometryTest::keepsFloatingBordersAndCornersForEveryNonzeroProgress()
{
    const auto solution = solve(inputs(Edge::Bottom));
    QVERIFY(solution.has_value());

    QVERIFY(!solution->screenEdgeBorderVisible(0.0));
    QVERIFY(!solution->floatingCornersVisible(0.0));
    QVERIFY(solution->screenEdgeBorderVisible(
        std::numeric_limits<qreal>::denorm_min()));
    QVERIFY(solution->floatingCornersVisible(
        std::numeric_limits<qreal>::denorm_min()));
    QVERIFY(solution->screenEdgeBorderVisible(0.25));
    QVERIFY(solution->floatingCornersVisible(0.25));
    QVERIFY(solution->screenEdgeBorderVisible(1.0));
    QVERIFY(solution->floatingCornersVisible(1.0));

    Inputs flushInputs = inputs(Edge::Bottom);
    flushInputs.floatingGap = 0;
    const auto flushSolution = solve(flushInputs);
    QVERIFY(flushSolution.has_value());
    QCOMPARE(flushSolution->attached.value, flushSolution->floated.value);
    QVERIFY(!flushSolution->screenEdgeBorderVisible(1.0));
    QVERIFY(!flushSolution->floatingCornersVisible(1.0));
}

void FloatingPanelGeometryTest::triggerOverlapsOneLogicalPixelInward_data()
{
    QTest::addColumn<Edge>("edge");
    QTest::addColumn<QRect>("trigger");

    QTest::newRow("top") << Edge::Top << QRect(2120, -200, 800, 49);
    QTest::newRow("right") << Edge::Right << QRect(3471, -100, 49, 500);
    QTest::newRow("bottom") << Edge::Bottom << QRect(2120, 651, 800, 49);
    QTest::newRow("left") << Edge::Left << QRect(1920, -100, 49, 500);
}

void FloatingPanelGeometryTest::triggerOverlapsOneLogicalPixelInward()
{
    QFETCH(Edge, edge);
    QFETCH(QRect, trigger);

    const auto solution = solve(inputs(edge));
    QVERIFY(solution.has_value());
    QCOMPARE(solution->trigger.value, trigger);
}

void FloatingPanelGeometryTest::
    solvesExtremeOriginGeometryWithoutIntermediateOverflow_data()
{
    QTest::addColumn<Inputs>("input");
    QTest::addColumn<QRect>("envelope");

    constexpr int lowest = std::numeric_limits<int>::lowest();
    constexpr int highest = std::numeric_limits<int>::max();

    QTest::newRow("right edge at the minimum x coordinate")
        << Inputs{
               .outputGeometry =
                   rectangleFromCoordinates(lowest, 0, lowest + 1, 9),
               .edge = Edge::Right,
               .primaryAxisSpan = {0, 10},
               .panelDepth = 1,
               .floatingGap = 1,
           }
        << rectangleFromCoordinates(lowest, 0, lowest + 1, 9);
    QTest::newRow("bottom edge at the minimum y coordinate")
        << Inputs{
               .outputGeometry =
                   rectangleFromCoordinates(0, lowest, 9, lowest + 1),
               .edge = Edge::Bottom,
               .primaryAxisSpan = {0, 10},
               .panelDepth = 1,
               .floatingGap = 1,
           }
        << rectangleFromCoordinates(0, lowest, 9, lowest + 1);
    QTest::newRow("top edge at the maximum y coordinate")
        << Inputs{
               .outputGeometry =
                   rectangleFromCoordinates(0, highest - 1, 9, highest),
               .edge = Edge::Top,
               .primaryAxisSpan = {0, 10},
               .panelDepth = 1,
               .floatingGap = 1,
           }
        << rectangleFromCoordinates(0, highest - 1, 9, highest);
    QTest::newRow("left edge at the maximum x coordinate")
        << Inputs{
               .outputGeometry =
                   rectangleFromCoordinates(highest - 1, 0, highest, 9),
               .edge = Edge::Left,
               .primaryAxisSpan = {0, 10},
               .panelDepth = 1,
               .floatingGap = 1,
           }
        << rectangleFromCoordinates(highest - 1, 0, highest, 9);
}

void FloatingPanelGeometryTest::
    solvesExtremeOriginGeometryWithoutIntermediateOverflow()
{
    QFETCH(Inputs, input);
    QFETCH(QRect, envelope);

    const auto solution = solve(input);
    QVERIFY(solution.has_value());
    QCOMPARE(solution->envelope.value, envelope);
}

void FloatingPanelGeometryTest::solvesAlignedPartialPlacement_data()
{
    QTest::addColumn<PrimaryAxisAlignment>("alignment");
    QTest::addColumn<float>("offset");
    QTest::addColumn<int>("expectedStart");

    QTest::newRow("start") << PrimaryAxisAlignment::Start << 0.1F << 2080;
    QTest::newRow("center") << PrimaryAxisAlignment::Center << 0.0F << 2079;
    QTest::newRow("end") << PrimaryAxisAlignment::End << 0.1F << 2080;
}

void FloatingPanelGeometryTest::solvesAlignedPartialPlacement()
{
    QFETCH(PrimaryAxisAlignment, alignment);
    QFETCH(float, offset);
    QFETCH(int, expectedStart);

    const auto solution = solvePlacement({
        .outputGeometry = QRect(1920, -200, 1600, 900),
        .availablePrimaryGeometry = QRect(2000, -100, 800, 500),
        .edge = Edge::Bottom,
        .alignment = alignment,
        .maxLength = 0.8F,
        .offset = offset,
        .panelDepth = 48,
        .floatingGap = 12,
    });

    QVERIFY(solution.has_value());
    QCOMPARE(solution->primaryAxisSpan,
             (StablePrimaryAxisSpan{expectedStart, 640}));
    QCOMPARE(solution->envelope.value,
             QRect(expectedStart, 640, 640, 60));
}

void FloatingPanelGeometryTest::fullSpanEndPlacementStaysInsideEveryOutputEdge_data()
{
    QTest::addColumn<Edge>("edge");

    QTest::newRow("top") << Edge::Top;
    QTest::newRow("right") << Edge::Right;
    QTest::newRow("bottom") << Edge::Bottom;
    QTest::newRow("left") << Edge::Left;
}

void FloatingPanelGeometryTest::fullSpanEndPlacementStaysInsideEveryOutputEdge()
{
    QFETCH(Edge, edge);

    const QRect output(1920, -200, 1600, 900);
    const auto solution = solvePlacement({
        .outputGeometry = output,
        .availablePrimaryGeometry = output,
        .edge = edge,
        .alignment = PrimaryAxisAlignment::End,
        .maxLength = 1.0F,
        .offset = 0.0F,
        .panelDepth = 48,
        .floatingGap = 12,
    });

    QVERIFY(solution.has_value());
    QVERIFY(output.contains(solution->envelope.value));
}

void FloatingPanelGeometryTest::rejectsInvalidBoundaryGeometry_data()
{
    QTest::addColumn<Inputs>("input");

    Inputs invalidOutput = inputs(Edge::Bottom);
    invalidOutput.outputGeometry = {};
    QTest::newRow("invalid output") << invalidOutput;

    Inputs zeroDepth = inputs(Edge::Bottom);
    zeroDepth.panelDepth = 0;
    QTest::newRow("zero depth") << zeroDepth;

    Inputs negativeGap = inputs(Edge::Bottom);
    negativeGap.floatingGap = -1;
    QTest::newRow("negative gap") << negativeGap;

    Inputs spanBeforeOutput = inputs(Edge::Bottom);
    spanBeforeOutput.primaryAxisSpan.start = 1919;
    QTest::newRow("span before output") << spanBeforeOutput;

    Inputs spanAfterOutput = inputs(Edge::Bottom);
    spanAfterOutput.primaryAxisSpan = {3400, 121};
    QTest::newRow("span after output") << spanAfterOutput;

    Inputs envelopeTooDeep = inputs(Edge::Left);
    envelopeTooDeep.panelDepth = 1590;
    envelopeTooDeep.floatingGap = 11;
    QTest::newRow("envelope too deep") << envelopeTooDeep;

    Inputs triggerOutsideOutput = inputs(Edge::Top);
    triggerOutsideOutput.panelDepth =
        triggerOutsideOutput.outputGeometry.height();
    triggerOutsideOutput.floatingGap = 0;
    QTest::newRow("mandatory trigger pixel outside output")
        << triggerOutsideOutput;

    Inputs overflowingEnvelope = inputs(Edge::Bottom);
    overflowingEnvelope.panelDepth = std::numeric_limits<int>::max();
    overflowingEnvelope.floatingGap = 1;
    QTest::newRow("envelope depth integer overflow") << overflowingEnvelope;

    Inputs unsupportedEdge = inputs(Edge::Bottom);
    unsupportedEdge.edge = static_cast<Edge>(std::numeric_limits<int>::max());
    QTest::newRow("unsupported edge") << unsupportedEdge;

    Inputs unrepresentableOutputSpan = inputs(Edge::Bottom);
    unrepresentableOutputSpan.outputGeometry = rectangleFromCoordinates(
        std::numeric_limits<int>::lowest(),
        0,
        std::numeric_limits<int>::max(),
        99);
    QTest::newRow("output rectangle span exceeds integer range")
        << unrepresentableOutputSpan;
}

void FloatingPanelGeometryTest::rejectsInvalidBoundaryGeometry()
{
    QFETCH(Inputs, input);
    QVERIFY(!solve(input).has_value());
}

void FloatingPanelGeometryTest::rejectsInvalidPlacement_data()
{
    QTest::addColumn<PlacementInputs>("input");

    const PlacementInputs valid{
        .outputGeometry = QRect(1920, -200, 1600, 900),
        .availablePrimaryGeometry = QRect(2000, -100, 800, 500),
        .edge = Edge::Bottom,
        .alignment = PrimaryAxisAlignment::Center,
        .maxLength = 0.8F,
        .offset = 0.0F,
        .panelDepth = 48,
        .floatingGap = 12,
    };

    PlacementInputs unavailable = valid;
    unavailable.availablePrimaryGeometry = {};
    QTest::newRow("invalid available primary geometry") << unavailable;

    PlacementInputs outsideOutput = valid;
    outsideOutput.availablePrimaryGeometry.moveLeft(1900);
    QTest::newRow("available primary span outside output") << outsideOutput;

    PlacementInputs zeroLength = valid;
    zeroLength.maxLength = 0.0F;
    QTest::newRow("zero maximum length") << zeroLength;

    PlacementInputs oversize = valid;
    oversize.maxLength = 1.1F;
    QTest::newRow("oversize maximum length") << oversize;

    PlacementInputs offsetOutside = valid;
    offsetOutside.offset = 1.2F;
    QTest::newRow("derived span outside output") << offsetOutside;

    PlacementInputs unsupportedEdge = valid;
    unsupportedEdge.edge =
        static_cast<Edge>(std::numeric_limits<int>::max());
    QTest::newRow("unsupported edge") << unsupportedEdge;

    PlacementInputs unsupportedAlignment = valid;
    unsupportedAlignment.alignment =
        static_cast<PrimaryAxisAlignment>(std::numeric_limits<int>::max());
    QTest::newRow("unsupported alignment") << unsupportedAlignment;

    PlacementInputs unrepresentableOutputSpan = valid;
    unrepresentableOutputSpan.outputGeometry = rectangleFromCoordinates(
        std::numeric_limits<int>::lowest(),
        0,
        std::numeric_limits<int>::max(),
        99);
    QTest::newRow("output rectangle span exceeds integer range")
        << unrepresentableOutputSpan;

    PlacementInputs unrepresentableAvailableSpan = valid;
    unrepresentableAvailableSpan.availablePrimaryGeometry =
        rectangleFromCoordinates(std::numeric_limits<int>::lowest(),
                                 0,
                                 std::numeric_limits<int>::max(),
                                 99);
    QTest::newRow("available rectangle span exceeds integer range")
        << unrepresentableAvailableSpan;

    PlacementInputs startAdditionOverflow = valid;
    startAdditionOverflow.alignment = PrimaryAxisAlignment::Start;
    startAdditionOverflow.offset =
        static_cast<float>(std::numeric_limits<int>::max() / 800);
    QTest::newRow("representable delta overflows panel start")
        << startAdditionOverflow;

    PlacementInputs roundedMaximumLength = valid;
    roundedMaximumLength.outputGeometry =
        QRect(0, 0, std::numeric_limits<int>::max(), 100);
    roundedMaximumLength.availablePrimaryGeometry =
        roundedMaximumLength.outputGeometry;
    roundedMaximumLength.maxLength = 1.0F;
    QTest::newRow("maximum available length rounds beyond integer range")
        << roundedMaximumLength;

    PlacementInputs centerExpressionOverflow = valid;
    centerExpressionOverflow.outputGeometry = QRect(0, 0, 1024, 100);
    centerExpressionOverflow.availablePrimaryGeometry =
        centerExpressionOverflow.outputGeometry;
    centerExpressionOverflow.alignment = PrimaryAxisAlignment::Center;
    centerExpressionOverflow.maxLength = 0.5F;
    centerExpressionOverflow.offset = 2097151.75F;
    QTest::newRow("center expression exceeds integer range after valid offset")
        << centerExpressionOverflow;

    PlacementInputs endExpressionOverflow = centerExpressionOverflow;
    endExpressionOverflow.alignment = PrimaryAxisAlignment::End;
    endExpressionOverflow.offset = -2097151.75F;
    QTest::newRow("end expression exceeds integer range after valid offset")
        << endExpressionOverflow;

    for (const auto alignment : {
             PrimaryAxisAlignment::Start,
             PrimaryAxisAlignment::Center,
             PrimaryAxisAlignment::End}) {
        PlacementInputs hugePositiveOffset = valid;
        hugePositiveOffset.alignment = alignment;
        hugePositiveOffset.offset = std::numeric_limits<float>::max();
        QTest::addRow("huge positive finite offset %d",
                      static_cast<int>(alignment))
            << hugePositiveOffset;

        PlacementInputs hugeNegativeOffset = valid;
        hugeNegativeOffset.alignment = alignment;
        hugeNegativeOffset.offset = std::numeric_limits<float>::lowest();
        QTest::addRow("huge negative finite offset %d",
                      static_cast<int>(alignment))
            << hugeNegativeOffset;
    }
}

void FloatingPanelGeometryTest::rejectsInvalidPlacement()
{
    QFETCH(PlacementInputs, input);
    QVERIFY(!solvePlacement(input).has_value());
}

QTEST_MAIN(FloatingPanelGeometryTest)

#include "floatingpanelgeometrytest.moc"
