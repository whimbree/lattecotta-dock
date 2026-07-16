/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// EX-22 ActivitySetAlgebra (docs/QML_EXTRACTION_PLAN.md). Case list ported
// from capt's activitysetalgebratest (latte-dock-qt6 941bb7fb, 7 slots:
// removeAll duplicate semantics, order preservation, empty cases), re-derived
// against our synchronizer bodies, which are byte-identical to the Qt5
// ancestor (f0ad7b23:app/layouts/synchronizer.cpp:146-186). The two
// consumerPattern slots pin the input/output contracts the switchToLayout
// consumer composes at synchronizer.cpp:789 and :795.

#include <QtTest>

#include "layouts/activitysetalgebra.h"

using namespace Latte::Layouts::ActivitySetAlgebra;

class ActivitySetAlgebraTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void freeActivities_removesAssigned();
    void freeActivities_removeAllDuplicateSemantics();
    void freeActivities_emptyAssignedLeavesUnchanged();
    void freeRunningActivities_filtersAssigned();
    void freeRunningActivities_preservesRunningOrder();
    void validActivities_dropsUnknownIds();
    void validActivities_emptyLayoutGivesEmpty();
    void consumerPattern_freeActivitiesCascade();
    void consumerPattern_foreignMachineActivities();
};

void ActivitySetAlgebraTest::freeActivities_removesAssigned()
{
    // Order of the remaining ids follows allActivities: the switchToLayout
    // consumer takes freeActivities()[0], so order is contract, not detail.
    const QStringList all = {"act-a", "act-b", "act-c", "act-d"};
    const QStringList assigned = {"act-c", "act-a"};

    QCOMPARE(freeActivities(all, assigned), (QStringList{"act-b", "act-d"}));
}

void ActivitySetAlgebraTest::freeActivities_removeAllDuplicateSemantics()
{
    // removeAll semantics: every occurrence of an assigned id goes away;
    // duplicates of a non-assigned id all survive. The live feeder
    // (activities() vs QHash keys) never produces duplicates, but the
    // pure function's contract is the QStringList::removeAll one.
    const QStringList all = {"act-a", "act-b", "act-a", "act-b", "act-c"};
    const QStringList assigned = {"act-b"};

    QCOMPARE(freeActivities(all, assigned), (QStringList{"act-a", "act-a", "act-c"}));
}

void ActivitySetAlgebraTest::freeActivities_emptyAssignedLeavesUnchanged()
{
    const QStringList all = {"act-x", "act-y"};

    QCOMPARE(freeActivities(all, {}), all);
    QVERIFY(freeActivities({}, {"act-x"}).isEmpty());
}

void ActivitySetAlgebraTest::freeRunningActivities_filtersAssigned()
{
    const QStringList running = {"act-a", "act-b", "act-c"};
    const QStringList assigned = {"act-b"};

    QCOMPARE(freeRunningActivities(running, assigned), (QStringList{"act-a", "act-c"}));
}

void ActivitySetAlgebraTest::freeRunningActivities_preservesRunningOrder()
{
    // The consumer prefers freerunningactivities[0] when lastUsedActivity
    // is absent, so preserving the running order is contract.
    const QStringList running = {"act-z", "act-m", "act-a"};

    QCOMPARE(freeRunningActivities(running, {}), running);
    QVERIFY(freeRunningActivities({}, {"act-z"}).isEmpty());
}

void ActivitySetAlgebraTest::validActivities_dropsUnknownIds()
{
    // Result order follows layoutActivities, not allActivities.
    const QStringList layout = {"act-c", "act-unknown", "act-a"};
    const QStringList all = {"act-a", "act-b", "act-c"};

    QCOMPARE(validActivities(layout, all), (QStringList{"act-c", "act-a"}));
}

void ActivitySetAlgebraTest::validActivities_emptyLayoutGivesEmpty()
{
    const QStringList all = {"act-a", "act-b"};

    QVERIFY(validActivities({}, all).isEmpty());
    QVERIFY(validActivities({"act-a"}, {}).isEmpty());
}

void ActivitySetAlgebraTest::consumerPattern_freeActivitiesCascade()
{
    // synchronizer.cpp:795-808, the free-activities layout arm: prefer a
    // free RUNNING activity (lastUsedActivity if still free, else the first),
    // fall back to the first free PAUSED activity. Realistic UUID-shaped ids;
    // assigned comes from QHash keys, so its order carries no meaning.
    const QStringList all = {"8f0a-uuid-1", "9b1c-uuid-2", "a2d3-uuid-3", "b4e5-uuid-4"};
    const QStringList running = {"9b1c-uuid-2", "a2d3-uuid-3"};
    const QStringList assigned = {"9b1c-uuid-2"};

    const QStringList freeRunning = freeRunningActivities(running, assigned);
    QCOMPARE(freeRunning, (QStringList{"a2d3-uuid-3"}));

    // lastUsedActivity free and running -> the consumer picks it.
    QVERIFY(freeRunning.contains("a2d3-uuid-3"));

    // Every running activity assigned -> the consumer falls back to
    // freeActivities()[0], which must be the first unassigned id in
    // activities() order.
    const QStringList allAssigned = {"9b1c-uuid-2", "a2d3-uuid-3"};
    QVERIFY(freeRunningActivities(running, allAssigned).isEmpty());
    QCOMPARE(freeActivities(all, allAssigned).first(), QStringLiteral("8f0a-uuid-1"));
}

void ActivitySetAlgebraTest::consumerPattern_foreignMachineActivities()
{
    // synchronizer.cpp:789: a layout file can carry activity ids from a
    // different machine; validActivities keeps them OUT of switching
    // decisions without touching the stored list. The surviving subset
    // keeps the layout's own order.
    const QStringList layoutActivities = {"foreign-machine-uuid", "local-uuid-2", "local-uuid-1"};
    const QStringList allOnThisMachine = {"local-uuid-1", "local-uuid-2"};

    QCOMPARE(validActivities(layoutActivities, allOnThisMachine),
             (QStringList{"local-uuid-2", "local-uuid-1"}));
}

QTEST_GUILESS_MAIN(ActivitySetAlgebraTest)
#include "activitysetalgebratest.moc"
