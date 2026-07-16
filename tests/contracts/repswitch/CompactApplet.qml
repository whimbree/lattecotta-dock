/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Passive probe standing where Latte's shell CompactApplet.qml stands: the
//! compactRepresentationExpander libplasma instantiates from
//! Containment::compactApplet() (resolved through the host containment's qrc
//! path here). It declares exactly the properties AppletQuickItem writes -
//! plasmoidItem at creation, compactRepresentation and fullRepresentation on
//! every wire/unwire - and records nothing else, so the test observes what
//! libplasma DOES to an expander rather than what an expander does back.

import QtQuick

Item {
    objectName: "probeExpander"

    property Item plasmoidItem
    property Item compactRepresentation
    property Item fullRepresentation
}
