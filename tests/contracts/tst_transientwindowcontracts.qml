/*
    SPDX-License-Identifier: GPL-2.0-or-later

    Transient-window contracts for applet-created dialogs. See README.md in
    this directory for the contract-suite rules.
*/

import QtQuick 2.15
import QtQuick.Window 2.2
import QtTest 1.2

import org.kde.plasma.core as PlasmaCore

Item {
    id: root
    width: 400
    height: 300

    //! Contract (a negative, and load-bearing exactly because it is one): a
    //! PlasmaCore.Dialog declared inside an Item - the way applets declare
    //! their own viewers, the comic applet's FullViewWidget is the live
    //! case - gets NO transientParent, even once the declaring item's
    //! window is mapped. Qt 6 moved the Qt 5 "windows declared inside items
    //! become transient for the item's window" magic into
    //! QQuickWindowQmlImpl (the QML `Window {}` type); plain QQuickWindow
    //! subclasses registered from C++ no longer get it, and libplasma only
    //! assigns a transient parent when a visualParent is set - which applet
    //! viewers do not do. Corona::eventFilter therefore must resolve the
    //! hosting Latte::View through the dialog's QObject parent chain (see
    //! appletwindowparentingtest.cpp for that half); keying on
    //! transientParent matches nothing. Earned in the plan item
    //! "Applet-created dialogs open on the wrong screen" (2026-07-15): the
    //! first draft of the screen-forwarding fix keyed on transientParent
    //! and this pin is what killed it.
    PlasmaCore.Dialog {
        id: appletStyleDialog
        visible: false
    }

    TestCase {
        name: "TransientWindowContracts"
        when: windowShown

        function test_applet_style_dialog_gets_no_transient_parent() {
            var hostWindow = root.Window.window;
            verify(hostWindow !== null, "the declaring item must live in a window");
            //! give any deferred parenting hook a chance to run before
            //! asserting its absence
            wait(100);
            compare(appletStyleDialog.transientParent, null);
        }
    }
}
