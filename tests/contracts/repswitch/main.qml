/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Minimal applet main script for representationswitchtest.cpp, loaded by
//! libplasma through the qrc applet path (:/qt/qml/plasma/applet/<pluginid>/
//! main.qml, the packageless route Applet::mainScript() checks first). The
//! switch thresholds mirror the comic-strip shape from 9ea29eaa: an applet
//! whose item size crosses switchWidth/switchHeight at runtime.

import QtQuick
import org.kde.plasma.plasmoid

PlasmoidItem {
    switchWidth: 100
    switchHeight: 100

    compactRepresentation: Item {
        objectName: "testCompactRep"
    }

    fullRepresentation: Item {
        objectName: "testFullRep"
    }
}
