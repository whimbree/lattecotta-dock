/*
    SPDX-FileCopyrightText: 2016 Smith AR <audoban@openmailbox.org>
    SPDX-FileCopyrightText: 2016 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.7
import QtQuick.Layouts 1.1

import org.kde.plasma.plasmoid 2.0
import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.plasma.components 3.0 as PlasmaComponents
import org.kde.kquickcontrolsaddons 2.0
import org.kde.kirigami 2.20 as Kirigami

import org.kde.latte.core 0.2 as LatteCore
import org.kde.latte.components 1.0 as LatteComponents
import org.kde.latte.private.containment 0.1 as LatteContainment

import org.kde.latte.abilities.items 0.1 as AbilityItem

import "colorizer" as Colorizer
import "communicator" as Communicator
import "../debugger" as Debugger

Item {
    id: appletItem
    width: isInternalViewSplitter && !root.inConfigureAppletsMode ? 0 : computeWidth
    height: isInternalViewSplitter && !root.inConfigureAppletsMode ? 0 : computeHeight

    //any applets that exceed their limits should not take events from their surrounding applets
    //clip: !isSeparator && !parabolicAreaLoader.active

    signal mousePressed(int x, int y, int button);
    signal mouseReleased(int x, int y, int button);

    property bool animationsEnabled: true
    property bool indexerIsSupported: communicator.indexerIsSupported
    property bool parabolicEffectIsSupported: true
    property bool canShowAppletNumberBadge: !indexerIsSupported
                                            && !isSeparator
                                            && !isMarginsAreaSeparator
                                            && !isHidden
                                            && !isSpacer
                                            && !isInternalViewSplitter

    readonly property bool canFillScreenEdge: communicator.requires.screenEdgeMarginSupported || communicator.indexerIsSupported
    readonly property bool canFillThickness: applet && applet.plasmoid && applet.plasmoid.hasOwnProperty("constraintHints")
                                             && ((applet.plasmoid.constraintHints & PlasmaCore.Types.CanFillArea) === PlasmaCore.Types.CanFillArea);

    readonly property bool isMarginsAreaSeparator: applet && applet.plasmoid && applet.plasmoid.hasOwnProperty("constraintHints")
                                                   && ((applet.plasmoid.constraintHints & PlasmaCore.Types.MarginAreasSeparator) === PlasmaCore.Types.MarginAreasSeparator);

    //! Ancestor marker for the shell package's CompactApplet expander: it finds
    //! its hosting AppletItem by walking up until this marker instead of a fixed
    //! number of parent hops (the Plasma 6 visual tree depth is not stable).
    readonly property bool isLatteAppletContainer: true

    readonly property color highlightColor: Kirigami.Theme.focusColor

    //! Fill Applet(s)
    property bool isAutoFillApplet:  isRequestingFill
    property bool isParabolicEdgeSpacer: false

    property bool isRequestingFill: {
        if (!applet || !applet.Layout) {
            return false;
        }

        if ((root.isHorizontal && applet.Layout.fillWidth===true)
                || (root.isVertical && applet.Layout.fillHeight===true)) {
            return !isHidden;
        }

        return false;
    }

    property int maxAutoFillLength: -1 //it is used in calculations for fillWidth,fillHeight applets
    property int minAutoFillLength: -1 //it is used in calculations for fillWidth,fillHeight applets

    readonly property bool inConfigureAppletsDragging: root.dragOverlay
                                                       && root.dragOverlay.currentApplet
                                                       && root.dragOverlay.pressed

    property bool appletBlocksColorizing: !communicator.requires.latteSideColoringEnabled || communicator.indexerIsSupported
    property bool appletBlocksParabolicEffect: communicator.requires.parabolicEffectLocked
    readonly property bool lockZoom: !parabolicEffectIsSupported
                                     || appletBlocksParabolicEffect
                                     || (fastLayoutManager && applet && (fastLayoutManager.lockedZoomApplets.indexOf(applet.plasmoid.id)>=0))
    readonly property bool userBlocksColorizing: appletBlocksColorizing
                                                 || (fastLayoutManager && applet && (fastLayoutManager.userBlocksColorizingApplets.indexOf(applet.plasmoid.id)>=0))

    //! the containment's colorizer Manager (main.qml id), aliased once as an
    //! appletItem property so the D21 push below can reach it QUALIFIED
    //! (appletItem.colorizerHost.*) from the nested wrapper without one
    //! unqualified-access warning per colour it pushes.
    readonly property var colorizerHost: colorizerManager

    //! D21 (approach B, docs/known-defects.md): when the colorizer is engaged
    //! this stock applet's OWN Kirigami.Theme color group is set to the decided
    //! scheme (the _wrapper push below), so its native content - the digital
    //! clock's Text.NativeRendering label, symbolic icons - renders with the
    //! right contrast WITHOUT the layer-FBO ColorOverlay. Same applet scope the
    //! overlay used to cover: engaged, not self-colored (latte-aware applets keep
    //! their LatteBridge.colorPalette path), not user-blocked, not a colorful
    //! icon (those stay native, the flatten never suited them), not an inline
    //! full-representation content view, not an internal splitter.
    readonly property bool colorizerPaletteActive: colorizerHost.mustBeShown
                                                   && !userBlocksColorizing
                                                   && !isInternalViewSplitter
                                                   && !isShowingInlineFullRepresentation
                                                   && !colorfulnessProbe.blocksColorizing

    //! why the palette push is or is not applied to this applet - the
    //! viewAppletsData colorizer readback (observability-first). "applied" is
    //! the active state; the rest name the single winning exemption.
    readonly property string colorizerExemptionReason: {
        if (colorizerPaletteActive) {
            return "applied";
        } else if (!colorizerHost.mustBeShown) {
            return "notEngaged";
        } else if (isInternalViewSplitter) {
            return "splitter";
        } else if (appletBlocksColorizing) {
            return "selfColored";
        } else if (userBlocksColorizing) {
            return "userBlocked";
        } else if (isShowingInlineFullRepresentation) {
            return "inlineFull";
        } else if (colorfulnessProbe.blocksColorizing) {
            return "colorful";
        }

        return "unknown";
    }

    property bool isActive: (isExpanded
                             && !appletItem.communicator.indexerIsSupported
                             && applet.plasmoid.pluginName !== "org.kde.activeWindowControl"
                             && applet.plasmoid.pluginName !== "org.kde.plasma.appmenu")

    property bool isExpanded: false

    //! Plasma 6 inline representation switch: AppletQuickItem re-parents the
    //! full representation item INTO ITSELF when the applet grows past
    //! switchWidth/switchHeight (popup-expanded reps live in the popup
    //! dialog's mainItem instead, and resting reps are parentless or in the
    //! expander). The parent identity is therefore the exact inline signal.
    readonly property bool isShowingInlineFullRepresentation: applet
                                                              && applet.fullRepresentationItem
                                                              && applet.fullRepresentationItem.parent === applet

    property bool isScheduledForDestruction: (fastLayoutManager && applet && fastLayoutManager.appletsInScheduledDestruction.indexOf(applet.plasmoid.id)>=0)
    property bool isHidden: (!root.inConfigureAppletsMode && ((applet && applet.plasmoid.status === PlasmaCore.Types.HiddenStatus ) || isInternalViewSplitter)) || isScheduledForDestruction
    property bool isInternalViewSplitter: (internalSplitterId > 0)
    property bool isZoomed: false
    property bool isPlaceHolder: false
    property bool isPressed: viewSignalsConnector.pressed
    property bool isSeparator: applet && (applet.plasmoid.pluginName === "audoban.applet.separator"
                                          || applet.plasmoid.pluginName === "org.kde.latte.separator")
    property bool isSpacer: applet && (applet.plasmoid.pluginName === "org.kde.latte.spacer")
    property bool isSystray: applet && (applet.plasmoid.pluginName === "org.kde.plasma.systemtray" || applet.plasmoid.pluginName === "org.nomad.systemtray" )

    property bool firstChildOfStartLayout: index === appletItem.layouter.startLayout.firstVisibleIndex
    property bool firstChildOfMainLayout: index === appletItem.layouter.mainLayout.firstVisibleIndex
    property bool lastChildOfMainLayout: index === appletItem.layouter.mainLayout.lastVisibleIndex
    property bool lastChildOfEndLayout: index === appletItem.layouter.endLayout.lastVisibleIndex

    readonly property bool atScreenEdge: {
        if (appletItem.myView.alignment === LatteCore.Types.Center) {
            return false;
        }

        if (appletItem.myView.alignment === LatteCore.Types.Justify) {
            //! Justify case
            if (root.maxLengthPerCentage!==100 || Plasmoid.configuration.offset!==0) {
                return false;
            }

            if (root.isHorizontal) {
                if (firstChildOfStartLayout) {
                    return latteView && latteView.x === latteView.screenGeometry.x;
                } else if (lastChildOfEndLayout) {
                    return latteView && ((latteView.x + latteView.width) === (latteView.screenGeometry.x + latteView.screenGeometry.width));
                }
            } else {
                if (firstChildOfStartLayout) {
                    return latteView && latteView.y === latteView.screenGeometry.y;
                } else if (lastChildOfEndLayout) {
                    return latteView && ((latteView.y + latteView.height) === (latteView.screenGeometry.y + latteView.screenGeometry.height));
                }
            }

            return false;
        }

        if (appletItem.myView.alignment === LatteCore.Types.Left && Plasmoid.configuration.offset===0) {
            //! Left case
            return firstChildOfMainLayout;
        } else if (appletItem.myView.alignment === LatteCore.Types.Right && Plasmoid.configuration.offset===0) {
            //! Right case
            return lastChildOfMainLayout;
        }

        if (appletItem.myView.alignment === LatteCore.Types.Top && Plasmoid.configuration.offset===0) {
            return firstChildOfMainLayout && latteView && latteView.y === latteView.screenGeometry.y;
        } else if (appletItem.myView.alignment === LatteCore.Types.Bottom && Plasmoid.configuration.offset===0) {
            return lastChildOfMainLayout && latteView && ((latteView.y + latteView.height) === (latteView.screenGeometry.y + latteView.screenGeometry.height));
        }

        return false;
    }

    //applet is in starting edge
    property bool firstAppletInContainer: (index >=0) &&
                                          ((index === layouter.startLayout.firstVisibleIndex)
                                           || (index === layouter.mainLayout.firstVisibleIndex)
                                           || (index === layouter.endLayout.firstVisibleIndex))

    //applet is in ending edge
    property bool lastAppletInContainer: (index >=0) &&
                                         ((index === layouter.startLayout.lastVisibleIndex)
                                          || (index === layouter.mainLayout.lastVisibleIndex)
                                          || (index === layouter.endLayout.lastVisibleIndex))

    readonly property bool acceptMouseEvents: applet
                                              && !indexerIsSupported
                                              && !originalAppletBehavior
                                              && parabolicEffectIsSupported
                                              && !appletItem.isSeparator
                                              && !communicator.requires.parabolicEffectLocked

    //! This property is an effort in order to group behaviors into one property. This property is responsible to enable/disable
    //! Applets OnTop MouseArea which is used for ParabolicEffect and ThinTooltips. For Latte panels things
    //! are pretty straight, the original plasma behavior is replicated so parabolic effect and thin tooltips are disabled.
    //! For Latte docks things are a bit more complicated. Applets that can not support parabolic effect inside docks
    //! are presenting their original plasma behavior and also applets that even though can be zoomed user has chose
    //! to lock its parabolic effect.
    readonly property bool originalAppletBehavior: root.behaveAsPlasmaPanel
                                                   || !parabolicEffectIsSupported
                                                   || (root.behaveAsDockWithMask && !parabolicEffectIsSupported)
                                                   || (root.behaveAsDockWithMask && parabolicEffectIsSupported && lockZoom)

    readonly property bool isIndicatorDrawn: indicatorBackLayer.level.isDrawn
    readonly property bool isSquare: parabolicEffectIsSupported
    readonly property bool screenEdgeMarginSupported: communicator.requires.screenEdgeMarginSupported || communicator.indexerIsSupported

    property int animationTime: appletItem.animations.speedFactor.normal * (1.2*appletItem.animations.duration.small)
    property int index: -1
    property int maxWidth: root.isHorizontal ? root.height : root.width
    property int maxHeight: root.isHorizontal ? root.height : root.width
    property int internalSplitterId: 0

    property int previousIndex: -1
    property int spacersMaxSize: Math.max(0,Math.ceil(0.55 * metrics.iconSize) - metrics.totals.lengthEdges)
    property int status: applet ? applet.plasmoid.status : -1

    //! some metrics
    readonly property int appletMinimumLength: _wrapper.appletMinimumLength
    readonly property int appletPreferredLength: _wrapper.appletPreferredLength
    readonly property int appletMaximumLength: _wrapper.appletMaximumLength

    //! separators tracking: the walk verdicts live in the C++ core
    //! (org.kde.latte.core VisibleIndex, EX-06); only the live bridge
    //! delegation stays here - when the resolved neighbor manages
    //! sub-indexed items its own edge answers, read through the bridge
    readonly property bool tailAppletIsSeparator: {
        if (appletItem.isSeparator || appletItem.index<0) {
            return false;
        }

        var neighbor = LatteCore.VisibleIndex.hiddenSkippingNeighbor(appletItem.indexer.rowEntries, appletItem.index, LatteCore.VisibleIndex.Tail);

        if (neighbor.index >= 0 && appletItem.indexer.clients.indexOf(neighbor.index)>=0) {
            var tailBridge = appletItem.indexer.getClientBridge(neighbor.index);

            if (tailBridge && tailBridge.client) {
                return tailBridge.client.lastHeadItemIsSeparator;
            }
        }

        return neighbor.isSeparator === true;
    }

    readonly property bool headAppletIsSeparator: {
        if (appletItem.isSeparator || appletItem.index<0) {
            return false;
        }

        var neighbor = LatteCore.VisibleIndex.hiddenSkippingNeighbor(appletItem.indexer.rowEntries, appletItem.index, LatteCore.VisibleIndex.Head);

        if (neighbor.index >= 0 && appletItem.indexer.clients.indexOf(neighbor.index)>=0) {
            var headBridge = appletItem.indexer.getClientBridge(neighbor.index);

            if (headBridge && headBridge.client) {
                return headBridge.client.firstTailItemIsSeparator;
            }
        }

        return neighbor.isSeparator === true;
    }

    readonly property bool inMarginsArea: LatteCore.VisibleIndex.isInMarginsArea(appletItem.indexer.rowEntries, appletItem.index)

    //! local margins
    readonly property bool parabolicEffectMarginsEnabled: appletItem.parabolic.factor.zoom>1 && !originalAppletBehavior && !communicator.parabolicEffectIsSupported

    property int lengthAppletPadding:{
        if (!isIndicatorDrawn) {
            return 0;
        }

        return metrics.fraction.lengthAppletPadding === -1 || parabolicEffectMarginsEnabled ? metrics.padding.length : metrics.padding.lengthApplet;
    }

    property int lengthAppletFullMargin: 0
    property int lengthAppletFullMargins: 2 * lengthAppletFullMargin

    property int internalWidthMargins: root.isVertical ? metrics.totals.thicknessEdges : 2 * lengthAppletPadding
    property int internalHeightMargins: root.isHorizontal ? root.metrics.totals.thicknessEdges : 2 * lengthAppletPadding

    readonly property string pluginName: isInternalViewSplitter ? "org.kde.latte.splitter" : (applet ? applet.plasmoid.pluginName : "")

    //! are set by the indicator
    readonly property int iconOffsetX: indicatorBackLayer.level.requested.iconOffsetX
    readonly property int iconOffsetY: indicatorBackLayer.level.requested.iconOffsetY
    readonly property int iconTransformOrigin: indicatorBackLayer.level.requested.iconTransformOrigin
    readonly property real iconOpacity: indicatorBackLayer.level.requested.iconOpacity
    readonly property real iconRotation: indicatorBackLayer.level.requested.iconRotation
    readonly property real iconScale: indicatorBackLayer.level.requested.iconScale

    property real computeWidth: root.isVertical ? wrapper.width :
                                                  hiddenSpacerLeft.width+wrapper.width+hiddenSpacerRight.width

    property real computeHeight: root.isVertical ? hiddenSpacerLeft.height + wrapper.height + hiddenSpacerRight.height :
                                                   wrapper.height

    property Item applet: null
    property Item latteStyleApplet: applet && ((applet.plasmoid.pluginName === "org.kde.latte.spacer") || (applet.plasmoid.pluginName === "org.kde.latte.separator")) ?
                                        (applet.children[0] ? applet.children[0] : null) : null

    property Item appletWrapper: wrapper.wrapperContainer

    property Item tooltipVisualParent: titleTooltipParent

    readonly property alias communicator: _communicator
    readonly property alias wrapper: _wrapper
    readonly property alias restoreAnimation: _restoreAnimation

    property Item animations: null
    property Item debug: null
    property Item environment: null
    property Item indexer: null
    property Item indicators: null
    property Item launchers: null
    property Item layouter: null
    property Item layouts: null
    property Item metrics: null
    property Item myView: null
    property Item parabolic: null
    property Item shortcuts: null
    property Item thinTooltip: null
    property Item userRequests: null

    property bool containsMouse: parabolicAreaLoader.active && parabolicAreaLoader.item.containsMouse

    //! keyboard focus mode: the same visible-index match
    //! onSglActivateEntryAtIndex activates on, so highlight and Enter
    //! always agree; applets that manage their own position shortcuts
    //! (tasks) are excluded here exactly as they are there - their
    //! sub-items highlight themselves (BasicItem.isKeyboardFocused)
    readonly property bool isKeyboardFocused: appletItem.shortcuts && appletItem.shortcuts.keyboardFocusedEntryIndex >= 0
                                              && appletItem.shortcuts.unifiedGlobalShortcuts
                                              && !_communicator.positionShortcutsAreSupported
                                              && appletItem.indexer && appletItem.indexer.visibleIndex(appletItem.index) === appletItem.shortcuts.keyboardFocusedEntryIndex
    //! Screen-reader surface (Phase 10 AT-SPI rollout): a plain applet
    //! container acts as a button whose press toggles the applet's popup -
    //! toggleExpanded(), the same body Meta+<number> entry activation
    //! runs. Pruned from the tree: containers whose applet manages its own
    //! sub-items (the tasks plasmoid - each task announces itself) and
    //! non-interactive fillers. Accessible.focused mirrors the keyboard
    //! focus mode's focused entry, so screen readers follow the
    //! Meta+Alt+D traversal.
    Accessible.ignored: !applet || isSeparator || isSpacer || isHidden || isInternalViewSplitter || communicator.indexerIsSupported
    Accessible.role: Accessible.Button
    Accessible.name: applet && applet.plasmoid ? applet.plasmoid.title : ""
    Accessible.focusable: true
    Accessible.focused: isKeyboardFocused
    Accessible.onPressAction: appletItem.toggleExpanded()

    //! whether this item has live ParabolicArea slots; the parabolic
    //! ability's row builder maps items without one to DeadStop (a live
    //! scale stack dies at them - EX-02 in docs/QML_EXTRACTION_PLAN.md)
    readonly property bool hasParabolicMessagesHandler: parabolicAreaLoader.active
    property bool pressed: viewSignalsConnector.pressed


    //// BEGIN :: Animate Applet when a new applet is dragged in the view

    //when the applet moves caused by its resize, don't animate.
    //this is completely heuristic, but looks way less "jumpy"
    property bool movingForResize: false
    property int oldX: x
    property int oldY: y

    onXChanged: {
        if (root.isVertical) {
            return;
        }

        if (movingForResize) {
            movingForResize = false;
            return;
        } else if (inDraggingOverAppletOrOutOfContainment) {
            return;
        }

        var draggingAppletInConfigure = root.dragOverlay && root.dragOverlay.currentApplet;
        var isCurrentAppletInDragging = draggingAppletInConfigure && (root.dragOverlay.currentApplet === appletItem);
        var dropApplet = root.dragInfo.entered && root.dragInfo.isPlasmoid

        if ((isCurrentAppletInDragging || !draggingAppletInConfigure) && !dropApplet) {
            return;
        }

        if (!root.isVertical) {
            translation.x = oldX - x;
            translation.y = 0;
        } else {
            translation.y = oldY - y;
            translation.x = 0;
        }

        translAnim.running = true

        if (!root.isVertical) {
            oldX = x;
            oldY = 0;
        } else {
            oldY = y;
            oldX = 0;
        }
    }

    onYChanged: {
        if (root.isHorizontal) {
            return;
        }

        if (movingForResize) {
            movingForResize = false;
            return;
        } else if (inDraggingOverAppletOrOutOfContainment) {
            return;
        }

        var draggingAppletInConfigure = root.dragOverlay && root.dragOverlay.currentApplet;
        var isCurrentAppletInDragging = draggingAppletInConfigure && (root.dragOverlay.currentApplet === appletItem);
        var dropApplet = root.dragInfo.entered && root.dragInfo.isPlasmoid

        if ((isCurrentAppletInDragging || !draggingAppletInConfigure) && !dropApplet) {
            return;
        }
        if (!root.isVertical) {
            translation.x = oldX - x;
            translation.y = 0;
        } else {
            translation.y = oldY - y;
            translation.x = 0;
        }

        translAnim.running = true;

        if (!root.isVertical) {
            oldX = x;
            oldY = 0;
        } else {
            oldY = y;
            oldX = 0;
        }
    }

    transform: Translate {
        id: translation
    }

    NumberAnimation {
        id: translAnim
        duration: appletItem.animations.duration.large
        easing.type: Easing.InOutQuad
        target: translation
        properties: "x,y"
        to: 0
    }

    Behavior on lengthAppletPadding {
        NumberAnimation {
            duration: 0.8 * appletItem.animations.duration.proposed
            easing.type: Easing.OutCubic
        }
    }

    //// END :: Animate Applet when a new applet is dragged in the view

    /// BEGIN functions
    //! Toggles this applet's popup through the view's extended interface -
    //! the one activation body shared by Meta+<number> entry activation,
    //! the keyboard focus mode's Enter, neutral-area clicks and the
    //! screen-reader press action, so they can never diverge.
    function toggleExpanded() {
        if (!applet || !root.latteView) {
            //! legitimate transient states, not defects: the applet is not
            //! attached yet during load, and the view reference drops
            //! during teardown - nothing to toggle in either
            return;
        }

        root.latteView.extendedInterface.toggleAppletExpanded(applet.plasmoid.id);
    }

    function activateAppletForNeutralAreas(mouse){
        //if the event is at the active indicator or spacers area then try to expand the applet,
        //unfortunately for other applets there is no other way to activate them yet
        //for example the icon-only applets
        var choords = mapToItem(appletItem.appletWrapper, mouse.x, mouse.y);

        var wrapperContainsMouse = choords.x>=0 && choords.y>=0 && choords.x<appletItem.appletWrapper.width && choords.y<appletItem.appletWrapper.height;
        var appletItemContainsMouse = mouse.x>=0 && mouse.y>=0 && mouse.x<width && mouse.y<height;

        //console.log(" APPLET :: " + mouse.x +  " _ " + mouse.y);
        //console.log(" WRAPPER :: " + choords.x + " _ " + choords.y);

        var inThicknessNeutralArea = !wrapperContainsMouse && (appletItem.metrics.margin.screenEdge>0);
        var appletNeutralAreaEnabled = !(inThicknessNeutralArea && root.dragActiveWindowEnabled);

        if (appletItemContainsMouse && !wrapperContainsMouse && appletNeutralAreaEnabled) {
            //console.log("PASSED");
            appletItem.toggleExpanded();
        } else {
            //console.log("REJECTED");
        }
    }

    //! the rank math is core-side (assignedLayoutIndex, the fe63a63e
    //! semantics: edge spacers/internal splitters are uncounted and an
    //! uncounted self keeps index -1); this only scans the live children.
    //! endLayout's beginIndex is deliberately very high so mainLayout and
    //! endLayout never need to exchange hovering messages.
    function checkIndex(){
        index = -1;

        var grids = [layoutsContainer.startLayout, layoutsContainer.mainLayout, layoutsContainer.endLayout];
        var counts = [appletItem.layouter.startLayout.count, appletItem.layouter.mainLayout.count, appletItem.layouter.endLayout.count];

        for (var g=0; g<grids.length; ++g) {
            var counted = [];
            var selfPosition = -1;

            for(var i=0; i<counts[g]; ++i){
                var child = grids[g].children[i];
                counted.push(!(child.isParabolicEdgeSpacer || child.isInternalViewSplitter));
                if (child === appletItem){
                    selfPosition = i;
                }
            }

            if (selfPosition >= 0) {
                index = LatteCore.VisibleIndex.assignedLayoutIndex(counted, selfPosition, grids[g].beginIndex);
                return;
            }
        }
    }

    function sltClearZoom(){
        if (communicator.parabolicEffectIsSupported) {
            communicator.bridge.parabolic.client.sglClearZoom();
        } else {
            restoreAnimation.start();
        }
    }

    function updateParabolicEffectIsSupported(){
        parabolicEffectIsSupportedTimer.start();
    }

    //! Reduce calculations and give the time to applet to adjust to prevent binding loops
    Timer{
        id: parabolicEffectIsSupportedTimer
        interval: 100
        onTriggered: {
            if (wrapper.zoomScale !== 1) {
                return;
            }

            if (appletItem.communicator.indexerIsSupported) {
                appletItem.parabolicEffectIsSupported = true;
                return;
            }

            var maxSize = 1.5 * appletItem.metrics.iconSize;
            var maxForMinimumSize = 1.5 * appletItem.metrics.iconSize;

            if ( isSystray
                    || appletItem.isAutoFillApplet
                    || (((applet && root.isHorizontal && (applet.width > maxSize || applet.Layout.minimumWidth > maxForMinimumSize))
                         || (applet && root.isVertical && (applet.height > maxSize || applet.Layout.minimumHeight > maxForMinimumSize)))
                        && !appletItem.isSpacer) ) {
                appletItem.parabolicEffectIsSupported = false;
            } else {
                appletItem.parabolicEffectIsSupported = true;
            }
        }
    }

    function slotDestroyInternalViewSplitters() {
        if (isInternalViewSplitter) {
            destroy();
        }
    }

    //! pos in global root positioning
    function containsPos(pos) {
        var relPos = root.mapToItem(appletItem,pos.x, pos.y);

        if (relPos.x>=0 && relPos.x<=width && relPos.y>=0 && relPos.y<=height)
            return true;

        return false;
    }

    ///END functions

    //BEGIN connections
    //! Plasma 6 undo contract: deleting a widget marks its applet destroyed() and
    //! keeps the object alive while the "Widget Removed" undo notification is open.
    //! This watcher was born (71b0d75a) because at libplasma 6.6.5 containment-type
    //! applets (System Tray) got NO immediate appletRemoved - askDestroy() guarded
    //! the emit with !isContainment() - and their slot sat as a ghost for the whole
    //! undo window (measured 60s, the libplasma fallback timer). libplasma 6.7
    //! widened the guard (containment() != q), so every class now gets the
    //! immediate emit and removeAppletItem parks too; both calls meet in
    //! setAppletInScheduledDestruction's per-id idempotence. The watcher stays
    //! load-bearing for the UNDO direction: destroyedChanged(false) fires before
    //! libplasma re-emits appletAdded, so this is what unparks and re-shows the
    //! container in place (addAppletItem's "reaches here twice" guard relies on
    //! that ordering). Contract pinned by askdestroysignalorderingtest.
    Connections {
        target: appletItem.applet ? appletItem.applet.plasmoid : null
        function onDestroyedChanged(destroyed) {
            fastLayoutManager.setAppletInScheduledDestruction(appletItem.applet.plasmoid.id, destroyed);
        }
    }

    onAppletChanged: {
        if (!applet) {
            destroy();
        }
    }

    onIndexChanged: {
        if (index>-1) {
            previousIndex = index;
        }
    }

    onIsSystrayChanged: {
        updateParabolicEffectIsSupported();
    }

    onIsAutoFillAppletChanged: updateParabolicEffectIsSupported();
    onParentChanged: checkIndex()

    Component.onCompleted: {
        checkIndex();
        root.updateIndexes.connect(checkIndex);
        root.destroyInternalViewSplitters.connect(slotDestroyInternalViewSplitters);

        parabolic.sglClearZoom.connect(sltClearZoom);
    }

    Component.onDestruction: {
        appletItem.animations.needBothAxis.removeEvent(appletItem);

        root.updateIndexes.disconnect(checkIndex);
        root.destroyInternalViewSplitters.disconnect(slotDestroyInternalViewSplitters);

        parabolic.sglClearZoom.disconnect(sltClearZoom);
    }

    //! Bindings

    Binding {
        //! is used to aboid loop binding warnings on startup
        target: appletItem
        property: "lengthAppletFullMargin"
        when: !communicator.inStartup
        value: lengthAppletPadding + metrics.margin.length;
        restoreMode: Binding.RestoreNone
    }

    //! Connections
    Connections{
        target: appletItem.shortcuts

        function onSglActivateEntryAtIndex(entryIndex) {
            if (!appletItem.shortcuts.unifiedGlobalShortcuts) {
                return;
            }

            var visibleIndex = appletItem.indexer.visibleIndex(appletItem.index);

            if (visibleIndex === entryIndex && !communicator.positionShortcutsAreSupported) {
                appletItem.toggleExpanded();
            }
        }

        function onSglNewInstanceForEntryAtIndex(entryIndex) {
            if (!appletItem.shortcuts.unifiedGlobalShortcuts) {
                return;
            }

            var visibleIndex = appletItem.indexer.visibleIndex(appletItem.index);

            if (visibleIndex === entryIndex && !communicator.positionShortcutsAreSupported) {
                appletItem.toggleExpanded();
            }
        }
    }

    Connections {
        id: viewSignalsConnector
        target: root.latteView ? root.latteView : null
        enabled: !appletItem.indexerIsSupported && !appletItem.isSeparator && !appletItem.isSpacer && !appletItem.isHidden

        property bool pressed: false
        property bool blockWheel: false

        function onMousePressed(pos, button) {
            if (appletItem.containsPos(pos)) {
                viewSignalsConnector.pressed = true;
                var local = appletItem.mapFromItem(root, pos.x, pos.y);

                appletItem.mousePressed(local.x, local.y, button);
            }
        }

        function onMouseReleased(pos, button) {
            if (appletItem.containsPos(pos)) {
                viewSignalsConnector.pressed = false;
                var local = appletItem.mapFromItem(root, pos.x, pos.y);
                appletItem.mouseReleased(local.x, local.y, button);
            }
        }

        function onWheelScrolled(pos, angleDelta, buttons) {
            if (!appletItem.applet || !root.mouseWheelActions || viewSignalsConnector.blockWheel || !appletItem.myView.isShownFully) {
                return;
            }

            blockWheel = true;
            scrollDelayer.start();

            if (appletItem.containsPos(pos)
                    && (root.latteView.extendedInterface.appletIsExpandable(applet.plasmoid.id)
                        || (root.latteView.extendedInterface.appletIsActivationTogglesExpanded(applet.plasmoid.id)))) {
                var angle = angleDelta.y / 8;
                var expanded = root.latteView.extendedInterface.appletIsExpanded(applet.plasmoid.id);

                if ((angle > 12 && !expanded) /*positive direction*/
                        || (angle < -12 && expanded) /*negative direction*/) {
                    latteView.extendedInterface.toggleAppletExpanded(applet.plasmoid.id);
                }
            }
        }
    }

    Connections {
        target: root.latteView ? root.latteView.extendedInterface : null
        enabled: !appletItem.indexerIsSupported && !appletItem.isSeparator && !appletItem.isSpacer && !appletItem.isHidden

        onExpandedAppletStateChanged: {
            if (latteView.extendedInterface.hasExpandedApplet && appletItem.applet) {
                appletItem.isExpanded = latteView.extendedInterface.appletIsExpandable(appletItem.applet.plasmoid.id)
                        && latteView.extendedInterface.appletIsExpanded(appletItem.applet.plasmoid.id);
            } else {
                appletItem.isExpanded = false;
            }
        }
    }

    ///END connections

    //! It is used for any communication needed with the underlying applet
    Communicator.Engine{
        id: _communicator
    }

    /*  Rectangle{
        anchors.fill: parent
        color: "transparent"
        border.color: "green"
        border.width: 1
    }*/


    //! Main Applet Shown Area
    Flow{
        id: appletFlow
        width: appletItem.computeWidth
        height: appletItem.computeHeight

        // a hidden spacer for the first element to add stability
        // IMPORTANT: hidden spacers must be tested on vertical !!!
        HiddenSpacer{id: hiddenSpacerLeft}

        Item {
            width: wrapper.width
            height: wrapper.height

            AbilityItem.IndicatorObject {
                id: appletIndicatorObj
                animations: appletItem.animations
                metrics: appletItem.metrics
                host: appletItem.indicators

                isApplet: true

                isActive: appletItem.isActive
                //! keyboard focus mode reuses the hover chrome as its
                //! visible focus indicator
                isHovered: appletItem.containsMouse || appletItem.isKeyboardFocused
                isPressed: appletItem.isPressed
                isSquare: appletItem.isSquare

                hasActive: appletItem.isActive

                scaleFactor: appletItem.wrapper.zoomScale
                panelOpacity: root.background.currentOpacity
                shadowColor: appletItem.myView.itemShadow.shadowSolidColor

                colorPalette: colorizerManager.applyTheme

                //!icon colors
                iconBackgroundColor: appletItem.wrapper.overlayIconLoader.backgroundColor
                iconGlowColor: appletItem.wrapper.overlayIconLoader.glowColor
            }

            //! InConfigureApplets visual paddings
            Loader {
                anchors.fill: _wrapper
                active: root.inConfigureAppletsMode && !isInternalViewSplitter
                sourceComponent: PaddingsInConfigureApplets{
                    color: appletItem.highlightColor
                }
            }

            //! Indicator Back Layer
            IndicatorLevel{
                id: indicatorBackLayer
                level.isBackground: true
                level.indicator: appletIndicatorObj

                Loader{
                    anchors.fill: parent
                    active: appletItem.debug.graphicsEnabled && parent.active
                    sourceComponent: Rectangle{
                        color: "transparent"
                        border.width: 1
                        border.color: "purple"
                        opacity: 0.4
                    }
                }
            }

            ItemWrapper{
                id: _wrapper

                //! D21 approach B (docs/known-defects.md): push the colorizer's
                //! decided scheme into this stock applet's OWN Kirigami.Theme
                //! color group. Native content (the digital clock's
                //! Text.NativeRendering label, symbolic icons) then renders with
                //! the scheme's contrast directly, the way stock Plasma panels
                //! colour their applets - no layer-FBO ColorOverlay, which never
                //! captured NativeRendering text (it sampled empty and the
                //! original was hidden, so the clock went blank) and rendered
                //! overlay-exempt applets like show-desktop in their native
                //! Breeze-dark icon (white on a light panel). inherit flips back
                //! on for applets the colorizer does not apply to, so their own
                //! Plasma palette is left untouched.
                Kirigami.Theme.inherit: !appletItem.colorizerPaletteActive
                Kirigami.Theme.textColor: appletItem.colorizerHost.textColor
                Kirigami.Theme.backgroundColor: appletItem.colorizerHost.backgroundColor
                Kirigami.Theme.highlightColor: appletItem.colorizerHost.highlightColor
                Kirigami.Theme.highlightedTextColor: appletItem.colorizerHost.highlightedTextColor
                Kirigami.Theme.positiveTextColor: appletItem.colorizerHost.positiveTextColor
                Kirigami.Theme.neutralTextColor: appletItem.colorizerHost.neutralTextColor
                Kirigami.Theme.negativeTextColor: appletItem.colorizerHost.negativeTextColor

                TitleTooltipParent{
                    id: titleTooltipParent
                    metrics: appletItem.metrics
                    parabolic: appletItem.parabolic
                }
            }

            //! Deliberate Qt5 deviation, explicitly requested: applets whose
            //! rendered content is multicolored are exempt from colorizing
            //! automatically (flattening a full-color icon to the scheme text
            //! color produces a featureless blob; monochrome line-art is what
            //! the colorizer is for). Pixel-measured in C++, not icon-name
            //! guessed - see plugin/iconcolorfulness.cpp for why QML Canvas
            //! cannot do this analysis. Until the measurement lands the Qt5
            //! default applies, so symbolic icons never flash unthemed.
            LatteContainment.IconColorfulness {
                id: colorfulnessProbe
                target: appletItem.applet && root.colorizerEnabled
                        && !appletItem.appletBlocksColorizing
                        && !appletItem.isInternalViewSplitter ? appletItem.appletWrapper : null

                readonly property bool blocksColorizing: known && colorful

                //! icons often finish loading after the applet item exists;
                //! retry until a grab carries enough opaque pixels to judge
                readonly property Timer retry: Timer {
                    interval: 2000
                    repeat: true
                    running: colorfulnessProbe.target !== null && !colorfulnessProbe.known
                    onTriggered: colorfulnessProbe.measure()
                }
            }

            //! The Applet Colorizer
            Colorizer.Applet {
                id: appletColorizer
                anchors.fill: parent
                opacity: mustBeShown ? 1 : 0

                //! D21 approach B: the FBO ColorOverlay is RETIRED - the
                //! _wrapper Kirigami.Theme push above colours native content
                //! directly, which the overlay could never do for
                //! Text.NativeRendering (it sampled an empty FBO and blanked the
                //! clock). Held at mustBeShown:false so the overlay never
                //! engages and the wrapper is never hidden; kept inert as the
                //! single rollback point. The scope it used to cover now lives
                //! in appletItem.colorizerPaletteActive.
                readonly property bool mustBeShown: false

                Behavior on opacity {
                    NumberAnimation {
                        duration: 1.2 * appletItem.animations.duration.proposed
                        easing.type: Easing.OutCubic
                    }
                }
            }

            //! Indicator Front Layer
            IndicatorLevel{
                id: indicatorFrontLayer
                level.isForeground: true
                level.indicator: appletIndicatorObj
            }

            //! Applet Shortcut Visual Badge
            Item {
                id: shortcutBadgeContainer

                width: {
                    if (root.isHorizontal) {
                        return appletItem.metrics.iconSize * wrapper.zoomScale
                    } else {
                        return badgeThickness;
                    }
                }

                height: {
                    if (root.isHorizontal) {
                        return badgeThickness;
                    } else {
                        return appletItem.metrics.iconSize * wrapper.zoomScale
                    }
                }

                readonly property int badgeThickness: {
                    if (Plasmoid.location === PlasmaCore.Types.BottomEdge
                            || Plasmoid.location === PlasmaCore.Types.RightEdge) {
                        var marginthickness = appletItem.metrics.margin.tailThickness * wrapper.zoomMarginScale;
                        return (appletItem.metrics.iconSize * wrapper.zoomScale) + marginthickness + appletItem.metrics.margin.screenEdge;
                    }

                    var marginthickness = appletItem.metrics.margin.headThickness * wrapper.zoomMarginScale;
                    return (appletItem.metrics.iconSize * wrapper.zoomScale) + marginthickness;
                }

                ShortcutBadge{
                    anchors.fill: parent
                }

                states:[
                    State{
                        name: "horizontal"
                        when: Plasmoid.formFactor === PlasmaCore.Types.Horizontal

                        AnchorChanges{
                            target: shortcutBadgeContainer;
                            anchors.horizontalCenter: parent.horizontalCenter; anchors.verticalCenter: undefined;
                            anchors.right: undefined; anchors.left: undefined; anchors.top: undefined; anchors.bottom: parent.bottom;
                        }
                    },
                    State{
                        name: "vertical"
                        when: Plasmoid.formFactor === PlasmaCore.Types.Vertical

                        AnchorChanges{
                            target: shortcutBadgeContainer;
                            anchors.horizontalCenter: undefined; anchors.verticalCenter: parent.verticalCenter;
                            anchors.right: parent.right; anchors.left: undefined; anchors.top: undefined; anchors.bottom: undefined;
                        }
                    }
                ]
            }
        }

        // a hidden spacer on the right for the last item to add stability
        HiddenSpacer{id: hiddenSpacerRight; isRightSpacer: true}
    }// Flow with hidden spacers inside

    //Busy Indicator
    PlasmaComponents.BusyIndicator {
        z: 1000
        visible: applet && applet.plasmoid.busy
        running: visible
        anchors.centerIn: parent
        width: Math.min(parent.width, parent.height)
        height: width
    }

    Loader {
        id: parabolicAreaLoader
        width: root.isHorizontal ? appletItem.width : appletItem.metrics.mask.thickness.zoomedForItems
        height: root.isHorizontal ? appletItem.metrics.mask.thickness.zoomedForItems : appletItem.height
        //! must be enabled even for applets that are hidden in order to forward
        //! parabolic effect messages properly to surrounding plasma applets
        active: isParabolicEnabled || isThinTooltipEnabled || hasParabolicMessagesEnabled

        //! in hidden state applets must pass on parabolic messages to neighbours
        readonly property bool isParabolicEnabled: appletItem.parabolic.isEnabled && !lockZoom
        readonly property bool isThinTooltipEnabled: appletItem.thinTooltip.isEnabled && !isHidden
        readonly property bool hasParabolicMessagesEnabled: appletItem.parabolic.isEnabled && (!lockZoom || isSeparator || isMarginsAreaSeparator || isHidden)

        sourceComponent: ParabolicArea{}

        states:[
            State{
                name: "top"
                when: Plasmoid.location === PlasmaCore.Types.TopEdge

                AnchorChanges{
                    target: parabolicAreaLoader
                    anchors.horizontalCenter: parent.horizontalCenter; anchors.verticalCenter: undefined;
                    anchors.right: undefined; anchors.left: undefined; anchors.top: parent.top; anchors.bottom: undefined;
                }
            },
            State{
                name: "left"
                when: Plasmoid.location === PlasmaCore.Types.LeftEdge

                AnchorChanges{
                    target: parabolicAreaLoader
                    anchors.horizontalCenter: undefined; anchors.verticalCenter: parent.verticalCenter;
                    anchors.right: undefined; anchors.left: parent.left; anchors.top: undefined; anchors.bottom: undefined;
                }
            },
            State{
                name: "right"
                when: Plasmoid.location === PlasmaCore.Types.RightEdge

                AnchorChanges{
                    target: parabolicAreaLoader
                    anchors.horizontalCenter: undefined; anchors.verticalCenter: parent.verticalCenter;
                    anchors.right: parent.right; anchors.left: undefined; anchors.top: undefined; anchors.bottom: undefined;
                }
            },
            State{
                name: "bottom"
                when: Plasmoid.location === PlasmaCore.Types.BottomEdge

                AnchorChanges{
                    target: parabolicAreaLoader
                    anchors.horizontalCenter: parent.horizontalCenter; anchors.verticalCenter: undefined;
                    anchors.right: undefined; anchors.left: undefined; anchors.top: undefined; anchors.bottom: parent.bottom;
                }
            }
        ]
    }

    //! Debug Elements
    Loader{
        anchors.bottom: parent.bottom
        anchors.left: parent.left

        active: appletItem.debug.layouterEnabled
        sourceComponent: Debugger.Tag{
            label.text: (root.isHorizontal ? appletItem.width : appletItem.height) + labeltext
            label.color: appletItem.isAutoFillApplet ? "green" : "white"

            readonly property string labeltext: {
                if (appletItem.isAutoFillApplet) {
                    return " || max_fill:"+appletItem.maxAutoFillLength + " / min_fill:"+appletItem.minAutoFillLength;
                }

                return "";
            }
        }
    }

    //! A timer is needed in order to handle also touchpads that probably
    //! send too many signals very fast. This way the signals per sec are limited.
    //! The user needs to have a steady normal scroll in order to not
    //! notice a annoying delay
    Timer{
        id: scrollDelayer
        interval: 500

        onTriggered: viewSignalsConnector.blockWheel = false;
    }

    //BEGIN states
    states: [
        State {
            name: "left"
            when: (Plasmoid.location === PlasmaCore.Types.LeftEdge)

            AnchorChanges {
                target: appletFlow
                anchors{ top:undefined; bottom:undefined; left:parent.left; right:undefined;}
            }
        },
        State {
            name: "right"
            when: (Plasmoid.location === PlasmaCore.Types.RightEdge)

            AnchorChanges {
                target: appletFlow
                anchors{ top:undefined; bottom:undefined; left:undefined; right:parent.right;}
            }
        },
        State {
            name: "bottom"
            when: (Plasmoid.location === PlasmaCore.Types.BottomEdge)

            AnchorChanges {
                target: appletFlow
                anchors{ top:undefined; bottom:parent.bottom; left:undefined; right:undefined;}
            }
        },
        State {
            name: "top"
            when: (Plasmoid.location === PlasmaCore.Types.TopEdge)

            AnchorChanges {
                target: appletFlow
                anchors{ top:parent.top; bottom:undefined; left:undefined; right:undefined;}
            }
        }
    ]
    //END states

    //BEGIN animations
    ///////Restore Zoom Animation/////
    ParallelAnimation{
        id: _restoreAnimation

        PropertyAnimation {
            target: wrapper
            property: "zoomScale"
            to: 1
            duration: 3 * appletItem.animationTime
            easing.type: Easing.InCubic
        }
    }
}
