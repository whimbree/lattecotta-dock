/*
    SPDX-FileCopyrightText: 2019 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.1

import org.kde.plasma.plasmoid 2.0
import org.kde.kirigami 2.20 as Kirigami

import org.kde.latte.core 0.2 as LatteCore

Item{
    id: addItem

    property real backgroundOpacity: 1

    Rectangle{
        width: Math.min(parent.width, parent.height)
        height: width
        anchors.centerIn: parent

        radius: 0.05 * Math.max(width,height)

        color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g, Kirigami.Theme.backgroundColor.b, addItem.backgroundOpacity)
        border.width: 1
        border.color: outlineColor

        property int crossSize: Math.min(0.4*parent.width, 0.4 * parent.height)

        readonly property color outlineColorBase: Kirigami.Theme.backgroundColor
        //! D14: outlineColorBase is Kirigami.Theme.backgroundColor, invalid on
        //! this binding's first evaluation at creation before the palette
        //! resolves; guard so the invalid interim is not handed to the C++
        //! boundary (tools.cpp). Valid branch unchanged.
        readonly property real outlineColorBaseBrightness: outlineColorBase.valid ? LatteCore.Tools.colorBrightness(outlineColorBase) : 0
        readonly property color outlineColor: {
            if (outlineColorBaseBrightness > 127.5) {
                return Qt.darker(outlineColorBase, 1.5);
            } else {
                return Qt.lighter(outlineColorBase, 2.2);
            }
        }

        Rectangle{width: parent.crossSize; height: 4; radius:2; anchors.centerIn: parent; color: Kirigami.Theme.highlightColor}
        Rectangle{width: 4; height: parent.crossSize; radius:2; anchors.centerIn: parent; color: Kirigami.Theme.highlightColor}
    }
}
