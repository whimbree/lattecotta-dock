/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Regression tests for the shell CompactApplet expander, driving the REAL
//! shipped file (imported straight from the shell package, same pattern as
//! tst_autosize.qml driving autosize.js).
//!
//! Two landed fixes are pinned here:
//!
//! - 437d9a0c: applet popups size to the Plasma 6 contract (libplasma v6.6.5
//!   appletpopup.cpp LayoutChangedProxy): the full representation's IMPLICIT
//!   size is the live base, Layout.preferred overrides it when set,
//!   Layout.minimum is enforced, and everything stays a live binding because
//!   Plasma 6 applets grow their implicit size as content builds AFTER the
//!   representation swap (the volume applet reported implicit 0x0 at swap
//!   time and 213x108 by first show). The Qt5-era chain sampled once and
//!   latched a feedback loop through the rep's live width.
//!
//! - 1aa5238c: releasing an outgoing full representation detaches it to a
//!   null parent and leaves visibility alone. AppletQuickItem re-parents the
//!   same cached item into itself for the inline representation switch and
//!   never calls setVisible(true) there, so a rep hidden by a past release
//!   stayed invisible forever (the comic rendered an empty slot on hover).

import QtQuick
import QtQuick.Layouts
import QtTest

import "../../shell/package/contents/applet" as LatteShellApplet

Item {
    id: testRoot
    width: 800
    height: 600

    LatteShellApplet.CompactApplet {
        id: expander
    }

    //! stand-in for a Plasma 6 full representation: implicit size and Layout
    //! hints adjustable per test, exactly the inputs the sizing chain reads
    Component {
        id: repComponent
        Item {
            property real prefW: -1
            property real prefH: -1
            property real minW: 0
            property real minH: 0
            Layout.preferredWidth: prefW
            Layout.preferredHeight: prefH
            Layout.minimumWidth: minW
            Layout.minimumHeight: minH
        }
    }

    TestCase {
        id: testCase
        name: "CompactAppletFullRepresentation"
        when: windowShown

        //! the popup dialog is an id-internal of CompactApplet; it publishes
        //! objectName "popupWindow" so tests and probes can reach it
        property QtObject popupWindow: null

        function init() {
            if (!popupWindow) {
                for (var i = 0; i < expander.data.length; ++i) {
                    if (expander.data[i].objectName === "popupWindow") {
                        popupWindow = expander.data[i];
                        break;
                    }
                }
            }
            verify(popupWindow, "CompactApplet must expose its popup dialog as objectName popupWindow");
            expander.fullRepresentation = null;
            popupWindow.customPopupSize = Qt.size(-1, -1);
        }

        function makeRep(props) {
            var rep = createTemporaryObject(repComponent, testRoot, props || {});
            verify(rep);
            return rep;
        }

        //! 437d9a0c: the volume-applet shape. Implicit size is 0x0 at swap
        //! time and only grows once content builds; the popup must track it
        //! live instead of latching the value sampled at swap time.
        function test_sizingTracksImplicitSizeAsContentSettles() {
            var rep = makeRep();
            expander.fullRepresentation = rep;

            //! no hints at all yet: the legacy fallback must produce a real
            //! size, not 0 (an M-metrics based default)
            verify(popupWindow.mainItem.width > 0,
                   "zero-hint fallback width must be positive");
            verify(popupWindow.mainItem.height > 0,
                   "zero-hint fallback height must be positive");

            //! content settles after the swap - the once-sampled Qt5 chain
            //! could never see this
            rep.implicitWidth = 213;
            rep.implicitHeight = 108;
            compare(popupWindow.mainItem.width, 213,
                    "popup width must follow implicit width growing after the swap");
            compare(popupWindow.mainItem.height, 108,
                    "popup height must follow implicit height growing after the swap");

            //! and keeps following (no one-shot re-evaluation)
            rep.implicitWidth = 320;
            compare(popupWindow.mainItem.width, 320,
                    "popup width must keep tracking implicit width");
        }

        //! 437d9a0c: Layout.preferred overrides implicit when set, and
        //! releasing the override falls back to the live implicit base.
        function test_sizingPreferredOverridesImplicit() {
            var rep = makeRep({ implicitWidth: 213, implicitHeight: 108 });
            expander.fullRepresentation = rep;

            rep.prefW = 400;
            compare(popupWindow.mainItem.width, 400,
                    "preferred width must override the implicit base");
            compare(popupWindow.mainItem.height, 108,
                    "unset preferred height must leave the implicit base in charge");

            rep.prefW = -1;
            compare(popupWindow.mainItem.width, 213,
                    "clearing preferred must fall back to implicit, live");
        }

        //! 437d9a0c: Layout.minimum is enforced over every base. The live
        //! volume applet carries a 252px Layout.minimumWidth that the old
        //! chain ignored, clipping every device row.
        function test_sizingMinimumIsEnforced() {
            var rep = makeRep({ implicitWidth: 213, implicitHeight: 108 });
            expander.fullRepresentation = rep;

            rep.minW = 250;
            compare(popupWindow.mainItem.width, 250,
                    "minimum width must win over a smaller implicit base");

            //! preferred above the minimum wins
            rep.prefW = 400;
            compare(popupWindow.mainItem.width, 400);

            //! preferred below the minimum is clamped up
            rep.prefW = 220;
            compare(popupWindow.mainItem.width, 250,
                    "minimum width must win over a smaller preferred override");

            rep.minH = 300;
            compare(popupWindow.mainItem.height, 300,
                    "minimum height must win over a smaller implicit base");
        }

        //! resizable persistent popups: a user-chosen size (persisted as
        //! popupWidth/popupHeight, surfaced by the dialog as
        //! customPopupSize) overrides BOTH the implicit base and the
        //! Layout.preferred override - it is the window size the user last
        //! chose, exactly plasmashell's explicit-config-size semantics -
        //! and clearing it returns the chain to the live hints.
        function test_customSizeOverridesHintsAndClearsLive() {
            var rep = makeRep({ implicitWidth: 213, implicitHeight: 108 });
            expander.fullRepresentation = rep;
            rep.prefW = 400;

            popupWindow.customPopupSize = Qt.size(500, 350);
            compare(popupWindow.mainItem.width, 500,
                    "custom width must beat implicit and preferred");
            compare(popupWindow.mainItem.height, 350,
                    "custom height must beat the implicit base");

            //! hint churn while a custom size holds must not move the popup
            rep.implicitWidth = 320;
            rep.prefW = 450;
            compare(popupWindow.mainItem.width, 500,
                    "hint changes must not disturb a custom size");

            //! reset (the context menu entry deletes the keys): back to the
            //! live hint chain, not to a latched value
            popupWindow.customPopupSize = Qt.size(-1, -1);
            compare(popupWindow.mainItem.width, 450,
                    "clearing the custom size must return to the live hint chain");
            compare(popupWindow.mainItem.height, 108);
        }

        //! resizable persistent popups: Layout.minimum stays enforced over
        //! a custom size, mirroring plasmashell's updateMinSize() which
        //! grows even an explicitly configured popup when the minimum does.
        function test_customSizeRespectsMinimum() {
            var rep = makeRep({ implicitWidth: 213, implicitHeight: 108 });
            expander.fullRepresentation = rep;

            rep.minW = 250;
            rep.minH = 200;
            popupWindow.customPopupSize = Qt.size(220, 150);

            compare(popupWindow.mainItem.width, 250,
                    "minimum width must win over a smaller custom size");
            compare(popupWindow.mainItem.height, 200,
                    "minimum height must win over a smaller custom size");
        }

        //! 1aa5238c: the adopt side of the contract - an adopted rep becomes
        //! the popup's content: reparented, filled, and explicitly shown.
        function test_adoptedRepIsParentedFilledAndShown() {
            var rep = makeRep({ implicitWidth: 100, implicitHeight: 100, visible: false });
            expander.fullRepresentation = rep;

            compare(rep.parent, popupWindow.mainItem,
                    "adopted rep must live inside the popup's mainItem");
            compare(rep.anchors.fill, popupWindow.mainItem,
                    "adopted rep must fill the popup's mainItem");
            verify(rep.visible,
                   "adopting must show the rep: it is about to be the popup content");
        }

        //! 1aa5238c: the release side - the outgoing rep is detached to a
        //! NULL parent (the same release AppletQuickItem performs when
        //! un-swapping) with anchors neutralized, and its visibility is left
        //! alone. Hiding it here is the exact trap that left the comic's
        //! cached fullRepresentationItem invisible forever, because
        //! compactRepresentationCheck() re-parents the cached item without
        //! ever calling setVisible(true).
        //!
        //! Reading .visible on the detached item cannot pin this (a
        //! parentless QQuickItem reports effective visibility, always
        //! false), so the test performs AppletQuickItem's own re-use step -
        //! a bare re-parent with no setVisible(true) - and asserts the item
        //! comes back visible. Had the release hidden it, this is precisely
        //! the sequence that rendered the empty slot.
        function test_releasedRepKeepsVisibilityAndDetaches() {
            var rep1 = makeRep({ implicitWidth: 100, implicitHeight: 100 });
            expander.fullRepresentation = rep1;
            verify(rep1.visible);

            var rep2 = makeRep({ implicitWidth: 120, implicitHeight: 120 });
            expander.fullRepresentation = rep2;

            compare(rep1.parent, null,
                    "released rep must be detached to a null parent, leaving the popup's scenegraph");
            compare(rep1.anchors.fill, null,
                    "released rep must have its fill anchors neutralized");

            //! AppletQuickItem's inline switch: re-parent only, never re-show
            rep1.parent = testRoot;
            verify(rep1.visible,
                   "release must NOT hide the rep: AppletQuickItem re-uses it with a bare re-parent");
            rep1.parent = null;

            //! releasing to no representation at all takes the same path
            expander.fullRepresentation = null;
            compare(rep2.parent, null);
            rep2.parent = testRoot;
            verify(rep2.visible,
                   "release on switch-to-null must not hide the rep either");
            rep2.parent = null;
        }

        //! 1aa5238c round trip: a rep that comes back hidden (any past code
        //! path or owner may have hidden it) must still be shown on adoption.
        function test_readoptedHiddenRepIsShownAgain() {
            var rep1 = makeRep({ implicitWidth: 100, implicitHeight: 100 });
            var rep2 = makeRep({ implicitWidth: 120, implicitHeight: 120 });

            expander.fullRepresentation = rep1;
            expander.fullRepresentation = rep2;

            rep1.visible = false;
            expander.fullRepresentation = rep1;

            verify(rep1.visible,
                   "re-adopting a hidden cached rep must show it: it is the popup content now");
            compare(rep1.parent, popupWindow.mainItem);
        }
    }
}
