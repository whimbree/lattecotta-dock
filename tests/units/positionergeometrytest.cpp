/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, transplanted)
    SPDX-FileCopyrightText: 2026 Bree Spektor

    SPDX-License-Identifier: GPL-2.0-or-later
*/
// EX-09 PositionerGeometry (docs/tracking/QML_EXTRACTION_PLAN.md): the pure
// sizing/placement math of a dock view.
//
// Case provenance (per docs/reference/TESTING.md): all 28 slots are ported from
// latte-dock-qt6's positionergeometrytest.cpp at 81384003, github.com/CaptSilver/latte-dock-qt6 (their
// 4a829185 extraction); every pure function was diffed against our
// positioner.cpp bodies before adoption and matched exactly, so their
// fixture arithmetic - derived from the original positioner.cpp
// arithmetic and cross-checked - transfers verbatim.

#include "../../app/view/positionergeometry.h"

#include <QObject>
#include <QPoint>
#include <QRect>
#include <QRegion>
#include <QSize>
#include <QtTest>

using namespace Latte::ViewPart::PositionerGeometry;

class PositionerGeometryTest : public QObject
{
    Q_OBJECT

private:
    QRect screen1080p() const { return QRect(0, 0, 1920, 1080); }

    ViewGeometryInputs baseInputs() const
    {
        ViewGeometryInputs in;
        in.location = Plasma::Types::BottomEdge;
        in.formFactor = Plasma::Types::Horizontal;
        in.maxThickness = 96;
        in.maxNormalThickness = 96;
        in.editThickness = 128;
        in.viewWidth = 1920;
        in.viewHeight = 56;
        return in;
    }

private Q_SLOTS:
    // dockPosition, non-panel, all 4 edges
    void dockPosition_bottomEdge_nonPanel();
    void dockPosition_topEdge_nonPanel();
    void dockPosition_leftEdge_nonPanel();
    void dockPosition_rightEdge_nonPanel();

    // windowSize
    void windowSize_horizontalNonPanel();
    void windowSize_verticalNonPanel();
    void windowSize_degenerateClamp();

    // maximumNormalGeometry
    void maximumNormalGeometry_leftEdge();
    void maximumNormalGeometry_rightEdge();
    void maximumNormalGeometry_defaultBottomEdge();
    void maximumNormalGeometry_multiMonitorOffset();

    // canvasGeometry
    void canvasGeometry_topEdge();
    void canvasGeometry_bottomEdge();
    void canvasGeometry_leftEdge();
    void canvasGeometry_rightEdge();

    // slideEdge
    void slideEdge_topEdge();
    void slideEdge_rightEdge();
    void slideEdge_bottomEdge();
    void slideEdge_leftEdge();
    void slideEdge_floating_returnsNone();

    // forcedBorders
    void forcedBorders_bothOff_whenScreenEdgesMatch();
    void forcedBorders_top_forced();
    void forcedBorders_bottom_forced();
    void forcedBorders_topNotForced_whenRegionOccupied();
};

// ---------------------------------------------------------------------------
// dockPosition, non-panel
// ---------------------------------------------------------------------------

void PositionerGeometryTest::dockPosition_bottomEdge_nonPanel()
{
    // Non-panel bottom: {screenGeometry.x(), screenGeometry.y() + screenGeometry.height() - viewHeight}
    // = {0, 0 + 1080 - 56} = {0, 1024}
    ViewGeometryInputs in = baseInputs();
    in.location = Plasma::Types::BottomEdge;
    in.viewHeight = 56;

    QPoint pos = dockPosition(in, screen1080p());
    QCOMPARE(pos, QPoint(0, 1024));
}

void PositionerGeometryTest::dockPosition_topEdge_nonPanel()
{
    // Non-panel top: {screenGeometry.x(), screenGeometry.y()} = {0, 0}
    ViewGeometryInputs in = baseInputs();
    in.location = Plasma::Types::TopEdge;
    in.viewHeight = 56;

    QPoint pos = dockPosition(in, screen1080p());
    QCOMPARE(pos, QPoint(0, 0));
}

void PositionerGeometryTest::dockPosition_leftEdge_nonPanel()
{
    // Non-panel left: {availableScreenRect.x(), availableScreenRect.y()} = {0, 0}
    ViewGeometryInputs in = baseInputs();
    in.location = Plasma::Types::LeftEdge;
    in.formFactor = Plasma::Types::Vertical;

    QPoint pos = dockPosition(in, screen1080p());
    QCOMPARE(pos, QPoint(0, 0));
}

void PositionerGeometryTest::dockPosition_rightEdge_nonPanel()
{
    // Non-panel right: {availableScreenRect.right() - viewWidth + 1, availableScreenRect.y()}
    // = {1919 - 96 + 1, 0} = {1824, 0}
    ViewGeometryInputs in = baseInputs();
    in.location = Plasma::Types::RightEdge;
    in.formFactor = Plasma::Types::Vertical;
    in.viewWidth = 96;

    QPoint pos = dockPosition(in, screen1080p());
    QCOMPARE(pos, QPoint(1824, 0));
}

// ---------------------------------------------------------------------------
// windowSize
// ---------------------------------------------------------------------------

void PositionerGeometryTest::windowSize_horizontalNonPanel()
{
    // Horizontal non-panel: width=screenSize.width(), height=maxThickness
    // = {1920, 96}
    ViewGeometryInputs in = baseInputs();
    in.formFactor = Plasma::Types::Horizontal;
    in.maxThickness = 96;

    QSize sz = windowSize(in, screen1080p(), screen1080p().size());
    QCOMPARE(sz, QSize(1920, 96));
}

void PositionerGeometryTest::windowSize_verticalNonPanel()
{
    // Vertical non-panel: width=maxThickness, height=availableScreenRect.height()
    // = {96, 1080}
    ViewGeometryInputs in = baseInputs();
    in.formFactor = Plasma::Types::Vertical;
    in.maxThickness = 96;

    QSize sz = windowSize(in, screen1080p(), screen1080p().size());
    QCOMPARE(sz, QSize(96, 1080));
}

void PositionerGeometryTest::windowSize_degenerateClamp()
{
    // maxThickness=0 for non-panel horizontal: size=(1920, 0) -> clamped to (1920, 1)
    ViewGeometryInputs in = baseInputs();
    in.formFactor = Plasma::Types::Horizontal;
    in.maxThickness = 0;

    QSize sz = windowSize(in, screen1080p(), screen1080p().size());
    QCOMPARE(sz.width(), 1920);
    QCOMPARE(sz.height(), 1);
}

// ---------------------------------------------------------------------------
// maximumNormalGeometry
// ---------------------------------------------------------------------------

void PositionerGeometryTest::maximumNormalGeometry_leftEdge()
{
    // LeftEdge: xPos=currentScrGeometry.x()=0, yPos=currentScrGeometry.y()=0
    // maxWidth=50, maxHeight=1080
    // => QRect(0, 0, 50, 1080)
    QRect result = maximumNormalGeometry(Plasma::Types::LeftEdge, 50, screen1080p());
    QCOMPARE(result, QRect(0, 0, 50, 1080));
}

void PositionerGeometryTest::maximumNormalGeometry_rightEdge()
{
    // RightEdge: xPos=currentScrGeometry.right()-maxWidth+1 = 1919-50+1 = 1870
    // yPos=0, maxWidth=50, maxHeight=1080
    // => QRect(1870, 0, 50, 1080)
    QRect result = maximumNormalGeometry(Plasma::Types::RightEdge, 50, screen1080p());
    QCOMPARE(result, QRect(1870, 0, 50, 1080));
}

void PositionerGeometryTest::maximumNormalGeometry_defaultBottomEdge()
{
    // BottomEdge (default): QRect(0, 0, maxNormalThickness, maxHeight)
    // = QRect(0, 0, 50, 1080)
    QRect result = maximumNormalGeometry(Plasma::Types::BottomEdge, 50, screen1080p());
    QCOMPARE(result, QRect(0, 0, 50, 1080));
}

void PositionerGeometryTest::maximumNormalGeometry_multiMonitorOffset()
{
    // Screen at (1920, 0): LeftEdge xPos = 1920
    QRect screen2(1920, 0, 1920, 1080);
    QRect result = maximumNormalGeometry(Plasma::Types::LeftEdge, 50, screen2);
    QCOMPARE(result.x(), 1920);
    QCOMPARE(result.y(), 0);
    QCOMPARE(result.width(), 50);
    QCOMPARE(result.height(), 1080);
}

// ---------------------------------------------------------------------------
// canvasGeometry
// ---------------------------------------------------------------------------

void PositionerGeometryTest::canvasGeometry_topEdge()
{
    // TopEdge: canvas at (screenGeometry.x(), screenGeometry.y()), width=width, height=thickness
    // screenGeometry=1080p, available=same
    // => QRect(0, 0, 1920, 128)
    QRect canvas = canvasGeometry(Plasma::Types::TopEdge, Plasma::Types::Horizontal,
                                  128, screen1080p(), screen1080p());
    QCOMPARE(canvas, QRect(0, 0, 1920, 128));
}

void PositionerGeometryTest::canvasGeometry_bottomEdge()
{
    // BottomEdge: canvas at (screenGeometry.x(), screenGeometry.bottom() - thickness + 1)
    // = (0, 1079 - 128 + 1) = (0, 952)
    // width=1920, height=128
    // => QRect(0, 952, 1920, 128)
    QRect canvas = canvasGeometry(Plasma::Types::BottomEdge, Plasma::Types::Horizontal,
                                  128, screen1080p(), screen1080p());
    QCOMPARE(canvas, QRect(0, 952, 1920, 128));
}

void PositionerGeometryTest::canvasGeometry_leftEdge()
{
    // LeftEdge: canvas at (availableScreenRect.x(), availableScreenRect.y())
    // = (0, 0), width=thickness, height=availableScreenRect.height()
    // => QRect(0, 0, 128, 1080)
    QRect canvas = canvasGeometry(Plasma::Types::LeftEdge, Plasma::Types::Vertical,
                                  128, screen1080p(), screen1080p());
    QCOMPARE(canvas, QRect(0, 0, 128, 1080));
}

void PositionerGeometryTest::canvasGeometry_rightEdge()
{
    // RightEdge: canvas at (screenGeometry.right() - thickness + 1, availableScreenRect.y())
    // = (1919 - 128 + 1, 0) = (1792, 0)
    // width=thickness=128, height=availableScreenRect.height()=1080
    // => QRect(1792, 0, 128, 1080)
    QRect canvas = canvasGeometry(Plasma::Types::RightEdge, Plasma::Types::Vertical,
                                  128, screen1080p(), screen1080p());
    QCOMPARE(canvas, QRect(1792, 0, 128, 1080));
}

// ---------------------------------------------------------------------------
// slideEdge
// ---------------------------------------------------------------------------

void PositionerGeometryTest::slideEdge_topEdge()
{
    QCOMPARE(slideEdge(Plasma::Types::TopEdge), SlideEdge::Top);
}

void PositionerGeometryTest::slideEdge_rightEdge()
{
    QCOMPARE(slideEdge(Plasma::Types::RightEdge), SlideEdge::Right);
}

void PositionerGeometryTest::slideEdge_bottomEdge()
{
    QCOMPARE(slideEdge(Plasma::Types::BottomEdge), SlideEdge::Bottom);
}

void PositionerGeometryTest::slideEdge_leftEdge()
{
    QCOMPARE(slideEdge(Plasma::Types::LeftEdge), SlideEdge::Left);
}

void PositionerGeometryTest::slideEdge_floating_returnsNone()
{
    // Floating is not resolved here (caller responsibility); pure function returns None.
    QCOMPARE(slideEdge(Plasma::Types::Floating), SlideEdge::None);
}

// ---------------------------------------------------------------------------
// forcedBorders
// ---------------------------------------------------------------------------

void PositionerGeometryTest::forcedBorders_bothOff_whenScreenEdgesMatch()
{
    // availableScreenRect top matches screen top, bottom matches screen bottom
    // => both borders off
    QRect screen = screen1080p();
    // vertical dock available rect = whole screen (no other docks squeezing it)
    QRegion region(screen);
    ForcedBorders fb = forcedBorders(Plasma::Types::LeftEdge, 1, screen, screen, region);
    QVERIFY(!fb.top);
    QVERIFY(!fb.bottom);
}

void PositionerGeometryTest::forcedBorders_top_forced()
{
    // availableScreenRect.top() != screen.top() (a top dock reserves 40px)
    // and the region above is free => top=true
    QRect screen = screen1080p();
    QRect available(0, 40, 50, 1040); // left-edge dock, top cropped by 40
    // region = full screen minus no other docks (the top dock is in the other axis)
    QRegion region(screen);
    // edgeMargin=1, LeftEdge so x = screen.x() = 0
    // fitInRegion = QRect(0, available.y()-1, 1, 1) = QRect(0, 39, 1, 1)
    // that rect IS in the region (no dock blocks y=39) => subtracted.isNull() => top=true
    ForcedBorders fb = forcedBorders(Plasma::Types::LeftEdge, 1, screen, available, region);
    QVERIFY(fb.top);
    QVERIFY(!fb.bottom);
}

void PositionerGeometryTest::forcedBorders_bottom_forced()
{
    // availableScreenRect.bottom() != screen.bottom() (a bottom dock reserves 40px)
    // and the region below is free => bottom=true
    QRect screen = screen1080p();
    QRect available(0, 0, 50, 1040); // left-edge dock, bottom cropped
    QRegion region(screen);
    // edgeMargin=1, LeftEdge so x = screen.x() = 0
    // fitInRegion = QRect(0, available.bottom()+1, 1, 1) = QRect(0, 1040, 1, 1)
    // that rect IS in the region (screen extends to 1079) => bottom=true
    ForcedBorders fb = forcedBorders(Plasma::Types::LeftEdge, 1, screen, available, region);
    QVERIFY(!fb.top);
    QVERIFY(fb.bottom);
}

void PositionerGeometryTest::forcedBorders_topNotForced_whenRegionOccupied()
{
    // availableScreenRect.top() != screen.top(), but the strip above is occupied
    // (another dock sits there and removed it from the region) => top=false
    QRect screen = screen1080p();
    QRect available(0, 40, 50, 1040);
    // A top dock at (0,0,1920,40) is already removed from the region
    QRegion region = QRegion(screen).subtracted(QRect(0, 0, 1920, 40));
    // edgeMargin=1, LeftEdge => x=0
    // fitInRegion = QRect(0, 39, 1, 1), inside the subtracted band => NOT in region
    // subtracted != isNull => top=false
    ForcedBorders fb = forcedBorders(Plasma::Types::LeftEdge, 1, screen, available, region);
    QVERIFY(!fb.top);
}

QTEST_MAIN(PositionerGeometryTest)
#include "positionergeometrytest.moc"
