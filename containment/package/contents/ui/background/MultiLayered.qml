/*
    SPDX-FileCopyrightText: 2016 Smith AR <audoban@openmailbox.org>
    SPDX-FileCopyrightText: 2016 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.1
import QtQuick.Layouts 1.1
import QtQuick.Window 2.2

import org.kde.plasma.plasmoid 2.0

import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.ksvg 1.0 as KSvg
import org.kde.plasma.components 3.0 as PlasmaComponents
import org.kde.kquickcontrolsaddons 2.0

import org.kde.latte.core 0.2 as LatteCore
import org.kde.latte.components 1.0 as LatteComponents
import org.kde.latte.private.containment 0.1 as LatteContainment

import "../colorizer" as Colorizer

BackgroundProperties{
    id:barLine

    //! stateless resolver for the per-edge padding math and the effects
    //! area rect (EX-13; units/backgroundstate.h)
    LatteContainment.BackgroundStateResolver {
        id: backgroundStateResolver
    }

    readonly property alias panelBackgroundSvg: solidBackground
    readonly property int customShadowPaintMargin: LatteComponents.EffectMetrics.shadowPaddingFor(
                                                       Math.max(0, customShadow), 0, 0)

    //! Layer 0: Multi-Layer container in order to provide a consistent final element that acts
    //! as a single entity/background
    width: root.isHorizontal ? totals.visualLength : 16
    height: root.isVertical ? totals.visualLength : 16

    opacity: root.useThemePanel ? 1 : 0
    currentOpacity: overlayedBackground.backgroundOpacity>0 ? overlayedBackground.backgroundOpacity : solidBackground.opacity

    isShown: (solidBackground.opacity > 0) || (overlayedBackground.backgroundOpacity > 0)

    hasAllBorders: solidBackground.enabledBorders === KSvg.FrameSvg.AllBorders
    hasLeftBorder: hasAllBorders || ((solidBackground.enabledBorders & KSvg.FrameSvg.LeftBorder) > 0)
    hasRightBorder: hasAllBorders || ((solidBackground.enabledBorders & KSvg.FrameSvg.RightBorder) > 0)
    hasTopBorder: hasAllBorders || ((solidBackground.enabledBorders & KSvg.FrameSvg.TopBorder) > 0)
    hasBottomBorder: hasAllBorders || ((solidBackground.enabledBorders & KSvg.FrameSvg.BottomBorder) > 0)

    shadows.left: hasLeftBorder && root.behaveAsDockWithMask ? (customShadowIsEnabled ? barLine.customShadowPaintMargin : shadowsSvgItem.margins.left) : 0
    shadows.right: hasRightBorder && root.behaveAsDockWithMask ? (customShadowIsEnabled ? barLine.customShadowPaintMargin : shadowsSvgItem.margins.right) : 0
    shadows.top: hasTopBorder && root.behaveAsDockWithMask ? (customShadowIsEnabled ? barLine.customShadowPaintMargin : shadowsSvgItem.margins.top) : 0
    shadows.bottom: hasBottomBorder && root.behaveAsDockWithMask ? (customShadowIsEnabled ? barLine.customShadowPaintMargin : shadowsSvgItem.margins.bottom) : 0

    shadows.fixedLeft: (customDefShadowIsEnabled || customUserShadowIsEnabled) ? barLine.customShadowPaintMargin : shadowsSvgItem.fixedMargins.left
    shadows.fixedRight: (customDefShadowIsEnabled || customUserShadowIsEnabled) ? barLine.customShadowPaintMargin : shadowsSvgItem.fixedMargins.right
    shadows.fixedTop: (customDefShadowIsEnabled || customUserShadowIsEnabled) ? barLine.customShadowPaintMargin : shadowsSvgItem.fixedMargins.top
    shadows.fixedBottom: (customDefShadowIsEnabled || customUserShadowIsEnabled) ? barLine.customShadowPaintMargin : shadowsSvgItem.fixedMargins.bottom

    //! it can accept negative values in DockMode
    screenEdgeMargin: root.screenEdgeMarginEnabled ? metrics.margin.screenEdge - shadows.tailThickness : -shadows.tailThickness

    //! per-edge padding math delegates to the BackgroundState core (EX-13):
    //! edges at the ends of the length axis (top/bottom of a vertical view,
    //! left/right of a horizontal one) carry the roundness treatment, the
    //! others take the theme padding as-is
    paddings.top: backgroundStateResolver.edgePadding(barLine.hasTopBorder,
                                                      root.isVertical,
                                                      barLine.customRadiusIsEnabled,
                                                      barLine.customRadius,
                                                      barLine.themeExtendedBackground ? barLine.themeExtendedBackground.paddingTop : 0,
                                                      solidBackground.margins.top,
                                                      metrics.margin.length,
                                                      indicators.info.backgroundCornerMargin)
    paddings.bottom: backgroundStateResolver.edgePadding(barLine.hasBottomBorder,
                                                         root.isVertical,
                                                         barLine.customRadiusIsEnabled,
                                                         barLine.customRadius,
                                                         barLine.themeExtendedBackground ? barLine.themeExtendedBackground.paddingBottom : 0,
                                                         solidBackground.margins.bottom,
                                                         metrics.margin.length,
                                                         indicators.info.backgroundCornerMargin)
    paddings.left: backgroundStateResolver.edgePadding(barLine.hasLeftBorder,
                                                       root.isHorizontal,
                                                       barLine.customRadiusIsEnabled,
                                                       barLine.customRadius,
                                                       barLine.themeExtendedBackground ? barLine.themeExtendedBackground.paddingLeft : 0,
                                                       solidBackground.margins.left,
                                                       metrics.margin.length,
                                                       indicators.info.backgroundCornerMargin)
    paddings.right: backgroundStateResolver.edgePadding(barLine.hasRightBorder,
                                                        root.isHorizontal,
                                                        barLine.customRadiusIsEnabled,
                                                        barLine.customRadius,
                                                        barLine.themeExtendedBackground ? barLine.themeExtendedBackground.paddingRight : 0,
                                                        solidBackground.margins.right,
                                                        metrics.margin.length,
                                                        indicators.info.backgroundCornerMargin)

    length: {
        if (root.behaveAsPlasmaPanel && LatteCore.WindowSystem.compositingActive) {
            return root.isVertical ? root.height : root.width;
        }

        const maximumLength = root.maxLength;
        const requestedLength = myView.alignment === LatteCore.Types.Justify
                ? maximumLength
                : Math.max(root.minLength,
                           layoutsContainerItem.mainLayout.length + barLine.totals.paddingsLength);

        if (maximumLength <= 0) {
            //! Startup has not published a usable view span yet. The dock is
            //! offscreen in this state; the settled binding reevaluates once
            //! the positioner supplies real geometry.
            return requestedLength;
        }

        //! Resting end padding is presentation space, not a permanent hover
        //! reserve. Parabolic zoom may borrow it, while the rounded background
        //! and its drop shadow remain inside the configured primary span.
        return backgroundStateResolver.dockBackgroundLength(requestedLength,
                                                             maximumLength,
                                                             barLine.totals.shadowsLength);
    }

    thickness: {
        if (root.behaveAsPlasmaPanel) {
            return metrics.totals.thickness;
        } else {
            return Math.min(metrics.totals.thickness, barLine.totals.visualThickness);
        }
    }

    offset: {
        if (behaveAsPlasmaPanel) {
            return 0;
        }

        if (Plasmoid.formFactor === PlasmaCore.Types.Horizontal) {
            if (myView.alignment === LatteCore.Types.Left) {
                return root.offset - barLine.shadows.left;
            } else if (myView.alignment === LatteCore.Types.Right) {
                return root.offset - barLine.shadows.right;
            }
        }

        if (Plasmoid.formFactor === PlasmaCore.Types.Vertical) {
            if (myView.alignment === LatteCore.Types.Top) {
                return root.offset - barLine.shadows.top;
            } else if (myView.alignment === LatteCore.Types.Bottom) {
                return root.offset - barLine.shadows.bottom;
            }
        }

        if (myView.alignment === LatteCore.Types.Justify) {
            return 0;
        }

        if (myView.alignment !== LatteCore.Types.Center) {
            return root.offset;
        }

        const requestedOffset = root.offset + layoutsContainerItem.mainLayout.parabolicOffsetting;
        const viewPrimaryLength = Plasmoid.formFactor === PlasmaCore.Types.Horizontal
                ? barLine.parent.width : barLine.parent.height;
        if (viewPrimaryLength < barLine.totals.visualLength) {
            //! Same explicit startup state as the length binding above.
            return requestedOffset;
        }

        return backgroundStateResolver.centeredDockOffset(requestedOffset,
                                                          barLine.totals.visualLength,
                                                          viewPrimaryLength);
    }

    totals.visualThickness: {
        var itemMargins = 2*metrics.margin.tailThickness;
        var maximumItem = metrics.iconSize + itemMargins;

        if (totals.minThickness < maximumItem) {
            maximumItem = maximumItem - totals.minThickness;
        }

        var percentage = LatteCore.WindowSystem.compositingActive ? Plasmoid.configuration.panelSize/100 : 1;
        return Math.max(totals.minThickness, totals.minThickness + (percentage*maximumItem));
    }

    totals.visualMaxThickness: {
        var itemMargins = 2*metrics.margin.maxTailThickness;
        var maximumItem = metrics.maxIconSize + itemMargins;

        if (totals.minThickness < maximumItem) {
            maximumItem = maximumItem - totals.minThickness;
        }

        var percentage = LatteCore.WindowSystem.compositingActive ? Plasmoid.configuration.panelSize/100 : 1;
        return Math.max(totals.minThickness, totals.minThickness + (percentage*maximumItem));
    }

    totals.visualLength: {
        if (root.behaveAsPlasmaPanel) {
            return root.isVertical ? root.height : root.width;
        }

        return Math.max(background.length + totals.shadowsLength, totals.paddingsLength + totals.shadowsLength)
    }

    readonly property int tailRoundness: {
        if ((root.isHorizontal && hasLeftBorder) || (!root.isHorizontal && hasTopBorder)) {
            var customAppliedRadius = customRadiusIsEnabled ? customRadius : 0;
            var themePadding = themeExtendedBackground ? (root.isHorizontal ? themeExtendedBackground.paddingLeft : themeExtendedBackground.paddingTop) : 0;
            var solidBackgroundPadding = root.isHorizontal ? solidBackground.margins.left : solidBackground.margins.top;
            var expected = customRadiusIsEnabled ? customAppliedRadius : Math.max(themePadding, solidBackgroundPadding);
            return Math.max(0, expected - metrics.margin.length);
        }

        return 0;
    }

    readonly property int headRoundness: {
        if ((root.isHorizontal && hasRightBorder) || (!root.isHorizontal && hasBottomBorder)) {
            var customAppliedRadius = customRadiusIsEnabled ? customRadius : 0;
            var themePadding = themeExtendedBackground ? (root.isHorizontal ? themeExtendedBackground.paddingRight : themeExtendedBackground.paddingBottom) : 0;
            var solidBackgroundPadding = root.isHorizontal ? solidBackground.margins.right : solidBackground.margins.bottom;
            var expected = customRadiusIsEnabled ? customAppliedRadius : Math.max(themePadding, solidBackgroundPadding);
            return Math.max(0, expected - metrics.margin.length);
        }

        return 0;
    }

    readonly property int tailRoundnessMargin: {
        //! used from contents geometry in order to remove any roundness sectors, e.g. for popups placement
        if (root.isHorizontal) {
            return paddings.left > metrics.margin.length ? metrics.margin.length : 0
        } else {
            return paddings.top > metrics.margin.length ? metrics.margin.length : 0
        }
    }

    readonly property int headRoundnessMargin: {
        //! used from contents geometry in order to remove any roundness sectors, e.g. for popups placement
        if (root.isHorizontal) {
            return paddings.right > metrics.margin.length ? metrics.margin.length : 0
        } else {
            return paddings.bottom > metrics.margin.length ? metrics.margin.length : 0
        }
    }

    property int animationTime: 6*animations.speedFactor.current*animations.duration.small

    //! Opacity related
    readonly property bool isDefaultOpacityEnabled: Plasmoid.configuration.panelTransparency===-1

    //! Metrics related
    readonly property bool isGreaterThanItemThickness: root.useThemePanel && (totals.visualThickness >= (metrics.iconSize + metrics.margin.tailThickness))

    //! CustomShadowedRectangle  properties
    readonly property bool customShadowedRectangleIsEnabled: customRadiusIsEnabled || (customDefShadowIsEnabled || customUserShadowIsEnabled)

    readonly property bool customShadowIsSupported: LatteCore.WindowSystem.compositingActive
                                                    && kirigamiLibraryIsFound

    //!current shadow state but do not change other values of normal mode, for example if a Dock hides its screen edge thickness
    //!shouldn't change the fact that customShadowedRectangle is still used
    readonly property bool customShadowIsEnabled: (customDefShadowIsEnabled || customUserShadowIsEnabled) && panelShadowsActive
    readonly property bool customDefShadowIsEnabled: customShadowIsSupported && !customUserShadowIsEnabled && customRadiusIsEnabled
    readonly property bool customUserShadowIsEnabled: customShadowIsSupported && Plasmoid.configuration.backgroundShadowSize >= 0

    readonly property bool customRadiusIsEnabled: kirigamiLibraryIsFound && Plasmoid.configuration.backgroundRadius >= 0

    readonly property int customRadius: {
        if (customDefShadowIsEnabled && !customRadiusIsEnabled && themeExtendedBackground) {
            return themeExtendedBackground.roundness;
        }

        return Plasmoid.formFactor === PlasmaCore.Types.Horizontal ?
                    (Plasmoid.configuration.backgroundRadius/100) * solidBackground.height :
                    (Plasmoid.configuration.backgroundRadius/100) * solidBackground.width
    }
    readonly property int customShadow: {
        if (customDefShadowIsEnabled && themeExtendedBackground) {
            return themeExtendedBackground.shadowSize;
        }

        return Plasmoid.configuration.backgroundShadowSize;
    }

    readonly property color customShadowColor: themeExtendedBackground ? themeExtendedBackground.shadowColor : "black"

    property QtObject themeExtendedBackground: null

    Behavior on opacity{
        enabled: LatteCore.WindowSystem.compositingActive
        NumberAnimation {
            duration: barLine.animationTime
        }
    }

    Behavior on opacity{
        enabled: !LatteCore.WindowSystem.compositingActive
        NumberAnimation {
            duration: 0
        }
    }

    Binding {
        target: barLine
        property: "themeExtendedBackground"
        when: themeExtended
        value: {
            if (!themeExtended) {
                return null;
            }
            switch(Plasmoid.location) {
            case PlasmaCore.Types.BottomEdge: return themeExtended.backgroundBottomEdge;
            case PlasmaCore.Types.LeftEdge: return themeExtended.backgroundLeftEdge;
            case PlasmaCore.Types.TopEdge: return themeExtended.backgroundTopEdge;
            case PlasmaCore.Types.RightEdge: return themeExtended.backgroundRightEdge;
            default: return null;
            }
        }
        restoreMode: Binding.RestoreNone
    }

    onXChanged: solidBackground.updateEffectsArea();
    onYChanged: solidBackground.updateEffectsArea();
    onScreenEdgeMarginChanged: solidBackground.updateEffectsArea();

    //! Layer 1: Shadows that are drawn around the background but always inside the View window (these are internal drawn shadows).
    //!          When the container has chosen external shadows (these are shadows that are drawn out of the View window from the compositor)
    //!          in such case the internal drawn shadows are NOT drawn at all.
    KSvg.FrameSvgItem{
        id: shadowsSvgItem
        width: root.isVertical ?  background.thickness + totals.shadowsThickness : totals.visualLength
        height: root.isVertical ? totals.visualLength : background.thickness + totals.shadowsThickness
        enabledBorders: latteView && latteView.effects ? latteView.effects.enabledBorders : KSvg.FrameSvg.NoBorder
        imagePath: "widgets/panel-background"
        prefix: "shadow"
        opacity: hideShadow || !root.useThemePanel || (root.forceTransparentPanel && !root.forcePanelForBusyBackground) ? 0 : 1
        visible: (opacity == 0) ? false : true

        //! set true by default in order to avoid crash on startup because imagePath is set to ""
        readonly property bool themeHasShadow: themeExtended ? themeExtended.hasShadow : true

        readonly property bool hideShadow: root.behaveAsPlasmaPanel
                                           || !LatteCore.WindowSystem.compositingActive
                                           || !root.panelShadowsActive
                                           || !themeHasShadow
                                           || customShadowedRectangleIsEnabled

        Behavior on opacity {
            enabled: LatteCore.WindowSystem.compositingActive
            NumberAnimation { duration: barLine.animationTime }
        }


        Behavior on opacity{
            enabled: !LatteCore.WindowSystem.compositingActive
            NumberAnimation { duration: 0 }
        }
    }

    //! Layer 2: Provide visual solidness. Plasma themes by design may provide a panel-background svg that is not
    //!          solid. That means that user can not gain full solidness in such cases. This layer is responsible
    //!          to solve the previous mentioned plasma theme limitation.
    Colorizer.CustomBackground {
        id: backgroundLowestRectangle
        anchors.fill: solidBackground
        opacity: normalizedOpacity
        backgroundColor: colorizerManager.backgroundColor
        roundness: overlayedBackground.roundness
        visible: LatteCore.WindowSystem.compositingActive && solidBackground.exceedsThemeOpacityLimits

        readonly property real normalizedOpacity: visible ?  Math.min(1, (appliedOpacity - solidBackground.themeMaxOpacity)/(1-solidBackground.themeMaxOpacity)) : 0
        readonly property real appliedOpacity: visible ? solidBackground.appliedOpacity : 0

        Behavior on opacity{
            enabled: LatteCore.WindowSystem.compositingActive
            NumberAnimation { duration: barLine.animationTime }
        }

        Behavior on opacity{
            enabled: !LatteCore.WindowSystem.compositingActive
            NumberAnimation { duration: 0 }
        }
    }

    //! Layer 3: Original Plasma Theme "panel-background" svg. It is used for calculations and also to draw
    //!          the original background when to special settings and options exist from the user. It is also
    //!          doing one very important job which is to calculate the Effects Rectangle which is used from
    //!          the compositor to provide blurriness and from Mask calculations to provide the View Local Geometry
    KSvg.FrameSvgItem{
        id: solidBackground
        anchors.leftMargin: shadows.left
        anchors.rightMargin: shadows.right
        anchors.topMargin: shadows.top
        anchors.bottomMargin: shadows.bottom
        anchors.fill: shadowsSvgItem

        imagePath: "widgets/panel-background"
        opacity: normalizedOpacity

        readonly property bool exceedsThemeOpacityLimits: appliedOpacity > themeMaxOpacity
        readonly property bool forceSolidness: root.forceSolidPanel || !LatteCore.WindowSystem.compositingActive

        //! must be normalized to plasma theme maximum opacity
        readonly property real normalizedOpacity: Math.min(1, appliedOpacity / themeMaxOpacity)

        readonly property real appliedOpacity: overlayedBackground.backgroundOpacity > 0 && !paintInstantly ? 0 : overlayedBackground.midOpacity
        readonly property real themeMaxOpacity: themeExtendedBackground ? themeExtendedBackground.maxOpacity : 1

        //! When switching from overlaied background to regular one this must be done
        //! instantly otherwise the transition is not smooth
        readonly property bool paintInstantly: (root.hasExpandedApplet && root.plasmaBackgroundForPopups && !customRadiusIsEnabled)

        property int paddingsWidth: margins.left+margins.right
        property int paddingsHeight: margins.top + margins.bottom

        onWidthChanged: updateEffectsArea();
        onHeightChanged: updateEffectsArea();
        onImagePathChanged: solidBackground.adjustPrefix();


        Component.onCompleted: {
            root.updateEffectsArea.connect(updateEffectsArea);
            adjustPrefix();
        }

        Component.onDestruction: {
            root.updateEffectsArea.disconnect(updateEffectsArea);
        }

        //! Fix for FrameSvgItem QML version not updating its margins after a theme change
        //! with this hack we enforce such update. I could use the repaintNeeded signal but
        //! it is called more often than the themeChanged one.
        Connections {
            target: themeExtended
            onThemeChanged: {
                solidBackground.adjustPrefix();
                Plasmoid.configuration.panelShadows = !Plasmoid.configuration.panelShadows;
                Plasmoid.configuration.panelShadows = !Plasmoid.configuration.panelShadows;
                updateEffectsArea();
            }
        }

        Connections {
            target: latteView ? latteView.visibility : null
            onIsHiddenChanged: solidBackground.updateEffectsArea();
        }


        Connections{
            target: Plasmoid
            onLocationChanged: solidBackground.adjustPrefix();
        }

        function updateEffectsArea() {
            if (!updateEffectsAreaTimer.running) {
               // invUpdateEffectsArea(); // disabled in order to force Timer at all cases
                updateEffectsAreaTimer.start();
            }
        }

        //! the rect decision (no-compositing verbatim mapping, the 1x1
        //! hide mask, panel-vs-dock anchoring) lives in the BackgroundState
        //! core (EX-13); this shell keeps the latteView.effects.rect write
        //! and the coalescing timer below
        function invUpdateEffectsArea(){
            if (!latteView)
                return;

            var rootGeometry = solidBackground.mapToItem(root, 0, 0);
            latteView.effects.rect = backgroundStateResolver.effectsArea(LatteCore.WindowSystem.compositingActive,
                                                                         latteView.visibility.isHidden,
                                                                         root.behaveAsPlasmaPanel,
                                                                         rootGeometry.x,
                                                                         rootGeometry.y,
                                                                         solidBackground.width,
                                                                         solidBackground.height);
        }

        Timer {
            id: updateEffectsAreaTimer
            interval: 11 //! 90Hz or 90calls/sec
            onTriggered: solidBackground.invUpdateEffectsArea();
        }

        onRepaintNeeded: {
            if (root.behaveAsPlasmaPanel)
                adjustPrefix();
        }

        enabledBorders: latteView && latteView.effects ? latteView.effects.enabledBorders : KSvg.FrameSvg.NoBorder

        Behavior on opacity{
            enabled: LatteCore.WindowSystem.compositingActive && !solidBackground.paintInstantly
            NumberAnimation { duration: barLine.animationTime }
        }

        Behavior on opacity{
            enabled: !LatteCore.WindowSystem.compositingActive
            NumberAnimation { duration: 0 }
        }

        function adjustPrefix() {
            if (!Plasmoid) {
                return "";
            }
            var pre;
            switch (Plasmoid.location) {
            case PlasmaCore.Types.LeftEdge:
                pre = "west";
                break;
            case PlasmaCore.Types.TopEdge:
                pre = "north";
                break;
            case PlasmaCore.Types.RightEdge:
                pre = "east";
                break;
            case PlasmaCore.Types.BottomEdge:
                pre = "south";
                break;
            default:
                prefix = "";
            }

            prefix = [pre, ""];
        }
    }

    //! Layer 4: Plasma theme design does not provide a way to colorize the background. This layer
    //!          solves this by providing a custom background layer that respects the Colorizer palette
    Colorizer.CustomBackground {
        id: overlayedBackground
        anchors.fill: solidBackground

        readonly property bool busyBackground: root.forcePanelForBusyBackground
                                               && (solidBackground.opacity === 0 || !solidBackground.paintInstantly)
        //! compared against colorizerManager.plasmaTheme, the manager's null-safe
        //! stand-in for the Qt5 `theme` global this identity check was written
        //! against (same object as themeExtended.defaultTheme once the C++ View
        //! wires the interfaces object, null before that - every sibling site
        //! guards themeExtended for exactly that window). The identity algebra is
        //! documented at appliedSchemeIsPlasmaTheme (colorizerdecision.h).
        readonly property bool coloredView: colorizerManager.mustBeShown && colorizerManager.applyTheme !== colorizerManager.plasmaTheme

        backgroundOpacity: {
            if (busyBackground && !forceSolidness) {
                return root.myView.backgroundStoredOpacity;
            }

            if (coloredView || customShadowedRectangleIsEnabled) {
                return midOpacity;
            }

            return 0;
        }

        backgroundColor: colorizerManager.backgroundColor
        shadowColor: customShadowColor
        shadowEnabled: customShadowIsEnabled
        shadowSize: Math.max(0, customShadow)

        roundness: {
            if (customRadiusIsEnabled) {
                return customRadius;
            }

            return themeExtendedBackground ? themeExtendedBackground.roundness : 0
        }

        property real midOpacity: {
            if (forceSolidness) {
                return 1;
            } else if (!root.userShowPanelBackground || root.forcePanelForBusyBackground || root.forceTransparentPanel) {
                return 0;
            } else {
                return root.myView.backgroundStoredOpacity;
            }
        }

        readonly property bool forceSolidness: root.forceSolidPanel || !LatteCore.WindowSystem.compositingActive

        Behavior on backgroundOpacity{
            enabled: LatteCore.WindowSystem.compositingActive
            NumberAnimation { duration: barLine.animationTime }
        }

        Behavior on backgroundOpacity{
            enabled: !LatteCore.WindowSystem.compositingActive
            NumberAnimation { duration: 0 }
        }

        Behavior on backgroundColor{
            enabled: LatteCore.WindowSystem.compositingActive
            ColorAnimation { duration: barLine.animationTime }
        }

        Behavior on backgroundColor{
            enabled: !LatteCore.WindowSystem.compositingActive
            ColorAnimation { duration: 0 }
        }
    }

    //! Layer 5: Plasma theme design does not provide a way to draw background outline on demand. This layer
    //!          solves this by providing a custom background layer that only draws an outline on top of all
    //!          previous layers
    Loader{
        anchors.fill: solidBackground
        active: root.panelOutline && !(root.hasExpandedApplet && root.plasmaBackgroundForPopups)
        sourceComponent: Colorizer.CustomBackground{
            backgroundColor: "transparent"
            borderColor: colorizerManager.outlineColor
            borderWidth: themeExtended ? themeExtended.outlineWidth : 1
            roundness: overlayedBackground.roundness
        }
    }

    //! CustomBackground debugger
    /*Colorizer.CustomBackground {
        anchors.fill: solidBackground
        backgroundColor: "transparent"
        borderWidth: 1
        borderColor: "red"
        roundness: overlayedBackground.roundness
    }*/


    //BEGIN states
    //user set Panel Positions
    //0-Center, 1-Left, 2-Right, 3-Top, 4-Bottom
    states: [
        ///Left
        State {
            name: "leftCenter"
            when: (Plasmoid.location === PlasmaCore.Types.LeftEdge)&&(myView.alignment === LatteCore.Types.Center)

            AnchorChanges {
                target: barLine
                anchors{ top:undefined; bottom:undefined; left:parent.left; right:undefined; horizontalCenter:undefined; verticalCenter:parent.verticalCenter}
            }
            AnchorChanges {
                target: shadowsSvgItem
                anchors{ top:undefined; bottom:undefined; left:parent.left; right:undefined; horizontalCenter:undefined; verticalCenter:parent.verticalCenter}
            }
            PropertyChanges{
                target: barLine
                anchors.leftMargin: barLine.screenEdgeMargin;    anchors.rightMargin:0;     anchors.topMargin:0;    anchors.bottomMargin:0;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: background.offset;
            }
        },
        State {
            name: "leftJustify"
            when: (Plasmoid.location === PlasmaCore.Types.LeftEdge)&&(myView.alignment === LatteCore.Types.Justify)

            AnchorChanges {
                target: barLine
                anchors{ top:undefined; bottom:undefined; left:parent.left; right:undefined; horizontalCenter:undefined; verticalCenter:parent.verticalCenter}
            }
            AnchorChanges {
                target: shadowsSvgItem
                anchors{ top:undefined; bottom:undefined; left:parent.left; right:undefined; horizontalCenter:undefined; verticalCenter:parent.verticalCenter}
            }
            PropertyChanges{
                target: barLine
                anchors.leftMargin: barLine.screenEdgeMargin;    anchors.rightMargin:0;     anchors.topMargin:0;    anchors.bottomMargin:0;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }
        },
        ///Left
        State {
            name: "leftTop"
            when: (Plasmoid.location === PlasmaCore.Types.LeftEdge)&&(myView.alignment === LatteCore.Types.Top)

            AnchorChanges {
                target: barLine
                anchors{ top:parent.top; bottom:undefined; left:parent.left; right:undefined; horizontalCenter:undefined; verticalCenter:undefined}
            }
            AnchorChanges {
                target: shadowsSvgItem
                anchors{ top:parent.top; bottom:undefined; left:parent.left; right:undefined; horizontalCenter:undefined; verticalCenter:undefined}
            }
            PropertyChanges{
                target: barLine
                anchors.leftMargin: barLine.screenEdgeMargin;    anchors.rightMargin:0;     anchors.topMargin:background.offset;    anchors.bottomMargin:0;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }
        },
        ///Left
        State {
            name: "leftBottom"
            when: (Plasmoid.location === PlasmaCore.Types.LeftEdge)&&(myView.alignment === LatteCore.Types.Bottom)

            AnchorChanges {
                target: barLine
                anchors{ top:undefined; bottom:parent.bottom; left:parent.left; right:undefined; horizontalCenter:undefined; verticalCenter:undefined}
            }
            AnchorChanges {
                target: shadowsSvgItem
                anchors{ top:undefined; bottom:parent.bottom; left:parent.left; right:undefined; horizontalCenter:undefined; verticalCenter:undefined}
            }
            PropertyChanges{
                target: barLine
                anchors.leftMargin: barLine.screenEdgeMargin;    anchors.rightMargin:0;     anchors.topMargin:0;    anchors.bottomMargin:background.offset;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }
        },
        ///Right
        State {
            name: "rightCenter"
            when: (Plasmoid.location === PlasmaCore.Types.RightEdge)&&(myView.alignment === LatteCore.Types.Center)

            AnchorChanges {
                target: barLine
                anchors{ top:undefined; bottom:undefined; left:undefined; right:parent.right; horizontalCenter:undefined; verticalCenter:parent.verticalCenter}
            }
            AnchorChanges {
                target: shadowsSvgItem
                anchors{ top:undefined; bottom:undefined; left:undefined; right:parent.right; horizontalCenter:undefined; verticalCenter:parent.verticalCenter}
            }
            PropertyChanges{
                target: barLine
                anchors.leftMargin: 0;    anchors.rightMargin: barLine.screenEdgeMargin;     anchors.topMargin:0;    anchors.bottomMargin:0;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: background.offset;
            }
        },
        State {
            name: "rightJustify"
            when: (Plasmoid.location === PlasmaCore.Types.RightEdge)&&(myView.alignment === LatteCore.Types.Justify)

            AnchorChanges {
                target: barLine
                anchors{ top:undefined; bottom:undefined; left:undefined; right:parent.right; horizontalCenter:undefined; verticalCenter:parent.verticalCenter}
            }
            AnchorChanges {
                target: shadowsSvgItem
                anchors{ top:undefined; bottom:undefined; left:undefined; right:parent.right; horizontalCenter:undefined; verticalCenter:parent.verticalCenter}
            }
            PropertyChanges{
                target: barLine
                anchors.leftMargin: 0;    anchors.rightMargin: barLine.screenEdgeMargin;     anchors.topMargin:0;    anchors.bottomMargin:0;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }
        },
        State {
            name: "rightTop"
            when: (Plasmoid.location === PlasmaCore.Types.RightEdge)&&(myView.alignment === LatteCore.Types.Top)

            AnchorChanges {
                target: barLine
                anchors{ top:parent.top; bottom:undefined; left:undefined; right:parent.right; horizontalCenter:undefined; verticalCenter:undefined}
            }
            AnchorChanges {
                target: shadowsSvgItem
                anchors{ top:parent.top; bottom:undefined; left:undefined; right:parent.right; horizontalCenter:undefined; verticalCenter:undefined}
            }
            PropertyChanges{
                target: barLine
                anchors.leftMargin: 0;    anchors.rightMargin: barLine.screenEdgeMargin;     anchors.topMargin:background.offset;    anchors.bottomMargin:0;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }
        },
        State {
            name: "rightBottom"
            when: (Plasmoid.location === PlasmaCore.Types.RightEdge)&&(myView.alignment === LatteCore.Types.Bottom)

            AnchorChanges {
                target: barLine
                anchors{ top:undefined; bottom:parent.bottom; left:undefined; right:parent.right; horizontalCenter:undefined; verticalCenter:undefined }
            }
            AnchorChanges {
                target: shadowsSvgItem
                anchors{ top:undefined; bottom:parent.bottom; left:undefined; right:parent.right; horizontalCenter:undefined; verticalCenter:undefined}
            }
            PropertyChanges{
                target: barLine
                anchors.leftMargin: 0;    anchors.rightMargin: barLine.screenEdgeMargin;     anchors.topMargin:0;    anchors.bottomMargin:background.offset;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }
        },
        ///Bottom
        State {
            name: "bottomCenter"
            when: (Plasmoid.location === PlasmaCore.Types.BottomEdge)&&(myView.alignment === LatteCore.Types.Center)

            AnchorChanges {
                target: barLine
                anchors{ top:undefined; bottom:parent.bottom; left:undefined; right:undefined; horizontalCenter:parent.horizontalCenter; verticalCenter:undefined}
            }
            AnchorChanges {
                target: shadowsSvgItem
                anchors{ top:undefined; bottom:parent.bottom; left:undefined; right:undefined; horizontalCenter:parent.horizontalCenter; verticalCenter:undefined}
            }
            PropertyChanges{
                target: barLine
                anchors.leftMargin: 0;    anchors.rightMargin:0;     anchors.topMargin:0;    anchors.bottomMargin: barLine.screenEdgeMargin;
                anchors.horizontalCenterOffset: background.offset; anchors.verticalCenterOffset: 0;
            }
        },
        State {
            name: "bottomJustify"
            when: (Plasmoid.location === PlasmaCore.Types.BottomEdge)&&(myView.alignment === LatteCore.Types.Justify)

            AnchorChanges {
                target: barLine
                anchors{ top:undefined; bottom:parent.bottom; left:undefined; right:undefined; horizontalCenter:parent.horizontalCenter; verticalCenter:undefined}
            }
            AnchorChanges {
                target: shadowsSvgItem
                anchors{ top:undefined; bottom:parent.bottom; left:undefined; right:undefined; horizontalCenter:parent.horizontalCenter; verticalCenter:undefined}
            }
            PropertyChanges{
                target: barLine
                anchors.leftMargin: 0;    anchors.rightMargin:0;     anchors.topMargin:0;    anchors.bottomMargin: barLine.screenEdgeMargin;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }
        },
        State {
            name: "bottomLeft"
            when: (Plasmoid.location === PlasmaCore.Types.BottomEdge)
                  &&(((myView.alignment === LatteCore.Types.Left)&&(Qt.application.layoutDirection !== Qt.RightToLeft))
                     || ((myView.alignment === LatteCore.Types.Right)&&(Qt.application.layoutDirection === Qt.RightToLeft)))

            AnchorChanges {
                target: barLine
                anchors{ top:undefined; bottom:parent.bottom; left:parent.left; right:undefined; horizontalCenter:undefined; verticalCenter:undefined}
            }
            AnchorChanges {
                target: shadowsSvgItem
                anchors{ top:undefined; bottom:parent.bottom; left:parent.left; right:undefined; horizontalCenter:undefined; verticalCenter:undefined}
            }
            PropertyChanges{
                target: barLine
                anchors.leftMargin: background.offset;    anchors.rightMargin:0;     anchors.topMargin:0;    anchors.bottomMargin: barLine.screenEdgeMargin;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }

        },
        State {
            name: "bottomRight"
            when: (Plasmoid.location === PlasmaCore.Types.BottomEdge)
                  &&(((myView.alignment === LatteCore.Types.Right)&&(Qt.application.layoutDirection !== Qt.RightToLeft))
                     ||((myView.alignment === LatteCore.Types.Left)&&(Qt.application.layoutDirection === Qt.RightToLeft)))

            AnchorChanges {
                target: barLine
                anchors{ top:undefined; bottom:parent.bottom; left:undefined; right:parent.right; horizontalCenter:undefined; verticalCenter:undefined}
            }
            AnchorChanges {
                target: shadowsSvgItem
                anchors{ top:undefined; bottom:parent.bottom; left:undefined; right:parent.right; horizontalCenter:undefined; verticalCenter:undefined}
            }
            PropertyChanges{
                target: barLine
                anchors.leftMargin: 0;    anchors.rightMargin:background.offset;     anchors.topMargin:0;    anchors.bottomMargin: barLine.screenEdgeMargin;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }
        },
        ///Top
        State {
            name: "topCenter"
            when: (Plasmoid.location === PlasmaCore.Types.TopEdge)&&(myView.alignment === LatteCore.Types.Center)

            AnchorChanges {
                target: barLine
                anchors{ top:parent.top; bottom:undefined; left:undefined; right:undefined; horizontalCenter:parent.horizontalCenter; verticalCenter:undefined}
            }
            AnchorChanges {
                target: shadowsSvgItem
                anchors{ top:parent.top; bottom:undefined; left:undefined; right:undefined; horizontalCenter:parent.horizontalCenter; verticalCenter:undefined}
            }
            PropertyChanges{
                target: barLine
                anchors.leftMargin: 0;    anchors.rightMargin:0;     anchors.topMargin: barLine.screenEdgeMargin;    anchors.bottomMargin:0;
                anchors.horizontalCenterOffset: background.offset; anchors.verticalCenterOffset: 0;
            }
        },
        State {
            name: "topJustify"
            when: (Plasmoid.location === PlasmaCore.Types.TopEdge)&&(myView.alignment === LatteCore.Types.Justify)

            AnchorChanges {
                target: barLine
                anchors{ top:parent.top; bottom:undefined; left:undefined; right:undefined; horizontalCenter:parent.horizontalCenter; verticalCenter:undefined}
            }
            AnchorChanges {
                target: shadowsSvgItem
                anchors{ top:parent.top; bottom:undefined; left:undefined; right:undefined; horizontalCenter:parent.horizontalCenter; verticalCenter:undefined}
            }
            PropertyChanges{
                target: barLine
                anchors.leftMargin: 0;    anchors.rightMargin:0;     anchors.topMargin: barLine.screenEdgeMargin;    anchors.bottomMargin:0;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }
        },
        State {
            name: "topLeft"
            when: (Plasmoid.location === PlasmaCore.Types.TopEdge)
                  &&(((myView.alignment === LatteCore.Types.Left)&&(Qt.application.layoutDirection !== Qt.RightToLeft))
                     || ((myView.alignment === LatteCore.Types.Right)&&(Qt.application.layoutDirection === Qt.RightToLeft)))

            AnchorChanges {
                target: barLine
                anchors{ top:parent.top; bottom:undefined; left:parent.left; right:undefined; horizontalCenter:undefined; verticalCenter:undefined}
            }
            AnchorChanges {
                target: shadowsSvgItem
                anchors{ top:parent.top; bottom:undefined; left:parent.left; right:undefined; horizontalCenter:undefined; verticalCenter:undefined}
            }
            PropertyChanges{
                target: barLine
                anchors.leftMargin: background.offset;    anchors.rightMargin:0;     anchors.topMargin: barLine.screenEdgeMargin;    anchors.bottomMargin:0;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }
        },
        State {
            name: "topRight"
            when: (Plasmoid.location === PlasmaCore.Types.TopEdge)
                  &&(((myView.alignment === LatteCore.Types.Right)&&(Qt.application.layoutDirection !== Qt.RightToLeft))
                     ||((myView.alignment === LatteCore.Types.Left)&&(Qt.application.layoutDirection === Qt.RightToLeft)))

            AnchorChanges {
                target: barLine
                anchors{ top:parent.top; bottom:undefined; left:undefined; right:parent.right; horizontalCenter:undefined; verticalCenter:undefined}
            }
            AnchorChanges {
                target: shadowsSvgItem
                anchors{ top:parent.top; bottom:undefined; left:undefined; right:parent.right; horizontalCenter:undefined; verticalCenter:undefined}
            }
            PropertyChanges{
                target: barLine
                anchors.leftMargin: 0;    anchors.rightMargin:background.offset;     anchors.topMargin: barLine.screenEdgeMargin;    anchors.bottomMargin:0;
                anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
            }
        }
    ]
    //END states
}
