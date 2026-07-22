/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "lattecontainmentplugin.h"

// local
#include "autosizestepper.h"
#include "filldistributortools.h"
#include "colorizerdecider.h"
#include "backgroundstateresolver.h"
#include "layoutmanager.h"
#include "maskgeometrybridge.h"
#include "placementnormalizer.h"
#include "types.h"

// Qt
#include <QtQml>

void LatteContainmentPlugin::registerTypes(const char *uri)
{
    Q_ASSERT(uri == QLatin1String("org.kde.latte.private.containment"));
    qmlRegisterUncreatableType<Latte::Containment::Types>(uri, 0, 1, "Types", "Latte Containment Types uncreatable");
    qmlRegisterType<Latte::Containment::LayoutManager>(uri, 0, 1, "LayoutManager");
    qmlRegisterType<Latte::Containment::AutoSizeStepper>(uri, 0, 1, "AutoSizeStepper");
    qmlRegisterSingletonType<Latte::FillDistributorTools>(uri, 0, 1, "FillDistributor", &Latte::filldistributortools_qobject_singletontype_provider);
    qmlRegisterType<Latte::Containment::ColorizerDecider>(uri, 0, 1, "ColorizerDecider");
    qmlRegisterType<Latte::Containment::BackgroundStateResolver>(uri, 0, 1, "BackgroundStateResolver");
    qmlRegisterType<Latte::Containment::MaskGeometryBridge>(uri, 0, 1, "MaskGeometryBridge");
    qmlRegisterType<Latte::Containment::PlacementNormalizer>(uri, 0, 1, "PlacementNormalizer");
}
