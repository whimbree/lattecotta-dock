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
        controller->reconcileTargetPolicy(
            true, true, false, false, 1, false);
        controller->setAnimationDuration(400);
        return controller;
    }

private Q_SLOTS:
    void ownsOneAnimationAndKeepsStableGeometry();
    void reversesFromCurrentQrealValueAtFullDuration();
    void survivesRapidAlternatingTargetsWithoutGeometryReconfigure();
    void zeroDurationSettlesSynchronously();
    void canonicalizesNearEndpointsExactly();
    void togglesPopupHintFromTargetAndPreservesUnknownBits();
    void ineligibleControllerRemainsFloated();
    void preservesDockMaximizedGapPolicy();
    void defersOnlyANewAttachmentUntilPointerExit();
    void reconcilesEveryPolicyInputAtomically();
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
    controller->reconcileTargetPolicy(
        true, true, false, false, 0, false);
    animation->setCurrentTime(200);

    QCOMPARE(controller->currentVisibleGeometry(), QRectF(0, 1.5, 1440, 48));
    QCOMPARE(controller->fittsBridgeGeometry(), QRectF(0, 1.5, 1440, 58.5));
    QCOMPARE(controller->stableCanvasGeometry(), QRect(240, 1020, 1440, 60));
    QCOMPARE(controller->appletMeasurementBounds(), QRect(0, 0, 1440, 48));
    QCOMPARE(stableGeometryChanged.count(), 0);
}

void FloatingTransitionTest::reversesFromCurrentQrealValueAtFullDuration()
{
    QPropertyAnimation *animation{nullptr};
    QScopedPointer<FloatingTransition> controller(makeController(animation));

    controller->reconcileTargetPolicy(
        true, true, false, false, 0, false);
    QCOMPARE(controller->target(), FloatingTransition::Target::Floated);
    QCOMPARE(controller->phase(), FloatingTransition::Phase::Floating);
    QCOMPARE(animation->duration(), 400);
    QCOMPARE(animation->easingCurve().type(), QEasingCurve::OutCubic);

    animation->setCurrentTime(160);
    const qreal outwardValue = controller->floatingness();
    QVERIFY(qAbs(outwardValue - 0.784) < 0.0001);

    controller->reconcileTargetPolicy(
        true, true, false, false, 1, false);
    QCOMPARE(controller->target(), FloatingTransition::Target::Attached);
    QCOMPARE(controller->phase(), FloatingTransition::Phase::Attaching);
    QCOMPARE(animation->startValue().toReal(), outwardValue);
    QCOMPARE(animation->endValue().toReal(), 0.0);
    QCOMPARE(animation->duration(), 400);
    QCOMPARE(animation->easingCurve().type(), QEasingCurve::InCubic);

    animation->setCurrentTime(200);
    const qreal inwardValue = controller->floatingness();
    QVERIFY(qAbs(inwardValue - 0.686) < 0.0001);

    controller->reconcileTargetPolicy(
        true, true, false, false, 0, false);
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
            controller->reconcileTargetPolicy(
                true, true, false, false, 0, false);
        } else {
            controller->reconcileTargetPolicy(
                true, true, false, false, 1, false);
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
    controller.reconcileTargetPolicy(
        true, true, false, false, 0, false);

    controller.reconcileTargetPolicy(
        true, true, false, false, 1, false);
    QCOMPARE(controller.floatingness(), 0.0);
    controller.reconcileTargetPolicy(
        true, true, false, false, 0, false);
    QCOMPARE(controller.floatingness(), 1.0);
    QCOMPARE(controller.target(), FloatingTransition::Target::Floated);
    QCOMPARE(controller.phase(), FloatingTransition::Phase::Resting);
    QVERIFY(!controller.running());

    controller.reconcileTargetPolicy(
        true, true, false, false, 1, false);
    QCOMPARE(controller.floatingness(), 0.0);
    QCOMPARE(controller.target(), FloatingTransition::Target::Attached);
    QCOMPARE(controller.phase(), FloatingTransition::Phase::Resting);
    QVERIFY(!controller.running());
}

void FloatingTransitionTest::canonicalizesNearEndpointsExactly()
{
    FloatingTransition controller;
    controller.setAnimationDuration(0);
    controller.reconcileTargetPolicy(
        true, true, false, false, 0, false);

    QVERIFY(controller.setProperty("floatingness", 1e-13));
    QSignalSpy attachedGeometryChanged(
        &controller, &FloatingTransition::currentGeometryChanged);
    controller.reconcileTargetPolicy(
        true, true, false, false, 1, false);
    QCOMPARE(controller.floatingness(), 0.0);
    QCOMPARE(attachedGeometryChanged.count(), 1);

    QVERIFY(controller.setProperty("floatingness", 1.0 - 1e-13));
    QSignalSpy floatedGeometryChanged(
        &controller, &FloatingTransition::currentGeometryChanged);
    controller.reconcileTargetPolicy(
        true, true, false, false, 0, false);
    QCOMPARE(controller.floatingness(), 1.0);
    QCOMPARE(floatedGeometryChanged.count(), 1);
}

void FloatingTransitionTest::
    togglesPopupHintFromTargetAndPreservesUnknownBits()
{
    QPropertyAnimation *animation{nullptr};
    QScopedPointer<FloatingTransition> controller(
        makeController(animation));
    constexpr int unknownBits{0b1010'0000};
    constexpr int floatingBit{0b0000'0100};

    // makeController targets Attached while floatingness is still at 1.
    // The hint follows the target immediately, not the current progress.
    QCOMPARE(
        controller->displayHintsWithFloatingPreference(
            unknownBits | floatingBit,
            floatingBit,
            true),
        unknownBits);

    controller->reconcileTargetPolicy(
        true, true, false, false, 0, false);
    QCOMPARE(
        controller->displayHintsWithFloatingPreference(
            unknownBits,
            floatingBit,
            true),
        unknownBits | floatingBit);

    controller->reconcileTargetPolicy(
        true, true, false, false, 1, false);
    QCOMPARE(
        controller->displayHintsWithFloatingPreference(
            unknownBits | floatingBit,
            floatingBit,
            true),
        unknownBits);
    QCOMPARE(
        controller->displayHintsWithFloatingPreference(
            unknownBits | floatingBit,
            floatingBit,
            false),
        unknownBits);
}

void FloatingTransitionTest::ineligibleControllerRemainsFloated()
{
    FloatingTransition controller;
    controller.setAnimationDuration(0);

    QCOMPARE(controller.floatingness(), 1.0);
    QCOMPARE(controller.target(), FloatingTransition::Target::Floated);
    controller.reconcileTargetPolicy(
        false, true, false, false, 1, false);
    QCOMPARE(controller.floatingness(), 1.0);
    QCOMPARE(controller.target(), FloatingTransition::Target::Floated);

    controller.reconcileTargetPolicy(
        true, true, false, false, 1, false);
    QCOMPARE(controller.floatingness(), 0.0);
    QCOMPARE(controller.target(), FloatingTransition::Target::Attached);

    controller.reconcileTargetPolicy(
        false, true, false, false, 1, false);
    QCOMPARE(controller.floatingness(), 1.0);
    QCOMPARE(controller.target(), FloatingTransition::Target::Floated);
}

void FloatingTransitionTest::preservesDockMaximizedGapPolicy()
{
    FloatingTransition controller;
    controller.setAnimationDuration(0);

    controller.reconcileTargetPolicy(
        false, true, true, true, 0, false);
    QCOMPARE(controller.target(), FloatingTransition::Target::Floated);
    QVERIFY(!controller.dockGapHideRequested());
    QVERIFY(!controller.attachmentDeferredByPointer());

    controller.reconcileTargetPolicy(
        false, true, true, true, 0, true);
    QCOMPARE(controller.target(), FloatingTransition::Target::Floated);
    QVERIFY(controller.dockGapHideRequested());
    QVERIFY(!controller.attachmentDeferredByPointer());

    controller.reconcileTargetPolicy(
        false, true, true, true, 0, false);
    QCOMPARE(controller.target(), FloatingTransition::Target::Floated);

    QTest::ignoreMessage(
        QtCriticalMsg,
        "FloatingTransition refused an inconsistent Dock gap-hide request");
    controller.reconcileTargetPolicy(
        true, true, false, false, 1, true);
    QCOMPARE(controller.target(), FloatingTransition::Target::Floated);
    QVERIFY(!controller.floatingPanelEligible());
    QVERIFY(!controller.dockGapHideRequested());
}

void FloatingTransitionTest::defersOnlyANewAttachmentUntilPointerExit()
{
    FloatingTransition controller;
    controller.setAnimationDuration(0);

    controller.reconcileTargetPolicy(
        true, true, true, false, 1, false);
    QCOMPARE(controller.target(), FloatingTransition::Target::Attached);
    QVERIFY(!controller.attachmentDeferredByPointer());

    controller.reconcileTargetPolicy(
        true, true, true, true, 1, false);
    QCOMPARE(controller.target(), FloatingTransition::Target::Attached);
    QVERIFY(controller.pointerInsideView());
    QVERIFY(!controller.attachmentDeferredByPointer());

    controller.reconcileTargetPolicy(
        true, true, true, true, 0, false);
    QCOMPARE(controller.target(), FloatingTransition::Target::Floated);
    QVERIFY(!controller.attachmentDeferredByPointer());

    controller.reconcileTargetPolicy(
        true, true, true, true, 1, false);
    QCOMPARE(controller.target(), FloatingTransition::Target::Floated);
    QVERIFY(controller.attachmentDeferredByPointer());

    controller.reconcileTargetPolicy(
        true, true, true, false, 1, false);
    QCOMPARE(controller.target(), FloatingTransition::Target::Attached);
    QVERIFY(!controller.pointerInsideView());
    QVERIFY(!controller.attachmentDeferredByPointer());
}

void FloatingTransitionTest::reconcilesEveryPolicyInputAtomically()
{
    FloatingTransition controller;
    controller.setAnimationDuration(0);

    int observedSignals{0};
    const auto verifyCompletePolicy = [&controller, &observedSignals]() {
        ++observedSignals;
        const bool shouldAttach =
            controller.floatingPanelEligible()
            && controller.attachOnWindowTouchConfigured()
            && !controller.attachmentDeferredByPointer()
            && controller.touchingWindowCount() > 0;
        QVERIFY(!controller.attachmentDeferredByPointer()
                || (controller.attachmentWaitsForPointerExitConfigured()
                    && controller.pointerInsideView()));
        QCOMPARE(
            controller.target(),
            shouldAttach ? FloatingTransition::Target::Attached
                         : FloatingTransition::Target::Floated);
    };
    connect(&controller, &FloatingTransition::floatingPanelEligibleChanged,
            &controller, verifyCompletePolicy);
    connect(&controller,
            &FloatingTransition::attachOnWindowTouchConfiguredChanged,
            &controller, verifyCompletePolicy);
    connect(
        &controller,
        &FloatingTransition::
            attachmentWaitsForPointerExitConfiguredChanged,
        &controller,
        verifyCompletePolicy);
    connect(&controller, &FloatingTransition::pointerInsideViewChanged,
            &controller, verifyCompletePolicy);
    connect(&controller,
            &FloatingTransition::attachmentDeferredByPointerChanged,
            &controller, verifyCompletePolicy);
    connect(&controller, &FloatingTransition::dockGapHideRequestedChanged,
            &controller, verifyCompletePolicy);
    connect(&controller, &FloatingTransition::touchingWindowCountChanged,
            &controller, verifyCompletePolicy);
    connect(&controller, &FloatingTransition::targetChanged,
            &controller, verifyCompletePolicy);

    for (const bool eligible : {false, true}) {
        for (const bool configured : {false, true}) {
            for (const bool waitsForPointerExit : {false, true}) {
                for (const bool pointerInside : {false, true}) {
                    for (const int count : {0, 2}) {
                        controller.reconcileTargetPolicy(
                            eligible,
                            configured,
                            waitsForPointerExit,
                            pointerInside,
                            count, false);
                        QCOMPARE(
                            controller
                                .attachmentWaitsForPointerExitConfigured(),
                            waitsForPointerExit);
                        QCOMPARE(
                            controller.pointerInsideView(),
                            pointerInside);
                        verifyCompletePolicy();
                    }
                }
            }
        }
    }

    QVERIFY(observedSignals > 0);

    const auto previousTarget = controller.target();
    const bool previousEligible = controller.floatingPanelEligible();
    const bool previousConfigured =
        controller.attachOnWindowTouchConfigured();
    const bool previousWaitConfigured =
        controller.attachmentWaitsForPointerExitConfigured();
    const bool previousPointerInside =
        controller.pointerInsideView();
    const bool previousDeferred =
        controller.attachmentDeferredByPointer();
    const int previousCount = controller.touchingWindowCount();
    QTest::ignoreMessage(
        QtCriticalMsg,
        "FloatingTransition refused a negative touching-window count: -1");
    controller.reconcileTargetPolicy(
        true, true, false, false, -1, false);
    QCOMPARE(controller.target(), previousTarget);
    QCOMPARE(controller.floatingPanelEligible(), previousEligible);
    QCOMPARE(
        controller.attachOnWindowTouchConfigured(), previousConfigured);
    QCOMPARE(
        controller.attachmentWaitsForPointerExitConfigured(),
        previousWaitConfigured);
    QCOMPARE(controller.pointerInsideView(), previousPointerInside);
    QCOMPARE(
        controller.attachmentDeferredByPointer(), previousDeferred);
    QCOMPARE(controller.touchingWindowCount(), previousCount);
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
