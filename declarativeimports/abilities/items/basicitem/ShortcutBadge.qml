/*
    SPDX-FileCopyrightText: 2019 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.7

import org.kde.plasma.plasmoid 2.0
import org.kde.kirigami 2.20 as Kirigami
import org.kde.latte.components 1.0 as LatteComponents

Loader{
    //! the id was misspelled "shorcutBadge" (missing t) since Qt5, and the two
    //! references
    //! that spelled it CORRECTLY (the Connections handler and BadgeText's
    //! borderColor) therefore threw ReferenceError at runtime: badge indexes
    //! never updated on reorder and the border color stayed at BadgeText's
    //! default instead of the theme-contrasted pick (both reproduced with an
    //! offscreen probe, docs/agent-logs/EX-19.md). Fixing the id at the
    //! source repairs both; the qmllint gate pins the file from here on.
    id: shortcutBadge
    active: abilityItem.abilities.shortcuts.showPositionShortcutBadges && !abilityItem.isSeparator && !abilityItem.isHidden && abilityItem.abilities.shortcuts.isEnabled
    asynchronous: true
    visible: badgeString !== ""

    property int fixedIndex:-1

    readonly property int maxFixedIndex: abilityItem.abilities.shortcuts.badges.length
    readonly property real textColorBrightness: colorBrightness(Kirigami.Theme.textColor)
    readonly property string badgeString: (shortcutBadge.fixedIndex>=1 && shortcutBadge.fixedIndex<=maxFixedIndex) ?
                                              abilityItem.abilities.shortcuts.badges[shortcutBadge.fixedIndex-1] : ""
    readonly property color lightTextColor: textColorBrightness > 127.5 ? Kirigami.Theme.textColor : Kirigami.Theme.backgroundColor

    onActiveChanged: updateShortcutIndex();

    Connections {
        target: abilityItem
        function onItemIndexChanged() {
            shortcutBadge.updateShortcutIndex();
        }
    }

    function updateShortcutIndex() {
        if (shortcutBadge.active && abilityItem.abilities.shortcuts.showPositionShortcutBadges) {
            fixedIndex = abilityItem.abilities.shortcuts.shortcutIndex(abilityItem.itemIndex);
        } else {
            fixedIndex = -1;
        }
    }

    function colorBrightness(color) {
        return colorBrightnessFromRGB(color.r * 255, color.g * 255, color.b * 255);
    }

    // formula for brightness according to:
    // https://www.w3.org/TR/AERT/#color-contrast
    function colorBrightnessFromRGB(r, g, b) {
        return (r * 299 + g * 587 + b * 114) / 1000
    }

    sourceComponent: Item{
        LatteComponents.BadgeText {
            id: taskNumber

            //! layer EFFECT, not a sibling MultiEffect sampling this badge:
            //! a sibling redraws the content over the still visible original
            //! and MultiEffect's padded placement is not pixel-exact, which
            //! ghosted a shifted copy (same defect as the containment badge,
            //! fixed the same way in c7c46226 - this is its twin that stayed
            //! on the sibling arrangement)
            layer.enabled: abilityItem.abilities.myView.itemShadow.isEnabled
                           && abilityItem.abilities.environment.isGraphicsSystemAccelerated
            layer.effect: LatteComponents.ShadowedItem{
                shadowColor: abilityItem.abilities.myView.itemShadow.shadowColor
                shadowSizePx: abilityItem.abilities.myView.itemShadow.size/2
            }
            // when iconSize < 48, height is always = 24, height / iconSize > 50%
            // we prefer center aligned badges to top-left aligned ones
            property bool centerInParent: abilityItem.abilities.metrics.iconSize < 48

            anchors.left: centerInParent? undefined : parent.left
            anchors.top: centerInParent? undefined : parent.top
            anchors.centerIn: centerInParent? parent : undefined
            minimumWidth: 0.4 * (abilityItem.parabolicItem.zoom * abilityItem.abilities.metrics.iconSize)
            height: Math.max(24, 0.4 * (abilityItem.parabolicItem.zoom * abilityItem.abilities.metrics.iconSize))

            style3d: abilityItem.abilities.myView.badgesIn3DStyle
            textValue: shortcutBadge.badgeString
            borderColor: shortcutBadge.lightTextColor

            showNumber: false
            showText: true

            proportion: 0
            radiusPerCentage: 100
        }
    }
}
