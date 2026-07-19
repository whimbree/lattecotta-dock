/*
    SPDX-FileCopyrightText: 2018 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef COMMONTOOLS_H
#define COMMONTOOLS_H

// Qt
#include <QRect>
#include <QString>

class QQuickWindow;
class QWindow;

namespace Latte {

// the color brightness/luminance helpers moved to the ColorTools pure core
// (declarativeimports/core/units/colortools.h, EX-19 in
// docs/tracking/QML_EXTRACTION_PLAN.md) - one authority instead of nine copies

QString rectToString(const QRect &rect);
QRect stringToRect(const QString &str);

//! a wayland compositor is the display server, so compositing is
//! unconditionally active on the only platform the dock runs on
bool compositingActive();

//! returns the standard path found that contains the subPath
//! local paths have higher priority by default
QString standardPath(QString subPath, bool localFirst = true);

QString configPath();

//! The window of the QQuickItem that declared @p window in QML. A window
//! declared inside an item (the shape of every applet-created dialog) is a
//! resource child: its QObject parent chain leads to the declaring item,
//! whose window() is the visual host - on Qt 6 that chain is the ONLY route,
//! because no transientParent is assigned any more (contract tests:
//! appletwindowparentingtest.cpp, tst_transientwindowcontracts.qml).
//! Returns null for windows not declared inside an item, and never returns
//! @p window itself. Beware QWindow::parent(): it shadows QObject::parent()
//! with the (null here) window-parent overload - the first draft of the
//! corona screen forwarding walked it and matched nothing.
QQuickWindow *visualHostWindowOf(const QWindow *window);
}

#endif
