/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Honest-mock containment layout child: exactly the properties the
//! shipped IndexerPrivate.qml collectors/functions and the AppletItem.qml
//! walk bodies read on a layout child (docs/reference/TESTING.md mock rule).
import QtQuick 2.7

Item {
    property int index: -1
    property bool isSeparator: false
    property bool isHidden: false
    property bool isMarginsAreaSeparator: false
    //! appletIdForVisibleIndex reads applet.plasmoid.id
    property var applet: null
    property Item communicator: null
}
