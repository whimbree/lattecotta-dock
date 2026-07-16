/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Qt Quick contract: a Loader whose active binding gates on a source
//! object being non-null deactivates - destroying its loaded item - BEFORE
//! any binding inside that item can re-evaluate against the null. The gate
//! works because the Loader's active binding subscribes to the gating
//! property before the loaded item exists, and Qt delivers change
//! notifications in connection-establishment order.
//!
//! Depended on by: the 841c2ca4 null-svg guard in
//! indicators/org.kde.latte.plasma FrontLayer.qml. KSvg 6 dropped the Qt5
//! null guard in SvgItem::setSvg() (unguarded m_svg->devicePixelRatio(),
//! still at ksvg master as of 6.27), so a null reaching an existing
//! SvgItem's svg binding is a guaranteed crash - which is also why the KSvg
//! side of that incident CANNOT be pinned as a contract test directly: the
//! upstream behavior under test is a SIGSEGV, which crashes the harness
//! rather than failing a test. This file pins the ordering that makes our
//! guard sufficient instead; tst_plasmaindicatorgroupsvg.qml (tests/qml)
//! pins the guard itself in the real indicator file.

import QtQuick
import QtTest

TestCase {
    id: root
    name: "LoaderGateContracts"

    property QtObject payload: QtObject { objectName: "payload" }

    //! true if any inner binding ever evaluated against a null gate source
    property bool nullReachedInnerBinding: false
    property int innerEvaluations: 0

    Loader {
        id: gatedLoader
        active: root.payload !== null

        sourceComponent: Item {
            //! plays the arrow's svg binding: re-evaluates whenever the
            //! gating property changes - IF this item still exists then
            property QtObject probe: root.payload

            onProbeChanged: {
                root.innerEvaluations++;
                if (probe === null) {
                    root.nullReachedInnerBinding = true;
                }
            }
        }
    }

    function test_gateDestroysItemBeforeInnerBindingSeesNull() {
        verify(gatedLoader.active, "gate starts open: payload exists");
        verify(gatedLoader.item, "loader built its item");
        compare(gatedLoader.item.probe, root.payload);

        //! the 841c2ca4 transition: the source object goes away (an
        //! indicator style switch empties resources.svgs)
        root.payload = null;

        verify(!gatedLoader.active, "gate must close on the null transition");
        verify(!gatedLoader.item, "the loaded item must be destroyed by the closed gate");
        verify(!root.nullReachedInnerBinding,
               "an inner binding evaluated against null AFTER the gate closed - "
               + "connection order no longer protects the FrontLayer.qml svg guard (841c2ca4); "
               + "the SvgItem would have crashed in KSvg SvgItem::setSvg");
    }

    function test_gateReopensWithFreshItem() {
        //! the recovery arm: resources.svgs filling back up re-creates the
        //! item against the settled, non-null source
        root.payload = null;
        verify(!gatedLoader.item);

        var fresh = Qt.createQmlObject("import QtQuick; QtObject {}", root);
        root.payload = fresh;

        verify(gatedLoader.active);
        verify(gatedLoader.item, "gate reopening must rebuild the item");
        compare(gatedLoader.item.probe, fresh,
                "the fresh item must bind against the new source, never a stale one");
    }
}
