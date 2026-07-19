/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.0

import "./paraboliceffect" as ParabolicEffectTypes

Item {
    property bool isEnabled: false
    property bool restoreZoomIsBlocked: false

    property int spread: 3

    property ParabolicEffectTypes.Factor factor: ParabolicEffectTypes.Factor{
        zoom: 1.6
        maxZoom: 1.6
        marginThicknessZoomInPercentage: 1.0
    }

    readonly property ParabolicEffectTypes.PrivateProperties _privates: ParabolicEffectTypes.PrivateProperties {
        directRenderingEnabled: false
    }

    property Item currentParabolicItem: null

    signal sglClearZoom();
    signal sglUpdateLowerItemScale(int delegateIndex, variant newScales);
    signal sglUpdateHigherItemScale(int delegateIndex, variant newScales);

    //! EX-02/EX-03 (docs/tracking/QML_EXTRACTION_PLAN.md): the curve math lives in
    //! LatteCore.ParabolicMath and the routing in LatteCore.ParabolicRouter;
    //! applyParabolicEffect is implemented by the two shells that own a row
    //! (the containment host ability and the plasmoid client ability), not
    //! here - a bare definition instance is a value holder only
    readonly property int _spreadSteps: (spread - 1) / 2
}
