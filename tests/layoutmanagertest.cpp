/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// The containment LayoutManager's bookkeeping surface, driven without a
// Plasma stack: the per-applet option lists (locked-zoom,
// colorizing-blocked) that the QML side persists into the containment
// config, the QQuickItem anchor properties, and the masqueraded-index
// encoding the drag-and-drop path uses to smuggle a target index through a
// QPoint. A setter that emits on a no-op write, or stays silent on a real
// change, silently corrupts dock layout persistence - every mutator here is
// pinned to emit exactly once per real change.
//
// Transplanted from latte-dock-qt6 (tests/layoutmanagertest.cpp at
// origin/main) minus their scheduled-destruction cases: this tree's
// setAppletInScheduledDestruction contract deliberately DIVERGES (a park
// with no container is refused loudly instead of tracked blind - the
// 71b0d75a two-phase parking design), and layoutmanagerparkingtest pins
// that contract over real Plasma applets. The state machine itself lives
// there; only the plain bookkeeping lives here.

// local
#include "plugin/layoutmanager.h"

// Qt
#include <QObject>
#include <QPoint>
#include <QQuickItem>
#include <QSignalSpy>
#include <QTest>

using Latte::Containment::LayoutManager;

class LayoutManagerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    //! option lists
    void trackLockedZoomAppletsAndEmitOncePerChange();
    void trackColorizingBlockedAppletsAndEmitOncePerChange();

    //! setOption dispatch
    void dispatchLockZoomOptionThroughAddAndRemove();
    void dispatchColorizingOptionThroughAddAndRemove();
    void ignoreUnknownOptionName();

    //! QML anchor properties
    void holdQuickItemAnchorsAndEmitOncePerChange();

    //! masqueraded drag-and-drop index
    void roundTripMasqueradedIndexThroughPoint_data();
    void roundTripMasqueradedIndexThroughPoint();
    void classifyMasqueradedPointsAgainstRealCoordinates();

    void pinJustifySplitterId();
};

void LayoutManagerTest::trackLockedZoomAppletsAndEmitOncePerChange()
{
    LayoutManager lm;
    QSignalSpy spy(&lm, &LayoutManager::lockedZoomAppletsChanged);
    QVERIFY(spy.isValid());

    QVERIFY(lm.lockedZoomApplets().isEmpty());

    const QList<int> applets{3, 7, 11};
    lm.requestAppletsInLockedZoom(applets);
    QCOMPARE(lm.lockedZoomApplets(), applets);
    QCOMPARE(spy.count(), 1);

    // the same list again is a guarded no-op: no extra signal
    lm.requestAppletsInLockedZoom(applets);
    QCOMPARE(spy.count(), 1);
}

void LayoutManagerTest::trackColorizingBlockedAppletsAndEmitOncePerChange()
{
    LayoutManager lm;
    QSignalSpy spy(&lm, &LayoutManager::userBlocksColorizingAppletsChanged);
    QVERIFY(spy.isValid());

    const QList<int> applets{2, 4, 8};
    lm.requestAppletsDisabledColoring(applets);
    QCOMPARE(lm.userBlocksColorizingApplets(), applets);
    QCOMPARE(spy.count(), 1);

    lm.requestAppletsDisabledColoring(applets);
    QCOMPARE(spy.count(), 1);
}

void LayoutManagerTest::dispatchLockZoomOptionThroughAddAndRemove()
{
    LayoutManager lm;
    QSignalSpy spy(&lm, &LayoutManager::lockedZoomAppletsChanged);

    // enable for id 12: appended
    lm.setOption(12, QStringLiteral("lockZoom"), true);
    QCOMPARE(lm.lockedZoomApplets(), QList<int>{12});
    QCOMPARE(spy.count(), 1);

    // enabling an already-locked id is a no-op (the contains() guard)
    lm.setOption(12, QStringLiteral("lockZoom"), true);
    QCOMPARE(lm.lockedZoomApplets(), QList<int>{12});
    QCOMPARE(spy.count(), 1);

    // add a second, then remove the first
    lm.setOption(20, QStringLiteral("lockZoom"), true);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(lm.lockedZoomApplets(), (QList<int>{12, 20}));

    lm.setOption(12, QStringLiteral("lockZoom"), false);
    QCOMPARE(lm.lockedZoomApplets(), QList<int>{20});
    QCOMPARE(spy.count(), 3);

    // disabling an id that is not locked is a no-op
    lm.setOption(999, QStringLiteral("lockZoom"), false);
    QCOMPARE(spy.count(), 3);
}

void LayoutManagerTest::dispatchColorizingOptionThroughAddAndRemove()
{
    LayoutManager lm;
    QSignalSpy spy(&lm, &LayoutManager::userBlocksColorizingAppletsChanged);

    lm.setOption(3, QStringLiteral("userBlocksColorizing"), true);
    QCOMPARE(lm.userBlocksColorizingApplets(), QList<int>{3});
    QCOMPARE(spy.count(), 1);

    lm.setOption(3, QStringLiteral("userBlocksColorizing"), false);
    QVERIFY(lm.userBlocksColorizingApplets().isEmpty());
    QCOMPARE(spy.count(), 2);
}

void LayoutManagerTest::ignoreUnknownOptionName()
{
    LayoutManager lm;
    QSignalSpy lockSpy(&lm, &LayoutManager::lockedZoomAppletsChanged);
    QSignalSpy colorSpy(&lm, &LayoutManager::userBlocksColorizingAppletsChanged);

    // a property name LayoutManager does not know must touch nothing
    lm.setOption(7, QStringLiteral("somethingElse"), true);

    QVERIFY(lm.lockedZoomApplets().isEmpty());
    QVERIFY(lm.userBlocksColorizingApplets().isEmpty());
    QCOMPARE(lockSpy.count(), 0);
    QCOMPARE(colorSpy.count(), 0);
}

void LayoutManagerTest::holdQuickItemAnchorsAndEmitOncePerChange()
{
    LayoutManager lm;

    QQuickItem root, mainL, startL, endL, dnd, metrics;

    QSignalSpy rootSpy(&lm, &LayoutManager::rootItemChanged);
    QSignalSpy mainSpy(&lm, &LayoutManager::mainLayoutChanged);
    QSignalSpy startSpy(&lm, &LayoutManager::startLayoutChanged);
    QSignalSpy endSpy(&lm, &LayoutManager::endLayoutChanged);
    QSignalSpy dndSpy(&lm, &LayoutManager::dndSpacerChanged);
    QSignalSpy metricsSpy(&lm, &LayoutManager::metricsChanged);

    lm.setRootItem(&root);
    lm.setMainLayout(&mainL);
    lm.setStartLayout(&startL);
    lm.setEndLayout(&endL);
    lm.setDndSpacer(&dnd);
    lm.setMetrics(&metrics);

    QCOMPARE(lm.rootItem(), &root);
    QCOMPARE(lm.mainLayout(), &mainL);
    QCOMPARE(lm.startLayout(), &startL);
    QCOMPARE(lm.endLayout(), &endL);
    QCOMPARE(lm.dndSpacer(), &dnd);
    QCOMPARE(lm.metrics(), &metrics);

    QCOMPARE(rootSpy.count(), 1);
    QCOMPARE(mainSpy.count(), 1);
    QCOMPARE(startSpy.count(), 1);
    QCOMPARE(endSpy.count(), 1);
    QCOMPARE(dndSpy.count(), 1);
    QCOMPARE(metricsSpy.count(), 1);

    // the same pointer again stays silent
    lm.setMainLayout(&mainL);
    QCOMPARE(mainSpy.count(), 1);
}

void LayoutManagerTest::roundTripMasqueradedIndexThroughPoint_data()
{
    QTest::addColumn<int>("index");

    QTest::newRow("zero (the base itself)") << 0;
    QTest::newRow("one") << 1;
    QTest::newRow("small") << 5;
    QTest::newRow("mid") << 42;
    QTest::newRow("large") << 1000;
}

void LayoutManagerTest::roundTripMasqueradedIndexThroughPoint()
{
    QFETCH(int, index);

    LayoutManager lm;
    const QPoint p = lm.indexToMasquearadedPoint(index);

    // encoded as a degenerate diagonal point deep in negative space so it
    // can never collide with a real on-screen coordinate
    QCOMPARE(p.x(), p.y());
    QVERIFY(lm.isMasqueradedIndex(p.x(), p.y()));
    QCOMPARE(lm.masquearadedIndex(p.x(), p.y()), index);
}

void LayoutManagerTest::classifyMasqueradedPointsAgainstRealCoordinates()
{
    LayoutManager lm;

    // real on-screen coordinates are never classified as masqueraded
    QVERIFY(!lm.isMasqueradedIndex(10, 20));
    QVERIFY(!lm.isMasqueradedIndex(100, 100)); // equal but far above the base
    QVERIFY(!lm.isMasqueradedIndex(0, 0));

    // the base itself (index 0) is the boundary and must classify
    const QPoint base = lm.indexToMasquearadedPoint(0);
    QVERIFY(lm.isMasqueradedIndex(base.x(), base.y()));

    // x != y is rejected even when both sit deep in the masquerade range
    QVERIFY(!lm.isMasqueradedIndex(base.x(), base.y() - 1));
}

void LayoutManagerTest::pinJustifySplitterId()
{
    // the reserved applet id the justify-mode splitters serialize under;
    // real applet ids are positive, so the constant must stay negative
    QCOMPARE(LayoutManager::JUSTIFYSPLITTERID, -10);
}

QTEST_MAIN(LayoutManagerTest)

#include "layoutmanagertest.moc"
