/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../../app/view/floatingtransition.h"

#include <QPropertyAnimation>
#include <QSignalSpy>
#include <QtTest>

using Latte::ViewPart::FloatingTransition;
using namespace Latte::ViewPart::FloatingPanelGeometry;

class FloatingTransitionTest : public QObject
{
    Q_OBJECT

private:
    static Inputs geometry()
    {
        return {
            .outputGeometry = QRect(0, 0, 1920, 1080),
            .edge = Edge::Bottom,
            .primaryAxisSpan = {240, 1440},
            .panelDepth = 48,
            .floatingGap = 12,
        };
    }

    static FloatingTransition *makeController(QPropertyAnimation *&animation)
    {
        animation = new QPropertyAnimation;
        auto *controller = new FloatingTransition(nullptr, animation);
        controller->setAnimationDuration(400);
        return controller;
    }

private Q_SLOTS:
    void ownsOneAnimationAndKeepsStableGeometry();
    void reversesFromCurrentQrealValueAtFullDuration();
    void survivesRapidAlternatingTargetsWithoutGeometryReconfigure();
    void zeroDurationSettlesSynchronously();
    void rejectsInvalidConfigurationWithoutReplacingStableState();
};

void FloatingTransitionTest::ownsOneAnimationAndKeepsStableGeometry()
{
    QPropertyAnimation *animation{nullptr};
    QScopedPointer<FloatingTransition> controller(makeController(animation));
    QVERIFY(controller->configureGeometry(geometry()));

    QCOMPARE(controller->findChildren<QPropertyAnimation *>().size(), 1);
    QCOMPARE(controller->stableCanvasGeometry(), QRect(240, 1020, 1440, 60));
    QCOMPARE(controller->attachedGeometry(), QRect(0, 12, 1440, 48));
    QCOMPARE(controller->floatedGeometry(), QRect(0, 0, 1440, 48));
    QCOMPARE(controller->appletMeasurementBounds(), QRect(0, 0, 1440, 48));
    QCOMPARE(controller->primaryAxisStart(), 240);
    QCOMPARE(controller->primaryAxisLength(), 1440);
    QCOMPARE(controller->stableReservationDepth(), 48);
    QCOMPARE(controller->currentVisibleGeometry(), QRectF(0, 12, 1440, 48));
    QCOMPARE(controller->fittsBridgeGeometry(), QRectF(0, 12, 1440, 48));
    QCOMPARE(controller->contentTranslation(), QPointF(0, 12));

    QSignalSpy stableGeometryChanged(controller.data(),
                                     &FloatingTransition::stableGeometryChanged);
    controller->requestFloated();
    animation->setCurrentTime(200);

    QCOMPARE(controller->currentVisibleGeometry(), QRectF(0, 3, 1440, 48));
    QCOMPARE(controller->fittsBridgeGeometry(), QRectF(0, 3, 1440, 57));
    QCOMPARE(controller->stableCanvasGeometry(), QRect(240, 1020, 1440, 60));
    QCOMPARE(controller->appletMeasurementBounds(), QRect(0, 0, 1440, 48));
    QCOMPARE(stableGeometryChanged.count(), 0);
}

void FloatingTransitionTest::reversesFromCurrentQrealValueAtFullDuration()
{
    QPropertyAnimation *animation{nullptr};
    QScopedPointer<FloatingTransition> controller(makeController(animation));

    controller->requestFloated();
    QCOMPARE(controller->target(), FloatingTransition::Target::Floated);
    QCOMPARE(controller->phase(), FloatingTransition::Phase::Floating);
    QCOMPARE(animation->duration(), 400);
    QCOMPARE(animation->easingCurve().type(), QEasingCurve::OutQuad);

    animation->setCurrentTime(160);
    const qreal outwardValue = controller->floatingness();
    QVERIFY(qAbs(outwardValue - 0.64) < 0.0001);

    controller->requestAttached();
    QCOMPARE(controller->target(), FloatingTransition::Target::Attached);
    QCOMPARE(controller->phase(), FloatingTransition::Phase::Attaching);
    QCOMPARE(animation->startValue().toReal(), outwardValue);
    QCOMPARE(animation->endValue().toReal(), 0.0);
    QCOMPARE(animation->duration(), 400);
    QCOMPARE(animation->easingCurve().type(), QEasingCurve::InQuad);

    animation->setCurrentTime(200);
    const qreal inwardValue = controller->floatingness();
    QVERIFY(qAbs(inwardValue - 0.48) < 0.0001);

    controller->requestFloated();
    QCOMPARE(animation->startValue().toReal(), inwardValue);
    QCOMPARE(animation->endValue().toReal(), 1.0);
    QCOMPARE(animation->duration(), 400);
}

void FloatingTransitionTest::survivesRapidAlternatingTargetsWithoutGeometryReconfigure()
{
    QPropertyAnimation *animation{nullptr};
    QScopedPointer<FloatingTransition> controller(makeController(animation));
    QVERIFY(controller->configureGeometry(geometry()));
    const quint64 stableRevision = controller->geometryRevision();
    QSignalSpy stableGeometryChanged(controller.data(),
                                     &FloatingTransition::stableGeometryChanged);

    for (int iteration = 0; iteration < 20; ++iteration) {
        if ((iteration % 2) == 0) {
            controller->requestFloated();
        } else {
            controller->requestAttached();
        }
        animation->setCurrentTime(80 + (iteration % 3) * 40);
        QVERIFY(controller->floatingness() >= 0.0);
        QVERIFY(controller->floatingness() <= 1.0);
        QCOMPARE(animation->duration(), 400);
    }

    QCOMPARE(controller->geometryRevision(), stableRevision);
    QCOMPARE(stableGeometryChanged.count(), 0);
    QCOMPARE(controller->stableCanvasGeometry(), QRect(240, 1020, 1440, 60));
}

void FloatingTransitionTest::zeroDurationSettlesSynchronously()
{
    FloatingTransition controller;
    controller.setAnimationDuration(0);

    controller.requestFloated();
    QCOMPARE(controller.floatingness(), 1.0);
    QCOMPARE(controller.target(), FloatingTransition::Target::Floated);
    QCOMPARE(controller.phase(), FloatingTransition::Phase::Resting);
    QVERIFY(!controller.running());

    controller.requestAttached();
    QCOMPARE(controller.floatingness(), 0.0);
    QCOMPARE(controller.target(), FloatingTransition::Target::Attached);
    QCOMPARE(controller.phase(), FloatingTransition::Phase::Resting);
    QVERIFY(!controller.running());
}

void FloatingTransitionTest::rejectsInvalidConfigurationWithoutReplacingStableState()
{
    FloatingTransition controller;
    QVERIFY(controller.configureGeometry(geometry()));
    const QRect stableCanvas = controller.stableCanvasGeometry();
    const quint64 stableRevision = controller.geometryRevision();

    Inputs invalid = geometry();
    invalid.primaryAxisSpan = {0, 1921};
    QTest::ignoreMessage(
        QtCriticalMsg,
        "FloatingTransition refused invalid stable panel geometry QRect(0,0 1920x1080) 0 1921 48 12");
    QVERIFY(!controller.configureGeometry(invalid));
    QCOMPARE(controller.stableCanvasGeometry(), stableCanvas);
    QCOMPARE(controller.geometryRevision(), stableRevision);
}

QTEST_MAIN(FloatingTransitionTest)

#include "floatingtransitiontest.moc"
