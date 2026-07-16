/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.0

BridgeItem {
    id: parabolicBridge

    //! EX-02 (docs/QML_EXTRACTION_PLAN.md): the client's leftover stack
    //! re-enters the host row as another routing call at the neighbor
    //! index (the chain re-entered through a raw signal emission; routing
    //! decisions now live in the host's LatteCore.ParabolicRouter shell)
    function clientRequestUpdateLowerItemScale(newScales) {
        host.routeScalesFromIndex(appletIndex-1, newScales, true);
    }

    function clientRequestUpdateHigherItemScale(newScales) {
        host.routeScalesFromIndex(appletIndex+1, newScales, false);
    }

  /*Be Careful, needs to be considered how to not create
    endless recursion because each one calls the other.
    If client in inside a container and as such is using
    a parabolic host then the parabolic host clearZoom signal
    should be called when needed.

    Connections {
        target: client ? client : null
        onSglClearZoom: {
            if (parabolicBridge.host) {
                parabolicBridge.host.sglClearZoom();
            }
        }
    }*/

    Connections {
        target: parabolicBridge.host ? parabolicBridge.host : null
        function onSglClearZoom() {
            if (parabolicBridge.client) {
                parabolicBridge.client.sglClearZoom();
            }
        }
    }
}
