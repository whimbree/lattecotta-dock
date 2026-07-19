/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Pins the task items' screen-reader description surface (Phase 10 AT-SPI
//! rollout) through the SHIPPED LatteTasks.TooltipTextComposer singleton -
//! the exact call TaskItem.qml's Accessible.description binding performs,
//! with the same fact-bag shape. Instantiating the full TaskItem offscreen
//! is not feasible (it needs the whole abilities graph, the tasksModel
//! delegate context and the smart-launcher backend), so the composed
//! values are pinned here through the singleton and the attached-property
//! wiring is walked at the desk (the Orca script in
//! docs/reference/manual-flake-removal-testing.md). Vectors mirror
//! tests/units/tooltiptexttest.cpp; strings are English msgids - the
//! offscreen environment loads no catalogs, deterministically.

import QtQuick
import QtTest

import org.kde.latte.private.tasks 0.1 as LatteTasks

TestCase {
    id: root
    name: "TaskAccessibleText"

    function baseFacts() {
        return {
            isLauncher: false,
            isGroupParent: false,
            windowsCount: 0,
            showsAudioBadge: false,
            isMuted: false,
            showsProgressBadge: false,
            progressPercent: 0,
            infoBadgeCount: 0
        };
    }

    function test_plainWindowDescribesNothing() {
        //! the name already carries the title; a bare window adds nothing
        compare(LatteTasks.TooltipTextComposer.composeAccessibleDescription(baseFacts()), "",
                "a plain single window has an empty description");
    }

    function test_launcherAnnouncesItsKind() {
        var facts = baseFacts();
        facts.isLauncher = true;
        compare(LatteTasks.TooltipTextComposer.composeAccessibleDescription(facts), "launcher",
                "a pinned launcher announces its kind");
    }

    function test_groupAnnouncesWindowCount() {
        var facts = baseFacts();
        facts.isGroupParent = true;
        facts.windowsCount = 1;
        compare(LatteTasks.TooltipTextComposer.composeAccessibleDescription(facts), "1 window",
                "singular window count");

        facts.windowsCount = 4;
        compare(LatteTasks.TooltipTextComposer.composeAccessibleDescription(facts), "4 windows",
                "plural window count");
    }

    function test_badgesAnnounceTheirValues() {
        var facts = baseFacts();
        facts.showsAudioBadge = true;
        compare(LatteTasks.TooltipTextComposer.composeAccessibleDescription(facts), "playing audio",
                "audible audio badge");

        facts.isMuted = true;
        compare(LatteTasks.TooltipTextComposer.composeAccessibleDescription(facts), "audio muted",
                "muted audio badge");

        facts = baseFacts();
        facts.isGroupParent = true;
        facts.windowsCount = 2;
        facts.showsAudioBadge = true;
        facts.isMuted = true;
        facts.showsProgressBadge = true;
        facts.progressPercent = 45;
        facts.infoBadgeCount = 7;
        compare(LatteTasks.TooltipTextComposer.composeAccessibleDescription(facts),
                "2 windows, audio muted, 45% complete, 7 notifications",
                "fixed announcement order: group size, audio, progress, info");
    }

    function test_muteToggleLabelMatchesTheContextMenu() {
        //! the audio badge and ContextMenu.qml's checkable item share the
        //! msgid, so Orca announces both mute surfaces identically
        compare(LatteTasks.TooltipTextComposer.muteToggleLabel(), "Mute",
                "the badge's accessible name is the menu's Mute msgid");
    }

    function test_driftedFactBagRefusesLoudly() {
        //! boundary refusal: a shell drifting a key name composes nothing
        compare(LatteTasks.TooltipTextComposer.composeAccessibleDescription({ isLauncher: true }), "",
                "a partial fact bag is refused");
    }
}
