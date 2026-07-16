/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

import QtQuick 2.0

Item {
    property var separators: []
    property var hidden: []

    //! the RowEntry row snapshot both indexer twins gather from their live
    //! children: [{index, isSeparator, isHidden, isMarginsSeparator,
    //! subItemCount}]. All index math and neighbor predicates over it live
    //! in org.kde.latte.core VisibleIndex (EX-06 in
    //! docs/QML_EXTRACTION_PLAN.md).
    property var rowEntries: []
}
