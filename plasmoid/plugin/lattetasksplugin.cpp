/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "lattetasksplugin.h"

// local
#include "backend.h"
#include "launcherlistbridge.h"
#include "previewswitchbridge.h"
#include "scrolloverflowmath.h"
#include "smartlauncheritem.h"
#include "tooltiptextcomposer.h"
#include "types.h"

// Qt
#include <QtQml>


void LatteTasksPlugin::registerTypes(const char *uri)
{
    Q_ASSERT(QLatin1String(uri) == QLatin1String("org.kde.latte.private.tasks"));
    // Qt6 warns "Invalid QML element name 'Types'" (value-type names want lowercase).
    // Kept uppercase deliberately: the public QML API is LatteTasks.Types, used across
    // the QML tree; renaming for a benign, non-fatal warning is not worth the churn.
    qmlRegisterUncreatableType<Latte::Tasks::Types>(uri, 0, 1, "Types", QStringLiteral("Latte Tasks Types uncreatable"));

    qmlRegisterType<SmartLauncher::Item>(uri, 0, 1, "SmartLauncherItem");
    qmlRegisterType<Backend>(uri, 0, 1, "Backend");
    qmlRegisterType<Latte::Tasks::PreviewSwitchBridge>(uri, 0, 1, "PreviewSwitchBridge");

    //! stateless composer (EX-17): a singleton so the tooltip shells reach
    //! it module-qualified instead of through the context chain
    qmlRegisterSingletonType<Latte::Tasks::TooltipTextComposer>(
        uri, 0, 1, "TooltipTextComposer",
        [](QQmlEngine *, QJSEngine *) -> QObject * {
            return new Latte::Tasks::TooltipTextComposer;
        });

    //! stateless scroll math (EX-21): ScrollableList.qml reaches the
    //! ScrollMath core through this singleton
    qmlRegisterSingletonType<Latte::Tasks::ScrollOverflowMath>(
        uri, 0, 1, "ScrollOverflowMath",
        [](QQmlEngine *, QJSEngine *) -> QObject * {
            return new Latte::Tasks::ScrollOverflowMath;
        });
    //! launcher list algebra (EX-11): the stateless ops singleton plus the
    //! per-applet registries TasksExtendedManager instantiates
    qmlRegisterSingletonType<Latte::Tasks::LauncherListOps>(
        uri, 0, 1, "LauncherListOps",
        [](QQmlEngine *, QJSEngine *) -> QObject * {
            return new Latte::Tasks::LauncherListOps;
        });
    qmlRegisterType<Latte::Tasks::TasksExtendedRegistries>(uri, 0, 1, "TasksExtendedRegistries");
}

