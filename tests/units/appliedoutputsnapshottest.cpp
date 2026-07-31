/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../../app/view/appliedoutputsnapshot.h"

#include <QObject>
#include <QTest>

using namespace Latte::ViewPart;

class AppliedOutputSnapshotTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void identitySurvivesWithoutLiveScreen();
};

void AppliedOutputSnapshotTest::identitySurvivesWithoutLiveScreen()
{
    const AppliedOutputSnapshot snapshot{
        .identity = {
            .connector = QStringLiteral("DP-2"),
            .screenId = 10,
            .geometry = QRect{1920, -240, 1440, 2560},
        },
        .liveScreen = nullptr,
    };

    QVERIFY(!snapshot.liveScreen);
    QCOMPARE(snapshot.identity.connector,
             QStringLiteral("DP-2"));
    QCOMPARE(snapshot.identity.screenId, 10);
    QCOMPARE(snapshot.identity.geometry,
             QRect(1920, -240, 1440, 2560));
}

QTEST_GUILESS_MAIN(AppliedOutputSnapshotTest)

#include "appliedoutputsnapshottest.moc"
