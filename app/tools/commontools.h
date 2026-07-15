/*
    SPDX-FileCopyrightText: 2018 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef COMMONTOOLS_H
#define COMMONTOOLS_H

// Qt
#include <QColor>
#include <QRect>
#include <QString>

class QQuickWindow;
class QWindow;

namespace Latte {

float colorBrightness(QColor color);
float colorBrightness(QRgb rgb);
float colorBrightness(float r, float g, float b);

float colorLumina(QColor color);
float colorLumina(QRgb rgb);
float colorLumina(float r, float g, float b);

QString rectToString(const QRect &rect);
QRect stringToRect(const QString &str);

//! KF6 moved the X11 compositing query to KX11Extras; a wayland compositor is
//! the display server, so compositing is unconditionally active there
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
