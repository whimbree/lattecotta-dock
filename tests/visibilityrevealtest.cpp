/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, transplanted)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

// Pins VisibilityManager::revealsOnScreenEdge, the predicate that decides
// which visibility modes arm the wayland edge-ghost detector
// (WaylandInterface::setActiveEdge). Transplanted from latte-dock-qt6
// (tests/visibilityrevealtest.cpp at 81384003, github.com/CaptSilver/latte-dock-qt6) and extended over the
// full Types::Visibility domain: our enum also carries WindowsAlwaysCover,
// SidebarOnDemand, SidebarAutoHide and NormalWindow, and none of those may
// arm the edge detector either - updateKWinEdgesSupport() never creates an
// edge ghost for them (sidebars reveal by shortcut/on-demand, the cover
// family rides the stacking layer). A new enum value that hides the dock
// must be classified here deliberately, not fall through untested.

#include "view/visibilitymanager.h"

#include <QTest>
#include <QObject>

using Latte::ViewPart::VisibilityManager;

class VisibilityRevealTest : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void revealsOnScreenEdge_revealingModes();
    void revealsOnScreenEdge_nonRevealingModes();
    void usesClientScreenEdgeTrigger_includesLayerRaise();
};

void VisibilityRevealTest::revealsOnScreenEdge_revealingModes()
{
    //! These modes slide the dock away and ask KWin to reveal its real surface.
    //! The client trigger is only their unsupported-compositor fallback.
    QVERIFY(VisibilityManager::revealsOnScreenEdge(Latte::Types::AutoHide));
    QVERIFY(VisibilityManager::revealsOnScreenEdge(Latte::Types::DodgeActive));
    QVERIFY(VisibilityManager::revealsOnScreenEdge(Latte::Types::DodgeMaximized));
    QVERIFY(VisibilityManager::revealsOnScreenEdge(Latte::Types::DodgeAllWindows));
}

void VisibilityRevealTest::revealsOnScreenEdge_nonRevealingModes()
{
    //! AlwaysVisible never hides; the cover family changes stacking rather
    //! than sliding offscreen; Sidebar modes reveal on demand.
    QVERIFY(!VisibilityManager::revealsOnScreenEdge(Latte::Types::AlwaysVisible));
    QVERIFY(!VisibilityManager::revealsOnScreenEdge(Latte::Types::WindowsCanCover));
    QVERIFY(!VisibilityManager::revealsOnScreenEdge(Latte::Types::WindowsGoBelow));
    QVERIFY(!VisibilityManager::revealsOnScreenEdge(Latte::Types::WindowsAlwaysCover));
    QVERIFY(!VisibilityManager::revealsOnScreenEdge(Latte::Types::SidebarOnDemand));
    QVERIFY(!VisibilityManager::revealsOnScreenEdge(Latte::Types::SidebarAutoHide));
    QVERIFY(!VisibilityManager::revealsOnScreenEdge(Latte::Types::None));
    QVERIFY(!VisibilityManager::revealsOnScreenEdge(Latte::Types::NormalWindow));
}

void VisibilityRevealTest::usesClientScreenEdgeTrigger_includesLayerRaise()
{
    for (const auto mode : {
             Latte::Types::AutoHide,
             Latte::Types::DodgeActive,
             Latte::Types::DodgeMaximized,
             Latte::Types::DodgeAllWindows,
             Latte::Types::WindowsCanCover}) {
        QVERIFY(VisibilityManager::usesClientScreenEdgeTrigger(mode));
    }

    for (const auto mode : {
             Latte::Types::AlwaysVisible,
             Latte::Types::WindowsGoBelow,
             Latte::Types::WindowsAlwaysCover,
             Latte::Types::SidebarOnDemand,
             Latte::Types::SidebarAutoHide,
             Latte::Types::None,
             Latte::Types::NormalWindow}) {
        QVERIFY(!VisibilityManager::usesClientScreenEdgeTrigger(mode));
    }
}

QTEST_MAIN(VisibilityRevealTest)
#include "visibilityrevealtest.moc"
