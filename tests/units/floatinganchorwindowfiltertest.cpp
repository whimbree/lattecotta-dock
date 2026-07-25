/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../../app/view/floatinganchorwindowfilter.h"

#include <QWindow>

#include <QtTest>

using namespace Latte::ViewPart::FloatingPopupPresentation;

class EventProbe : public QObject
{
public:
    int eventCount{0};

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        if (event->type() == QEvent::DynamicPropertyChange) {
            ++eventCount;
        }
        return false;
    }
};

class FloatingAnchorWindowFilterTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void detachesOldWindowBeforeFollowingNewWindow();
};

void FloatingAnchorWindowFilterTest::
    detachesOldWindowBeforeFollowingNewWindow()
{
    EventProbe probe;
    AnchorWindowEventFilter filter{probe};
    QWindow first;
    QWindow second;

    QVERIFY(!filter.observes(nullptr));
    QVERIFY(filter.followWindow(&first));
    QVERIFY(filter.observes(&first));
    QVERIFY(!filter.followWindow(&first));

    first.setProperty("_floating_anchor_revision", 1);
    QCOMPARE(probe.eventCount, 1);

    QVERIFY(filter.followWindow(&second));
    QVERIFY(!filter.observes(&first));
    QVERIFY(filter.observes(&second));

    first.setProperty("_floating_anchor_revision", 2);
    QCOMPARE(probe.eventCount, 1);
    second.setProperty("_floating_anchor_revision", 1);
    QCOMPARE(probe.eventCount, 2);

    QVERIFY(filter.followWindow(nullptr));
    QVERIFY(!filter.observes(&second));
    second.setProperty("_floating_anchor_revision", 2);
    QCOMPARE(probe.eventCount, 2);
}

QTEST_MAIN(FloatingAnchorWindowFilterTest)

#include "floatinganchorwindowfiltertest.moc"
