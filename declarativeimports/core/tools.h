/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef LATTECORETOOLS_H
#define LATTECORETOOLS_H

// Qt
#include <QObject>
#include <QColor>
#include <QQmlEngine>
#include <QJSEngine>


namespace Latte{

//! Thin QML shell over the ColorTools pure core (EX-19 in
//! docs/tracking/QML_EXTRACTION_PLAN.md, units/colortools.h). This singleton is
//! public API for third-party indicator packages (they import
//! org.kde.latte.core), so its surface changes carefully and bad input
//! from QML is refused loudly instead of asserted away.
class Tools final: public QObject
{
    Q_OBJECT

public:
    explicit Tools(QObject *parent = nullptr);

public Q_SLOTS:
    Q_INVOKABLE double colorBrightness(QColor color);
    Q_INVOKABLE double colorLumina(QColor color);
    Q_INVOKABLE bool isLight(QColor color, double threshold = 127.5);
};

static QObject *tools_qobject_singletontype_provider(QQmlEngine *engine, QJSEngine *scriptEngine)
{
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)

// NOTE: QML engine is the owner of this resource
    return new Tools;
}

}

#endif
