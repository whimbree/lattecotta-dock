/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../../app/view/windowtouchstate.h"

#include <QRect>
#include <QtTest>

#include <array>

using namespace Latte::ViewPart;

class WindowTouchStateTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void countsOnePixelIntersectionOnEveryEdge_data();
    void countsOnePixelIntersectionOnEveryEdge();
    void rejectsAdjacencyWithoutOverlap();
    void handlesOffsetAndNegativeCoordinates();
    void letsOneSpanningWindowTouchMultipleTriggers();
    void excludesNonWindowsHiddenAndMinimized();
    void rejectsInvalidTriggerGeometry();
};

void WindowTouchStateTest::countsOnePixelIntersectionOnEveryEdge_data()
{
    QTest::addColumn<QRect>("trigger");
    QTest::addColumn<QRect>("window");

    QTest::newRow("top")
        << QRect(100, 20, 80, 2)
        << QRect(120, 21, 10, 40);
    QTest::newRow("right")
        << QRect(178, 100, 2, 80)
        << QRect(140, 120, 39, 10);
    QTest::newRow("bottom")
        << QRect(100, 178, 80, 2)
        << QRect(120, 140, 10, 39);
    QTest::newRow("left")
        << QRect(20, 100, 2, 80)
        << QRect(21, 120, 40, 10);
}

void WindowTouchStateTest::countsOnePixelIntersectionOnEveryEdge()
{
    QFETCH(QRect, trigger);
    QFETCH(QRect, window);

    const auto stableTrigger =
        StableWindowTouchTrigger::fromGeometry(trigger);
    QVERIFY(stableTrigger.has_value());

    const std::array candidates{
        WindowTouchCandidate{window, true, false, false},
    };
    QCOMPARE(
        countWindowsTouchingTrigger(*stableTrigger, candidates),
        1);
}

void WindowTouchStateTest::rejectsAdjacencyWithoutOverlap()
{
    const auto trigger =
        StableWindowTouchTrigger::fromGeometry(QRect(10, 10, 50, 2));
    QVERIFY(trigger.has_value());

    const std::array candidates{
        WindowTouchCandidate{QRect(20, 12, 10, 20), true, false, false},
        WindowTouchCandidate{QRect(60, 10, 10, 2), true, false, false},
    };
    QCOMPARE(countWindowsTouchingTrigger(*trigger, candidates), 0);
}

void WindowTouchStateTest::handlesOffsetAndNegativeCoordinates()
{
    const auto trigger =
        StableWindowTouchTrigger::fromGeometry(QRect(-1919, -400, 600, 2));
    QVERIFY(trigger.has_value());

    const std::array candidates{
        WindowTouchCandidate{
            QRect(-1800, -399, 120, 300), true, false, false},
        WindowTouchCandidate{
            QRect(-1300, -399, 120, 300), true, false, false},
    };
    QCOMPARE(countWindowsTouchingTrigger(*trigger, candidates), 1);
}

void WindowTouchStateTest::letsOneSpanningWindowTouchMultipleTriggers()
{
    const auto leftTrigger =
        StableWindowTouchTrigger::fromGeometry(QRect(-1, 0, 2, 100));
    const auto rightTrigger =
        StableWindowTouchTrigger::fromGeometry(QRect(199, 0, 2, 100));
    QVERIFY(leftTrigger.has_value());
    QVERIFY(rightTrigger.has_value());

    const std::array spanningWindow{
        WindowTouchCandidate{QRect(0, 20, 200, 40), true, false, false},
    };
    QCOMPARE(
        countWindowsTouchingTrigger(*leftTrigger, spanningWindow),
        1);
    QCOMPARE(
        countWindowsTouchingTrigger(*rightTrigger, spanningWindow),
        1);
}

void WindowTouchStateTest::excludesNonWindowsHiddenAndMinimized()
{
    const auto trigger =
        StableWindowTouchTrigger::fromGeometry(QRect(0, 0, 100, 2));
    QVERIFY(trigger.has_value());

    const QRect intersecting(20, 1, 10, 20);
    const std::array candidates{
        WindowTouchCandidate{intersecting, false, false, false},
        WindowTouchCandidate{intersecting, true, true, false},
        WindowTouchCandidate{intersecting, true, false, true},
        WindowTouchCandidate{intersecting, true, false, false},
    };
    QCOMPARE(countWindowsTouchingTrigger(*trigger, candidates), 1);
}

void WindowTouchStateTest::rejectsInvalidTriggerGeometry()
{
    QVERIFY(!StableWindowTouchTrigger::fromGeometry(QRect()).has_value());
    QVERIFY(!StableWindowTouchTrigger::fromGeometry(
                 QRect(0, 0, -1, 10))
                 .has_value());
}

QTEST_APPLESS_MAIN(WindowTouchStateTest)

#include "windowtouchstatetest.moc"
