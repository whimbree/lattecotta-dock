/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "../../app/view/panelshadowstate.h"

#include <QtTest>

using namespace Latte::ViewPart::PanelShadowState;

class PanelShadowStateTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void isolatesPaddingAndBordersPerWindow();
};

void PanelShadowStateTest::isolatesPaddingAndBordersPerWindow()
{
    Registry<int> states;
    const State first{
        KSvg::FrameSvg::TopBorder
            | KSvg::FrameSvg::LeftBorder,
        QMargins(-1, -2, -3, -4)};
    const State second{
        KSvg::FrameSvg::RightBorder
            | KSvg::FrameSvg::BottomBorder,
        QMargins(-5, -6, -7, -8)};

    QCOMPARE(states.update(101, first), Update::Inserted);
    QCOMPARE(states.update(202, second), Update::Inserted);
    QCOMPARE(states.update(101, first), Update::Unchanged);

    State changedFirst = first;
    changedFirst.extraPadding = QMargins(-9, -10, -11, -12);
    QCOMPARE(states.update(101, changedFirst), Update::Changed);
    QCOMPARE(states.stateFor(101), std::optional{changedFirst});
    QCOMPARE(states.stateFor(202), std::optional{second});

    QVERIFY(states.remove(101));
    QVERIFY(!states.stateFor(101));
    QCOMPARE(states.stateFor(202), std::optional{second});
    QVERIFY(!states.isEmpty());
    QVERIFY(states.remove(202));
    QVERIFY(states.isEmpty());
}

QTEST_GUILESS_MAIN(PanelShadowStateTest)

#include "panelshadowstatetest.moc"
