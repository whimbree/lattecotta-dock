/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Regression test for the 841c2ca4 null-svg guard, driving the REAL
//! org.kde.latte.plasma FrontLayer.qml. KSvg 6 dropped plasma-framework 5's
//! null guard in SvgItem::setSvg() (m_svg->devicePixelRatio() unguarded), so
//! writing a null svg into an existing SvgItem is a guaranteed crash - the
//! Latte -> Plasma indicator switch segfaulted on the first click, every
//! time. indicator.resources.svgs fills through a QML -> C++ -> QML round
//! trip that lands only after the package items incubate, so an empty list
//! is a legitimate transient state; the fix gates the group-arrow Loader on
//! the svg existing.
//!
//! The upstream crash itself cannot be pinned as a contract test: the
//! behavior IS a SIGSEGV and would crash the harness rather than fail (the
//! sibling ordering pin lives in tests/contracts/
//! tst_loadergatecontracts.qml). This test pins OUR guard in the shipped
//! file: no SvgItem exists while svgs is empty, the arrow appears bound to
//! a real svg once resources land, and emptying the list again (the next
//! style switch) tears the arrow down without any null write.
//!
//! The indicator config API's context properties (indicator, level, root)
//! are provided as properties of this file's root object: unqualified names
//! inside the dynamically created FrontLayer resolve through the creation
//! context's context-object chain, which is this file. The Plasmoid
//! attached object is null outside a real applet (its lookup walks to an
//! AppletContext and gives up), so Plasmoid.* bindings in the file log
//! TypeErrors; they are location cosmetics, not the guard under test.

import QtQuick
import QtTest

import org.kde.ksvg 1.0 as KSvg

Item {
    id: testRoot
    width: 200
    height: 200

    //! the names the indicator config API injects as context properties,
    //! mocked with the shapes FrontLayer.qml actually reads
    property QtObject indicator: QtObject {
        objectName: "mockIndicator"

        property real scaleFactor: 1.0
        property int currentIconSize: 48
        property int screenEdgeMargin: 0
        property bool isApplet: false
        property bool isGroup: true

        //! mirrors app/view/indicator/indicatorresources.h:
        //! Q_PROPERTY(QList<QObject *> svgs NOTIFY svgsChanged)
        property QtObject resources: QtObject {
            property var svgs: []
        }
    }

    property QtObject level: QtObject {
        signal mousePressed(int x, int y, int button)
    }

    property QtObject root: QtObject {
        property bool clickedAnimationEnabled: false
    }

    //! a REAL KSvg.Svg, the type the arrow's svg property demands; the Svg
    //! object itself is safe with an empty path - only SvgItem.setSvg(null)
    //! is the crash
    KSvg.Svg {
        id: realGroupSvg
    }

    TestCase {
        name: "PlasmaIndicatorGroupSvgGuard"
        when: windowShown

        property Item frontLayer: null

        function findGroupLoader(item) {
            //! the guard lives on the Loader anchored full-size at the
            //! FrontLayer root; identify it structurally (it is the only
            //! Loader child)
            for (var i = 0; i < item.children.length; ++i) {
                if (item.children[i] instanceof Loader) {
                    return item.children[i];
                }
            }
            return null;
        }

        function findSvgItem(item) {
            if (!item) {
                return null;
            }
            if (item instanceof KSvg.SvgItem) {
                return item;
            }
            for (var i = 0; i < item.children.length; ++i) {
                var found = findSvgItem(item.children[i]);
                if (found) {
                    return found;
                }
            }
            return null;
        }

        //! the three tests are one scenario (virgin empty state, resources
        //! landing, resources emptying again); the numeric prefixes hold
        //! that order under qmltestrunner's alphabetical scheduling
        function initTestCase() {
            var component = Qt.createComponent("../../indicators/org.kde.latte.plasma/package/ui/FrontLayer.qml");
            compare(component.status, Component.Ready, component.errorString());
            frontLayer = component.createObject(testRoot);
            verify(frontLayer, "the real FrontLayer.qml must instantiate against the mocked indicator API");
        }

        function test_1_noSvgItemWhileResourcesEmpty() {
            var loader = findGroupLoader(frontLayer);
            verify(loader, "FrontLayer must carry the gated group-arrow Loader");

            //! the transient state during an indicator style switch: the
            //! C++ round trip has not filled svgs yet
            compare(testRoot.indicator.resources.svgs.length, 0);
            verify(!loader.active,
                   "the group-arrow Loader must stay inactive while resources.svgs is empty - "
                   + "an active one would create a SvgItem with a null svg, the exact "
                   + "KSvg 6 setSvg(nullptr) crash 841c2ca4 fixed");
            verify(!loader.item);
        }

        function test_2_arrowAppearsBoundToRealSvgOnceResourcesLand() {
            testRoot.indicator.resources.svgs = [realGroupSvg];

            var loader = findGroupLoader(frontLayer);
            tryVerify(function() { return loader.active; }, 5000,
                      "the Loader must activate once a real svg exists");
            tryVerify(function() { return loader.item !== null; }, 5000);

            var arrow = findSvgItem(loader.item);
            verify(arrow, "the activated loader must contain the group-expander SvgItem");
            compare(arrow.svg, realGroupSvg,
                    "the arrow must be born bound to the settled, non-null svg");
        }

        function test_3_emptyingResourcesTearsArrowDownWithoutNullWrite() {
            //! the next style switch empties the list again; if this write
            //! reached an existing SvgItem as null, the harness would
            //! SIGSEGV here rather than fail - surviving it IS the assertion
            //! that the gate destroyed the arrow first
            testRoot.indicator.resources.svgs = [];

            var loader = findGroupLoader(frontLayer);
            tryVerify(function() { return !loader.active; }, 5000,
                      "emptying resources.svgs must close the gate");
            verify(!loader.item, "the arrow must be gone with the gate closed");
        }
    }
}
