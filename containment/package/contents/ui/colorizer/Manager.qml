/*
    SPDX-FileCopyrightText: 2018 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.7

import org.kde.plasma.plasmoid 2.0
import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.kirigami 2.20 as Kirigami

import org.kde.latte.core 0.2 as LatteCore
import org.kde.latte.private.app 0.1 as LatteApp
import org.kde.latte.private.containment 0.1 as LatteContainment

Loader{
    id: manager

    //! the loader loads the backgroundTracker component
    active: root.themeColors === LatteContainment.Types.SmartThemeColors

    readonly property bool backgroundIsBusy: item ? item.isBusy : false

    //! The Plasma 5 global "theme" context property is gone in Plasma 6. The default Plasma
    //! color scheme it exposed is available through themeExtended.defaultTheme (a SchemeColors
    //! object that, unlike Kirigami.Theme, carries the schemeFile/inactive* members this
    //! colorizer compares against and switches between).
    readonly property QtObject plasmaTheme: themeExtended ? themeExtended.defaultTheme : null

    //! D14: the theme source (plasmaTheme/colorPalette, or the Kirigami.Theme
    //! fallback) can serve a default-constructed invalid QColor on a binding's
    //! FIRST evaluation at item creation, before its palette resolves; the
    //! change notify then recomputes with the real color a beat later. Guard
    //! the brightness calls (here and the isLight one below) on color validity
    //! so the invalid interim is not handed to the C++ boundary, which loudly
    //! refuses it (declarativeimports/core/tools.cpp). The valid branch is
    //! unchanged, so the settled result is identical.
    readonly property real originalThemeTextColorBrightness: (plasmaTheme ? plasmaTheme.textColor : Kirigami.Theme.textColor).valid ? LatteCore.Tools.colorBrightness(plasmaTheme ? plasmaTheme.textColor : Kirigami.Theme.textColor) : 0
    readonly property color originalLightTextColor: {
        var base = plasmaTheme ? plasmaTheme : Kirigami.Theme;
        return originalThemeTextColorBrightness > 127.5 ? base.textColor : base.backgroundColor;
    }

    readonly property real themeTextColorBrightness: textColor.valid ? LatteCore.Tools.colorBrightness(textColor) : 0
    readonly property real backgroundColorBrightness: backgroundColor.valid ? LatteCore.Tools.colorBrightness(backgroundColor) : 0

    readonly property color outlineColorBase: backgroundColor
    readonly property real outlineColorBaseBrightness: outlineColorBase.valid ? LatteCore.Tools.colorBrightness(outlineColorBase) : 0
    readonly property color outlineColor: {
        if (!root.panelOutline) {
            return backgroundColor;
        }

        if (outlineColorBaseBrightness > 127.5) {
            return Qt.darker(outlineColorBase, 1.5);
        } else {
            return Qt.lighter(outlineColorBase, 2.2);
        }
    }

    //! same D14 startup transient as the brightness guards above (layout
    //! textColor can be transiently invalid); the "white" fallback is already
    //! valid, so only the layout branch needs guarding.
    readonly property bool editModeTextColorIsBright: editModeTextColor.valid ? LatteCore.Tools.isLight(editModeTextColor) : false
    readonly property color editModeTextColor: latteView && latteView.layout ? latteView.layout.textColor : "white"

    readonly property bool mustBeShown: decider.mustBeShown

    readonly property real currentBackgroundBrightness: item ? item.currentBrightness : -1000

    readonly property bool applyingWindowColors: (root.windowColors === LatteContainment.Types.ActiveWindowColors && latteView && latteView.windowsTracker
                                                  && selectedWindowsTracker.activeWindowScheme)
                                                 || (root.windowColors === LatteContainment.Types.TouchingWindowColors && latteView && latteView.windowsTracker
                                                     && latteView.windowsTracker.currentScreen.touchingWindowScheme)

    //! the selection tree lives in the ColorizerDecision core
    //! (containment/plugin/units/colorizerdecision.h); the decider below
    //! feeds it the environment facts and maps its choice back to objects
    readonly property QtObject applyTheme: decider.applyTheme

    property color applyColor: textColor

    readonly property color backgroundColor: applyTheme ? applyTheme.backgroundColor : Kirigami.Theme.backgroundColor
    readonly property color textColor: {
        //! latteView/layout re-checked at the boundary: the decider's inputs
        //! and this binding update in unspecified relative order, so a stale
        //! useLayoutTextColor must not read a layout that just went away
        if (decider.useLayoutTextColor && latteView && latteView.layout) {
            return latteView.layout.textColor;
        }

        return applyTheme ? applyTheme.textColor : Kirigami.Theme.textColor;
    }

    readonly property color inactiveBackgroundColor: applyTheme === plasmaTheme ? (plasmaTheme ? plasmaTheme.backgroundColor : Kirigami.Theme.backgroundColor) : applyTheme.inactiveBackgroundColor
    readonly property color inactiveTextColor: applyTheme === plasmaTheme ? (plasmaTheme ? plasmaTheme.textColor : Kirigami.Theme.textColor) : applyTheme.inactiveTextColor

    readonly property color highlightColor: applyTheme ? applyTheme.highlightColor : Kirigami.Theme.highlightColor
    readonly property color highlightedTextColor: applyTheme ? applyTheme.highlightedTextColor : Kirigami.Theme.highlightedTextColor
    readonly property color positiveTextColor: applyTheme ? applyTheme.positiveTextColor : Kirigami.Theme.positiveTextColor
    readonly property color neutralTextColor: applyTheme ? applyTheme.neutralTextColor : Kirigami.Theme.neutralTextColor
    readonly property color negativeTextColor: applyTheme ? applyTheme.negativeTextColor : Kirigami.Theme.negativeTextColor

    readonly property color buttonTextColor: applyTheme ? applyTheme.buttonTextColor : Kirigami.Theme.textColor
    readonly property color buttonBackgroundColor: applyTheme ? applyTheme.buttonBackgroundColor : Kirigami.Theme.backgroundColor
    readonly property color buttonHoverColor: applyTheme ? applyTheme.buttonHoverColor : Kirigami.Theme.hoverColor
    readonly property color buttonFocusColor: applyTheme ? applyTheme.buttonFocusColor : Kirigami.Theme.focusColor

    //! the published color-scheme file; latte-aware applets read it through
    //! the bridge's colorPalette, so name and type are public API. A null
    //! schemeColors is the decider's named kdeglobals fallback.
    readonly property string scheme: decider.schemeColors ? decider.schemeColors.schemeFile : "kdeglobals"

    LatteContainment.ColorizerDecider {
        id: decider

        //! candidate palette objects, resolved here where the live tree is
        //! reachable; every DECISION over them lives in the C++ core
        plasmaTheme: manager.plasmaTheme
        darkTheme: themeExtended ? themeExtended.darkTheme : null
        lightTheme: themeExtended ? themeExtended.lightTheme : null
        layoutScheme: latteView && latteView.layout ? latteView.layout.scheme : null
        selectedActiveWindowScheme: selectedWindowsTracker ? selectedWindowsTracker.activeWindowScheme : null
        currentScreenActiveWindowScheme: latteView && latteView.windowsTracker ? latteView.windowsTracker.currentScreen.activeWindowScheme : null
        touchingWindowScheme: latteView && latteView.windowsTracker ? latteView.windowsTracker.currentScreen.touchingWindowScheme : null

        themeColors: root.themeColors
        windowColors: root.windowColors

        graphicsSystemAccelerated: root.environment.isGraphicsSystemAccelerated
        compositingActive: LatteCore.WindowSystem.compositingActive
        themeExtendedExists: !!themeExtended
        plasmaThemeIsLight: themeExtended ? themeExtended.isLightTheme : false
        windowsTrackerEnabled: !!(latteView && latteView.windowsTracker)
        existsWindowTouching: latteView && latteView.windowsTracker ? latteView.windowsTracker.currentScreen.existsWindowTouching : false
        existsWindowTouchingEdge: latteView && latteView.windowsTracker ? latteView.windowsTracker.currentScreen.existsWindowTouchingEdge : false
        activeWindowTouching: latteView && latteView.windowsTracker ? latteView.windowsTracker.currentScreen.activeWindowTouching : false
        activeWindowTouchingEdge: latteView && latteView.windowsTracker ? latteView.windowsTracker.currentScreen.activeWindowTouchingEdge : false
        layoutExists: !!(latteView && latteView.layout)
        plasmaBackgroundForPopups: root.plasmaBackgroundForPopups
        hasExpandedApplet: root.hasExpandedApplet
        userShowPanelBackground: root.userShowPanelBackground
        plasmaStyleBusyForTouchingBusyVerticalView: root.plasmaStyleBusyForTouchingBusyVerticalView
        forceSolidPanel: root.forceSolidPanel
        forcePanelForBusyBackground: root.forcePanelForBusyBackground
        inConfigureAppletsMode: root.inConfigureAppletsMode
        editModeTextColorIsBright: manager.editModeTextColorIsBright
        currentBackgroundBrightness: manager.currentBackgroundBrightness
        backgroundStoredOpacity: root.myView.backgroundStoredOpacity
    }

    sourceComponent: LatteApp.BackgroundTracker {
        activity: root.myView.isReady ? root.myView.lastUsedActivity : ""
        location: Plasmoid.location
        screenName: latteView && latteView.positioner ? latteView.positioner.currentScreenName : ""
    }
}
