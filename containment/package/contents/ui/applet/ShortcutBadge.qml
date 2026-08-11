/*
    SPDX-FileCopyrightText: 2019 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.1

import org.kde.latte.components 1.0 as LatteComponents

Loader{
    id: appletNumberLoader

    active: appletItem.canShowAppletNumberBadge &&
            (appletItem.shortcuts.showPositionShortcutBadges
             || appletItem.shortcuts.showAppletShortcutBadges
             || (appletItem.shortcuts.showMetaBadge && applet.plasmoid.id===appletItem.shortcuts.applicationLauncherId))

    asynchronous: true
    visible: badgeString!==""

    property int fixedIndex: -1
    property string badgeString: ""

    onActiveChanged: {
        if (active && appletItem.shortcuts.showPositionShortcutBadges && appletItem.shortcuts.unifiedGlobalShortcuts) {
            fixedIndex = appletItem.indexer.visibleIndex(index);
        } else {
            fixedIndex = -1;
        }
    }

    Component.onCompleted: {
        if (active && appletItem.shortcuts.showPositionShortcutBadges && appletItem.shortcuts.unifiedGlobalShortcuts) {
            fixedIndex = appletItem.indexer.visibleIndex(index);
        } else {
            fixedIndex = -1;
        }
    }

    Binding{
        target: appletNumberLoader
        property:"badgeString"
        when: appletItem.shortcuts.showMetaBadge || appletItem.shortcuts.showAppletShortcutBadges
        value: {
            if (appletItem.shortcuts.showMetaBadge && applet && applet.plasmoid.id === appletItem.shortcuts.applicationLauncherId) {
                return '\u2318';
            }

            if (appletItem.shortcuts.showAppletShortcutBadges) {
                var plasmaShortcut = applet ? shortcutsEngine.appletShortcutBadge(applet.plasmoid.id) : "";

                if (plasmaShortcut !== "") {
                    return plasmaShortcut;
                }
            }

             if (appletNumberLoader.fixedIndex>=1 && appletNumberLoader.fixedIndex<20) {
                return appletItem.shortcuts.badges[appletNumberLoader.fixedIndex-1];
            } else {
                return "";
            }
        }
        restoreMode: Binding.RestoreNone
    }

    sourceComponent: Item{
        LatteComponents.BadgeText {
            id: appletNumber

            //! layer EFFECT, not a sibling MultiEffect sampling this badge:
            //! a sibling redraws the content over the still visible original
            //! and MultiEffect's padded placement is not pixel-exact, which
            //! ghosted a shifted copy (same defect as the ItemWrapper applet
            //! shadow, caught live on the clock)
            layer.enabled: appletItem.myView.itemShadow.isEnabled
                           && appletItem.environment.isGraphicsSystemAccelerated
            layer.effect: LatteComponents.ShadowedItem{
                shadowColor: appletItem.myView.itemShadow.shadowColor
                shadowSizePx: appletItem.myView.itemShadow.size/2
            }

            // when iconSize < 48, height is always = 24, height / iconSize > 50%
            // we prefer center aligned badges to top-left aligned ones
            property bool centerInParent: appletItem.metrics.iconSize < 48

            anchors.left: centerInParent? undefined : parent.left
            anchors.top: centerInParent? undefined : parent.top
            anchors.centerIn: centerInParent? parent : undefined
            minimumWidth: 0.4 * (wrapper.zoomScale * appletItem.metrics.iconSize)
            height: Math.max(24, 0.4 * (wrapper.zoomScale * appletItem.metrics.iconSize))

            borderColor: colorizerManager.originalLightTextColor
            proportion: 0
            radiusPerCentage: 100
            showNumber: false
            showText: true
            textValue: appletNumberLoader.badgeString

            style3d: appletItem.myView.badgesIn3DStyle
        }
    }
}
