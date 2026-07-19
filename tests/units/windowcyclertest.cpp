/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Behavioral test for the WindowCycler pure core (EX-16 in
// docs/tracking/QML_EXTRACTION_PLAN.md) and its QML-facing WindowCyclerTools
// wrapper (compiled in, per the sanitized-core rule). Expectations are
// hand-derived from the f0ad7b23 QML bodies the spec names (SubWindows.qml
// activateNextTask/activatePreviousTask/minimizeTask, the containment
// loader's and tools.js's activateNextPrevTask) and cross-checked against
// the shipped QML on every run by tests/qml/tst_windowcycler.qml and
// tst_taskcycle.qml - the twin-equivalence pair of this table.

#include "units/windowcycler.h"
#include "windowcyclertools.h"

#include <QRegularExpression>
#include <QtTest>

// C++
#include <optional>

using namespace Latte;

Q_DECLARE_METATYPE(std::optional<int>)
Q_DECLARE_METATYPE(std::optional<QString>)

namespace {

//! compact window-list builder: one token per window, id plus flags -
//! "a" plain, "b*" active, "c-" minimized, "d*-" both
QList<WindowCycler::GroupWindow> makeWindows(const QStringList &tokens)
{
    QList<WindowCycler::GroupWindow> windows;
    for (QString token : tokens) {
        WindowCycler::GroupWindow window;
        window.isActive = token.contains(QLatin1Char('*'));
        window.isMinimized = token.contains(QLatin1Char('-'));
        token.remove(QLatin1Char('*'));
        token.remove(QLatin1Char('-'));
        window.winId = token;
        windows.append(window);
    }
    return windows;
}

}

class WindowCyclerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void selectNext_data();
    void selectNext();
    void selectPrevious_data();
    void selectPrevious();
    void selectMinimizeTarget_data();
    void selectMinimizeTarget();

    void flattenExpandsGroupsAndDropsLaunchers();
    void flattenChildlessGroupContributesNothing();
    void flattenEmptyBar();

    void selectAdjacentTask_data();
    void selectAdjacentTask();

    void wrapperMapsNoneToMinusOne();
    void wrapperTreatsMinusOneLastActiveAsNone();
    void wrapperStringifiesNumericWinIds();
    void wrapperRefusesMalformedWindowEntry();
    void wrapperRefusesMalformedTaskEntry();
    void wrapperRefusesOutOfRangeActiveIndex();
    void wrapperFlattenChildRowBoundary();
};

void WindowCyclerTest::selectNext_data()
{
    QTest::addColumn<QStringList>("windows");
    QTest::addColumn<std::optional<QString>>("lastActive");
    QTest::addColumn<std::optional<int>>("expected");

    const std::optional<QString> noLast;

    QTest::newRow("active mid-list -> the window after it")
        << QStringList{"a*", "b", "c"} << noLast << std::optional<int>(1);
    QTest::newRow("active at the end wraps to the first")
        << QStringList{"a", "b", "c*"} << noLast << std::optional<int>(0);
    QTest::newRow("single active window wraps to itself")
        << QStringList{"a*"} << noLast << std::optional<int>(0);
    QTest::newRow("several actives: the first one decides (forward scan)")
        << QStringList{"a", "b*", "c*", "d"} << noLast << std::optional<int>(2);
    QTest::newRow("no active -> the last-active window")
        << QStringList{"a", "b", "c"} << std::optional<QString>("b") << std::optional<int>(1);
    QTest::newRow("no active, stale last-active -> the first window")
        << QStringList{"a", "b"} << std::optional<QString>("gone") << std::optional<int>(0);
    QTest::newRow("no active, no last-active -> the first window")
        << QStringList{"a", "b"} << noLast << std::optional<int>(0);
    QTest::newRow("last-active is ignored while a window is active")
        << QStringList{"a", "b*", "c"} << std::optional<QString>("a") << std::optional<int>(2);
    QTest::newRow("minimized windows still count as cycle stops")
        << QStringList{"a*", "b-", "c"} << noLast << std::optional<int>(1);
    QTest::newRow("empty list -> nothing to act on")
        << QStringList{} << noLast << std::optional<int>();
}

void WindowCyclerTest::selectNext()
{
    QFETCH(QStringList, windows);
    QFETCH(std::optional<QString>, lastActive);
    QFETCH(std::optional<int>, expected);

    QCOMPARE(WindowCycler::selectNext(makeWindows(windows), lastActive), expected);
}

void WindowCyclerTest::selectPrevious_data()
{
    QTest::addColumn<QStringList>("windows");
    QTest::addColumn<std::optional<QString>>("lastActive");
    QTest::addColumn<std::optional<int>>("expected");

    const std::optional<QString> noLast;

    QTest::newRow("active mid-list -> the window before it")
        << QStringList{"a", "b*", "c"} << noLast << std::optional<int>(0);
    QTest::newRow("active at the start wraps to the last")
        << QStringList{"a*", "b", "c"} << noLast << std::optional<int>(2);
    QTest::newRow("single active window wraps to itself")
        << QStringList{"a*"} << noLast << std::optional<int>(0);
    QTest::newRow("several actives: the last one decides (backward scan)")
        << QStringList{"a*", "b", "c*"} << noLast << std::optional<int>(1);
    QTest::newRow("no active -> the last-active window (last match wins)")
        << QStringList{"a", "b", "c"} << std::optional<QString>("c") << std::optional<int>(2);
    QTest::newRow("no active, stale last-active -> the first window")
        << QStringList{"a", "b"} << std::optional<QString>("gone") << std::optional<int>(0);
    QTest::newRow("no active, no last-active -> the first window")
        << QStringList{"a", "b"} << noLast << std::optional<int>(0);
    QTest::newRow("empty list -> nothing to act on")
        << QStringList{} << noLast << std::optional<int>();
}

void WindowCyclerTest::selectPrevious()
{
    QFETCH(QStringList, windows);
    QFETCH(std::optional<QString>, lastActive);
    QFETCH(std::optional<int>, expected);

    QCOMPARE(WindowCycler::selectPrevious(makeWindows(windows), lastActive), expected);
}

void WindowCyclerTest::selectMinimizeTarget_data()
{
    QTest::addColumn<QStringList>("windows");
    QTest::addColumn<std::optional<QString>>("lastActive");
    QTest::addColumn<std::optional<int>>("expected");

    const std::optional<QString> noLast;

    QTest::newRow("the active window is the toggle target")
        << QStringList{"a", "b*", "c"} << noLast << std::optional<int>(1);
    QTest::newRow("several actives: the last one decides")
        << QStringList{"a*", "b*", "c"} << noLast << std::optional<int>(1);
    QTest::newRow("no active -> the shown last-active window")
        << QStringList{"a", "b", "c"} << std::optional<QString>("b") << std::optional<int>(1);
    QTest::newRow("a minimized last-active match falls through the ladder")
        << QStringList{"a", "b-", "c"} << std::optional<QString>("b") << std::optional<int>(2);
    QTest::newRow("no active, no last-active -> the last shown window")
        << QStringList{"a", "b", "c-"} << noLast << std::optional<int>(1);
    QTest::newRow("every window minimized -> nothing to toggle")
        << QStringList{"a-", "b-"} << noLast << std::optional<int>();
    QTest::newRow("empty list -> nothing to toggle")
        << QStringList{} << noLast << std::optional<int>();
}

void WindowCyclerTest::selectMinimizeTarget()
{
    QFETCH(QStringList, windows);
    QFETCH(std::optional<QString>, lastActive);
    QFETCH(std::optional<int>, expected);

    QCOMPARE(WindowCycler::selectMinimizeTarget(makeWindows(windows), lastActive), expected);
}

void WindowCyclerTest::flattenExpandsGroupsAndDropsLaunchers()
{
    // launcher, plain, group of 3, startup, plain
    const QList<WindowCycler::TaskEntry> entries = {
        {true, false, false, 0},
        {false, false, false, 0},
        {false, false, true, 3},
        {false, true, false, 0},
        {false, false, false, 0},
    };

    const QList<WindowCycler::TaskPosition> expected = {
        {1, std::nullopt},
        {2, 0}, {2, 1}, {2, 2},
        {4, std::nullopt},
    };

    QCOMPARE(WindowCycler::flattenTasksForCycling(entries), expected);
}

void WindowCyclerTest::flattenChildlessGroupContributesNothing()
{
    // a group parent whose windows just closed can report zero rows
    const QList<WindowCycler::TaskEntry> entries = {
        {false, false, true, 0},
        {false, false, false, 0},
    };

    const QList<WindowCycler::TaskPosition> expected = {
        {1, std::nullopt},
    };

    QCOMPARE(WindowCycler::flattenTasksForCycling(entries), expected);
}

void WindowCyclerTest::flattenEmptyBar()
{
    QVERIFY(WindowCycler::flattenTasksForCycling({}).isEmpty());
}

void WindowCyclerTest::selectAdjacentTask_data()
{
    QTest::addColumn<int>("count");
    QTest::addColumn<std::optional<int>>("active");
    QTest::addColumn<bool>("next");
    QTest::addColumn<std::optional<int>>("expected");

    const std::optional<int> noActive;

    QTest::newRow("next mid-list") << 3 << std::optional<int>(1) << true << std::optional<int>(2);
    QTest::newRow("next at the end wraps to the first") << 3 << std::optional<int>(2) << true << std::optional<int>(0);
    QTest::newRow("previous mid-list") << 3 << std::optional<int>(1) << false << std::optional<int>(0);
    QTest::newRow("previous at the start wraps to the last") << 3 << std::optional<int>(0) << false << std::optional<int>(2);
    QTest::newRow("no active stop -> the first (next)") << 3 << noActive << true << std::optional<int>(0);
    QTest::newRow("no active stop -> the first (previous)") << 3 << noActive << false << std::optional<int>(0);
    QTest::newRow("single stop wraps to itself (next)") << 1 << std::optional<int>(0) << true << std::optional<int>(0);
    QTest::newRow("single stop wraps to itself (previous)") << 1 << std::optional<int>(0) << false << std::optional<int>(0);
    QTest::newRow("empty list -> nothing to act on") << 0 << noActive << true << std::optional<int>();
}

void WindowCyclerTest::selectAdjacentTask()
{
    QFETCH(int, count);
    QFETCH(std::optional<int>, active);
    QFETCH(bool, next);
    QFETCH(std::optional<int>, expected);

    const auto direction = next ? WindowCycler::CycleDirection::Next
                                : WindowCycler::CycleDirection::Previous;
    QCOMPARE(WindowCycler::selectAdjacentTask(count, active, direction), expected);
}

// --- the QML boundary (WindowCyclerTools, compiled in sanitized) --------

namespace {

QVariantMap windowEntry(const QVariant &winId, bool isActive, bool isMinimized)
{
    return QVariantMap{
        {QStringLiteral("winId"), winId},
        {QStringLiteral("isActive"), isActive},
        {QStringLiteral("isMinimized"), isMinimized},
    };
}

}

void WindowCyclerTest::wrapperMapsNoneToMinusOne()
{
    const WindowCyclerTools tools;

    QCOMPARE(tools.selectNext({}, QVariant(-1)), -1);
    QCOMPARE(tools.selectPrevious({}, QVariant(-1)), -1);
    QCOMPARE(tools.selectMinimizeTarget(
                 {windowEntry(QStringLiteral("a"), false, true)}, QVariant(-1)),
             -1);
    QCOMPARE(tools.selectAdjacentTask(0, -1, true), -1);
}

void WindowCyclerTest::wrapperTreatsMinusOneLastActiveAsNone()
{
    const WindowCyclerTools tools;
    const QVariantList windows{windowEntry(QStringLiteral("a"), false, false),
                               windowEntry(QStringLiteral("b"), false, false)};

    // "-1" must be the none-sentinel however QML spells it, never an id
    QCOMPARE(tools.selectNext(windows, QVariant(-1)), 0);
    QCOMPARE(tools.selectNext(windows, QVariant(-1.0)), 0);
    QCOMPARE(tools.selectNext(windows, QVariant(QStringLiteral("-1"))), 0);
    QCOMPARE(tools.selectNext(windows, QVariant()), 0);
    // while a real id still matches
    QCOMPARE(tools.selectNext(windows, QVariant(QStringLiteral("b"))), 1);
}

void WindowCyclerTest::wrapperStringifiesNumericWinIds()
{
    const WindowCyclerTools tools;
    // X11 hands numeric window ids; they compare as decimal strings
    // (8e8cdf31 - the windowinfowrap family)
    const QVariantList windows{windowEntry(QVariant(41943051), false, false),
                               windowEntry(QVariant(41943052), false, false)};

    QCOMPARE(tools.selectNext(windows, QVariant(41943052)), 1);
    QCOMPARE(tools.selectPrevious(windows, QVariant(QStringLiteral("41943051"))), 0);
}

void WindowCyclerTest::wrapperRefusesMalformedWindowEntry()
{
    const WindowCyclerTools tools;
    const QVariantList missingKey{QVariantMap{{QStringLiteral("winId"), QStringLiteral("a")}}};

    QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(QStringLiteral("malformed window entry")));
    QCOMPARE(tools.selectNext(missingKey, QVariant(-1)), -1);

    QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(QStringLiteral("malformed window entry")));
    QCOMPARE(tools.selectPrevious({QVariant(QStringLiteral("not a map"))}, QVariant(-1)), -1);

    QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(QStringLiteral("malformed window entry")));
    QCOMPARE(tools.selectMinimizeTarget(missingKey, QVariant(-1)), -1);
}

void WindowCyclerTest::wrapperRefusesMalformedTaskEntry()
{
    const WindowCyclerTools tools;

    QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(QStringLiteral("malformed task entry")));
    QVERIFY(tools.flattenTasksForCycling(
                {QVariantMap{{QStringLiteral("isLauncher"), false}}}).isEmpty());

    // a negative child count is refused at the boundary, never asserted in
    // release: this input arrives from QML
    QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(QStringLiteral("malformed task entry")));
    QVERIFY(tools.flattenTasksForCycling(
                {QVariantMap{{QStringLiteral("isLauncher"), false},
                             {QStringLiteral("isStartup"), false},
                             {QStringLiteral("isGroupParent"), true},
                             {QStringLiteral("childCount"), -2}}}).isEmpty());
}

void WindowCyclerTest::wrapperRefusesOutOfRangeActiveIndex()
{
    const WindowCyclerTools tools;

    QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(QStringLiteral("out-of-range input")));
    QCOMPARE(tools.selectAdjacentTask(3, 3, true), -1);

    QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(QStringLiteral("out-of-range input")));
    QCOMPARE(tools.selectAdjacentTask(3, -2, true), -1);

    QTest::ignoreMessage(QtCriticalMsg, QRegularExpression(QStringLiteral("out-of-range input")));
    QCOMPARE(tools.selectAdjacentTask(-1, -1, true), -1);
}

void WindowCyclerTest::wrapperFlattenChildRowBoundary()
{
    const WindowCyclerTools tools;
    const QVariantList entries{
        QVariantMap{{QStringLiteral("isLauncher"), false},
                    {QStringLiteral("isStartup"), false},
                    {QStringLiteral("isGroupParent"), false},
                    {QStringLiteral("childCount"), 0}},
        QVariantMap{{QStringLiteral("isLauncher"), false},
                    {QStringLiteral("isStartup"), false},
                    {QStringLiteral("isGroupParent"), true},
                    {QStringLiteral("childCount"), 2}},
    };

    const QVariantList positions = tools.flattenTasksForCycling(entries);
    QCOMPARE(positions.size(), 3);

    // a plain task's stop carries the -1 boundary convention...
    QCOMPARE(positions[0].toMap().value(QStringLiteral("row")).toInt(), 0);
    QCOMPARE(positions[0].toMap().value(QStringLiteral("childRow")).toInt(), -1);
    // ...and a group child carries its real child row
    QCOMPARE(positions[1].toMap().value(QStringLiteral("row")).toInt(), 1);
    QCOMPARE(positions[1].toMap().value(QStringLiteral("childRow")).toInt(), 0);
    QCOMPARE(positions[2].toMap().value(QStringLiteral("childRow")).toInt(), 1);
}

QTEST_GUILESS_MAIN(WindowCyclerTest)

#include "windowcyclertest.moc"
