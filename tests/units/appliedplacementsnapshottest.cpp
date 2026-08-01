/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../../app/view/appliedplacementsnapshot.h"

#include <QObject>
#include <QTest>

using namespace Latte::ViewPart;

class AppliedPlacementSnapshotTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void placementSurvivesWithoutLiveScreen();
};

void AppliedPlacementSnapshotTest::placementSurvivesWithoutLiveScreen()
{
    const AppliedPlacementSnapshot snapshot{
        .output = {
            .connector = QStringLiteral("DP-2"),
            .screenId = 10,
            .geometry = QRect{1920, -240, 1440, 2560},
        },
        .edge = Plasma::Types::LeftEdge,
        .orientation = Plasma::Types::Vertical,
        .alignment = Latte::Types::Bottom,
        .followsPrimary = false,
        .liveScreen = nullptr,
    };

    QVERIFY(!snapshot.liveScreen);
    QCOMPARE(snapshot.output.connector,
             QStringLiteral("DP-2"));
    QCOMPARE(snapshot.output.screenId, 10);
    QCOMPARE(snapshot.output.geometry,
             QRect(1920, -240, 1440, 2560));
    QCOMPARE(snapshot.edge, Plasma::Types::LeftEdge);
    QCOMPARE(snapshot.orientation,
             Plasma::Types::Vertical);
    QCOMPARE(static_cast<int>(snapshot.alignment),
             static_cast<int>(Latte::Types::Bottom));
    QVERIFY(!snapshot.followsPrimary);
}

QTEST_GUILESS_MAIN(AppliedPlacementSnapshotTest)

#include "appliedplacementsnapshottest.moc"
