/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../../app/view/floatinginputevent.h"

#include <QPointingDevice>
#include <QtTest>

#include <utility>

using namespace Latte::ViewPart;

Q_DECLARE_METATYPE(FloatingPanelGeometry::Edge)

class FloatingInputEventTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void routesMouseEventsAcrossEveryEdge_data();
    void routesMouseEventsAcrossEveryEdge();
    void preservesWheelMetadataAcrossEveryEdge_data();
    void preservesWheelMetadataAcrossEveryEdge();
    void consumesRasterFringeWithoutObserverDelivery();
};

void FloatingInputEventTest::routesMouseEventsAcrossEveryEdge_data()
{
    QTest::addColumn<FloatingPanelGeometry::Edge>("edge");
    QTest::addColumn<QEvent::Type>("type");

    for (const auto [edgeName, edge] : {
             std::pair{"top", FloatingPanelGeometry::Edge::Top},
             std::pair{"right", FloatingPanelGeometry::Edge::Right},
             std::pair{"bottom", FloatingPanelGeometry::Edge::Bottom},
             std::pair{"left", FloatingPanelGeometry::Edge::Left},
         }) {
        for (const auto [typeName, type] : {
                 std::pair{"move", QEvent::MouseMove},
                 std::pair{"press", QEvent::MouseButtonPress},
                 std::pair{"release", QEvent::MouseButtonRelease},
                 std::pair{"double", QEvent::MouseButtonDblClick},
             }) {
            const QByteArray rowName =
                QByteArray(edgeName) + '-'
                + QByteArray(typeName);
            QTest::newRow(rowName.constData()) << edge << type;
        }
    }
}

void FloatingInputEventTest::routesMouseEventsAcrossEveryEdge()
{
    QFETCH(FloatingPanelGeometry::Edge, edge);
    QFETCH(QEvent::Type, type);

    const FloatingPanelGeometry::Inputs inputs{
        .outputGeometry = QRect(100, 200, 900, 700),
        .edge = edge,
        .primaryAxisSpan =
            FloatingPanelGeometry::isHorizontal(edge)
            ? FloatingPanelGeometry::StablePrimaryAxisSpan{175, 500}
            : FloatingPanelGeometry::StablePrimaryAxisSpan{260, 380},
        .panelDepth = 48,
        .floatingGap = 11,
    };
    const auto solution = FloatingPanelGeometry::solve(inputs);
    QVERIFY(solution.has_value());

    const QRectF visible = solution->visibleMask(0.5).value;
    const QRectF bridge = solution->fittsBridge(0.5).value;
    QPointF gapPosition = bridge.center();
    if (FloatingPanelGeometry::isHorizontal(edge)) {
        gapPosition.setY(edge == FloatingPanelGeometry::Edge::Top
                             ? visible.top() - 1.0
                             : visible.bottom() + 1.0);
    } else {
        gapPosition.setX(edge == FloatingPanelGeometry::Edge::Left
                             ? visible.left() - 1.0
                             : visible.right() + 1.0);
    }
    QCOMPARE(solution->classifyInput(0.5, gapPosition),
             FloatingPanelGeometry::InputDisposition::
                 ProjectToVisibleMask);

    const QPointF projected =
        solution->positionAdjustedForVisibleMask(
            0.5, gapPosition);
    const QPointF projectedGlobal =
        projected + QPointF(1000.25, -300.75);
    const Qt::MouseButton button =
        type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton;
    const QPointingDevice *const device =
        QPointingDevice::primaryPointingDevice();
    QVERIFY(device);
    QMouseEvent source{
        type,
        gapPosition,
        gapPosition + QPointF(5.0, 7.0),
        gapPosition + QPointF(1100.0, 220.0),
        button,
        Qt::LeftButton | Qt::RightButton,
        Qt::ShiftModifier | Qt::AltModifier,
        Qt::MouseEventSynthesizedByApplication,
        device};
    source.setTimestamp(0x12345678);

    auto route = FloatingInputEvent::routeMouseEvent(
        solution->classifyInput(0.5, gapPosition),
        source,
        projected,
        projectedGlobal);
    QVERIFY(!route.consumed);
    QVERIFY(route.projected);
    QCOMPARE(route.projected->type(), type);
    QCOMPARE(route.projected->position(), projected);
    QCOMPARE(route.projected->scenePosition(), projected);
    QCOMPARE(route.projected->globalPosition(), projectedGlobal);
    QCOMPARE(route.projected->button(), button);
    QCOMPARE(route.projected->buttons(),
             Qt::LeftButton | Qt::RightButton);
    QCOMPARE(route.projected->modifiers(),
             Qt::ShiftModifier | Qt::AltModifier);
    QCOMPARE(route.projected->source(),
             Qt::MouseEventSynthesizedByApplication);
    QCOMPARE(route.projected->pointingDevice(),
             device);
    QCOMPARE(route.projected->timestamp(), quint64{0x12345678});

    int observerCount{0};
    if (route.projected) {
        ++observerCount;
    }
    QCOMPARE(observerCount, 1);

    const auto forwarded = FloatingInputEvent::routeMouseEvent(
        FloatingPanelGeometry::InputDisposition::Forward,
        source,
        projected,
        projectedGlobal);
    QVERIFY(!forwarded.consumed);
    QVERIFY(!forwarded.projected);
    ++observerCount;
    QCOMPARE(observerCount, 2);
}

void FloatingInputEventTest::preservesWheelMetadataAcrossEveryEdge_data()
{
    QTest::addColumn<FloatingPanelGeometry::Edge>("edge");
    QTest::newRow("top") << FloatingPanelGeometry::Edge::Top;
    QTest::newRow("right") << FloatingPanelGeometry::Edge::Right;
    QTest::newRow("bottom") << FloatingPanelGeometry::Edge::Bottom;
    QTest::newRow("left") << FloatingPanelGeometry::Edge::Left;
}

void FloatingInputEventTest::preservesWheelMetadataAcrossEveryEdge()
{
    QFETCH(FloatingPanelGeometry::Edge, edge);

    const QPointF original{31.25, 42.75};
    const QPointF projected =
        FloatingPanelGeometry::isHorizontal(edge)
        ? QPointF(31.25, 48.0)
        : QPointF(48.0, 42.75);
    const QPointF projectedGlobal =
        projected + QPointF(400.5, 900.25);
    const QPointingDevice *const device =
        QPointingDevice::primaryPointingDevice();
    QVERIFY(device);
    QWheelEvent source{
        original,
        original + QPointF(800.0, 600.0),
        QPoint(3, -4),
        QPoint(120, -240),
        Qt::MiddleButton,
        Qt::ControlModifier | Qt::MetaModifier,
        Qt::ScrollMomentum,
        true,
        Qt::MouseEventSynthesizedBySystem,
        device};
    source.setTimestamp(0xabcde);

    auto route = FloatingInputEvent::routeWheelEvent(
        FloatingPanelGeometry::InputDisposition::
            ProjectToVisibleMask,
        source,
        projected,
        projectedGlobal);
    QVERIFY(!route.consumed);
    QVERIFY(route.projected);
    QCOMPARE(route.projected->position(), projected);
    QCOMPARE(route.projected->globalPosition(), projectedGlobal);
    QCOMPARE(route.projected->pixelDelta(), QPoint(3, -4));
    QCOMPARE(route.projected->angleDelta(), QPoint(120, -240));
    QCOMPARE(route.projected->buttons(), Qt::MiddleButton);
    QCOMPARE(route.projected->modifiers(),
             Qt::ControlModifier | Qt::MetaModifier);
    QCOMPARE(route.projected->phase(), Qt::ScrollMomentum);
    QVERIFY(route.projected->inverted());
    QCOMPARE(route.projected->source(),
             Qt::MouseEventSynthesizedBySystem);
    QCOMPARE(route.projected->pointingDevice(),
             device);
    QCOMPARE(route.projected->timestamp(), quint64{0xabcde});
}

void FloatingInputEventTest::
    consumesRasterFringeWithoutObserverDelivery()
{
    QMouseEvent source{
        QEvent::MouseMove,
        QPointF(4, 4),
        QPointF(4, 4),
        QPointF(40, 40),
        Qt::NoButton,
        Qt::NoButton,
        Qt::NoModifier};
    const auto route = FloatingInputEvent::routeMouseEvent(
        FloatingPanelGeometry::InputDisposition::
            ConsumeWithoutForwarding,
        source,
        QPointF{},
        QPointF{});
    QVERIFY(route.consumed);
    QVERIFY(!route.projected);
    const int observerCount = route.consumed ? 0 : 1;
    QCOMPARE(observerCount, 0);
}

QTEST_MAIN(FloatingInputEventTest)

#include "floatinginputeventtest.moc"
