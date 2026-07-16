/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Honest-mock client layout child: the properties the shipped client
//! abilities Indexer.qml collectors read on a task item.
import QtQuick 2.7

Item {
    property int itemIndex: -1
    property bool isSeparator: false
    property bool isHidden: false
    property bool isSeparatorHidden: false
}
