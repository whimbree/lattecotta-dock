/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, transplanted)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// EX-08 ScreenGeometryCalculator (docs/tracking/QML_EXTRACTION_PLAN.md): the
// available-screen geometry math over view-footprint snapshots.
//
// Case provenance (per docs/reference/TESTING.md): the eleven core slots are ported
// from latte-dock-qt6's screengeometrycalculatortest.cpp at 81384003, github.com/CaptSilver/latte-dock-qt6 -
// their calculator was diffed line-by-line against our corona bodies
// before adoption, so their fixture arithmetic transfers. Two additions
// are ours: the 1b932ed9 self-origin case (a view's own footprint
// reserves like any other - the shell passes ALL views, no exclusion)
// and a multi-dock same-edge case shaped like a real multi-dock layout.
//
// Fixtures trace the arithmetic against a 1920x1080 screen at the origin
// (right=1919, bottom=1079).

#include <QtTest>
#include <QRect>
#include <QRegion>

#include "../../app/screengeometrycalculator.h"

using namespace Latte;
namespace Calc = Latte::ScreenGeometryCalculator;

class ScreenGeometryCalculatorTest : public QObject
{
    Q_OBJECT

private:
    QRect screen() const { return QRect(0, 0, 1920, 1080); }

    //! a footprint that passes every gate (always-visible, on-screen,
    //! has visibility)
    ViewFootprint visibleView(Plasma::Types::Location loc, const QRect &geom, int thickness) const
    {
        ViewFootprint fp;
        fp.location = loc;
        fp.occupiedGeometry = geom;
        fp.normalThickness = thickness;
        fp.visibilityMode = Latte::Types::AlwaysVisible;
        fp.hasVisibility = true;
        fp.behaveAsPlasmaPanel = false;
        return fp;
    }

private Q_SLOTS:
    void emptyFootprints_returnsStartRect();
    void bottomEdgeNonPanel_reservesThickness();
    void topEdgeNonPanel_reservesThickness();
    void leftEdgeNonPanel_reservesThickness();
    void topAndBottom_accumulate();
    void panelDesktopUse_bottom_usesScreenEdgeMargin();
    void ignoredVisibilityMode_skipped();
    void ignoredEdge_skipped();
    void normalWindowAndNone_autoBlacklisted();
    void offScreenDesktopUse_skipped();
    void region_bottomNonPanel_subtractsFootprint();
    void region_partialBottomDoesNotShortenSeparatedSideDock();
    void selfOriginFootprint_reservesLikeAnyOther();
    void multiDockSameEdge_deepestReservationWins();
};

void ScreenGeometryCalculatorTest::emptyFootprints_returnsStartRect()
{
    QCOMPARE(Calc::availableRect(screen(), screen(), {}, {}, {}, false), screen());
}

void ScreenGeometryCalculatorTest::bottomEdgeNonPanel_reservesThickness()
{
    //! bottom dock occupying y=1040..1079, thickness 40:
    //! setBottom(qMin(1079, 1040+40-40 = 1040)) -> bottom 1040
    QList<ViewFootprint> fps{visibleView(Plasma::Types::BottomEdge, QRect(0, 1040, 1920, 40), 40)};

    QCOMPARE(Calc::availableRect(screen(), screen(), fps, {}, {}, false),
             QRect(0, 0, 1920, 1041));
}

void ScreenGeometryCalculatorTest::topEdgeNonPanel_reservesThickness()
{
    //! top dock at y=0, thickness 30: setTop(qMax(0, 0+30)) -> top 30
    QList<ViewFootprint> fps{visibleView(Plasma::Types::TopEdge, QRect(0, 0, 1920, 30), 30)};

    QCOMPARE(Calc::availableRect(screen(), screen(), fps, {}, {}, false),
             QRect(0, 30, 1920, 1050));
}

void ScreenGeometryCalculatorTest::leftEdgeNonPanel_reservesThickness()
{
    //! left dock at x=0, thickness 50: setLeft(qMax(0, 0+50)) -> left 50
    QList<ViewFootprint> fps{visibleView(Plasma::Types::LeftEdge, QRect(0, 0, 50, 1080), 50)};

    QCOMPARE(Calc::availableRect(screen(), screen(), fps, {}, {}, false),
             QRect(50, 0, 1870, 1080));
}

void ScreenGeometryCalculatorTest::topAndBottom_accumulate()
{
    QList<ViewFootprint> fps{
        visibleView(Plasma::Types::TopEdge, QRect(0, 0, 1920, 30), 30),
        visibleView(Plasma::Types::BottomEdge, QRect(0, 1040, 1920, 40), 40)};

    //! top -> 30, bottom -> 1040; height = 1040 - 30 + 1 = 1011
    QCOMPARE(Calc::availableRect(screen(), screen(), fps, {}, {}, false),
             QRect(0, 30, 1920, 1011));
}

void ScreenGeometryCalculatorTest::panelDesktopUse_bottom_usesScreenEdgeMargin()
{
    //! plasma panel + desktopUse: appliedThickness = margin(10) +
    //! thickness(40) = 50; setBottom(qMin(1079, 1079-50 = 1029))
    ViewFootprint fp = visibleView(Plasma::Types::BottomEdge, QRect(0, 1030, 1920, 50), 40);
    fp.behaveAsPlasmaPanel = true;
    fp.screenEdgeMargin = 10;

    QCOMPARE(Calc::availableRect(screen(), screen(), {fp}, {}, {}, true),
             QRect(0, 0, 1920, 1030));
}

void ScreenGeometryCalculatorTest::ignoredVisibilityMode_skipped()
{
    ViewFootprint fp = visibleView(Plasma::Types::BottomEdge, QRect(0, 1040, 1920, 40), 40);
    fp.visibilityMode = Latte::Types::AutoHide;

    QList<Latte::Types::Visibility> ignoreModes{Latte::Types::AutoHide};

    QCOMPARE(Calc::availableRect(screen(), screen(), {fp}, ignoreModes, {}, false), screen());
}

void ScreenGeometryCalculatorTest::ignoredEdge_skipped()
{
    QList<ViewFootprint> fps{visibleView(Plasma::Types::BottomEdge, QRect(0, 1040, 1920, 40), 40)};
    QList<Plasma::Types::Location> ignoreEdges{Plasma::Types::BottomEdge};

    QCOMPARE(Calc::availableRect(screen(), screen(), fps, {}, ignoreEdges, false), screen());
}

void ScreenGeometryCalculatorTest::normalWindowAndNone_autoBlacklisted()
{
    //! None and NormalWindow are blacklisted even with empty ignoreModes
    ViewFootprint normalWin = visibleView(Plasma::Types::BottomEdge, QRect(0, 1040, 1920, 40), 40);
    normalWin.visibilityMode = Latte::Types::NormalWindow;

    ViewFootprint none = visibleView(Plasma::Types::TopEdge, QRect(0, 0, 1920, 30), 30);
    none.visibilityMode = Latte::Types::None;

    QCOMPARE(Calc::availableRect(screen(), screen(), {normalWin, none}, {}, {}, false), screen());
}

void ScreenGeometryCalculatorTest::offScreenDesktopUse_skipped()
{
    //! a view sliding off-screen during desktop startup is ignored when
    //! desktopUse is set
    ViewFootprint fp = visibleView(Plasma::Types::BottomEdge, QRect(0, 1040, 1920, 40), 40);
    fp.isOffScreen = true;

    QCOMPARE(Calc::availableRect(screen(), screen(), {fp}, {}, {}, true), screen());
}

void ScreenGeometryCalculatorTest::region_bottomNonPanel_subtractsFootprint()
{
    //! The occupied rectangle is subtracted directly. Alignment and maximum
    //! length were already resolved by the owning View.
    QList<ViewFootprint> fps{visibleView(Plasma::Types::BottomEdge, QRect(0, 1040, 1920, 40), 40)};

    QRegion result = Calc::availableRegion(screen(), screen(), fps, {}, {}, false);

    QCOMPARE(result, QRegion(QRect(0, 0, 1920, 1040)));
}

void ScreenGeometryCalculatorTest::region_partialBottomDoesNotShortenSeparatedSideDock()
{
    const QRect nestedScreen(0, 0, 1600, 1000);
    const ViewFootprint bottom = visibleView(
        Plasma::Types::BottomEdge, QRect(378, 912, 844, 88), 88);

    const QRegion result = Calc::availableRegion(
        nestedScreen, nestedScreen, {bottom}, {}, {}, false);

    //! The side dock occupies x=1512..1599. The masked bottom canvas spans
    //! the output, but its stable occupied rectangle ends at x=1221.
    QVERIFY(result.contains(QRect(1512, 0, 88, 1000)));
}

void ScreenGeometryCalculatorTest::selfOriginFootprint_reservesLikeAnyOther()
{
    //! the 1b932ed9 divergence: a view asking for the available rect must
    //! see the area corrected by its OWN reserved thickness (upstream
    //! d30143f7 excluded the origin view and the settings chrome mapped
    //! 99px off-screen on cold starts). The shell passes every view on
    //! the screen including the asker, so a single-dock screen still
    //! reserves - this is exactly the bottomEdge case with the footprint
    //! being the caller's own
    QList<ViewFootprint> ownFootprintOnly{
        visibleView(Plasma::Types::BottomEdge, QRect(0, 981, 1920, 99), 99)};

    QCOMPARE(Calc::availableRect(screen(), screen(), ownFootprintOnly, {}, {}, false),
             QRect(0, 0, 1920, 982));
}

void ScreenGeometryCalculatorTest::multiDockSameEdge_deepestReservationWins()
{
    //! two bottom docks (my real layout shape: a shallow full-width panel
    //! and a thicker centered dock): setBottom takes the qMin, so the
    //! deeper reservation wins
    QList<ViewFootprint> fps{
        visibleView(Plasma::Types::BottomEdge, QRect(0, 1050, 1920, 30), 30),
        visibleView(Plasma::Types::BottomEdge, QRect(400, 1000, 1120, 80), 80)};

    //! shallow: bottom -> 1050; deep: bottom -> qMin(1050, 1000+80-80=1000)
    QCOMPARE(Calc::availableRect(screen(), screen(), fps, {}, {}, false),
             QRect(0, 0, 1920, 1001));
}

QTEST_GUILESS_MAIN(ScreenGeometryCalculatorTest)

#include "screengeometrycalculatortest.moc"
