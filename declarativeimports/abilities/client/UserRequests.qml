/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.0

import org.kde.latte.abilities.definition 0.1 as AbilityDefinition

AbilityDefinition.UserRequests {
    id: _userRequests
    property Item bridge: null
    readonly property bool isActive: bridge !== null

    //! the bridge whose host signal is currently forwarded; a disconnect only
    //! works against the exact object the connect was made on, so it must be
    //! remembered instead of recomputed from the (possibly already changed)
    //! bridge property. The old code connected on every isActive rise without
    //! ever disconnecting - it even re-CONNECTED from Component.onDestruction -
    //! so every destroyed applet left the host signal wired to a dead client.
    property Item connectedBridge: null

    function updateSignals() {
        if (connectedBridge === bridge) {
            return;
        }

        if (connectedBridge) {
            connectedBridge.userRequests.sglViewType.disconnect(_userRequests.sglViewType);
        }

        connectedBridge = bridge;

        if (connectedBridge) {
            connectedBridge.userRequests.sglViewType.connect(_userRequests.sglViewType);
        }
    }

    onBridgeChanged: updateSignals()
    Component.onCompleted: updateSignals()

    Component.onDestruction: {
        if (connectedBridge) {
            connectedBridge.userRequests.sglViewType.disconnect(_userRequests.sglViewType);
            connectedBridge = null;
        }
    }
}
