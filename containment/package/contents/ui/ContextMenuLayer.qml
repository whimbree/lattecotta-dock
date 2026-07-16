/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.7

import org.kde.latte.private.app 0.1 as LatteApp

//! Applet-aware right-click menus for the dock window itself: Configure
//! <Applet>, the applet's own actions, plus the Latte containment entries.
//! Owner decision 2026-07-12: the configure entry is available ALWAYS, not
//! only in edit mode (matches plasmashell and Qt5-era Latte, which handled
//! this in the removed ViewPart::ContextMenu until upstream d3538eee).
//!
//! Isolated in its own file because org.kde.latte.private.app only resolves
//! in-process (the qml compile gate deliberately skips such files).
LatteApp.ContextMenuLayer {
    anchors.fill: parent
    view: latteView
}
