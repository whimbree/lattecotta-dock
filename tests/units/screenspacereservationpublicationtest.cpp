/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../../app/view/helpers/screenspacereservationpublication.h"

#include <QtTest>

using Latte::ViewPart::ScreenSpaceReservationPublicationState;
using Latte::ViewPart::ScreenSpaceReservationPublicationTarget;

class ScreenSpaceReservationPublicationTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void successfulUpdateCommitsOnce();
    void failedUpdateKeepsCommittedStateAndRetries();
    void failedRemovalKeepsCommittedStateAndRetries();
    void sameGeometryOutputMigrationPublishes();
    void successfulRemovalRequiresRepublish();
};

void ScreenSpaceReservationPublicationTest::
successfulUpdateCommitsOnce()
{
    ScreenSpaceReservationPublicationState state;
    const ScreenSpaceReservationPublicationTarget target{
        QRect(0, 952, 1600, 48),
        10,
        Plasma::Types::BottomEdge};
    int attempts{0};

    const auto publish =
        [&attempts, &target](const auto &candidate) {
            ++attempts;
            return candidate == target;
        };
    QVERIFY(state.update(target, false, publish));
    QVERIFY(state.update(target, false, publish));

    QCOMPARE(attempts, 1);
    QCOMPARE(state.publishedStruts(), target.struts);
    QVERIFY(state.committedTarget());
    QVERIFY(*state.committedTarget() == target);
    QVERIFY(!state.retryRequired());
}

void ScreenSpaceReservationPublicationTest::
failedUpdateKeepsCommittedStateAndRetries()
{
    ScreenSpaceReservationPublicationState state;
    const QRect committed(0, 952, 1600, 48);
    const QRect candidate(0, 936, 1600, 64);
    const ScreenSpaceReservationPublicationTarget
        committedTarget{
            committed,
            10,
            Plasma::Types::BottomEdge};
    const ScreenSpaceReservationPublicationTarget
        candidateTarget{
            candidate,
            10,
            Plasma::Types::BottomEdge};
    QVERIFY(state.update(
        committedTarget,
        false,
        [](const auto &) {
            return true;
        }));

    int attempts{0};
    QVERIFY(!state.update(
        candidateTarget,
        false,
        [&attempts](const auto &) {
            ++attempts;
            return false;
        }));
    QCOMPARE(state.publishedStruts(), committed);
    QVERIFY(state.retryRequired());

    QVERIFY(state.update(
        candidateTarget,
        false,
        [&attempts](const auto &) {
            ++attempts;
            return true;
        }));
    QCOMPARE(attempts, 2);
    QCOMPARE(state.publishedStruts(), candidate);
    QVERIFY(!state.retryRequired());

    QVERIFY(!state.update(
        candidateTarget,
        true,
        [&attempts](const auto &) {
            ++attempts;
            return false;
        }));
    QCOMPARE(state.publishedStruts(), candidate);
    QVERIFY(state.retryRequired());

    QVERIFY(state.update(
        candidateTarget,
        false,
        [&attempts](const auto &) {
            ++attempts;
            return true;
        }));
    QCOMPARE(attempts, 4);
    QCOMPARE(state.publishedStruts(), candidate);
    QVERIFY(!state.retryRequired());
}

void ScreenSpaceReservationPublicationTest::
failedRemovalKeepsCommittedStateAndRetries()
{
    ScreenSpaceReservationPublicationState state;
    const QRect committed(0, 952, 1600, 48);
    const ScreenSpaceReservationPublicationTarget
        committedTarget{
            committed,
            10,
            Plasma::Types::BottomEdge};
    QVERIFY(state.update(
        committedTarget,
        false,
        [](const auto &) {
            return true;
        }));

    int attempts{0};
    QVERIFY(!state.remove(
        [&attempts]() {
            ++attempts;
            return false;
        }));
    QCOMPARE(state.publishedStruts(), committed);
    QVERIFY(state.retryRequired());

    QVERIFY(state.remove(
        [&attempts]() {
            ++attempts;
            return true;
        }));
    QCOMPARE(attempts, 2);
    QCOMPARE(state.publishedStruts(), QRect());
    QVERIFY(!state.retryRequired());
}

void ScreenSpaceReservationPublicationTest::
sameGeometryOutputMigrationPublishes()
{
    ScreenSpaceReservationPublicationState state;
    const QRect geometry(0, 952, 1600, 48);
    const ScreenSpaceReservationPublicationTarget
        primary{
            geometry,
            10,
            Plasma::Types::BottomEdge};
    const ScreenSpaceReservationPublicationTarget
        secondary{
            geometry,
            11,
            Plasma::Types::BottomEdge};
    int attempts{0};

    QVERIFY(state.update(
        primary,
        false,
        [&attempts](const auto &) {
            ++attempts;
            return true;
        }));
    QVERIFY(state.update(
        secondary,
        false,
        [&attempts](const auto &) {
            ++attempts;
            return true;
        }));

    QCOMPARE(attempts, 2);
    QVERIFY(state.committedTarget());
    QCOMPARE(
        state.committedTarget()->outputId,
        secondary.outputId);
    QCOMPARE(
        state.committedTarget()->struts,
        secondary.struts);
}

void ScreenSpaceReservationPublicationTest::
successfulRemovalRequiresRepublish()
{
    ScreenSpaceReservationPublicationState state;
    const ScreenSpaceReservationPublicationTarget target{
        QRect(0, 952, 1600, 48),
        10,
        Plasma::Types::BottomEdge};
    int publications{0};
    const auto publish =
        [&publications](const auto &) {
            ++publications;
            return true;
        };

    QVERIFY(state.update(target, false, publish));
    QVERIFY(state.remove([]() {
        return true;
    }));
    QVERIFY(!state.committedTarget());
    QVERIFY(state.update(target, false, publish));

    QCOMPARE(publications, 2);
    QVERIFY(state.committedTarget());
    QVERIFY(*state.committedTarget() == target);
}

QTEST_GUILESS_MAIN(ScreenSpaceReservationPublicationTest)

#include "screenspacereservationpublicationtest.moc"
