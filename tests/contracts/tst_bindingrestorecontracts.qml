/*
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, transplanted)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later

    Qt 6 Binding.restoreMode contracts. Qt 6 changed the default restoreMode
    from Qt 5's RestoreNone to RestoreBindingOrValue, so a "Binding { when: }"
    meant to FREEZE its target's last value on deactivation instead RESETS it
    to the declared default. Latte leans on the freeze idiom everywhere an
    applet's captured layout length must persist while zoomScale rises above 1
    (the applet vanished on hover under the Qt 6 default), while relocation
    hiding runs, or while a drag/reorder blocks updates - 23 shipped QML files
    carry restoreMode: Binding.RestoreNone for exactly this reason (e.g.
    containment applet ItemWrapper sizing, LayoutsContainer, the Indexer
    abilities). scripts/qml-effect-rules.sh enforces the source rule (every
    when-gated Binding declares an explicit restoreMode); THIS test pins the
    upstream semantics that make the rule necessary, so a Qt pin bump that
    changes them again fails here first. Transplanted from latte-dock-qt6's
    appletzoomsizetest, reshaped into the qmltest contract suite.
*/

import QtQuick 2.15
import QtTest 1.2

TestCase {
    id: root
    name: "Qt6BindingRestoreContracts"
    when: windowShown

    //! the freeze idiom Latte ships: captured length survives the gate closing
    Component {
        id: frozenZoomComponent
        Item {
            id: frozenItem
            property real zoomScale: 1
            property real layoutLength: 0
            readonly property real scaledLength: zoomScale * layoutLength
            Binding {
                target: frozenItem
                property: "layoutLength"
                when: frozenItem.zoomScale === 1
                value: 64
                restoreMode: Binding.RestoreNone
            }
        }
    }

    //! the same shape WITHOUT restoreMode - the Qt 6 default trap, kept as a
    //! living definition of what the shipped code must avoid
    Component {
        id: defaultModeZoomComponent
        Item {
            id: defaultItem
            property real zoomScale: 1
            property real layoutLength: 0
            readonly property real scaledLength: zoomScale * layoutLength
            Binding {
                target: defaultItem
                property: "layoutLength"
                when: defaultItem.zoomScale === 1
                value: 64
            }
        }
    }

    //! Contract: with RestoreNone the captured value persists once "when"
    //! turns false, so zoomScale * layoutLength grows on hover instead of
    //! collapsing to zero.
    function test_restoreNoneKeepsCapturedValueWhenGateCloses() {
        var item = createTemporaryObject(frozenZoomComponent, root);
        verify(item);
        compare(item.scaledLength, 64);

        item.zoomScale = 1.84375;
        compare(item.scaledLength, 1.84375 * 64,
                "RestoreNone no longer freezes a when-gated Binding's last value - "
                + "every freeze site in shipped QML needs a re-audit");
    }

    //! Contract: the Qt 6 default (RestoreBindingOrValue) resets the target
    //! to its declared value (0) on deactivation - the exact regression that
    //! made hovered applets vanish. If this ever stops failing the reset,
    //! the default changed again and the source rule may be obsolete.
    function test_qt6DefaultResetsToDeclaredValueWhenGateCloses() {
        var item = createTemporaryObject(defaultModeZoomComponent, root);
        verify(item);
        compare(item.scaledLength, 64);

        item.zoomScale = 1.84375;
        compare(item.scaledLength, 0,
                "the Qt 6 default restoreMode no longer resets when-gated Bindings - "
                + "re-read the restoreMode rule in scripts/qml-effect-rules.sh");
    }
}
