/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.15

import org.kde.latte.core 0.2 as LatteCore

//! Keyboard focus mode traversal (2026-07-17, continuation feature - no
//! Qt5 twin). Active while the C++ View temporarily accepts keyboard
//! focus (View::keyboardNavigationIsActive; Meta+Alt+D or the
//! setViewKeyboardNavigation D-Bus action). Traverses the same 1-based
//! visible entry space Meta+<number> addresses - slot math in the
//! LatteCore.VisibleIndex core over the indexer's rowEntries - and
//! activates through the shortcuts host's activateEntryAtIndex, the exact
//! Meta+N path (a task's click path, an expandable applet's toggle).
//! The focused entry is published through
//! shortcuts.keyboardFocusedEntryIndex (items light their indicator hover
//! state from it) and the position shortcut badges show for orientation,
//! the same view the Meta press-and-hold gives.
//!
//! Keys: arrows step (no wrap - a dock row has real edges), Home/End
//! jump, Enter/Return/Space activate, Escape leaves the mode. Escape is
//! one of the mode's three exit paths; losing window focus (e.g. an
//! activated window taking it) exits from the C++ side regardless of
//! anything here - the handler never has to be alive for the window to
//! return to focus-refusing.

Item {
    id: handler

    //! the C++ Latte::View (a window, not an Item)
    property QtObject view: null
    //! the containment ability hosts (abilities/PositionShortcuts.qml,
    //! abilities/Indexer.qml)
    property Item shortcuts: null
    property Item indexer: null

    readonly property bool navigating: view !== null && view.keyboardNavigationIsActive
    property int currentSlot: -1

    onNavigatingChanged: {
        if (handler.navigating) {
            handler.forceActiveFocus();
            //! stepping 1 by 0 lands on the first slot, or -1 when the
            //! row owns none (an empty dock is navigable-but-empty)
            handler.moveFocusToSlot(LatteCore.VisibleIndex.steppedVisibleSlot(handler.indexer.rowEntries, 1, 0));
            handler.shortcuts.setShowAppletShortcutBadges(true, false, false, -1);
        } else {
            handler.moveFocusToSlot(-1);
            handler.shortcuts.setShowAppletShortcutBadges(false, false, false, -1);
        }
    }

    function moveFocusToSlot(slot: int) {
        handler.currentSlot = slot;
        handler.shortcuts.keyboardFocusedEntryIndex = slot;
    }

    function stepFocus(delta: int) {
        handler.moveFocusToSlot(LatteCore.VisibleIndex.steppedVisibleSlot(handler.indexer.rowEntries, handler.currentSlot, delta));
    }

    function activateFocusedEntry() {
        if (handler.currentSlot >= 1) {
            handler.shortcuts.activateEntryAtIndex(handler.currentSlot);
        }
    }

    Keys.onPressed: (event) => {
        if (!handler.navigating) {
            return;
        }

        switch (event.key) {
        case Qt.Key_Left:
        case Qt.Key_Up:
            handler.stepFocus(-1);
            event.accepted = true;
            break;
        case Qt.Key_Right:
        case Qt.Key_Down:
            handler.stepFocus(1);
            event.accepted = true;
            break;
        case Qt.Key_Home:
            handler.moveFocusToSlot(LatteCore.VisibleIndex.steppedVisibleSlot(handler.indexer.rowEntries, 1, 0));
            event.accepted = true;
            break;
        case Qt.Key_End:
            //! stepping the slot count by 0 is the last slot, or -1 when
            //! the row owns none - the same empty-row convention as Home
            handler.moveFocusToSlot(LatteCore.VisibleIndex.steppedVisibleSlot(handler.indexer.rowEntries, LatteCore.VisibleIndex.countVisibleSlots(handler.indexer.rowEntries), 0));
            event.accepted = true;
            break;
        case Qt.Key_Return:
        case Qt.Key_Enter:
        case Qt.Key_Space:
            handler.activateFocusedEntry();
            event.accepted = true;
            break;
        case Qt.Key_Escape:
            handler.view.exitKeyboardNavigation();
            event.accepted = true;
            break;
        }
    }
}
