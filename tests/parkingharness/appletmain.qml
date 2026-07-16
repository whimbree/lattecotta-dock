/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Minimal applet main script for layoutmanagerparkingtest.cpp, loaded by
//! libplasma through the qrc applet path. No representations: the parking
//! test needs real AppletQuickItems (LayoutManager resolves containers by
//! casting their applet property and reading applet()->id()), not
//! representation churn.

import QtQuick
import org.kde.plasma.plasmoid

PlasmoidItem {
}
