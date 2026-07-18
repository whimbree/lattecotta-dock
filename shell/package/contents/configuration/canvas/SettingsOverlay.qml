/*
    SPDX-FileCopyrightText: 2019 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.7
import QtQuick.Layouts 1.1

import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.plasma.components 3.0 as PlasmaComponents

import org.kde.latte.core 0.2 as LatteCore
import org.kde.latte.components 1.0 as LatteComponents

import "controls" as SettingsControls
import "maxlength" as MaximumLength

//import "../../code/ColorizerTools.js" as ColorizerTools

Item{
    id: settingsRoot
    readonly property bool containsMouse: false /*headerSettings.containsMouse || ruler.containsMouse
                                          || tooltipMouseArea.containsMouse || editBackMouseArea.containsMouse*/
    readonly property int thickness: ruler.thickness + headerSettings.thickness + spacing * 6
    readonly property int spacing: 4

    //! The rearrange/exit toggle button, surfaced up to CanvasConfiguration so CanvasConfigView can keep
    //! exactly its rect interactive while the rest of the canvas is click-through in configure-applets mode.
    property alias rearrangeToggle: headerSettings.rearrangeButton

    property int textShadow: {
        if (textColorIsDark)  {
            return 1;
        } else {
            return 6;
        }
    }

    property string tooltip: ""

    //! D14: textColor derives from the theme palette, which is transiently
    //! invalid on first evaluation at creation; guard so the invalid interim is
    //! not handed to the C++ boundary (tools.cpp). Valid branch unchanged.
    readonly property real textColorBrightness: textColor.valid ? LatteCore.Tools.colorBrightness(textColor) : 0
    readonly property bool textColorIsDark: textColorBrightness < 127.5

    readonly property color bestContrastedTextColor: {
        if (imageTiler.opacity <= 0.4 && !universalSettings.inConfigureAppletsMode && themeExtended) {
            return latteView.colorizer.currentBackgroundBrightness > 127.5 ?
                        themeExtended.lightTheme.textColor :
                        themeExtended.darkTheme.textColor;
        }

        return latteView && latteView.layout ? latteView.layout.textColor : "#D7E3FF";
    }

    readonly property color textColor: bestContrastedTextColor

    layer.enabled: graphicsSystem.isAccelerated
    layer.effect: LatteComponents.ShadowedItem{
        shadowSizePx: settingsRoot.textShadow
        shadowColor: root.appShadowColorSolid
        shadowVerticalOffset: 0
    }

    HeaderSettings{
        id: headerSettings
    }

    MaximumLength.Ruler {
        id: ruler
        //! Horizontal docks: flush with the canvas' inner edge, so the ruler
        //! marks exactly where the blueprint area begins (both the canvas and
        //! the blueprint span editThickness from the screen edge). Vertical
        //! docks keep the stacking margin, their header shares the same band.
        thicknessMargin: root.isHorizontal ? 0 : headerSettings.thickness + 3 * spacing
        thickMargin: 3
    }
}
