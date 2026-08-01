/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef APPLIEDPLACEMENTSNAPSHOT_H
#define APPLIEDPLACEMENTSNAPSHOT_H

#include <coretypes.h>

#include <Plasma/Plasma>

#include <QPointer>
#include <QRect>
#include <QScreen>
#include <QString>

namespace Latte {
namespace ViewPart {

//! Durable identity and geometry from one accepted output publication.
struct AppliedOutputIdentity
{
    QString connector;
    int screenId{-1};
    QRect geometry;
};

//! One immutable accepted placement. Value state survives output destruction;
//! only the process-owned compositor handle clears with its QScreen.
struct AppliedPlacementSnapshot
{
    AppliedOutputIdentity output;
    Plasma::Types::Location edge{Plasma::Types::Floating};
    Plasma::Types::FormFactor orientation{Plasma::Types::Planar};
    Latte::Types::Alignment alignment{Latte::Types::NoneAlignment};
    bool followsPrimary{false};
    QPointer<QScreen> liveScreen;
};

}
}

#endif
