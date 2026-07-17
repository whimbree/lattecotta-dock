/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, transplanted)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Covers the pure logic of the activity-manager helper
// (app/data/activitiesinfo). KActivities 6 removed
// Consumer::runningActivities()/Info::State, so Latte reads the
// running/stopped distinction from org.kde.ActivityManager. The state
// validation (Running == 2; anything unrecognized collapses to Invalid
// instead of being cast across) is the bit that was previously lost, so it
// gets a deterministic table. The live DBus query (list()) needs a session
// activity manager and stays live-verified, not tested here.
//
// Transplanted from latte-dock-qt6 (tests/activitiesinfotest.cpp at
// 81384003, github.com/CaptSilver/latte-dock-qt6) and extended: their exclusion case skips Starting (the state
// most likely to be mistaken for running), their table has no negative or
// first-out-of-range boundary rows, and the empty-input row is new.

#include "data/activitiesinfo.h"
#include "data/activitydata.h"

#include <QObject>
#include <QtTest>

using namespace Latte;

class ActivitiesInfoTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void stateFromManager_data();
    void stateFromManager();
    void runningKeepsOnlyRunning();
    void runningPreservesManagerOrder();
    void runningFromNoRecordsIsEmpty();
};

void ActivitiesInfoTest::stateFromManager_data()
{
    QTest::addColumn<int>("managerState");
    QTest::addColumn<int>("expected");

    // org.kde.ActivityManager wire values (same numbering as the removed
    // KActivities::Info::State): Invalid=0, Unknown=1, Running=2, Starting=3,
    // Stopped=4, Stopping=5. Everything else must collapse to Invalid - a
    // blind enum cast would smuggle future manager states into Latte as
    // nonsense enum values.
    QTest::newRow("invalid")        << 0 << int(Data::Activity::Invalid);
    QTest::newRow("unknown")        << 1 << int(Data::Activity::Invalid);
    QTest::newRow("running")        << 2 << int(Data::Activity::Running);
    QTest::newRow("starting")       << 3 << int(Data::Activity::Starting);
    QTest::newRow("stopped")        << 4 << int(Data::Activity::Stopped);
    QTest::newRow("stopping")       << 5 << int(Data::Activity::Stopping);
    QTest::newRow("first past end") << 6 << int(Data::Activity::Invalid);
    QTest::newRow("negative")       << -1 << int(Data::Activity::Invalid);
    QTest::newRow("garbage")        << 99 << int(Data::Activity::Invalid);
}

void ActivitiesInfoTest::stateFromManager()
{
    QFETCH(int, managerState);
    QFETCH(int, expected);
    QCOMPARE(int(ActivitiesInfo::stateFromManager(managerState)), expected);
}

void ActivitiesInfoTest::runningKeepsOnlyRunning()
{
    // Every non-Running state is present, including Starting: an activity
    // that is still starting must not be cycled to or receive layout
    // assignments yet, so only strict Running passes the filter.
    const QVector<ActivitiesInfo::Record> records = {
        {QStringLiteral("a"), Data::Activity::Running},
        {QStringLiteral("b"), Data::Activity::Stopped},
        {QStringLiteral("c"), Data::Activity::Running},
        {QStringLiteral("d"), Data::Activity::Stopping},
        {QStringLiteral("e"), Data::Activity::Invalid},
        {QStringLiteral("f"), Data::Activity::Starting},
    };

    const QStringList running = ActivitiesInfo::runningActivitiesFrom(records);

    QCOMPARE(running, QStringList({QStringLiteral("a"), QStringLiteral("c")}));
    QVERIFY2(!running.contains(QStringLiteral("b")), "a stopped activity leaked into the running set");
    QVERIFY2(!running.contains(QStringLiteral("f")), "a starting activity leaked into the running set");
}

void ActivitiesInfoTest::runningPreservesManagerOrder()
{
    // Order must follow the manager's list so next/previous cycling is stable
    // (abstractwindowinterface cycles by index into this list).
    const QVector<ActivitiesInfo::Record> records = {
        {QStringLiteral("z"), Data::Activity::Running},
        {QStringLiteral("m"), Data::Activity::Running},
        {QStringLiteral("a"), Data::Activity::Running},
    };

    QCOMPARE(ActivitiesInfo::runningActivitiesFrom(records),
             QStringList({QStringLiteral("z"), QStringLiteral("m"), QStringLiteral("a")}));
}

void ActivitiesInfoTest::runningFromNoRecordsIsEmpty()
{
    // The no-activities session (or a failed DBus reply upstream of the pure
    // filter) yields an empty list, never a crash or an invented entry.
    QCOMPARE(ActivitiesInfo::runningActivitiesFrom({}), QStringList());
}

QTEST_GUILESS_MAIN(ActivitiesInfoTest)

#include "activitiesinfotest.moc"
