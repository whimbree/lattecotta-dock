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
};

void VisibilityRevealTest::revealsOnScreenEdge_revealingModes()
{
    //! These modes hide the dock and must re-reveal it when the pointer reaches the screen edge,
    //! so WaylandInterface::setActiveEdge() arms the edge-ghost detector for them.
    QVERIFY(VisibilityManager::revealsOnScreenEdge(Latte::Types::AutoHide));
    QVERIFY(VisibilityManager::revealsOnScreenEdge(Latte::Types::DodgeActive));
    QVERIFY(VisibilityManager::revealsOnScreenEdge(Latte::Types::DodgeMaximized));
    QVERIFY(VisibilityManager::revealsOnScreenEdge(Latte::Types::DodgeAllWindows));
}

void VisibilityRevealTest::revealsOnScreenEdge_nonRevealingModes()
{
    //! AlwaysVisible never hides; the WindowsCanCover/GoBelow/AlwaysCover family uses a
    //! stacking-layer mechanism rather than edge reveal; the Sidebar modes reveal on demand
    //! (shortcut / explicit toggle). The edge detector must NOT be armed for any of these.
    QVERIFY(!VisibilityManager::revealsOnScreenEdge(Latte::Types::AlwaysVisible));
    QVERIFY(!VisibilityManager::revealsOnScreenEdge(Latte::Types::WindowsCanCover));
    QVERIFY(!VisibilityManager::revealsOnScreenEdge(Latte::Types::WindowsGoBelow));
    QVERIFY(!VisibilityManager::revealsOnScreenEdge(Latte::Types::WindowsAlwaysCover));
    QVERIFY(!VisibilityManager::revealsOnScreenEdge(Latte::Types::SidebarOnDemand));
    QVERIFY(!VisibilityManager::revealsOnScreenEdge(Latte::Types::SidebarAutoHide));
    QVERIFY(!VisibilityManager::revealsOnScreenEdge(Latte::Types::None));
    QVERIFY(!VisibilityManager::revealsOnScreenEdge(Latte::Types::NormalWindow));
}

QTEST_MAIN(VisibilityRevealTest)
#include "visibilityrevealtest.moc"
