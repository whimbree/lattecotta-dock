/*
    SPDX-FileCopyrightText: 2016 Smith AR <audoban@openmailbox.org>
    SPDX-FileCopyrightText: 2016 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

pragma ComponentBehavior: Bound

import QtQuick 2.7
import QtQuick.Layouts

import org.kde.plasma.plasmoid 2.0
import org.kde.plasma.core 2.0 as PlasmaCore
import org.kde.plasma.components 3.0 as PlasmaComponents
import org.kde.kquickcontrolsaddons 2.0
import org.kde.kirigami 2.20 as Kirigami

import org.kde.latte.core 0.2 as LatteCore
import org.kde.latte.components 1.0 as LatteComponents
import org.kde.latte.abilities.items 0.1 as AbilityItem

import "colorizer" as Colorizer
import "communicator" as Communicator
import "../debugger" as Debugger

Item {
    id: appletItem
    width: appletItem.isInternalViewSplitter && !appletItem.rootItem.inConfigureAppletsMode ? 0 : appletItem.computeWidth
    height: appletItem.isInternalViewSplitter && !appletItem.rootItem.inConfigureAppletsMode ? 0 : appletItem.computeHeight

    //any applets that exceed their limits should not take events from their surrounding applets
    //clip: !isSeparator && !parabolicAreaLoader.active

    signal mousePressed(int x, int y, int button);
    signal mouseReleased(int x, int y, int button);

    property bool animationsEnabled: true
    property bool indexerIsSupported: appletItem.communicator.indexerIsSupported
    property bool parabolicEffectIsSupported: true
    property bool canShowAppletNumberBadge: !appletItem.indexerIsSupported
                                            && !appletItem.isSeparator
                                            && !appletItem.isMarginsAreaSeparator
                                            && !appletItem.isHidden
                                            && !appletItem.isSpacer
                                            && !appletItem.isInternalViewSplitter

    readonly property bool canFillScreenEdge: appletItem.communicator.requires.screenEdgeMarginSupported || appletItem.communicator.indexerIsSupported
    readonly property bool canFillThickness: appletItem.applet && appletItem.applet.plasmoid && appletItem.applet.plasmoid.hasOwnProperty("constraintHints")
                                             && ((appletItem.applet.plasmoid.constraintHints & PlasmaCore.Types.CanFillArea) === PlasmaCore.Types.CanFillArea);

    readonly property bool isMarginsAreaSeparator: appletItem.applet && appletItem.applet.plasmoid && appletItem.applet.plasmoid.hasOwnProperty("constraintHints")
                                                   && ((appletItem.applet.plasmoid.constraintHints & PlasmaCore.Types.MarginAreasSeparator) === PlasmaCore.Types.MarginAreasSeparator);

    //! Ancestor marker for the shell package's CompactApplet expander: it finds
    //! its hosting AppletItem by walking up until this marker instead of a fixed
    //! number of parent hops (the Plasma 6 visual tree depth is not stable).
    readonly property bool isLatteAppletContainer: true

    readonly property color highlightColor: Kirigami.Theme.focusColor

    //! Fill Applet(s)
    property bool isAutoFillApplet: appletItem.isRequestingFill
    property bool isParabolicEdgeSpacer: false

    property bool isRequestingFill: {
        if (!appletItem.applet || !appletItem.applet.Layout) {
            return false;
        }

        if ((appletItem.rootItem.isHorizontal && appletItem.applet.Layout.fillWidth===true)
                || (appletItem.rootItem.isVertical && appletItem.applet.Layout.fillHeight===true)) {
            return !appletItem.isHidden;
        }

        return false;
    }

    property int maxAutoFillLength: -1 //it is used in calculations for fillWidth,fillHeight applets
    property int minAutoFillLength: -1 //it is used in calculations for fillWidth,fillHeight applets

    readonly property bool inConfigureAppletsDragging: appletItem.rootItem.dragOverlay
                                                       && appletItem.rootItem.dragOverlay.currentApplet
                                                       && appletItem.rootItem.dragOverlay.pressed

    property bool appletBlocksColorizing: !appletItem.communicator.requires.latteSideColoringEnabled || appletItem.communicator.indexerIsSupported
    property bool appletBlocksParabolicEffect: appletItem.communicator.requires.parabolicEffectLocked
    readonly property bool lockZoom: !appletItem.parabolicEffectIsSupported
                                     || appletItem.appletBlocksParabolicEffect
                                     || (appletItem.layoutManagerHost && appletItem.applet && (appletItem.layoutManagerHost.lockedZoomApplets.indexOf(appletItem.applet.plasmoid.id)>=0))
    readonly property bool userBlocksColorizing: appletItem.appletBlocksColorizing
                                                 || (appletItem.layoutManagerHost && appletItem.applet && (appletItem.layoutManagerHost.userBlocksColorizingApplets.indexOf(appletItem.applet.plasmoid.id)>=0))

    //! D21 (stock applet palette propagation): when the colorizer is engaged,
    //! this applet's OWN Kirigami.Theme color group is set to the decided
    //! scheme (the _wrapper push below). Palette-responsive native content,
    //! including the digital clock's Text.NativeRendering label, symbolic
    //! icons, and the Global Menu's inline full representation, gains the right
    //! contrast without the old layer-FBO ColorOverlay. Fixed image, SVG, and
    //! Rectangle pixels do not consume palette roles, so they remain unchanged
    //! and need no whole-applet exemption.
    readonly property bool colorizerPaletteActive: appletItem.colorizerHost.mustBeShown
                                                   && !appletItem.userBlocksColorizing
                                                   && !appletItem.isInternalViewSplitter

    //! why the palette push is or is not applied to this applet - the
    //! viewAppletsData colorizer readback (observability-first). "applied" is
    //! the active state; the rest name the single winning exemption.
    readonly property string colorizerExemptionReason: {
        if (appletItem.colorizerPaletteActive) {
            return "applied";
        } else if (!appletItem.colorizerHost.mustBeShown) {
            return "notEngaged";
        } else if (appletItem.isInternalViewSplitter) {
            return "splitter";
        } else if (appletItem.appletBlocksColorizing) {
            return "selfColored";
        } else if (appletItem.userBlocksColorizing) {
            return "userBlocked";
        }

        return "unknown";
    }

    property bool isActive: (appletItem.isExpanded
                             && !appletItem.communicator.indexerIsSupported
                             && appletItem.applet.plasmoid.pluginName !== "org.kde.activeWindowControl"
                             && appletItem.applet.plasmoid.pluginName !== "org.kde.plasma.appmenu")

    property bool isExpanded: false

    property bool isScheduledForDestruction: (appletItem.layoutManagerHost && appletItem.applet && appletItem.layoutManagerHost.appletsInScheduledDestruction.indexOf(appletItem.applet.plasmoid.id)>=0)
    property bool isHidden: (!appletItem.rootItem.inConfigureAppletsMode && ((appletItem.applet && appletItem.applet.plasmoid.status === PlasmaCore.Types.HiddenStatus ) || appletItem.isInternalViewSplitter)) || appletItem.isScheduledForDestruction
    property bool isInternalViewSplitter: (appletItem.internalSplitterId > 0)
    property bool isZoomed: false
    property bool isPlaceHolder: false
    property bool isPressed: viewSignalsConnector.pressed
    property bool isSeparator: appletItem.applet && (appletItem.applet.plasmoid.pluginName === "audoban.applet.separator"
                                                     || appletItem.applet.plasmoid.pluginName === "org.kde.latte.separator")
    property bool isSpacer: appletItem.applet && (appletItem.applet.plasmoid.pluginName === "org.kde.latte.spacer")
    property bool isSystray: appletItem.applet && (appletItem.applet.plasmoid.pluginName === "org.kde.plasma.systemtray" || appletItem.applet.plasmoid.pluginName === "org.nomad.systemtray" )

    property bool firstChildOfStartLayout: appletItem.index === appletItem.layouter.startLayout.firstVisibleIndex
    property bool firstChildOfMainLayout: appletItem.index === appletItem.layouter.mainLayout.firstVisibleIndex
    property bool lastChildOfMainLayout: appletItem.index === appletItem.layouter.mainLayout.lastVisibleIndex
    property bool lastChildOfEndLayout: appletItem.index === appletItem.layouter.endLayout.lastVisibleIndex

    readonly property bool atScreenEdge: {
        if (appletItem.myView.alignment === LatteCore.Types.Center) {
            return false;
        }

        if (appletItem.myView.alignment === LatteCore.Types.Justify) {
            //! Justify case
            if (appletItem.rootItem.maxLengthPerCentage!==100 || Plasmoid.configuration.offset!==0) {
                return false;
            }

            if (appletItem.rootItem.isHorizontal) {
                if (appletItem.firstChildOfStartLayout) {
                    return appletItem.rootItem.latteView && appletItem.rootItem.latteView.x === appletItem.rootItem.latteView.screenGeometry.x;
                } else if (appletItem.lastChildOfEndLayout) {
                    return appletItem.rootItem.latteView && ((appletItem.rootItem.latteView.x + appletItem.rootItem.latteView.width) === (appletItem.rootItem.latteView.screenGeometry.x + appletItem.rootItem.latteView.screenGeometry.width));
                }
            } else {
                if (appletItem.firstChildOfStartLayout) {
                    return appletItem.rootItem.latteView && appletItem.rootItem.latteView.y === appletItem.rootItem.latteView.screenGeometry.y;
                } else if (appletItem.lastChildOfEndLayout) {
                    return appletItem.rootItem.latteView && ((appletItem.rootItem.latteView.y + appletItem.rootItem.latteView.height) === (appletItem.rootItem.latteView.screenGeometry.y + appletItem.rootItem.latteView.screenGeometry.height));
                }
            }

            return false;
        }

        if (appletItem.myView.alignment === LatteCore.Types.Left && Plasmoid.configuration.offset===0) {
            //! Left case
            return appletItem.firstChildOfMainLayout;
        } else if (appletItem.myView.alignment === LatteCore.Types.Right && Plasmoid.configuration.offset===0) {
            //! Right case
            return appletItem.lastChildOfMainLayout;
        }

        if (appletItem.myView.alignment === LatteCore.Types.Top && Plasmoid.configuration.offset===0) {
            return appletItem.firstChildOfMainLayout && appletItem.rootItem.latteView && appletItem.rootItem.latteView.y === appletItem.rootItem.latteView.screenGeometry.y;
        } else if (appletItem.myView.alignment === LatteCore.Types.Bottom && Plasmoid.configuration.offset===0) {
            return appletItem.lastChildOfMainLayout && appletItem.rootItem.latteView && ((appletItem.rootItem.latteView.y + appletItem.rootItem.latteView.height) === (appletItem.rootItem.latteView.screenGeometry.y + appletItem.rootItem.latteView.screenGeometry.height));
        }

        return false;
    }

    //applet is in starting edge
    property bool firstAppletInContainer: (appletItem.index >=0) &&
                                          ((appletItem.index === appletItem.layouter.startLayout.firstVisibleIndex)
                                           || (appletItem.index === appletItem.layouter.mainLayout.firstVisibleIndex)
                                           || (appletItem.index === appletItem.layouter.endLayout.firstVisibleIndex))

    //applet is in ending edge
    property bool lastAppletInContainer: (appletItem.index >=0) &&
                                         ((appletItem.index === appletItem.layouter.startLayout.lastVisibleIndex)
                                          || (appletItem.index === appletItem.layouter.mainLayout.lastVisibleIndex)
                                          || (appletItem.index === appletItem.layouter.endLayout.lastVisibleIndex))

    readonly property bool acceptMouseEvents: appletItem.applet
                                              && !appletItem.indexerIsSupported
                                              && !appletItem.originalAppletBehavior
                                              && appletItem.parabolicEffectIsSupported
                                              && !appletItem.isSeparator
                                              && !appletItem.communicator.requires.parabolicEffectLocked

    //! This property is an effort in order to group behaviors into one property. This property is responsible to enable/disable
    //! Applets OnTop MouseArea which is used for ParabolicEffect and ThinTooltips. For Latte panels things
    //! are pretty straight, the original plasma behavior is replicated so parabolic effect and thin tooltips are disabled.
    //! For Latte docks things are a bit more complicated. Applets that can not support parabolic effect inside docks
    //! are presenting their original plasma behavior and also applets that even though can be zoomed user has chose
    //! to lock its parabolic effect.
    readonly property bool originalAppletBehavior: appletItem.rootItem.behaveAsPlasmaPanel
                                                   || !appletItem.parabolicEffectIsSupported
                                                   || (appletItem.rootItem.behaveAsDockWithMask && !appletItem.parabolicEffectIsSupported)
                                                   || (appletItem.rootItem.behaveAsDockWithMask && appletItem.parabolicEffectIsSupported && appletItem.lockZoom)

    readonly property bool isIndicatorDrawn: indicatorBackLayer.level.isDrawn
    readonly property bool isSquare: appletItem.parabolicEffectIsSupported
    readonly property bool screenEdgeMarginSupported: appletItem.communicator.requires.screenEdgeMarginSupported || appletItem.communicator.indexerIsSupported

    property int animationTime: appletItem.animations.speedFactor.normal * (1.2*appletItem.animations.duration.small)
    property int index: -1
    property int maxWidth: appletItem.rootItem.isHorizontal ? appletItem.rootItem.height : appletItem.rootItem.width
    property int maxHeight: appletItem.rootItem.isHorizontal ? appletItem.rootItem.height : appletItem.rootItem.width
    property int internalSplitterId: 0

    property int previousIndex: -1
    property int spacersMaxSize: Math.max(0,Math.ceil(0.55 * appletItem.metrics.iconSize) - appletItem.metrics.totals.lengthEdges)
    property int status: appletItem.applet ? appletItem.applet.plasmoid.status : -1

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
    readonly property bool parabolicEffectMarginsEnabled: appletItem.parabolic.factor.zoom>1 && !appletItem.originalAppletBehavior && !appletItem.communicator.parabolicEffectIsSupported

    property int lengthAppletPadding:{
        if (!appletItem.isIndicatorDrawn) {
            return 0;
        }

        return appletItem.metrics.fraction.lengthAppletPadding === -1 || appletItem.parabolicEffectMarginsEnabled ? appletItem.metrics.padding.length : appletItem.metrics.padding.lengthApplet;
    }

    property int lengthAppletFullMargin: 0
    property int lengthAppletFullMargins: 2 * appletItem.lengthAppletFullMargin

    property int internalWidthMargins: appletItem.rootItem.isVertical ? appletItem.metrics.totals.thicknessEdges : 2 * appletItem.lengthAppletPadding
    property int internalHeightMargins: appletItem.rootItem.isHorizontal ? appletItem.metrics.totals.thicknessEdges : 2 * appletItem.lengthAppletPadding

    readonly property string pluginName: appletItem.isInternalViewSplitter ? "org.kde.latte.splitter" : (appletItem.applet ? appletItem.applet.plasmoid.pluginName : "")

    //! are set by the indicator
    readonly property int iconOffsetX: indicatorBackLayer.level.requested.iconOffsetX
    readonly property int iconOffsetY: indicatorBackLayer.level.requested.iconOffsetY
    readonly property int iconTransformOrigin: indicatorBackLayer.level.requested.iconTransformOrigin
    readonly property real iconOpacity: indicatorBackLayer.level.requested.iconOpacity
    readonly property real iconRotation: indicatorBackLayer.level.requested.iconRotation
    readonly property real iconScale: indicatorBackLayer.level.requested.iconScale

    property real computeWidth: appletItem.rootItem.isVertical ? appletItem.wrapper.width :
                                                                 hiddenSpacerLeft.width+appletItem.wrapper.width+hiddenSpacerRight.width

    property real computeHeight: appletItem.rootItem.isVertical ? hiddenSpacerLeft.height + appletItem.wrapper.height + hiddenSpacerRight.height :
                                                                  appletItem.wrapper.height

    property Item applet: null
    property Item latteStyleApplet: appletItem.applet && ((appletItem.applet.plasmoid.pluginName === "org.kde.latte.spacer") || (appletItem.applet.plasmoid.pluginName === "org.kde.latte.separator")) ?
                                        (appletItem.applet.children[0] ? appletItem.applet.children[0] : null) : null

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
    required property Item rootItem
    property Item shortcuts: null
    property Item thinTooltip: null
    property Item userRequests: null
    required property Item colorizerHost
    required property QtObject layoutManagerHost

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
    Accessible.ignored: !appletItem.applet || appletItem.isSeparator || appletItem.isSpacer || appletItem.isHidden || appletItem.isInternalViewSplitter || appletItem.communicator.indexerIsSupported
    Accessible.role: Accessible.Button
    Accessible.name: appletItem.applet && appletItem.applet.plasmoid ? appletItem.applet.plasmoid.title : ""
    Accessible.focusable: true
    Accessible.focused: appletItem.isKeyboardFocused
    Accessible.onPressAction: appletItem.toggleExpanded()

    //! whether this item has live ParabolicArea slots; the parabolic
    //! ability's row builder maps items without one to DeadStop (a live
    //! scale stack dies at them - EX-02 in docs/tracking/QML_EXTRACTION_PLAN.md)
    readonly property bool hasParabolicMessagesHandler: parabolicAreaLoader.active
    property bool pressed: viewSignalsConnector.pressed


    //// BEGIN :: Animate Applet when a new applet is dragged in the view

    //when the applet moves caused by its resize, don't animate.
    //this is completely heuristic, but looks way less "jumpy"
    property bool movingForResize: false
    property int oldX: appletItem.x
    property int oldY: appletItem.y

    onXChanged: {
        if (appletItem.rootItem.isVertical) {
            return;
        }

        if (appletItem.movingForResize) {
            appletItem.movingForResize = false;
            return;
        } else if (appletItem.rootItem.inDraggingOverAppletOrOutOfContainment) {
            return;
        }

        var draggingAppletInConfigure = appletItem.rootItem.dragOverlay && appletItem.rootItem.dragOverlay.currentApplet;
        var isCurrentAppletInDragging = draggingAppletInConfigure && (appletItem.rootItem.dragOverlay.currentApplet === appletItem);
        var dropApplet = appletItem.rootItem.dragInfo.entered && appletItem.rootItem.dragInfo.isPlasmoid

        if ((isCurrentAppletInDragging || !draggingAppletInConfigure) && !dropApplet) {
            return;
        }

        if (!appletItem.rootItem.isVertical) {
            translation.x = appletItem.oldX - appletItem.x;
            translation.y = 0;
        } else {
            translation.y = appletItem.oldY - appletItem.y;
            translation.x = 0;
        }

        translAnim.running = true

        if (!appletItem.rootItem.isVertical) {
            appletItem.oldX = appletItem.x;
            appletItem.oldY = 0;
        } else {
            appletItem.oldY = appletItem.y;
            appletItem.oldX = 0;
        }
    }

    onYChanged: {
        if (appletItem.rootItem.isHorizontal) {
            return;
        }

        if (appletItem.movingForResize) {
            appletItem.movingForResize = false;
            return;
        } else if (appletItem.rootItem.inDraggingOverAppletOrOutOfContainment) {
            return;
        }

        var draggingAppletInConfigure = appletItem.rootItem.dragOverlay && appletItem.rootItem.dragOverlay.currentApplet;
        var isCurrentAppletInDragging = draggingAppletInConfigure && (appletItem.rootItem.dragOverlay.currentApplet === appletItem);
        var dropApplet = appletItem.rootItem.dragInfo.entered && appletItem.rootItem.dragInfo.isPlasmoid

        if ((isCurrentAppletInDragging || !draggingAppletInConfigure) && !dropApplet) {
            return;
        }
        if (!appletItem.rootItem.isVertical) {
            translation.x = appletItem.oldX - appletItem.x;
            translation.y = 0;
        } else {
            translation.y = appletItem.oldY - appletItem.y;
            translation.x = 0;
        }

        translAnim.running = true;

        if (!appletItem.rootItem.isVertical) {
            appletItem.oldX = appletItem.x;
            appletItem.oldY = 0;
        } else {
            appletItem.oldY = appletItem.y;
            appletItem.oldX = 0;
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
        if (!appletItem.applet || !appletItem.rootItem.latteView) {
            //! legitimate transient states, not defects: the applet is not
            //! attached yet during load, and the view reference drops
            //! during teardown - nothing to toggle in either
            return;
        }

        appletItem.rootItem.latteView.extendedInterface.toggleAppletExpanded(appletItem.applet.plasmoid.id);
    }

    function activateAppletForNeutralAreas(mouse){
        //if the event is at the active indicator or spacers area then try to expand the applet,
        //unfortunately for other applets there is no other way to activate them yet
        //for example the icon-only applets
        var choords = appletItem.mapToItem(appletItem.appletWrapper, mouse.x, mouse.y);

        var wrapperContainsMouse = choords.x>=0 && choords.y>=0 && choords.x<appletItem.appletWrapper.width && choords.y<appletItem.appletWrapper.height;
        var appletItemContainsMouse = mouse.x>=0 && mouse.y>=0 && mouse.x<appletItem.width && mouse.y<appletItem.height;

        //console.log(" APPLET :: " + mouse.x +  " _ " + mouse.y);
        //console.log(" WRAPPER :: " + choords.x + " _ " + choords.y);

        var inThicknessNeutralArea = !wrapperContainsMouse && (appletItem.metrics.margin.screenEdge>0);
        var appletNeutralAreaEnabled = !(inThicknessNeutralArea && appletItem.rootItem.dragActiveWindowEnabled);

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
        appletItem.index = -1;

        var grids = [appletItem.layouts.startLayout, appletItem.layouts.mainLayout, appletItem.layouts.endLayout];
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
                appletItem.index = LatteCore.VisibleIndex.assignedLayoutIndex(counted, selfPosition, grids[g].beginIndex);
                return;
            }
        }
    }

    function sltClearZoom(){
        if (appletItem.communicator.parabolicEffectIsSupported) {
            appletItem.communicator.bridge.parabolic.client.sglClearZoom();
        } else {
            appletItem.restoreAnimation.start();
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
            if (appletItem.wrapper.zoomScale !== 1) {
                return;
            }

            if (appletItem.communicator.indexerIsSupported) {
                appletItem.parabolicEffectIsSupported = true;
                return;
            }

            var maxSize = 1.5 * appletItem.metrics.iconSize;
            var maxForMinimumSize = 1.5 * appletItem.metrics.iconSize;

            if ( appletItem.isSystray
                    || appletItem.isAutoFillApplet
                    || (((appletItem.applet && appletItem.rootItem.isHorizontal && (appletItem.applet.width > maxSize || appletItem.applet.Layout.minimumWidth > maxForMinimumSize))
                         || (appletItem.applet && appletItem.rootItem.isVertical && (appletItem.applet.height > maxSize || appletItem.applet.Layout.minimumHeight > maxForMinimumSize)))
                        && !appletItem.isSpacer) ) {
                appletItem.parabolicEffectIsSupported = false;
            } else {
                appletItem.parabolicEffectIsSupported = true;
            }
        }
    }

    function slotDestroyInternalViewSplitters() {
        if (appletItem.isInternalViewSplitter) {
            appletItem.destroy();
        }
    }

    //! pos in global root positioning
    function containsPos(pos) {
        var relPos = appletItem.rootItem.mapToItem(appletItem,pos.x, pos.y);

        if (relPos.x>=0 && relPos.x<=appletItem.width && relPos.y>=0 && relPos.y<=appletItem.height)
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
            appletItem.layoutManagerHost.setAppletInScheduledDestruction(appletItem.applet.plasmoid.id, destroyed);
        }
    }

    onAppletChanged: {
        if (!appletItem.applet) {
            appletItem.destroy();
        }
    }

    onIndexChanged: {
        if (appletItem.index>-1) {
            appletItem.previousIndex = appletItem.index;
        }
    }

    onIsSystrayChanged: {
        appletItem.updateParabolicEffectIsSupported();
    }

    onIsAutoFillAppletChanged: appletItem.updateParabolicEffectIsSupported();
    onParentChanged: appletItem.checkIndex()

    Component.onCompleted: {
        appletItem.checkIndex();
        appletItem.rootItem.updateIndexes.connect(appletItem.checkIndex);
        appletItem.rootItem.destroyInternalViewSplitters.connect(appletItem.slotDestroyInternalViewSplitters);

        appletItem.parabolic.sglClearZoom.connect(appletItem.sltClearZoom);
    }

    Component.onDestruction: {
        appletItem.animations.needBothAxis.removeEvent(appletItem);

        appletItem.rootItem.updateIndexes.disconnect(appletItem.checkIndex);
        appletItem.rootItem.destroyInternalViewSplitters.disconnect(appletItem.slotDestroyInternalViewSplitters);

        appletItem.parabolic.sglClearZoom.disconnect(appletItem.sltClearZoom);
    }

    //! Bindings

    Binding {
        //! is used to aboid loop binding warnings on startup
        target: appletItem
        property: "lengthAppletFullMargin"
        when: !appletItem.communicator.inStartup
        value: appletItem.lengthAppletPadding + appletItem.metrics.margin.length;
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

            if (visibleIndex === entryIndex && !appletItem.communicator.positionShortcutsAreSupported) {
                appletItem.toggleExpanded();
            }
        }

        function onSglNewInstanceForEntryAtIndex(entryIndex) {
            if (!appletItem.shortcuts.unifiedGlobalShortcuts) {
                return;
            }

            var visibleIndex = appletItem.indexer.visibleIndex(appletItem.index);

            if (visibleIndex === entryIndex && !appletItem.communicator.positionShortcutsAreSupported) {
                appletItem.toggleExpanded();
            }
        }
    }

    Connections {
        id: viewSignalsConnector
        target: appletItem.rootItem.latteView ? appletItem.rootItem.latteView : null
        enabled: !appletItem.indexerIsSupported && !appletItem.isSeparator && !appletItem.isSpacer && !appletItem.isHidden

        property bool pressed: false
        property bool blockWheel: false

        function onMousePressed(pos, button) {
            if (appletItem.containsPos(pos)) {
                viewSignalsConnector.pressed = true;
                var local = appletItem.mapFromItem(appletItem.rootItem, pos.x, pos.y);

                appletItem.mousePressed(local.x, local.y, button);
            }
        }

        function onMouseReleased(pos, button) {
            if (appletItem.containsPos(pos)) {
                viewSignalsConnector.pressed = false;
                var local = appletItem.mapFromItem(appletItem.rootItem, pos.x, pos.y);
                appletItem.mouseReleased(local.x, local.y, button);
            }
        }

        function onWheelScrolled(pos, angleDelta, buttons) {
            if (!appletItem.applet || !appletItem.rootItem.mouseWheelActions || viewSignalsConnector.blockWheel || !appletItem.myView.isShownFully) {
                return;
            }

            viewSignalsConnector.blockWheel = true;
            scrollDelayer.start();

            if (appletItem.containsPos(pos)
                    && (appletItem.rootItem.latteView.extendedInterface.appletIsExpandable(appletItem.applet.plasmoid.id)
                        || (appletItem.rootItem.latteView.extendedInterface.appletIsActivationTogglesExpanded(appletItem.applet.plasmoid.id)))) {
                var angle = angleDelta.y / 8;
                var expanded = appletItem.rootItem.latteView.extendedInterface.appletIsExpanded(appletItem.applet.plasmoid.id);

                if ((angle > 12 && !expanded) /*positive direction*/
                        || (angle < -12 && expanded) /*negative direction*/) {
                    appletItem.rootItem.latteView.extendedInterface.toggleAppletExpanded(appletItem.applet.plasmoid.id);
                }
            }
        }
    }

    Connections {
        target: appletItem.rootItem.latteView ? appletItem.rootItem.latteView.extendedInterface : null
        enabled: !appletItem.indexerIsSupported && !appletItem.isSeparator && !appletItem.isSpacer && !appletItem.isHidden

        function onExpandedAppletStateChanged() {
            if (appletItem.rootItem.latteView.extendedInterface.hasExpandedApplet && appletItem.applet) {
                appletItem.isExpanded = appletItem.rootItem.latteView.extendedInterface.appletIsExpandable(appletItem.applet.plasmoid.id)
                        && appletItem.rootItem.latteView.extendedInterface.appletIsExpanded(appletItem.applet.plasmoid.id);
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
            id: appletShownArea
            width: appletItem.wrapper.width
            height: appletItem.wrapper.height

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
                panelOpacity: appletItem.rootItem.background.currentOpacity
                shadowColor: appletItem.myView.itemShadow.shadowSolidColor

                colorPalette: appletItem.colorizerHost.applyTheme

                //!icon colors
                iconBackgroundColor: appletItem.wrapper.overlayIconLoader.backgroundColor
                iconGlowColor: appletItem.wrapper.overlayIconLoader.glowColor
            }

            //! InConfigureApplets visual paddings
            Loader {
                anchors.fill: _wrapper
                active: appletItem.rootItem.inConfigureAppletsMode && !appletItem.isInternalViewSplitter
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
                    active: appletItem.debug.graphicsEnabled && indicatorBackLayer.active
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

                //! D21 (stock applet palette propagation): push the colorizer's
                //! decided scheme into this applet's OWN Kirigami.Theme color
                //! group. Palette-responsive native content then renders with
                //! the scheme's contrast directly, the way stock Plasma panels
                //! color their applets. The retired layer-FBO ColorOverlay never
                //! captured NativeRendering text, while palette propagation
                //! leaves fixed pixels untouched. inherit flips back on for
                //! applets the colorizer does not apply to, so their own Plasma
                //! palette is left untouched.
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

            //! The Applet Colorizer
            Colorizer.Applet {
                id: appletColorizer
                anchors.fill: parent
                opacity: appletColorizer.mustBeShown ? 1 : 0

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
                    if (appletItem.rootItem.isHorizontal) {
                        return appletItem.metrics.iconSize * appletItem.wrapper.zoomScale
                    } else {
                        return shortcutBadgeContainer.badgeThickness;
                    }
                }

                height: {
                    if (appletItem.rootItem.isHorizontal) {
                        return shortcutBadgeContainer.badgeThickness;
                    } else {
                        return appletItem.metrics.iconSize * appletItem.wrapper.zoomScale
                    }
                }

                readonly property int badgeThickness: {
                    if (Plasmoid.location === PlasmaCore.Types.BottomEdge
                            || Plasmoid.location === PlasmaCore.Types.RightEdge) {
                        var marginthickness = appletItem.metrics.margin.tailThickness * appletItem.wrapper.zoomMarginScale;
                        return (appletItem.metrics.iconSize * appletItem.wrapper.zoomScale) + marginthickness + appletItem.metrics.margin.screenEdge;
                    }

                    var marginthickness = appletItem.metrics.margin.headThickness * appletItem.wrapper.zoomMarginScale;
                    return (appletItem.metrics.iconSize * appletItem.wrapper.zoomScale) + marginthickness;
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
                            anchors.horizontalCenter: appletShownArea.horizontalCenter; anchors.verticalCenter: undefined;
                            anchors.right: undefined; anchors.left: undefined; anchors.top: undefined; anchors.bottom: appletShownArea.bottom;
                        }
                    },
                    State{
                        name: "vertical"
                        when: Plasmoid.formFactor === PlasmaCore.Types.Vertical

                        AnchorChanges{
                            target: shortcutBadgeContainer;
                            anchors.horizontalCenter: undefined; anchors.verticalCenter: appletShownArea.verticalCenter;
                            anchors.right: appletShownArea.right; anchors.left: undefined; anchors.top: undefined; anchors.bottom: undefined;
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
        id: appletBusyIndicator
        z: 1000
        visible: appletItem.applet && appletItem.applet.plasmoid.busy
        running: appletBusyIndicator.visible
        anchors.centerIn: parent
        width: Math.min(appletItem.width, appletItem.height)
        height: appletBusyIndicator.width
    }

    Loader {
        id: parabolicAreaLoader
        width: appletItem.rootItem.isHorizontal ? appletItem.width : appletItem.metrics.mask.thickness.zoomedForItems
        height: appletItem.rootItem.isHorizontal ? appletItem.metrics.mask.thickness.zoomedForItems : appletItem.height
        //! must be enabled even for applets that are hidden in order to forward
        //! parabolic effect messages properly to surrounding plasma applets
        active: parabolicAreaLoader.isParabolicEnabled || parabolicAreaLoader.isThinTooltipEnabled || parabolicAreaLoader.hasParabolicMessagesEnabled

        //! in hidden state applets must pass on parabolic messages to neighbours
        readonly property bool isParabolicEnabled: appletItem.parabolic.isEnabled && !appletItem.lockZoom
        readonly property bool isThinTooltipEnabled: appletItem.thinTooltip.isEnabled && !appletItem.isHidden
        readonly property bool hasParabolicMessagesEnabled: appletItem.parabolic.isEnabled && (!appletItem.lockZoom || appletItem.isSeparator || appletItem.isMarginsAreaSeparator || appletItem.isHidden)

        sourceComponent: ParabolicArea{}

        states:[
            State{
                name: "top"
                when: Plasmoid.location === PlasmaCore.Types.TopEdge

                AnchorChanges{
                    target: parabolicAreaLoader
                    anchors.horizontalCenter: appletItem.horizontalCenter; anchors.verticalCenter: undefined;
                    anchors.right: undefined; anchors.left: undefined; anchors.top: appletItem.top; anchors.bottom: undefined;
                }
            },
            State{
                name: "left"
                when: Plasmoid.location === PlasmaCore.Types.LeftEdge

                AnchorChanges{
                    target: parabolicAreaLoader
                    anchors.horizontalCenter: undefined; anchors.verticalCenter: appletItem.verticalCenter;
                    anchors.right: undefined; anchors.left: appletItem.left; anchors.top: undefined; anchors.bottom: undefined;
                }
            },
            State{
                name: "right"
                when: Plasmoid.location === PlasmaCore.Types.RightEdge

                AnchorChanges{
                    target: parabolicAreaLoader
                    anchors.horizontalCenter: undefined; anchors.verticalCenter: appletItem.verticalCenter;
                    anchors.right: appletItem.right; anchors.left: undefined; anchors.top: undefined; anchors.bottom: undefined;
                }
            },
            State{
                name: "bottom"
                when: Plasmoid.location === PlasmaCore.Types.BottomEdge

                AnchorChanges{
                    target: parabolicAreaLoader
                    anchors.horizontalCenter: appletItem.horizontalCenter; anchors.verticalCenter: undefined;
                    anchors.right: undefined; anchors.left: undefined; anchors.top: undefined; anchors.bottom: appletItem.bottom;
                }
            }
        ]
    }

    //! Debug Elements
    Loader{
        anchors.bottom: appletItem.bottom
        anchors.left: appletItem.left

        active: appletItem.debug.layouterEnabled
        sourceComponent: Debugger.Tag{
            id: debuggerTag
            label.text: (appletItem.rootItem.isHorizontal ? appletItem.width : appletItem.height) + debuggerTag.labeltext
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
                anchors{ top:undefined; bottom:undefined; left:appletItem.left; right:undefined;}
            }
        },
        State {
            name: "right"
            when: (Plasmoid.location === PlasmaCore.Types.RightEdge)

            AnchorChanges {
                target: appletFlow
                anchors{ top:undefined; bottom:undefined; left:undefined; right:appletItem.right;}
            }
        },
        State {
            name: "bottom"
            when: (Plasmoid.location === PlasmaCore.Types.BottomEdge)

            AnchorChanges {
                target: appletFlow
                anchors{ top:undefined; bottom:appletItem.bottom; left:undefined; right:undefined;}
            }
        },
        State {
            name: "top"
            when: (Plasmoid.location === PlasmaCore.Types.TopEdge)

            AnchorChanges {
                target: appletFlow
                anchors{ top:appletItem.top; bottom:undefined; left:undefined; right:undefined;}
            }
        }
    ]
    //END states

    //BEGIN animations
    ///////Restore Zoom Animation/////
    ParallelAnimation{
        id: _restoreAnimation

        PropertyAnimation {
            target: appletItem.wrapper
            property: "zoomScale"
            to: 1
            duration: 3 * appletItem.animationTime
            easing.type: Easing.InCubic
        }
    }
}
