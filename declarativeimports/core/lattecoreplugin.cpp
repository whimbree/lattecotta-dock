/*
    SPDX-FileCopyrightText: 2016 Smith AR <audoban@openmailbox.org>
    SPDX-FileCopyrightText: 2016 Michail Vourlakos <mvourlakos@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "lattecoreplugin.h"

// local
#include "badgemathtools.h"
#include "dialog.h"
#include "environment.h"
#include "iconitem.h"
#include "parabolicmathtools.h"
#include "parabolicroutertools.h"
#include "quickwindowsystem.h"
#include "tools.h"
#include "visibleindextools.h"
#include "windowcyclertools.h"

#include <types.h>

// Qt
#include <QtQml>


void LatteCorePlugin::registerTypes(const char *uri)
{
    Q_ASSERT(uri == QLatin1String("org.kde.latte.core"));
    qmlRegisterUncreatableType<Latte::Types>(uri, 0, 2, "Types", "Latte Types uncreatable");
    qmlRegisterType<Latte::IconItem>(uri, 0, 2, "IconItem");
    qmlRegisterType<Latte::Quick::Dialog>(uri, 0, 2, "Dialog");
    qmlRegisterSingletonType<Latte::Environment>(uri, 0, 2, "Environment", &Latte::environment_qobject_singletontype_provider);
    qmlRegisterSingletonType<Latte::Tools>(uri, 0, 2, "Tools", &Latte::tools_qobject_singletontype_provider);
    qmlRegisterSingletonType<Latte::ParabolicMathTools>(uri, 0, 2, "ParabolicMath", &Latte::parabolicmathtools_qobject_singletontype_provider);
    qmlRegisterSingletonType<Latte::ParabolicRouterTools>(uri, 0, 2, "ParabolicRouter", &Latte::parabolicroutertools_qobject_singletontype_provider);
    qmlRegisterSingletonType<Latte::VisibleIndexTools>(uri, 0, 2, "VisibleIndex", &Latte::visibleindextools_qobject_singletontype_provider);
    qmlRegisterSingletonType<Latte::QuickWindowSystem>(uri, 0, 2, "WindowSystem", &Latte::windowsystem_qobject_singletontype_provider);
    qmlRegisterSingletonType<Latte::WindowCyclerTools>(uri, 0, 2, "WindowCycler", &Latte::windowcyclertools_qobject_singletontype_provider);

    //! stateless badge math (EX-20): lives in LatteCore because the shared
    //! components module's BadgeText.qml consumes it too
    qmlRegisterSingletonType<Latte::BadgeMathTools>(
        uri, 0, 2, "BadgeMath",
        [](QQmlEngine *, QJSEngine *) -> QObject * {
            return new Latte::BadgeMathTools;
        });
}
