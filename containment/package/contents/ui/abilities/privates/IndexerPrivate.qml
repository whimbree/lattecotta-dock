/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.7
import org.kde.plasma.plasmoid 2.0

import org.kde.latte.core 0.2 as LatteCore

import org.kde.latte.abilities.definition 0.1 as AbilityDefinition

AbilityDefinition.Indexer {
    id: indxr
    property Item layouts: null
    property bool updateIsBlocked: false

    property var clients: []
    property var clientsBridges: []

    property var marginsAreaSeparators: []

    property int clientsTrackingWindowsCount: 0

    Binding{
        target: indxr
        property: "separators"
        when: !indxr.updateIsBlocked
        restoreMode: Binding.RestoreNone
        value: {
            var seps = [];

            var sLayout = indxr.layouts.startLayout;
            for (var i=0; i<sLayout.children.length; ++i){
                var appletItem = sLayout.children[i];
                if (appletItem && appletItem.isSeparator && appletItem.index>=0) {
                    seps.push(appletItem.index);
                }
            }

            var mLayout = indxr.layouts.mainLayout;
            for (var i=0; i<mLayout.children.length; ++i){
                var appletItem = mLayout.children[i];
                if (appletItem && appletItem.isSeparator && appletItem.index>=0) {
                    seps.push(appletItem.index);
                }
            }

            var eLayout = indxr.layouts.endLayout;
            for (var i=0; i<eLayout.children.length; ++i){
                var appletItem = eLayout.children[i];
                if (appletItem && appletItem.isSeparator && appletItem.index>=0) {
                    seps.push(appletItem.index);
                }
            }

            return seps;
        }
    }

    Binding {
        target: indxr
        property: "hidden"
        when: !indxr.updateIsBlocked
        restoreMode: Binding.RestoreNone
        value: {
            var hdn = [];

            var sLayout = indxr.layouts.startLayout;
            for (var i=0; i<sLayout.children.length; ++i){
                var appletItem = sLayout.children[i];
                if (appletItem && appletItem.isHidden && appletItem.index>=0) {
                    hdn.push(appletItem.index);
                }
            }

            var mLayout = indxr.layouts.mainLayout;
            for (var i=0; i<mLayout.children.length; ++i){
                var appletItem = mLayout.children[i];
                if (appletItem && appletItem.isHidden && appletItem.index>=0) {
                    hdn.push(appletItem.index);
                }
            }

            var eLayout = indxr.layouts.endLayout;
            for (var i=0; i<eLayout.children.length; ++i){
                var appletItem = eLayout.children[i];
                if (appletItem && appletItem.isHidden && appletItem.index>=0) {
                    hdn.push(appletItem.index);
                }
            }

            return hdn;
        }
    }

    Binding{
        target: indxr
        property: "marginsAreaSeparators"
        when: !indxr.updateIsBlocked
        restoreMode: Binding.RestoreNone
        value: {
            var seps = [];
            var grid;

            for (var l=0; l<=2; ++l) {
                if (l===0) {
                    grid = indxr.layouts.startLayout;
                } else if (l===1) {
                    grid = indxr.layouts.mainLayout;
                } else if (l===2) {
                    grid = indxr.layouts.endLayout;
                }

                for (var i=0; i<grid.children.length; ++i){
                    var appletItem = grid.children[i];
                    if (appletItem
                            && appletItem.isMarginsAreaSeparator
                            && appletItem.index>=0) {
                        seps.push(appletItem.index);
                    }
                }
            }

            return seps;
        }
    }

    //! the RowEntry snapshot feeding org.kde.latte.core VisibleIndex
    //! (EX-06): all index math and neighbor predicates moved to the C++
    //! core; this Binding only gathers what the live children say, the
    //! same collector idiom as separators/hidden above. subItemCount asks
    //! a client's own indexer for its visible items count through the
    //! same bridge read the deleted visibleItemsBeforeCount used,
    //! null-guarded the way the clientsBridges collector always was (the
    //! deleted function dereferenced it bare and would have broken the
    //! whole binding on an unresolved client).
    Binding {
        target: indxr
        property: "rowEntries"
        when: !indxr.updateIsBlocked
        restoreMode: Binding.RestoreNone
        value: {
            var row = [];
            var grid;

            for (var l=0; l<=2; ++l) {
                if (l===0) {
                    grid = indxr.layouts.startLayout;
                } else if (l===1) {
                    grid = indxr.layouts.mainLayout;
                } else {
                    grid = indxr.layouts.endLayout;
                }

                for (var i=0; i<grid.children.length; ++i){
                    var appletItem = grid.children[i];
                    if (!appletItem || !(appletItem.index>=0)) {
                        continue;
                    }

                    var subItems = 1;
                    if (appletItem.communicator && appletItem.communicator.indexerIsSupported) {
                        var bridgeIndexer = appletItem.communicator.bridge ? appletItem.communicator.bridge.indexer : null;
                        subItems = (bridgeIndexer && bridgeIndexer.client) ? bridgeIndexer.client.visibleItemsCount : 1;
                    }

                    row.push({index: appletItem.index,
                              isSeparator: appletItem.isSeparator === true,
                              isHidden: appletItem.isHidden === true,
                              isMarginsSeparator: appletItem.isMarginsAreaSeparator === true,
                              subItemCount: subItems});
                }
            }

            return row;
        }
    }

    Binding {
        target: indxr
        property: "clients"
        when: !indxr.updateIsBlocked
        restoreMode: Binding.RestoreNone
        value: {
            var clns = [];

            var sLayout = indxr.layouts.startLayout;
            for (var i=0; i<sLayout.children.length; ++i){
                var appletItem = sLayout.children[i];
                if (appletItem
                        && appletItem.index>=0
                        && appletItem.communicator
                        && appletItem.communicator.indexerIsSupported) {
                    clns.push(appletItem.index);
                }
            }

            var mLayout = indxr.layouts.mainLayout;
            for (var i=0; i<mLayout.children.length; ++i){
                var appletItem = mLayout.children[i];
                if (appletItem
                        && appletItem.index>=0
                        && appletItem.communicator
                        && appletItem.communicator.indexerIsSupported) {
                    clns.push(appletItem.index);
                }
            }

            var eLayout = indxr.layouts.endLayout;
            for (var i=0; i<eLayout.children.length; ++i){
                var appletItem = eLayout.children[i];
                if (appletItem
                        && appletItem.index>=0
                        && appletItem.communicator
                        && appletItem.communicator.indexerIsSupported) {
                    clns.push(appletItem.index);
                }
            }

            return clns;
        }
    }

    Binding {
        target: indxr
        property: "clientsBridges"
        when: !indxr.updateIsBlocked
        restoreMode: Binding.RestoreNone
        value: {
            var bdgs = [];

            var sLayout = indxr.layouts.startLayout;
            for (var i=0; i<sLayout.children.length; ++i){
                var appletItem = sLayout.children[i];
                if (appletItem
                        && appletItem.index>=0
                        && appletItem.communicator
                        && appletItem.communicator.indexerIsSupported
                        && appletItem.communicator.bridge
                        && appletItem.communicator.bridge.indexer) {
                    bdgs.push(appletItem.communicator.bridge.indexer);
                }
            }

            var mLayout = indxr.layouts.mainLayout;
            for (var i=0; i<mLayout.children.length; ++i){
                var appletItem = mLayout.children[i];
                if (appletItem
                        && appletItem.index>=0
                        && appletItem.communicator
                        && appletItem.communicator.indexerIsSupported
                        && appletItem.communicator.bridge
                        && appletItem.communicator.bridge.indexer) {
                    bdgs.push(appletItem.communicator.bridge.indexer);
                }
            }

            var eLayout = indxr.layouts.endLayout;
            for (var i=0; i<eLayout.children.length; ++i){
                var appletItem = eLayout.children[i];
                if (appletItem
                        && appletItem.index>=0
                        && appletItem.communicator
                        && appletItem.communicator.indexerIsSupported
                        && appletItem.communicator.bridge
                        && appletItem.communicator.bridge.indexer) {
                    bdgs.push(appletItem.communicator.bridge.indexer);
                }
            }

            return bdgs;
        }
    }

    Binding{
        target: indxr
        property: "clientsTrackingWindowsCount"
        when: !(root.appletIsDragged || indxr.updateIsBlocked)
        restoreMode: Binding.RestoreNone
        value: {
            var cnts = 0;
            var grid;

            for (var l=0; l<=2; ++l) {
                if (l===0) {
                    grid = indxr.layouts.startLayout;
                } else if (l===1) {
                    grid = indxr.layouts.mainLayout;
                } else if (l===2) {
                    grid = indxr.layouts.endLayout;
                }

                for (var i=0; i<grid.children.length; ++i){
                    var appletItem = grid.children[i];
                    if (appletItem
                            && appletItem.communicator
                            && appletItem.communicator.requires.windowsTrackingEnabled) {
                        cnts = cnts + 1;
                    }
                }
            }

            return cnts;
        }
    }

    //! visibleIndex(-1) answers -1 now: the C++ core refuses to invent a
    //! visible index for an entry the row does not have. Qt5 answered
    //! 1 + countBefore for any unknown index, which let an unindexed
    //! ParabolicEdgeSpacer (index -1) claim visible index 1 through
    //! visibleIndexBelongsAtApplet.
    function visibleIndex(actualIndex: int) : int {
        return LatteCore.VisibleIndex.visibleIndexOf(indxr.rowEntries, actualIndex);
    }

    function visibleIndexBelongsAtApplet(applet: Item, itemVisibleIndex: int) : bool {
        if (itemVisibleIndex<0 || !applet) {
            return false;
        }

        return LatteCore.VisibleIndex.belongsAtVisibleIndex(indxr.rowEntries, applet.index, itemVisibleIndex);
    }
}
