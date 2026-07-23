/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Shell pin for the view-type chain and background-state predicates
//! (EX-13). The decision bodies this file's subjects used to carry in
//! containment main.qml / background/MultiLayered.qml live in the
//! BackgroundState core now (containment/plugin/units/backgroundstate.h;
//! tests/units/backgroundstatetest.cpp pins the full decision tables).
//! What must stay pinned HERE is the shell: the BackgroundStateResolver
//! the containment instantiates resolves from the staged install,
//! delegates to the core, and speaks the QML boundary's types - the
//! Latte::Types view-type ints, a rect for latteView.effects.rect, and
//! plain reals for the paddings.

import QtQuick
import QtTest

import org.kde.latte.private.containment 0.1 as LatteContainment

TestCase {
    id: root
    name: "BackgroundStateResolverShell"

    LatteContainment.BackgroundStateResolver {
        id: resolver
    }

    function test_viewTypeChainSpeaksLatteTypesInts() {
        //! the throwaway confusion case: Justify (10) + thick background +
        //! zoom 1.0 resolves PanelView (1) - full-width Panel rendering is
        //! correct, not a regression
        var inQuestion = resolver.viewTypeInQuestion(true, false, 10, 0.0, 100.0, true, 1.0);
        compare(inQuestion, 1);

        //! the floating internal gap forces the effective type back to Dock
        compare(resolver.viewType(true, inQuestion, true, true), 0);
        compare(resolver.viewType(true, inQuestion, false, false), 1);
    }

    function test_backgroundClusterComposesAcrossPredicates() {
        //! no compositing forces solid, forced solid vetoes transparency,
        //! and blur follows - the exact composition main.qml's bindings
        //! feed each other
        var solid = resolver.solidPanelForced(true, false, false, true, false, false, false, false, false);
        verify(solid);

        var transparent = resolver.transparentPanelForced(true, true, true, false, solid,
                                                          false, false, 0, false);
        verify(!transparent);

        verify(resolver.blurActive(true, transparent, false));
    }

    function test_effectsAreaReturnsTheHideMaskRect() {
        var area = resolver.effectsArea(true, true, false, 12.0, 3.0, 800.0, 48.0);
        compare(area.x, -1);
        compare(area.y, -1);
        compare(area.width, 1);
        compare(area.height, 1);
    }

    function test_zoomBorrowsBackgroundPaddingButKeepsChromeInView() {
        compare(resolver.dockBackgroundLength(1230, 1240, 40), 1200);
        compare(resolver.dockBackgroundLength(1307, 1240, 40), 1200);
        compare(resolver.centeredDockOffset(-34, 1240, 1240), 0);

        //! A shorter dock keeps its available centered movement.
        compare(resolver.dockBackgroundLength(900, 1240, 40), 900);
        compare(resolver.centeredDockOffset(80, 940, 1240), 80);
    }

    function test_edgePaddingScalesRoundnessBeyondMargins() {
        //! (radius 12 - margin 2) * factor 0.5
        compare(resolver.edgePadding(true, true, true, 12.0, 0.0, 0.0, 2.0, 0.5), 5.0);
        //! no border, no padding
        compare(resolver.edgePadding(false, true, true, 12.0, 8.0, 6.0, 2.0, 0.5), 0.0);
    }
}
