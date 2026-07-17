/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.0

Item {
    property bool showPositionShortcutBadges: false
    property var badges: ['1','2','3','4','5','6','7','8','9','0', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.']

    //! keyboard focus mode (2026-07-17, no Qt5 twin): the 1-based visible
    //! entry index that currently holds the keyboard focus, -1 when the
    //! mode is off. Written only by the containment's keyboard-navigation
    //! handler; items light their indicator hover state when their own
    //! shortcut index matches.
    property int keyboardFocusedEntryIndex: -1

    signal sglActivateEntryAtIndex(int entryIndex);
    signal sglNewInstanceForEntryAtIndex(int entryIndex);
}
