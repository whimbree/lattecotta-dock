/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../../app/view/floatingpanelgeometry.h"

#include <QRect>
#include <QtTest>

using namespace Latte::ViewPart::FloatingPanelGeometry;

Q_DECLARE_METATYPE(Inputs)
Q_DECLARE_METATYPE(Edge)

class FloatingPanelGeometryTest : public QObject
{
    Q_OBJECT

private:
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
    void triggerOverlapsOneLogicalPixelInward_data();
    void triggerOverlapsOneLogicalPixelInward();
    void rejectsInvalidBoundaryGeometry_data();
    void rejectsInvalidBoundaryGeometry();
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
}

void FloatingPanelGeometryTest::rejectsInvalidBoundaryGeometry()
{
    QFETCH(Inputs, input);
    QVERIFY(!solve(input).has_value());
}

QTEST_MAIN(FloatingPanelGeometryTest)

#include "floatingpanelgeometrytest.moc"
