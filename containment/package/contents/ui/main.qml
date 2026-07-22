/*
    SPDX-FileCopyrightText: 2016 Smith AR <audoban@openmailbox.org>
    SPDX-FileCopyrightText: 2016 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.8
import QtQuick.Layouts 1.1
import QtQuick.Window 2.2

import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.plasma.components 3.0 as PlasmaComponents
import org.kde.kquickcontrolsaddons 2.0
import org.kde.plasma.plasmoid 2.0

import org.kde.latte.abilities.host 0.1 as AbilityHost

import org.kde.latte.core 0.2 as LatteCore
import org.kde.latte.components 1.0 as LatteComponents
import org.kde.latte.private.app 0.1 as LatteApp
import org.kde.latte.private.containment 0.1 as LatteContainment

import "abilities" as Ability
import "applet" as Applet
import "colorizer" as Colorizer
import "editmode" as EditMode
import "layouts" as Layouts
import "./background" as Background
import "./debugger" as Debugger

ContainmentItem {
    id: root
    objectName: "containmentViewLayout"

    LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft && !root.isVertical
    LayoutMirroring.childrenInherit: true

    //// BEGIN SIGNALS
    signal destroyInternalViewSplitters();
    signal emptyAreasWheel(QtObject wheel);
    signal separatorsUpdated();
    signal updateEffectsArea();
    signal updateIndexes();

    signal broadcastedToApplet(string pluginName, string action, variant value);
    //// END SIGNALS

    ////BEGIN properties
    readonly property int version: LatteCore.Environment.makeVersion(0,9,75)
    readonly property bool kirigamiLibraryIsFound: LatteCore.Environment.frameworksVersion >= LatteCore.Environment.makeVersion(5,69,0)

    property bool backgroundOnlyOnMaximized: Plasmoid.configuration.backgroundOnlyOnMaximized
    readonly property bool behaveAsPlasmaPanel: viewType === LatteCore.Types.PanelView
    readonly property bool behaveAsDockWithMask: !behaveAsPlasmaPanel

    readonly property bool viewIsAvailable: latteView && latteView.visibility && latteView.effects

    //! the Panel-vs-Dock chain and the background-state predicate cluster
    //! below delegate to the BackgroundState core (EX-13 in
    //! docs/tracking/QML_EXTRACTION_PLAN.md); the null guards around latteView and
    //! its trackers stay here because call arguments evaluate eagerly,
    //! unlike the short-circuiting && chains they replace
    property int viewType: _backgroundState.viewType(!!(latteView && latteView.visibility),
                                                     viewTypeInQuestion,
                                                     screenEdgeMarginEnabled,
                                                     floatingInternalGapIsForced)

    //! viewType as chosen before considering other options such as floating internal gap enforcement.
    //! It helps with binding loops
    property int viewTypeInQuestion: _backgroundState.viewTypeInQuestion(!!(latteView && latteView.visibility),
                                                                         background.customShadowedRectangleIsEnabled,
                                                                         Plasmoid.configuration.alignment,
                                                                         Plasmoid.configuration.minLength,
                                                                         Plasmoid.configuration.maxLength,
                                                                         background.isGreaterThanItemThickness,
                                                                         parabolic.factor.maxZoom)

    property bool blurEnabled: _backgroundState.blurActive(Plasmoid.configuration.blurEnabled,
                                                           forceTransparentPanel,
                                                           forcePanelForBusyBackground)

    readonly property bool inDraggingOverAppletOrOutOfContainment: latteView && latteView.containsDrag && !backDropArea.containsDrag

    readonly property Item dragInfo: Item {
        property bool entered: backDropArea.dragInfo.entered
        property bool isTask: backDropArea.dragInfo.isTask
        property bool isPlasmoid: backDropArea.dragInfo.isPlasmoid
        property bool isSeparator: backDropArea.dragInfo.isSeparator
        property bool isLatteTasks: backDropArea.dragInfo.isLatteTasks
        property bool onlyLaunchers: backDropArea.dragInfo.onlyLaunchers
    }

    property bool containsOnlyPlasmaTasks: latteView ? latteView.extendedInterface.hasPlasmaTasks && !latteView.extendedInterface.hasLatteTasks : false
    property bool dockContainsMouse: latteView && latteView.visibility ? latteView.visibility.containsMouse : false

    property bool disablePanelShadowMaximized: Plasmoid.configuration.disablePanelShadowForMaximized && LatteCore.WindowSystem.compositingActive
    property bool drawShadowsExternal: panelShadowsActive && behaveAsPlasmaPanel

    property bool editMode: Plasmoid.userConfiguring
    property bool windowIsTouching: latteView && latteView.windowsTracker
                                    && (latteView.windowsTracker.currentScreen.activeWindowTouching
                                        || latteView.windowsTracker.currentScreen.activeWindowTouchingEdge
                                        || hasExpandedApplet)

    property bool floatingInternalGapIsForced: Plasmoid.configuration.floatingInternalGapIsForced

    property bool hasFloatingGapInputEventsDisabled: root.screenEdgeMarginEnabled
                                                     && !latteView.byPassWM
                                                     && !root.inConfigureAppletsMode
                                                     && !parabolic.isEnabled
                                                     && (root.behaveAsPlasmaPanel || (root.behaveAsDockWithMask && !root.floatingInternalGapIsForced))

    property bool forceSolidPanel: _backgroundState.solidPanelForced(!!(latteView && latteView.visibility),
                                                                     LatteCore.WindowSystem.compositingActive,
                                                                     inConfigureAppletsMode,
                                                                     userShowPanelBackground,
                                                                     Plasmoid.configuration.solidBackgroundForMaximized,
                                                                     hasExpandedApplet,
                                                                     plasmaBackgroundForPopups,
                                                                     !!(latteView && latteView.windowsTracker && latteView.windowsTracker.currentScreen.existsWindowTouching),
                                                                     !!(latteView && latteView.windowsTracker && latteView.windowsTracker.currentScreen.existsWindowTouchingEdge))

    property bool forceTransparentPanel: _backgroundState.transparentPanelForced(root.backgroundOnlyOnMaximized,
                                                                                 !!(latteView && latteView.visibility),
                                                                                 LatteCore.WindowSystem.compositingActive,
                                                                                 inConfigureAppletsMode,
                                                                                 forceSolidPanel,
                                                                                 !!(latteView && latteView.windowsTracker && latteView.windowsTracker.currentScreen.existsWindowTouching),
                                                                                 !!(latteView && latteView.windowsTracker && latteView.windowsTracker.currentScreen.existsWindowTouchingEdge),
                                                                                 windowColors,
                                                                                 !!(selectedWindowsTracker && selectedWindowsTracker.existsWindowActive))

    //! normalBusyForTouchingBusyVerticalView (is touching a vertical view
    //! that is in busy state and the user prefers isBusy transparency)
    //! folded into the core - this predicate was its only consumer
    property bool forcePanelForBusyBackground: _backgroundState.panelForcedForBusyBackground(userShowPanelBackground,
                                                                                             !!(latteView && latteView.windowsTracker && latteView.windowsTracker.currentScreen.isTouchingBusyVerticalView),
                                                                                             root.backgroundOnlyOnMaximized,
                                                                                             forceTransparentPanel,
                                                                                             colorizerManager.backgroundIsBusy,
                                                                                             root.themeColors)

    property bool appletIsDragged: root.dragOverlay && root.dragOverlay.pressed
    property bool hideThickScreenGap: false /*set through binding*/
    property bool hideLengthScreenGaps: false /*set through binding*/

    property bool mirrorScreenGap: screenEdgeMarginEnabled
                                   && Plasmoid.configuration.floatingGapIsMirrored
                                   && latteView.visibility.mode === LatteCore.Types.AlwaysVisible



    property int themeColors: Plasmoid.configuration.themeColors
    property int windowColors: Plasmoid.configuration.windowColors

    property bool colorizerEnabled: themeColors !== LatteContainment.Types.PlasmaThemeColors || windowColors !== LatteContainment.Types.NoneWindowColors

    property bool plasmaBackgroundForPopups: Plasmoid.configuration.plasmaBackgroundForPopups

    readonly property bool hasExpandedApplet: latteView && latteView.extendedInterface.hasExpandedApplet;
    readonly property bool hasUserSpecifiedBackground: (latteView && latteView.layout && latteView.layout.background.startsWith("/")) ?
                                                           true : false

    readonly property bool inConfigureAppletsMode: root.editMode && universalSettings && universalSettings.inConfigureAppletsMode

    property bool closeActiveWindowEnabled: Plasmoid.configuration.closeActiveWindowEnabled
    property bool dragActiveWindowEnabled: Plasmoid.configuration.dragActiveWindowEnabled
    property bool immutable: Plasmoid.immutable
    property bool inFullJustify: (Plasmoid.configuration.alignment === LatteCore.Types.Justify) && (maxLengthPerCentage===100)
    property bool inStartup: true

    property bool isHorizontal: Plasmoid.formFactor === PlasmaCore.Types.Horizontal
    property bool isVertical: !isHorizontal

    property bool mouseWheelActions: Plasmoid.configuration.mouseWheelActions
    property bool onlyAddingStarup: true //is used for the initialization phase in startup where there aren't removals, this variable provides a way to grow icon size

    //FIXME: possibly this is going to be the default behavior, this user choice
    //has been dropped from the Dock Configuration Window
    //property bool smallAutomaticIconJumps: Plasmoid.configuration.smallAutomaticIconJumps
    property bool smallAutomaticIconJumps: true

    property bool userShowPanelBackground: LatteCore.WindowSystem.compositingActive ? Plasmoid.configuration.useThemePanel : true
    property bool useThemePanel: noApplets === 0 || !LatteCore.WindowSystem.compositingActive ?
                                     true : (Plasmoid.configuration.useThemePanel || Plasmoid.configuration.solidBackgroundForMaximized)

    property bool plasma515: LatteCore.Environment.plasmaDesktopVersion >= LatteCore.Environment.makeVersion(5,15,0)
    property bool plasma518: LatteCore.Environment.plasmaDesktopVersion >= LatteCore.Environment.makeVersion(5,18,0)

    readonly property int minAppletLengthInConfigure: 16
    readonly property int maxJustifySplitterSize: 64

    property bool maximizeWhenMaximized: Plasmoid.configuration.maximizeWhenMaximized;
    property real minLengthPerCentage: Plasmoid.configuration.minLength
    property real maxLengthPerCentage: hideLengthScreenGaps ? 100 : Plasmoid.configuration.maxLength

    property int minLength: {
        if (myView.alignment === LatteCore.Types.Justify) {
            return maxLength;
        }

        if (root.isHorizontal) {
            return behaveAsPlasmaPanel && LatteCore.WindowSystem.compositingActive ? width : width * (minLengthPerCentage/100)
        } else {
            return behaveAsPlasmaPanel && LatteCore.WindowSystem.compositingActive ? height : height * (minLengthPerCentage/100)
        }
    }

    property int maxLength: {
        const maximize = behaveAsPlasmaPanel
          || (maximizeWhenMaximized && latteView.windowsTracker.currentScreen.existsWindowMaximized);
        if (root.isHorizontal) {
            return maximize ? width : width * (maxLengthPerCentage/100)
        } else {
            return maximize ? height : height * (maxLengthPerCentage/100)
        }
    }

    property int scrollAction: Plasmoid.configuration.scrollAction

    property bool panelOutline: Plasmoid.configuration.panelOutline
    property int panelEdgeSpacing: Math.max(background.lengthMargins, 1.5*myView.itemShadow.size)

    property bool backgroundShadowsInRegularStateEnabled: LatteCore.WindowSystem.compositingActive
                                                          && userShowPanelBackground
                                                          && Plasmoid.configuration.panelShadows

    property bool panelShadowsActive: _backgroundState.panelShadowsActive(userShowPanelBackground,
                                                                          inConfigureAppletsMode,
                                                                          Plasmoid.configuration.panelShadows,
                                                                          disablePanelShadowMaximized,
                                                                          !!(latteView && latteView.windowsTracker && latteView.windowsTracker.currentScreen.activeWindowMaximized),
                                                                          blurEnabled,
                                                                          background.currentOpacity,
                                                                          forcePanelForBusyBackground,
                                                                          backgroundOnlyOnMaximized,
                                                                          forceTransparentPanel,
                                                                          hasExpandedApplet,
                                                                          plasmaBackgroundForPopups)

    property int offset: {
        if (behaveAsPlasmaPanel) {
            return 0;
        }

        if (root.isHorizontal) {
            return width * (Plasmoid.configuration.offset/100);
        } else {
            height * (Plasmoid.configuration.offset/100)
        }
    }

    property int editShadow: {
        if (!LatteCore.WindowSystem.compositingActive) {
            return 0;
        } else if (latteView && latteView.screenGeometry) {
            return latteView.screenGeometry.height/90;
        } else {
            return 7;
        }
    }

    property bool screenEdgeMarginEnabled: Plasmoid.configuration.screenEdgeMargin >= 0

    property int widthMargins: root.isVertical ? metrics.totals.thicknessEdges : metrics.totals.lengthEdges
    property int heightMargins: root.isHorizontal ? metrics.totals.thicknessEdges : metrics.totals.lengthEdges

    property var iconsArray: [16, 22, 32, 48, 64, 96, 128, 256]

    property Item dragOverlay: _dragOverlay
    property Item toolBox

    readonly property alias animations: _animations
    readonly property alias autosize: _autosize
    readonly property alias background: _background
    readonly property alias debug: _debug
    readonly property alias environment: _environment
    readonly property alias indexer: _indexer
    readonly property alias indicators: _indicators
    readonly property alias layouter: _layouter
    readonly property alias launchers: _launchers
    readonly property alias metrics: _metrics
    readonly property alias myView: _myView
    readonly property alias parabolic: _parabolic
    readonly property alias thinTooltip: _thinTooltip
    readonly property alias userRequests: _userRequests

    readonly property alias maskManager: visibilityManager
    readonly property alias layoutsContainerItem: layoutsContainer

    readonly property alias latteView: _interfaces.view
    readonly property alias layoutsManager: _interfaces.layoutsManager
    readonly property alias shortcutsEngine: _interfaces.globalShortcuts
    readonly property alias themeExtended: _interfaces.themeExtended
    readonly property alias universalSettings: _interfaces.universalSettings

    readonly property QtObject selectedWindowsTracker: {
        if (latteView && latteView.windowsTracker) {
            switch(Plasmoid.configuration.activeWindowFilter) {
            case LatteContainment.Types.ActiveInCurrentScreen:
                return latteView.windowsTracker.currentScreen;
            case LatteContainment.Types.ActiveFromAllScreens:
                return latteView.windowsTracker.allScreens;
            }
        }

        return null;
    }

    Plasmoid.backgroundHints: PlasmaCore.Types.NoBackground

    //// BEGIN properties in functions
    property int noApplets: {
        var count1 = 0;
        var count2 = 0;

        count1 = layoutsContainer.mainLayout.children.length;
        var tempLength = layoutsContainer.mainLayout.children.length;

        for (var i=tempLength-1; i>=0; --i) {
            var applet = layoutsContainer.mainLayout.children[i];
            if (applet && (applet === dndSpacer ||  applet.isInternalViewSplitter))
                count1--;
        }

        count2 = layoutsContainer.endLayout.children.length;
        tempLength = layoutsContainer.endLayout.children.length;

        for (var i=tempLength-1; i>=0; --i) {
            var applet = layoutsContainer.endLayout.children[i];
            if (applet && (applet === dndSpacer || applet.isInternalViewSplitter))
                count2--;
        }

        return (count1 + count2);
    }

    ///The index of user's current icon size
    property int currentIconIndex:{
        for(var i=iconsArray.length-1; i>=0; --i){
            if(iconsArray[i] === metrics.iconSize){
                return i;
            }
        }
        return 3;
    }

    //// END properties in functions

    ////////////////END properties

    //////////////START OF BINDINGS

    //! Wait until the mouse leaves the view
    Binding {
        target: root
        property: "hideThickScreenGap"
        when: !(Plasmoid.configuration.floatingGapHidingWaitsMouse && dockContainsMouse)
        value: screenEdgeMarginEnabled
               && Plasmoid.configuration.hideFloatingGapForMaximized
               && latteView && latteView.windowsTracker
               && latteView.windowsTracker.currentScreen.existsWindowMaximized
        restoreMode: Binding.RestoreNone
    }

    //! Binding is needed in order for hideLengthScreenGaps to be activated or not only after
    //! View sliding in/out has finished. This way the animation is smoother for behaveAsPlasmaPanels
    Binding{
        target: root
        property: "hideLengthScreenGaps"
        when: latteView && latteView.positioner && latteView.visibility
              && ((root.behaveAsPlasmaPanel && latteView.positioner.slideOffset === 0)
                  || root.behaveAsDockWithMask)
              && !(Plasmoid.configuration.floatingGapHidingWaitsMouse && dockContainsMouse)
        value: (hideThickScreenGap
                && (latteView.visibility.mode === LatteCore.Types.AlwaysVisible
                    || latteView.visibility.mode === LatteCore.Types.WindowsGoBelow)
                && (Plasmoid.configuration.alignment === LatteCore.Types.Justify)
                && Plasmoid.configuration.maxLength>85)
        restoreMode: Binding.RestoreNone
    }

    //////////////END OF BINDINGS


    //////////////START OF CONNECTIONS
    onEditModeChanged: {
        if (!editMode) {
            layouter.updateSizeForAppletsInFill();

            //! Leaving edit mode clears the rearrange (configure-applets) sub-mode. In that sub-mode the
            //! canvas is click-through, so its on-canvas toggle can't turn it back off; resetting here
            //! guarantees you can always get out (Escape / leave edit mode) and never re-enter stuck in
            //! rearrange.
            if (universalSettings && universalSettings.inConfigureAppletsMode) {
                universalSettings.inConfigureAppletsMode = false;
            }
        }

        //! This is used in case the dndspacer has been left behind
        //! e.g. the user drops a folder and a context menu is appearing
        //! but the user decides to not make a choice for the applet type
        if (dndSpacer.parent !== root) {
            dndSpacer.parent = root;
        }

        //Block Hiding events
        if (editMode) {
            latteView.visibility.addBlockHidingEvent("main[qml]::inEditMode()");
        } else {
            latteView.visibility.removeBlockHidingEvent("main[qml]::inEditMode()");
        }
    }

    onInConfigureAppletsModeChanged: {
        updateIndexes();
    }

    //! It is used only when the user chooses different alignment types and not during startup
    Connections {
        target: latteView ? latteView : null
        onAlignmentChanged: {
            if (latteView.alignment === LatteCore.Types.NoneAlignment) {
                return;
            }

            var previousalignment = Plasmoid.configuration.alignment;

            if (latteView.alignment===LatteCore.Types.Justify && previousalignment!==LatteCore.Types.Justify) { // main -> justify
                layouter.appletsInParentChange = true;
                fastLayoutManager.addJustifySplittersInMainLayout();
                console.log("LAYOUTS: Moving applets from MAIN to THREE Layouts mode...");
                fastLayoutManager.moveAppletsBasedOnJustifyAlignment();
                layouter.appletsInParentChange = false;
            } else if (latteView.alignment!==LatteCore.Types.Justify && previousalignment===LatteCore.Types.Justify ) { // justify ->main
                layouter.appletsInParentChange = true;
                console.log("LAYOUTS: Moving applets from THREE to MAIN Layout mode...");
                fastLayoutManager.joinLayoutsToMainLayout();
                layouter.appletsInParentChange = false;
            }

            root.updateIndexes();
            root.applyNormalizedPlacement(latteView.alignment);
            fastLayoutManager.save();
        }
    }

    onLatteViewChanged: {
        if (latteView) {
            if (latteView.positioner) {
                latteView.positioner.hidingForRelocationStarted.connect(visibilityManager.slotHideDockDuringLocationChange);
                latteView.positioner.showingAfterRelocationFinished.connect(visibilityManager.slotShowDockAfterLocationChange);
            }

            if (latteView.visibility) {
                latteView.visibility.onContainsMouseChanged.connect(visibilityManager.slotContainsMouseChanged);
                latteView.visibility.onMustBeHide.connect(visibilityManager.slotMustBeHide);
                latteView.visibility.onMustBeShown.connect(visibilityManager.slotMustBeShown);
            }

            //! the startup slide-in was deferred because latteView was
            //! still null when inStartup flipped; run it now
            if (pendingStartupFinished) {
                pendingStartupFinished = false;
                finishStartup();
            }
        }
    }

    Connections {
        target: latteView
        onPositionerChanged: {
            if (latteView.positioner) {
                latteView.positioner.hidingForRelocationStarted.connect(visibilityManager.slotHideDockDuringLocationChange);
                latteView.positioner.showingAfterRelocationFinished.connect(visibilityManager.slotShowDockAfterLocationChange);
            }
        }

        onVisibilityChanged: {
            if (latteView.visibility) {
                latteView.visibility.onContainsMouseChanged.connect(visibilityManager.slotContainsMouseChanged);
                latteView.visibility.onMustBeHide.connect(visibilityManager.slotMustBeHide);
                latteView.visibility.onMustBeShown.connect(visibilityManager.slotMustBeShown);
            }
        }
    }


    onMaxLengthChanged: {
        layouter.updateSizeForAppletsInFill();
    }

    onToolBoxChanged: {
        if (toolBox) {
            toolBox.visible = false;
        }
    }

    onIsVerticalChanged: {
        applyNormalizedPlacement(Plasmoid.configuration.alignment);
    }

    Component.onCompleted: {
        upgrader_v010_alignment();
        applyNormalizedPlacement(Plasmoid.configuration.alignment);

        fastLayoutManager.restore();
        Plasmoid.internalAction("configure").visible = !Plasmoid.immutable;
        Plasmoid.internalAction("configure").enabled = !Plasmoid.immutable;
    }

    Component.onDestruction: {
        console.debug("Destroying Latte Dock Containment ui...");

        layouter.appletsInParentChange = true;
        fastLayoutManager.save();

        if (latteView) {
            if (latteView.positioner) {
                latteView.positioner.hidingForRelocationStarted.disconnect(visibilityManager.slotHideDockDuringLocationChange);
                latteView.positioner.showingAfterRelocationFinished.disconnect(visibilityManager.slotShowDockAfterLocationChange);
            }

            if (latteView.visibility) {
                latteView.visibility.onContainsMouseChanged.disconnect(visibilityManager.slotContainsMouseChanged);
                latteView.visibility.onMustBeHide.disconnect(visibilityManager.slotMustBeHide);
                latteView.visibility.onMustBeShown.disconnect(visibilityManager.slotMustBeShown);
            }
        }
    }

    Containment.onAppletAdded: {
        // Plasma 6 signal: appletAdded(Plasma::Applet *applet, const QRectF &geometryHint).
        // The drop position arrives as a QRectF; pull x/y from it.
        var x = geometryHint.x;
        var y = geometryHint.y;

        if (fastLayoutManager.isMasqueradedIndex(x, y)) {
            var index = fastLayoutManager.masquearadedIndex(x, y);
            fastLayoutManager.addAppletItem(applet, index);
        } else {
            fastLayoutManager.addAppletItem(applet, x, y);
        }
    }

    Containment.onAppletRemoved: fastLayoutManager.removeAppletItem(applet);

    Plasmoid.onUserConfiguringChanged: {
        if (!Plasmoid.userConfiguring) {
            return;
        }

        //! Qt5 collapsed every expanded applet popup here with
        //! `Plasmoid.applets[i].expanded = false`. On Plasma 6 that loop is dead:
        //! Containment::applets hands back Plasma::Applet objects (returning the
        //! graphic items instead is a KF6 TODO in libplasma) and `expanded` lives
        //! on the applet's graphic item, so the first write threw a TypeError that
        //! aborted the whole handler. The applet-to-item hop (itemForApplet) is
        //! C++ territory; extendedInterface.deactivateApplets() is exactly that
        //! walk, already used for the hide-on-autohide collapse (view.cpp).
        if (latteView && latteView.extendedInterface) {
            latteView.extendedInterface.deactivateApplets();
        } else {
            console.warn("containment main: no extended interface while entering configure mode; expanded applet popups stay open");
        }
    }

    Plasmoid.onImmutableChanged: {
        Plasmoid.internalAction("configure").visible = !Plasmoid.immutable;
        Plasmoid.internalAction("configure").enabled = !Plasmoid.immutable;
    }
    //////////////END OF CONNECTIONS

    //////////////START OF FUNCTIONS
    function createAppletItem(applet) {
        var appletContainer = appletItemComponent.createObject(dndSpacer.parent);
        initAppletContainer(appletContainer, applet);

        // don't show applet if it chooses to be hidden but still make it  accessible in the panelcontroller
        appletContainer.visible = Qt.binding(function() {
            return (appletContainer.applet && appletContainer.applet.plasmoid.status !== PlasmaCore.Types.HiddenStatus || (!Plasmoid.immutable && root.inConfigureAppletsMode)) && !appletContainer.isHidden;
        });
        return appletContainer;
    }

    function initAppletContainer(appletContainer, applet) {
        appletContainer.applet = applet;
        applet.parent = appletContainer.appletWrapper;
        applet.anchors.fill = appletContainer.appletWrapper;
        applet.visible = true;
    }

    function createJustifySplitter() {
        var splitter = appletItemComponent.createObject(root);
        splitter.internalSplitterId = internalViewSplittersCount()+1;
        splitter.visible = true;
        return splitter;
    }

    //! it is used in order to check the right click position
    //! the only way to take into account the visual appearance
    //! of the applet (including its spacers)
    function appletContainsPos(appletId, pos){
        for (var i = 0; i < layoutsContainer.startLayout.children.length; ++i) {
            var child = layoutsContainer.startLayout.children[i];

            if (child && child.applet && child.applet.plasmoid.id === appletId && child.containsPos(pos))
                return true;
        }

        for (var i = 0; i < layoutsContainer.mainLayout.children.length; ++i) {
            var child = layoutsContainer.mainLayout.children[i];

            if (child && child.applet && child.applet.plasmoid.id === appletId && child.containsPos(pos))
                return true;
        }

        for (var i = 0; i < layoutsContainer.endLayout.children.length; ++i) {
            var child = layoutsContainer.endLayout.children[i];

            if (child && child.applet && child.applet.plasmoid.id === appletId && child.containsPos(pos))
                return true;
        }

        return false;
    }

    function internalViewSplittersCount(){
        var splitters = 0;
        for (var container in layoutsContainer.startLayout.children) {
            var item = layoutsContainer.startLayout.children[container];
            if(item && item.isInternalViewSplitter) {
                splitters = splitters + 1;
            }
        }

        for (var container in layoutsContainer.mainLayout.children) {
            var item = layoutsContainer.mainLayout.children[container];
            if(item && item.isInternalViewSplitter) {
                splitters = splitters + 1;
            }
        }

        for (var container in layoutsContainer.endLayout.children) {
            var item = layoutsContainer.endLayout.children[container];
            if(item && item.isInternalViewSplitter) {
                splitters = splitters + 1;
            }
        }

        return splitters;
    }

    function mouseInCanBeHoveredApplet(){
        var applets = layoutsContainer.startLayout.children;

        for(var i=0; i<applets.length; ++i){
            var applet = applets[i];

            if(applet && applet.containsMouse && !applet.originalAppletBehavior && applet.parabolicEffectIsSupported){
                return true;
            }
        }

        applets = layoutsContainer.mainLayout.children;

        for(var i=0; i<applets.length; ++i){
            var applet = applets[i];

            if(applet && applet.containsMouse && !applet.originalAppletBehavior && applet.parabolicEffectIsSupported){
                return true;
            }
        }

        ///check second layout also
        applets = layoutsContainer.endLayout.children;

        for(var i=0; i<applets.length; ++i){
            var applet = applets[i];

            if(applet && applet.containsMouse && !applet.originalAppletBehavior && applet.parabolicEffectIsSupported){
                return true;
            }
        }

        return false;
    }

    function sizeIsFromAutomaticMode(size){

        for(var i=iconsArray.length-1; i>=0; --i){
            if(iconsArray[i] === size){
                return true;
            }
        }

        return false;
    }

    function upgrader_v010_alignment() {
        //! IMPORTANT, special case because it needs to be loaded on Component constructor
        if (!Plasmoid.configuration.alignmentUpgraded) {
            Plasmoid.configuration.alignment = Plasmoid.configuration.panelPosition;
            Plasmoid.configuration.alignmentUpgraded = true;
        }
    }

    //! The sole containment-side write boundary for output, semantic
    //! alignment, length, and offset. Offset is committed before alignment so
    //! a negative centered value never becomes an edge margin, even for one
    //! binding evaluation between the two writes.
    function applyNormalizedPlacement(requestedAlignment) {
        const normalized = placementNormalizer.normalize(Plasmoid.location,
                                                         requestedAlignment,
                                                         Plasmoid.configuration.minLength,
                                                         Plasmoid.configuration.maxLength,
                                                         Plasmoid.configuration.offset);
        if (!normalized.accepted) {
            return false;
        }

        if (Plasmoid.configuration.offset !== normalized.offset) {
            Plasmoid.configuration.offset = normalized.offset;
        }
        if (Plasmoid.configuration.minLength !== normalized.minLength) {
            Plasmoid.configuration.minLength = normalized.minLength;
        }
        if (Plasmoid.configuration.maxLength !== normalized.maxLength) {
            Plasmoid.configuration.maxLength = normalized.maxLength;
        }
        if (Plasmoid.configuration.alignment !== normalized.alignment) {
            Plasmoid.configuration.alignment = normalized.alignment;
        }

        return true;
    }
    //END functions

    ///////////////BEGIN components
    Component {
        id: appletItemComponent
        Applet.AppletItem{
            animations: _animations
            colorizerHost: colorizerManager
            debug: _debug
            environment: _environment
            indexer: _indexer
            indicators: _indicators
            launchers: _launchers
            layouter: _layouter
            layoutManagerHost: fastLayoutManager
            layouts: layoutsContainer
            metrics: _metrics
            myView: _myView
            parabolic: _parabolic
            rootItem: root
            shortcuts: _shortcuts
            thinTooltip: _thinTooltip
            userRequests: _userRequests
        }
    }

    Upgrader {
        id: upgrader
    }

    ///////////////END components

    //! stateless resolver for the view-type chain and the background-state
    //! predicate cluster (EX-13; units/backgroundstate.h)
    LatteContainment.BackgroundStateResolver {
        id: _backgroundState
    }

    LatteContainment.PlacementNormalizer {
        id: placementNormalizer
    }

    LatteContainment.LayoutManager{
        id:fastLayoutManager
        plasmoidObj: Plasmoid
        rootItem: root
        dndSpacerItem: dndSpacer
        mainLayout: layoutsContainer.mainLayout
        startLayout: layoutsContainer.startLayout
        endLayout: layoutsContainer.endLayout
        metrics: _metrics

        onAppletOrderChanged: root.updateIndexes();
        onSplitterPositionChanged: root.updateIndexes();
        onSplitterPosition2Changed: root.updateIndexes();
    }

    ///////////////BEGIN UI elements

    Loader{
        active: debug.windowEnabled
        sourceComponent: Debugger.DebugWindow{}
    }

    Loader{
        anchors.fill: parent
        active: debug.graphicsEnabled
        z:10

        sourceComponent: Item{
            Rectangle{
                anchors.fill: parent
                color: "yellow"
                opacity: 0.06
            }
        }
    }

    BindingsExternal {
        id: bindingsExternal
        containmentItem: root
    }

    VisibilityManager{
        id: visibilityManager
        layouts: layoutsContainer
    }

    DragDropArea {
        id: backDropArea
        anchors.fill: parent
        containmentItem: root
        //! document ids outrank the shell's same-named properties here,
        //! so these resolve to the ids (the EX-14 injection seam)
        dndSpacer: dndSpacer
        fastLayoutManager: fastLayoutManager
        animations: _animations

        //! Applet-aware right-click menus for the dock window itself; first
        //! child so it sits at the BOTTOM of the stacking order: applets that
        //! handle their own mouse buttons (tasks) win first, everything else
        //! falls through to this layer. See ContextMenuLayer.qml for why the
        //! layer lives in a separate file.
        Loader {
            anchors.fill: parent
            active: latteView !== null && latteView !== undefined
            source: "ContextMenuLayer.qml"
        }

        //! Edit mode blueprint, drawn inside the containment as in Qt5 Latte. It cannot
        //! live in the canvas config window: that is a separate layer surface and wayland
        //! cannot stack dock > grid > wallpaper across two surfaces, so the grid must be
        //! inside the dock window itself. It sits BELOW the dock background: Qt5 stacked
        //! the whole dock window (background included) above the grid window, so the
        //! background renders at its true opacity during edit mode (WYSIWYG for the
        //! opacity setting) and the grid shows only through its transparent parts and
        //! in the edit band around it.
        Image {
            id: editBlueprint
            //! Sized to editThickness at the screen edge, like Qt5's edit visual: the
            //! window is taller than the edit area (parabolic zoom headroom), so filling
            //! it would draw the grid far above the dock.
            width: root.isHorizontal ? parent.width : thickness
            height: root.isHorizontal ? thickness : parent.height
            x: Plasmoid.location === PlasmaCore.Types.RightEdge ? parent.width - width : 0
            y: Plasmoid.location === PlasmaCore.Types.BottomEdge ? parent.height - height : 0
            fillMode: Image.Tile
            source: latteView && latteView.layout ? latteView.layout.background : ""

            readonly property int thickness: latteView ? latteView.editThickness : 0

            visible: opacity > 0
            //! Qt5 semantics: solid while rearranging widgets (the grid is the rearrange
            //! backdrop) and without compositing (no real transparency); otherwise the
            //! editBackgroundOpacity setting, which the mouse wheel adjusts in edit mode,
            //! so the dock background stays visible through the grid.
            opacity: {
                if (!root.editMode) {
                    return 0;
                }

                if (root.inConfigureAppletsMode || !LatteCore.WindowSystem.compositingActive) {
                    return 1;
                }

                return Plasmoid.configuration.editBackgroundOpacity;
            }

            Behavior on opacity {
                NumberAnimation {
                    //! _animations by id, not backDropArea's injected
                    //! animations property: a BINDING can evaluate before
                    //! the injected property is assigned (the EX-04
                    //! startup-order class); the document id always resolves
                    duration: _animations.duration.large
                    easing.type: Easing.OutCubic
                }
            }
        }

        Item{
            anchors.fill: layoutsContainer

            Background.MultiLayered{
                id: _background
            }
        }

        Layouts.LayoutsContainer {
            id: layoutsContainer
        }
    }

    Colorizer.Manager {
        id: colorizerManager
    }

    EditMode.ConfigOverlay{
        id: _dragOverlay
    }

    Item {
        id: dndSpacer

        width: root.isHorizontal ? length : thickness
        height: root.isHorizontal ? thickness : length

        property int length: opacity > 0 ? (dndSpacerAddItem.length + metrics.totals.lengthEdges + metrics.totals.lengthPaddings) : 0

        readonly property bool isDndSpacer: true
        readonly property int thickness: metrics.totals.thickness + metrics.margin.screenEdge
        readonly property int maxLength: 96

        Layout.minimumWidth: width
        Layout.minimumHeight: height
        Layout.preferredWidth: Layout.minimumWidth
        Layout.preferredHeight: Layout.minimumHeight
        Layout.maximumWidth: Layout.minimumWidth
        Layout.maximumHeight: Layout.minimumHeight
        opacity: 0
        visible: parent === layoutsContainer.startLayout
                 || parent === layoutsContainer.mainLayout
                 || parent === layoutsContainer.endLayout

        z:1500

        Behavior on length {
            NumberAnimation {
                duration: animations.duration.large
                easing.type: Easing.InQuad
            }
        }

        Behavior on opacity {
            NumberAnimation {
                duration: animations.duration.large
                easing.type: Easing.InQuad
            }
        }

        Item {
            id: dndSpacerAddItemContainer
            width: root.isHorizontal ? parent.length : parent.thickness - metrics.margin.screenEdge
            height: root.isHorizontal ? parent.thickness - metrics.margin.screenEdge : parent.length

            property int thickMargin: metrics.margin.screenEdge

            LatteComponents.AddItem{
                id: dndSpacerAddItem
                anchors.centerIn: parent
                width: length
                height: width

                readonly property int length: Math.min(metrics.iconSize, 96)
            }

            states:[
                State{
                    name: "bottom"
                    when: Plasmoid.location === PlasmaCore.Types.BottomEdge

                    AnchorChanges{
                        target: dndSpacerAddItemContainer;
                        anchors.horizontalCenter: parent.horizontalCenter; anchors.verticalCenter: undefined;
                        anchors.right: undefined; anchors.left: undefined; anchors.top: undefined; anchors.bottom: parent.bottom;
                    }
                    PropertyChanges{
                        target: dndSpacerAddItemContainer;
                        anchors.leftMargin: 0;    anchors.rightMargin: 0;     anchors.topMargin:0;    anchors.bottomMargin: dndSpacerAddItemContainer.thickMargin;
                        anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
                    }
                },
                State{
                    name: "top"
                    when: Plasmoid.location === PlasmaCore.Types.TopEdge

                    AnchorChanges{
                        target: dndSpacerAddItemContainer;
                        anchors.horizontalCenter: parent.horizontalCenter; anchors.verticalCenter: undefined;
                        anchors.right: undefined; anchors.left: undefined; anchors.top: parent.top; anchors.bottom: undefined;
                    }
                    PropertyChanges{
                        target: dndSpacerAddItemContainer;
                        anchors.leftMargin: 0;    anchors.rightMargin: 0;     anchors.topMargin: dndSpacerAddItemContainer.thickMargin;    anchors.bottomMargin: 0;
                        anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
                    }
                },
                State{
                    name: "left"
                    when: Plasmoid.location === PlasmaCore.Types.LeftEdge

                    AnchorChanges{
                        target: dndSpacerAddItemContainer;
                        anchors.horizontalCenter: undefined; anchors.verticalCenter: parent.verticalCenter;
                        anchors.right: undefined; anchors.left: parent.left; anchors.top: undefined; anchors.bottom: undefined;
                    }
                    PropertyChanges{
                        target: dndSpacerAddItemContainer;
                        anchors.leftMargin: dndSpacerAddItemContainer.thickMargin;    anchors.rightMargin: 0;     anchors.topMargin:0;    anchors.bottomMargin: 0;
                        anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
                    }
                },
                State{
                    name: "right"
                    when: Plasmoid.location === PlasmaCore.Types.RightEdge

                    AnchorChanges{
                        target: dndSpacerAddItemContainer;
                        anchors.horizontalCenter: undefined; anchors.verticalCenter: parent.verticalCenter;
                        anchors.right: parent.right; anchors.left: undefined; anchors.top: undefined; anchors.bottom: undefined;
                    }
                    PropertyChanges{
                        target: dndSpacerAddItemContainer;
                        anchors.leftMargin: 0;    anchors.rightMargin: dndSpacerAddItemContainer.thickMargin;     anchors.topMargin:0;    anchors.bottomMargin: 0;
                        anchors.horizontalCenterOffset: 0; anchors.verticalCenterOffset: 0;
                    }
                }
            ]
        }
    }

    Behavior on maxLengthPerCentage {
        enabled: root.behaveAsDockWithMask && Plasmoid.configuration.floatingGapHidingWaitsMouse && dockContainsMouse
        NumberAnimation {
            duration: animations.duration.short
            easing.type: Easing.InQuad
        }
    }

    ///////////////END UI elements

    ///////////////BEGIN ABILITIES

    Ability.Animations {
        id: _animations
        layouts: layoutsContainer
        metrics: _metrics
        settings: universalSettings
    }

    Ability.AutoSize {
        id: _autosize
        alignment: Plasmoid.configuration.alignment
        animations: _animations
        autoSizeEnabled: Plasmoid.configuration.autoSizeEnabled
        containment: root
        layouts: layoutsContainer
        layouter: _layouter
        metrics: _metrics
        parabolic: _parabolic
        view: latteView
        visibility: visibilityManager
    }

    Ability.Debug {
        id: _debug
    }

    AbilityHost.Environment{
        id: _environment
    }

    Ability.Indexer {
        id: _indexer
        layouts: layoutsContainer
    }

    Ability.Indicators{
        id: _indicators
        view: latteView
    }

    Ability.Launchers {
        id: _launchers
        layouts: layoutsContainer
        layoutName: latteView && latteView.layout ? latteView.layout.name : ""
    }

    Ability.Layouter {
        id: _layouter
        animations: _animations
        indexer: _indexer
        layouts: layoutsContainer
    }

    Ability.Metrics {
        id: _metrics
        animations: _animations
        autosize: _autosize
        background: _background
        indicators: _indicators
        parabolic: _parabolic
    }

    Ability.MyView {
        id: _myView
        layouts: layoutsContainer
    }

    Ability.ParabolicEffect {
        id: _parabolic
        animations: _animations
        debug: _debug
        layouts: layoutsContainer
        myView: _myView
        view: latteView
        settings: universalSettings
    }

    Ability.PositionShortcuts {
        id: _shortcuts
        layouts: layoutsContainer
    }

    //! keyboard focus mode traversal; only receives key events while the
    //! C++ View temporarily accepts keyboard focus
    KeyboardNavigationHandler {
        id: _keyboardNavigation
        view: root.latteView
        shortcuts: _shortcuts
        indexer: _indexer
    }

    Ability.ThinTooltip {
        id: _thinTooltip
        debug: _debug
        layouts: layoutsContainer
        view: latteView
    }

    Ability.UserRequests {
        id: _userRequests
        view: latteView
    }

    LatteApp.Interfaces {
        id: _interfaces
        plasmoidInterface: root

        Component.onCompleted: {
            //! view can be null here: on Plasma 6 the C++ View wrapper is constructed after this
            //! containment graphic object, so _latte_view_object is not injected yet. The View ctor
            //! re-reads it and onViewChanged below does the wiring once view becomes available.
            if (view) {
                view.interfacesGraphicObj = _interfaces;
            }
        }

        onViewChanged: {
            if (view) {
                view.interfacesGraphicObj = _interfaces;

                if (!root.inStartup) {
                    //! used from recreating views
                    root.inStartup = true;
                    startupDelayer.start();
                }
            }
        }
    }

    ///////////////END ABILITIES

    ///////////////BEGIN TIMER elements

    //! It is used in order to slide-in the latteView on startup
    property bool pendingStartupFinished: false

    function finishStartup() {
        latteView.positioner.startupFinished();
        latteView.positioner.slideInDuringStartup();
        visibilityManager.slotMustBeShown();
    }

    onInStartupChanged: {
        if (inStartup) {
            return;
        }

        //! latteView can still be null here on Plasma 6: the C++ View
        //! wrapper is injected after the containment graphic object exists.
        //! The slide-in must be deferred, not skipped - skipping leaves the
        //! dock content permanently slid out of its own window (mapped but
        //! painting nothing). The deferred run happens in the
        //! onLatteViewChanged handler further up.
        if (latteView) {
            finishStartup();
        } else {
            pendingStartupFinished = true;
        }
    }

    Connections {
        target:fastLayoutManager
        onHasRestoredAppletsChanged: {
            //! startup-chain breadcrumb (pairs with startupStrandedWatchdog):
            //! one stranded startup died between this signal and the delayer
            //! firing, mechanism still unproven - keep the hops named
            console.log("startup: hasRestoredApplets ->", fastLayoutManager.hasRestoredApplets);
            if (fastLayoutManager.hasRestoredApplets) {
                startupDelayer.start();
            }
        }
    }

    Timer {
        //! Give a little more time to layouter and applets to load and be positioned properly during startup when
        //! the view is drawn out-of-screen and afterwards trigger the startup animation sequence:
        //! 1.slide-out when out-of-screen //slotMustBeHide()
        //! 2.be positioned properly at correct screen //slideInDuringStartup(), triggers Positioner::syncGeometry()
        //! 3.slide-in properly in correct screen //slotMustBeShown();
        id: startupDelayer
        interval: 1000
        onTriggered: {
            //! second startup-chain breadcrumb (see the watchdog below)
            console.log("startup: delayer fired, starting slide-out");
            visibilityManager.slotMustBeHide();
            startupStrandedWatchdog.start();
        }
    }

    Timer {
        //! Startup-stranding watchdog (found live 2026-07-16, Phase 4 struts
        //! item): four cold/loaded startups left root.inStartup TRUE forever
        //! on every view - the slide-out chain below startupDelayer never
        //! finished, so startupFinished() never reached the positioner, the
        //! views kept their off-screen startup coordinates and every strut
        //! publication landed in the remove branch (AlwaysVisible docks
        //! reserved nothing; windows maximized under them). The docks LOOK
        //! fine in that state because layer-shell placement rides anchors,
        //! not the window position, which is what let it hide. Warm runs
        //! never strand; the mechanism is timing-dependent and still
        //! unproven - this watchdog names the state loudly at the next
        //! natural occurrence instead of leaving it silent.
        id: startupStrandedWatchdog
        interval: 15000
        onTriggered: {
            if (root.inStartup) {
                console.warn("STARTUP STRANDED: inStartup still true 15s after the startup delayer fired",
                             "- slidingOut:", visibilityManager.inSlidingOut,
                             "slidingIn:", visibilityManager.inSlidingIn,
                             "latteView:", latteView ? "set" : "null",
                             "pendingStartupFinished:", pendingStartupFinished,
                             "screen:", latteView && latteView.positioner ? latteView.positioner.currentScreenName : "?");
            }
        }
    }

    ///////////////END TIMER elements

    Loader{
        anchors.fill: parent
        active: debug.localGeometryEnabled
        sourceComponent: Rectangle{
            x: latteView.localGeometry.x
            y: latteView.localGeometry.y
            //! when view is resized there is a chance that geometry is faulty stacked in old values
            width: Math.min(latteView.localGeometry.width, root.width) //! fixes updating
            height: Math.min(latteView.localGeometry.height, root.height) //! fixes updating

            color: "blue"
            border.width: 2
            border.color: "red"

            opacity: 0.35
        }
    }

    Loader{
        anchors.fill: parent
        active: latteView && latteView.effects && debug.inputMaskEnabled
        sourceComponent: Rectangle{
            x: latteView.effects.inputMask.x
            y: latteView.effects.inputMask.y
            //! when view is resized there is a chance that geometry is faulty stacked in old values
            width: Math.min(latteView.effects.inputMask.width, root.width) //! fixes updating
            height: Math.min(latteView.effects.inputMask.height, root.height) //! fixes updating

            color: "purple"
            border.width: 1
            border.color: "black"

            opacity: 0.20
        }
    }
}
