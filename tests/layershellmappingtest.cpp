/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Pins the pure layer-shell mapping functions in app/wm/waylandlayershell.h.
//! These mappings are the compositor-facing contract of the wayland port:
//! anchors place the dock (setPosition is ignored for layer surfaces),
//! exclusive zones are the struts, and two of the cases below encode protocol
//! rules whose violation kills the surface or aborts the client outright.

#include "wm/waylandlayershell.h"

// Qt
#include <QGuiApplication>
#include <QRegion>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QWindow>
#include <QtTest>

using namespace Latte::WindowSystem;
using LSW = LayerShellQt::Window;

class LayerShellMappingTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void anchorsForBottomEdge();
    void anchorsForLeftEdge();
    void anchorsForMaskedDockSpanBothLengthEdges();
    void exclusiveEdgeIsAlwaysAnchored();
    void layerByVisibilityMode();
    void exclusiveZoneByLocation();
    void floatingGapMapsToEdgeMargins();
    void floatingPanelSurfaceIsLiftedOffItsEdge();
    void seededSizeForUnspannedAxes();
    void viewPlacementPinsSolvedRectangle();
    void appliedViewPlacementPreservesSurfacePolicy();
    void reservationPlacementTracksOccupiedSpan();
    void canvasPlacementByEdge();
    void canvasInputRegionPlainEditMode();
    void canvasInputRegionConfigureAppletsClickThrough();
    void canvasInputRegionKeepsChromeInteractive();
    void reservationZoneAppliesAndReleases();
    void canvasPlacementOptsOutOfExclusiveZones();
    void fixedPlacementOptsOutOfExclusiveZones();
    void sameScreenPlacementDoesNotRemap();
    void hiddenWindowRetargetsWithoutMapping();
    void visibleCrossScreenFixedPlacementRemaps();
    void visibleCrossScreenCanvasPlacementRemaps();
};

void LayerShellMappingTest::initTestCase()
{
    //! main() below configures the offscreen platform with two outputs (the
    //! landscape+portrait pair of the real setup) so the cross-screen
    //! retarget tests can drive an actual screen change. Pin that setup
    //! loudly: if the offscreen configfile contract ever changes, every
    //! screen-dependent test here would otherwise misreport.
    const auto screens = QGuiApplication::screens();
    QVERIFY2(screens.size() == 2,
             qPrintable(QStringLiteral("expected the 2 configured offscreen outputs, got %1")
                            .arg(screens.size())));
    QCOMPARE(screens.at(0)->geometry(), QRect(0, 0, 1920, 1080));
    QCOMPARE(screens.at(1)->geometry(), QRect(1920, 0, 1440, 2560));
}

void LayerShellMappingTest::anchorsForBottomEdge()
{
    //! a sized panel window (windowSpansScreenLength=false): alignment moves it
    QCOMPARE(LayerShell::anchorsFor(Plasma::Types::BottomEdge, Latte::Types::Center, false),
             LSW::Anchors(LSW::AnchorBottom));
    QCOMPARE(LayerShell::anchorsFor(Plasma::Types::BottomEdge, Latte::Types::Left, false),
             LSW::Anchors(LSW::AnchorBottom | LSW::AnchorLeft));
    QCOMPARE(LayerShell::anchorsFor(Plasma::Types::BottomEdge, Latte::Types::Right, false),
             LSW::Anchors(LSW::AnchorBottom | LSW::AnchorRight));
    QCOMPARE(LayerShell::anchorsFor(Plasma::Types::BottomEdge, Latte::Types::Justify, false),
             LSW::Anchors(LSW::AnchorBottom | LSW::AnchorLeft | LSW::AnchorRight));
}

void LayerShellMappingTest::anchorsForLeftEdge()
{
    QCOMPARE(LayerShell::anchorsFor(Plasma::Types::LeftEdge, Latte::Types::Center, false),
             LSW::Anchors(LSW::AnchorLeft));
    QCOMPARE(LayerShell::anchorsFor(Plasma::Types::LeftEdge, Latte::Types::Top, false),
             LSW::Anchors(LSW::AnchorLeft | LSW::AnchorTop));
    QCOMPARE(LayerShell::anchorsFor(Plasma::Types::LeftEdge, Latte::Types::Bottom, false),
             LSW::Anchors(LSW::AnchorLeft | LSW::AnchorBottom));
    QCOMPARE(LayerShell::anchorsFor(Plasma::Types::LeftEdge, Latte::Types::Justify, false),
             LSW::Anchors(LSW::AnchorLeft | LSW::AnchorTop | LSW::AnchorBottom));
}

void LayerShellMappingTest::anchorsForMaskedDockSpanBothLengthEdges()
{
    //! A masked dock's window covers the full screen length and centres its
    //! contents internally, so its surface anchors BOTH length edges for EVERY
    //! alignment - the compositor pins it corner to corner rather than centring
    //! it inside the region other docks' exclusive zones leave free (the -20px
    //! drift this parameter fixes). Horizontal: length edges are left+right.
    const LSW::Anchors bottomSpan(LSW::AnchorBottom | LSW::AnchorLeft | LSW::AnchorRight);
    for (auto alignment : {Latte::Types::Center, Latte::Types::Left,
                           Latte::Types::Right, Latte::Types::Justify}) {
        QCOMPARE(LayerShell::anchorsFor(Plasma::Types::BottomEdge, alignment, true), bottomSpan);
    }
    QCOMPARE(LayerShell::anchorsFor(Plasma::Types::TopEdge, Latte::Types::Center, true),
             LSW::Anchors(LSW::AnchorTop | LSW::AnchorLeft | LSW::AnchorRight));
}

void LayerShellMappingTest::exclusiveEdgeIsAlwaysAnchored()
{
    //! updateAnchoring() sets edgeFor(location) as the exclusive edge right
    //! after anchorsFor(location, alignment). That is only legal if the edge
    //! is among the anchors for every combination: the compositor kills a
    //! surface whose exclusive edge is not one of its anchors.
    const QList<Plasma::Types::Location> locations{
        Plasma::Types::TopEdge, Plasma::Types::BottomEdge,
        Plasma::Types::LeftEdge, Plasma::Types::RightEdge};
    const QList<Latte::Types::Alignment> alignments{
        Latte::Types::Center, Latte::Types::Left, Latte::Types::Right,
        Latte::Types::Top, Latte::Types::Bottom, Latte::Types::Justify};

    for (const auto location : locations) {
        for (const auto alignment : alignments) {
            for (const bool spansScreenLength : {false, true}) {
                const LSW::Anchors anchors = LayerShell::anchorsFor(location, alignment, spansScreenLength);
                QVERIFY2(anchors.testFlag(LayerShell::edgeFor(location)),
                         qPrintable(QStringLiteral("exclusive edge not anchored for location=%1 alignment=%2 spans=%3")
                                        .arg(int(location)).arg(int(alignment)).arg(spansScreenLength)));
            }
        }
    }
}

void LayerShellMappingTest::layerByVisibilityMode()
{
    QCOMPARE(LayerShell::layerFor(Latte::Types::WindowsCanCover), LSW::LayerBottom);
    QCOMPARE(LayerShell::layerFor(Latte::Types::WindowsAlwaysCover), LSW::LayerBottom);
    QCOMPARE(LayerShell::layerFor(Latte::Types::AlwaysVisible), LSW::LayerTop);
    QCOMPARE(LayerShell::layerFor(Latte::Types::AutoHide), LSW::LayerTop);
    QCOMPARE(LayerShell::layerFor(Latte::Types::DodgeActive), LSW::LayerTop);
    QCOMPARE(LayerShell::layerFor(Latte::Types::NormalWindow), LSW::LayerTop);

    //! WindowsGoBelow means windows go below the dock; the X11 backend maps
    //! it to keep-above, so the layer must be Top. latte-dock-qt6 put it in
    //! the LayerBottom branch, which also dragged its front-layer default
    //! down - this pins the corrected mapping.
    QCOMPARE(LayerShell::layerFor(Latte::Types::WindowsGoBelow), LSW::LayerTop);
}

void LayerShellMappingTest::exclusiveZoneByLocation()
{
    QCOMPARE(LayerShell::exclusiveZoneFor(QRect(0, 1040, 1920, 40), Plasma::Types::BottomEdge), 40);
    QCOMPARE(LayerShell::exclusiveZoneFor(QRect(0, 0, 1920, 40), Plasma::Types::TopEdge), 40);
    QCOMPARE(LayerShell::exclusiveZoneFor(QRect(0, 0, 48, 1080), Plasma::Types::LeftEdge), 48);
    QCOMPARE(LayerShell::exclusiveZoneFor(QRect(1872, 0, 48, 1080), Plasma::Types::RightEdge), 48);
    QCOMPARE(LayerShell::exclusiveZoneFor(QRect(), Plasma::Types::BottomEdge), 0);
}

void LayerShellMappingTest::floatingGapMapsToEdgeMargins()
{
    //! the floating gap (screenEdgeMargin) is a real offset off the anchored
    //! edge, realised as a layer-shell margin on THAT edge - not folded into
    //! surface thickness. Before this the panel sat flush and grew instead of
    //! floating. A top panel is pushed down, a bottom panel up, sides inward.
    QCOMPARE(LayerShell::edgeMarginsFor(Plasma::Types::TopEdge, 12), QMargins(0, 12, 0, 0));
    QCOMPARE(LayerShell::edgeMarginsFor(Plasma::Types::BottomEdge, 12), QMargins(0, 0, 0, 12));
    QCOMPARE(LayerShell::edgeMarginsFor(Plasma::Types::LeftEdge, 12), QMargins(12, 0, 0, 0));
    QCOMPARE(LayerShell::edgeMarginsFor(Plasma::Types::RightEdge, 12), QMargins(0, 0, 12, 0));

    //! the offset edge is always the exclusive/anchored edge, so the margin
    //! actually takes effect (a margin on an unanchored edge is ignored)
    const QList<Plasma::Types::Location> edges{
        Plasma::Types::TopEdge, Plasma::Types::BottomEdge,
        Plasma::Types::LeftEdge, Plasma::Types::RightEdge};
    for (const auto edge : edges) {
        const QMargins m = LayerShell::edgeMarginsFor(edge, 20);
        const LSW::Anchor exclusive = LayerShell::edgeFor(edge);
        const int onExclusive = (exclusive == LSW::AnchorTop) ? m.top()
                              : (exclusive == LSW::AnchorBottom) ? m.bottom()
                              : (exclusive == LSW::AnchorLeft) ? m.left()
                              : m.right();
        QCOMPARE(onExclusive, 20);
    }

    //! no gap and a disabled gap both mean flush: no margin, so the panel is
    //! never lifted. A non-edge location carries no offset either.
    QCOMPARE(LayerShell::edgeMarginsFor(Plasma::Types::TopEdge, 0), QMargins());
    QCOMPARE(LayerShell::edgeMarginsFor(Plasma::Types::TopEdge, -1), QMargins());
    QCOMPARE(LayerShell::edgeMarginsFor(Plasma::Types::Floating, 12), QMargins());
}

void LayerShellMappingTest::floatingPanelSurfaceIsLiftedOffItsEdge()
{
    //! end to end on a live layer surface: a floating top panel (edgeMargin>0)
    //! gets a real top margin so the compositor places it OFF the screen edge,
    //! leaving the gap. This is the fix for "the gap grows the panel instead of
    //! offsetting it" - the surface was anchored flush with no margin.
    QScreen *screen = QGuiApplication::screens().at(0);

    QWindow window;
    LSW *ls = LSW::get(&window);
    QVERIFY(ls);

    QVERIFY(LayerShell::configureView(&window, screen, Plasma::Types::TopEdge,
                                      Latte::Types::Center, false, 16));
    QCOMPARE(ls->exclusiveEdge(), LSW::AnchorTop);
    QCOMPARE(ls->margins(), QMargins(0, 16, 0, 0));

    //! disabling the gap at runtime (edgeMargin back to 0) must bring the panel
    //! flush again, not leave a stale margin welded on - updateAnchoring runs
    //! on every gap change and always (re)sets the margin
    LayerShell::updateAnchoring(&window, screen, Plasma::Types::TopEdge, Latte::Types::Center, false, 0);
    QCOMPARE(ls->margins(), QMargins(0, 0, 0, 0));

    //! a masked dock (windowSpansScreenLength=true) realises the gap through
    //! its mask, so its surface stays flush even though a gap is configured:
    //! the caller passes edgeMargin 0 for it, and the surface must not move
    LayerShell::updateAnchoring(&window, screen, Plasma::Types::TopEdge, Latte::Types::Center, true, 0);
    QCOMPARE(ls->margins(), QMargins(0, 0, 0, 0));
}

void LayerShellMappingTest::seededSizeForUnspannedAxes()
{
    const QSize screen(1920, 1080);

    //! a Center bottom dock anchors a single edge; a 0x0 window must be
    //! seeded (length -> screen width, thickness -> 1px) or the first
    //! surface commit is protocol-rejected
    const auto bottomCenter = LayerShell::anchorsFor(Plasma::Types::BottomEdge, Latte::Types::Center, false);
    QCOMPARE(LayerShell::seededLayerSize(bottomCenter, Plasma::Types::BottomEdge, QSize(0, 0), screen),
             QSize(1920, 1));

    //! an already-sized window stays untouched, so re-running on a runtime
    //! edge change is safe
    QCOMPARE(LayerShell::seededLayerSize(bottomCenter, Plasma::Types::BottomEdge, QSize(800, 48), screen),
             QSize(800, 48));

    //! a Justify dock spans left+right, so width 0 is legal (the compositor
    //! stretches it) and must not be overwritten
    const auto bottomJustify = LayerShell::anchorsFor(Plasma::Types::BottomEdge, Latte::Types::Justify, false);
    QCOMPARE(LayerShell::seededLayerSize(bottomJustify, Plasma::Types::BottomEdge, QSize(0, 48), screen),
             QSize(0, 48));

    //! vertical dock: thickness is the width, length axis is vertical
    const auto leftCenter = LayerShell::anchorsFor(Plasma::Types::LeftEdge, Latte::Types::Center, false);
    QCOMPARE(LayerShell::seededLayerSize(leftCenter, Plasma::Types::LeftEdge, QSize(0, 0), screen),
             QSize(1, 1080));
}

void LayerShellMappingTest::viewPlacementPinsSolvedRectangle()
{
    const QRect screen(1440, 425, 2560, 1440);

    //! A partial-width bottom dock does not intersect this right dock. Latte's
    //! region solver therefore keeps the side surface full-height. The visual
    //! surface is placed from the output's top-left, independently from
    //! KWin's edge-wide exclusive-zone work area.
    const QRect fullHeightRight(3616, 425, 384, 1440);
    const auto right = LayerShell::viewPlacement(Plasma::Types::RightEdge,
                                                 fullHeightRight, screen);
    QCOMPARE(right.anchors, LSW::Anchors(LSW::AnchorTop | LSW::AnchorLeft));
    QCOMPARE(right.margins, QMargins(2176, 0, 0, 0));

    //! A shorter solved surface carries its exact origin and explicit size.
    const QRect insetRight(3616, 469, 384, 1272);
    const auto inset = LayerShell::viewPlacement(Plasma::Types::RightEdge,
                                                 insetRight, screen);
    QCOMPARE(inset.anchors, LSW::Anchors(LSW::AnchorTop | LSW::AnchorLeft));
    QCOMPARE(inset.margins, QMargins(2176, 44, 0, 0));

    const QRect partialBottom(1812, 1741, 1815, 124);
    const auto bottom = LayerShell::viewPlacement(Plasma::Types::BottomEdge,
                                                  partialBottom, screen);
    QCOMPARE(bottom.anchors, LSW::Anchors(LSW::AnchorTop | LSW::AnchorLeft));
    QCOMPARE(bottom.margins, QMargins(372, 1316, 0, 0));
}

void LayerShellMappingTest::appliedViewPlacementPreservesSurfacePolicy()
{
    QWindow window;
    const QRect screenGeometry(0, 0, 1920, 1080);
    const QRect viewGeometry(1872, 40, 48, 1000);

    QVERIFY(LayerShell::configureView(&window, QGuiApplication::screens().at(0),
                                      Plasma::Types::RightEdge,
                                      Latte::Types::Center, false));
    LayerShell::setExclusiveZone(&window, 48);
    LSW *const ls = LSW::get(&window);
    QVERIFY(ls);
    ls->setLayer(LSW::LayerBottom);
    ls->setKeyboardInteractivity(LSW::KeyboardInteractivityOnDemand);

    LayerShell::applyViewPlacement(&window, Plasma::Types::RightEdge,
                                   viewGeometry, screenGeometry);

    QCOMPARE(ls->anchors(), LSW::Anchors(LSW::AnchorTop | LSW::AnchorLeft));
    QCOMPARE(ls->margins(), QMargins(1872, 40, 0, 0));
    QCOMPARE(ls->exclusiveEdge(), LSW::AnchorNone);
    QCOMPARE(ls->exclusionZone(), -1);
    QCOMPARE(ls->layer(), LSW::LayerBottom);
    QCOMPARE(ls->keyboardInteractivity(), LSW::KeyboardInteractivityOnDemand);
}

void LayerShellMappingTest::reservationPlacementTracksOccupiedSpan()
{
    const QRect screen(1440, 425, 2560, 1440);

    const QRect partialBottom(1812, 1741, 1815, 124);
    const auto bottom = LayerShell::reservationPlacement(
        Plasma::Types::BottomEdge, partialBottom, screen);
    QCOMPARE(bottom.anchors,
             LSW::Anchors(LSW::AnchorBottom | LSW::AnchorLeft | LSW::AnchorRight));
    QCOMPARE(bottom.exclusiveEdge, LSW::AnchorBottom);
    QCOMPARE(bottom.margins, QMargins(372, 0, -372, 0));
    QCOMPARE(bottom.surfaceSize, QSize(1815, 1));
    QCOMPARE(bottom.exclusiveZone, 124);

    const QRect insetRight(3912, 469, 88, 1272);
    const auto right = LayerShell::reservationPlacement(
        Plasma::Types::RightEdge, insetRight, screen);
    QCOMPARE(right.anchors,
             LSW::Anchors(LSW::AnchorRight | LSW::AnchorTop | LSW::AnchorBottom));
    QCOMPARE(right.exclusiveEdge, LSW::AnchorRight);
    QCOMPARE(right.margins, QMargins(0, 44, 0, -44));
    QCOMPARE(right.surfaceSize, QSize(1, 1272));
    QCOMPARE(right.exclusiveZone, 88);
}

void LayerShellMappingTest::canvasPlacementByEdge()
{
    const QRect screen(0, 0, 1920, 1080);

    //! horizontal docks: the canvas spans the full screen width on the dock
    //! edge, zero margins
    const auto bottom = LayerShell::canvasPlacement(Plasma::Types::BottomEdge, QRect(0, 1040, 1920, 40), screen);
    QCOMPARE(bottom.anchors, LSW::Anchors(LSW::AnchorBottom | LSW::AnchorLeft | LSW::AnchorRight));
    QCOMPARE(bottom.margins, QMargins(0, 0, 0, 0));

    const auto top = LayerShell::canvasPlacement(Plasma::Types::TopEdge, QRect(0, 0, 1920, 40), screen);
    QCOMPARE(top.anchors, LSW::Anchors(LSW::AnchorTop | LSW::AnchorLeft | LSW::AnchorRight));
    QCOMPARE(top.margins, QMargins(0, 0, 0, 0));

    //! vertical docks: the canvas starts at the available area's top (y=100
    //! here, below a top panel), carried by a top margin on a top anchor -
    //! margins only take effect on anchored edges
    const auto left = LayerShell::canvasPlacement(Plasma::Types::LeftEdge, QRect(0, 100, 48, 980), screen);
    QCOMPARE(left.anchors, LSW::Anchors(LSW::AnchorLeft | LSW::AnchorTop));
    QCOMPARE(left.margins, QMargins(0, 100, 0, 0));

    const auto right = LayerShell::canvasPlacement(Plasma::Types::RightEdge, QRect(1872, 200, 48, 880), screen);
    QCOMPARE(right.anchors, LSW::Anchors(LSW::AnchorRight | LSW::AnchorTop));
    QCOMPARE(right.margins, QMargins(0, 200, 0, 0));
}

void LayerShellMappingTest::canvasInputRegionPlainEditMode()
{
    //! plain edit mode: the canvas catches input everywhere EXCEPT the dock
    //! strip, which stays click-through so widgets remain hoverable and
    //! right-clickable (Qt5/X11 stacked the dock above the canvas; on
    //! wayland same-layer surfaces cannot restack, so the region carves the
    //! dock's rect out instead)
    const QSize canvas(1920, 40);
    const QRect dockStrip(400, 20, 1120, 20);
    const QRegion region = LayerShell::canvasInputRegion(false, canvas, QRect(), dockStrip);

    QCOMPARE(region, QRegion(QRect(0, 0, 1920, 40)) - QRegion(dockStrip));
    QVERIFY(region.contains(QPoint(100, 20)));
    QVERIFY2(!region.contains(QPoint(960, 30)),
             "the dock strip must stay click-through in plain edit mode");

    //! without a dock strip the whole canvas catches input
    const QRegion whole = LayerShell::canvasInputRegion(false, canvas, QRect(), QRect());
    QCOMPARE(whole, QRegion(QRect(0, 0, 1920, 40)));
}

void LayerShellMappingTest::canvasInputRegionConfigureAppletsClickThrough()
{
    //! configure-applets mode without chrome: the canvas overlays the dock,
    //! so it must catch no on-surface pixel or every right-click/drag hits
    //! the grid instead of the widgets
    const QSize canvas(1920, 40);
    const QRegion region = LayerShell::canvasInputRegion(true, canvas, QRect(), QRect());

    QVERIFY2(region.intersected(QRect(QPoint(0, 0), canvas)).isEmpty(),
             "the on-surface input area must be empty so events reach the dock beneath");

    //! and it must express that with an off-surface region, not an empty
    //! QRegion: the Qt wayland plugin maps an empty mask to the infinite
    //! (grab-all) input region, the exact opposite of click-through
    QVERIFY2(!region.isEmpty(),
             "click-through needs an off-surface region; an empty QRegion means grab-all on wayland");
}

void LayerShellMappingTest::canvasInputRegionKeepsChromeInteractive()
{
    //! configure-applets mode with chrome (the rearrange/exit toggle strip):
    //! the chrome keeps catching input, the dock area stays click-through
    const QSize canvas(1920, 40);
    const QRect chrome(0, 0, 1920, 8);
    const QRegion region = LayerShell::canvasInputRegion(true, canvas, chrome, QRect());

    QVERIFY(region.contains(QPoint(960, 4)));
    QVERIFY(!region.contains(QPoint(960, 30)));
}

void LayerShellMappingTest::reservationZoneAppliesAndReleases()
{
    //! A reservation surface publishes exactly the occupied thickness and
    //! releases it with 0 without changing the visual dock's zone -1 policy.
    QWindow window;
    QScreen *const screen = QGuiApplication::screens().at(0);
    LSW *const ls = LayerShell::applyReservationPlacement(
        &window, screen, Plasma::Types::BottomEdge,
        QRect(400, 1040, 1120, 40), screen->geometry());
    QVERIFY(ls);

    QCOMPARE(ls->exclusionZone(), 40);
    QCOMPARE(ls->exclusiveEdge(), LSW::AnchorBottom);
    QCOMPARE(window.size(), QSize(1120, 1));

    LayerShell::clearReservation(&window);
    QCOMPARE(ls->exclusionZone(), 0);
}

void LayerShellMappingTest::canvasPlacementOptsOutOfExclusiveZones()
{
    //! ec5d2316's other half: an overlay surface must carry zone -1 (opt out
    //! of every other surface's exclusive zone) or the compositor shifts it
    //! off the screen edge by the dock's own strut (measured live: canvas at
    //! y=1206 instead of 1294). The canvas windows are reused, so drive a
    //! stale dock-shaped configuration first: the placement must also clear
    //! the exclusive edge - a stale edge not among the overlay anchors makes
    //! the compositor kill the surface.
    QScreen *screen = QGuiApplication::screens().at(0);
    const QRect screenGeometry = screen->geometry();

    QWindow window;
    LSW *ls = LSW::get(&window);
    QVERIFY(ls);

    QVERIFY(LayerShell::configureView(&window, screen, Plasma::Types::BottomEdge,
                                      Latte::Types::Center, false));
    LayerShell::setExclusiveZone(&window, 88);
    QCOMPARE(ls->exclusiveEdge(), LSW::AnchorBottom);

    const QRect canvasGeometry(0, screenGeometry.height() - 146, screenGeometry.width(), 146);
    LayerShell::applyCanvasPlacement(&window, Plasma::Types::BottomEdge, screen, canvasGeometry, screenGeometry);

    QCOMPARE(ls->exclusionZone(), -1);
    QCOMPARE(ls->exclusiveEdge(), LSW::AnchorNone);
    QCOMPARE(ls->anchors(), LSW::Anchors(LSW::AnchorBottom | LSW::AnchorLeft | LSW::AnchorRight));
    QCOMPARE(ls->margins(), QMargins(0, 0, 0, 0));
    QCOMPARE(window.screen(), screen);
}

void LayerShellMappingTest::fixedPlacementOptsOutOfExclusiveZones()
{
    //! fixed-position surfaces (settings window, widget explorer, secondary
    //! chooser) compute their position in absolute screen coordinates, so a
    //! compositor strut-shift would land them somewhere else than requested:
    //! zone -1, no exclusive edge, and the position rides on top+left
    //! margins relative to the pinned screen (7ac419d1, d670c97a).
    QScreen *screen = QGuiApplication::screens().at(0);
    const QRect screenGeometry = screen->geometry();

    QWindow window;
    LSW *ls = LSW::get(&window);
    QVERIFY(ls);

    QVERIFY(LayerShell::configureView(&window, screen, Plasma::Types::BottomEdge,
                                      Latte::Types::Justify, false));
    LayerShell::setExclusiveZone(&window, 88);

    const QRect geometry(screenGeometry.x() + 100, screenGeometry.y() + 50, 600, 400);
    LayerShell::applyFixedGeometry(&window, screen, geometry, screenGeometry);

    QCOMPARE(ls->exclusionZone(), -1);
    QCOMPARE(ls->exclusiveEdge(), LSW::AnchorNone);
    QCOMPARE(ls->anchors(), LSW::Anchors(LSW::AnchorTop | LSW::AnchorLeft));
    QCOMPARE(ls->margins(), QMargins(100, 50, 0, 0));
    QCOMPARE(window.screen(), screen);
}

void LayerShellMappingTest::sameScreenPlacementDoesNotRemap()
{
    //! a mapped layer surface only needs the destroy/recreate cycle when it
    //! actually changes outputs; a same-screen re-placement must not hide
    //! the window even transiently (793faad2's remap is deliberately gated)
    QScreen *screen = QGuiApplication::screens().at(0);

    QWindow window;
    QVERIFY(LSW::get(&window));
    window.setScreen(screen);
    window.resize(200, 100);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QSignalSpy visibleSpy(&window, &QWindow::visibleChanged);
    LayerShell::applyFixedGeometry(&window, screen, QRect(10, 10, 200, 100), screen->geometry());

    QCOMPARE(visibleSpy.count(), 0);
    QVERIFY(window.isVisible());
    QCOMPARE(window.screen(), screen);
}

void LayerShellMappingTest::hiddenWindowRetargetsWithoutMapping()
{
    //! a hidden window has no surface to remap: retargeting must be a plain
    //! screen assignment and must never show the window as a side effect
    QScreen *target = QGuiApplication::screens().at(1);

    QWindow window;
    QVERIFY(LSW::get(&window));
    window.setScreen(QGuiApplication::screens().at(0));

    QSignalSpy visibleSpy(&window, &QWindow::visibleChanged);
    LayerShell::applyFixedGeometry(&window, target,
                                   QRect(target->geometry().x() + 10, target->geometry().y() + 10, 200, 100),
                                   target->geometry());

    QCOMPARE(visibleSpy.count(), 0);
    QVERIFY(!window.isVisible());
    QCOMPARE(window.screen(), target);
}

void LayerShellMappingTest::visibleCrossScreenFixedPlacementRemaps()
{
    //! a mapped wlr-layer surface is bound to the output it was created on
    //! and the protocol has no request to move it (793faad2, d670c97a):
    //! carrying a visible window to another screen must destroy the surface
    //! first (hide), retarget, and map a fresh one (show) - observable as
    //! exactly one hide followed by one show
    QScreen *origin = QGuiApplication::screens().at(0);
    QScreen *target = QGuiApplication::screens().at(1);

    QWindow window;
    QVERIFY(LSW::get(&window));
    window.setScreen(origin);
    window.resize(200, 100);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QSignalSpy visibleSpy(&window, &QWindow::visibleChanged);
    LayerShell::applyFixedGeometry(&window, target,
                                   QRect(target->geometry().x() + 10, target->geometry().y() + 10, 200, 100),
                                   target->geometry());

    QCOMPARE(visibleSpy.count(), 2);
    QCOMPARE(visibleSpy.at(0).at(0).toBool(), false);
    QCOMPARE(visibleSpy.at(1).at(0).toBool(), true);
    QVERIFY(window.isVisible());
    QCOMPARE(window.screen(), target);
}

void LayerShellMappingTest::visibleCrossScreenCanvasPlacementRemaps()
{
    //! the canvas overlay is reused across edited views (d670c97a): its
    //! placement path must carry the same hide/retarget/show cycle, and the
    //! placement margins must be the ones of the TARGET screen - they are
    //! interpreted relative to the surface's own output
    QScreen *origin = QGuiApplication::screens().at(0);
    QScreen *target = QGuiApplication::screens().at(1);
    const QRect targetGeometry = target->geometry();

    QWindow window;
    LSW *ls = LSW::get(&window);
    QVERIFY(ls);
    window.setScreen(origin);
    window.resize(200, 100);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QSignalSpy visibleSpy(&window, &QWindow::visibleChanged);
    const QRect canvasGeometry(targetGeometry.x(), targetGeometry.y() + 300, 146, targetGeometry.height() - 300);
    LayerShell::applyCanvasPlacement(&window, Plasma::Types::LeftEdge, target, canvasGeometry, targetGeometry);

    QCOMPARE(visibleSpy.count(), 2);
    QCOMPARE(visibleSpy.at(0).at(0).toBool(), false);
    QCOMPARE(visibleSpy.at(1).at(0).toBool(), true);
    QVERIFY(window.isVisible());
    QCOMPARE(window.screen(), target);
    QCOMPARE(ls->anchors(), LSW::Anchors(LSW::AnchorLeft | LSW::AnchorTop));
    QCOMPARE(ls->margins(), QMargins(0, 300, 0, 0));
}

//! The mapping tests need a real multi-output topology; the offscreen
//! platform only exposes one unless configured through its JSON config
//! file, so a custom main writes one and selects it before the platform
//! initializes. The geometries mirror the real landscape+portrait setup the
//! cross-screen defects were caught on.
int main(int argc, char *argv[])
{
    QTemporaryFile config(QDir::tempPath() + QStringLiteral("/layershellmappingtest-XXXXXX.json"));
    if (!config.open()) {
        qCritical() << "cannot create the offscreen screen configuration file";
        return 1;
    }

    config.write(R"({
        "screens": [
            { "name": "landscape", "x": 0, "y": 0, "width": 1920, "height": 1080 },
            { "name": "portrait", "x": 1920, "y": 0, "width": 1440, "height": 2560 }
        ]
    })");
    config.flush();

    const QByteArray platform = QByteArray("offscreen:configfile=") + config.fileName().toUtf8();
    qputenv("QT_QPA_PLATFORM", platform);

    QGuiApplication app(argc, argv);
    app.setAttribute(Qt::AA_Use96Dpi, true);

    LayerShellMappingTest tc;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&tc, argc, argv);
}

#include "layershellmappingtest.moc"
