/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef APPLIEDOUTPUTSNAPSHOT_H
#define APPLIEDOUTPUTSNAPSHOT_H

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

//! The identity survives output destruction; only the process-owned live
//! handle clears when QGuiApplication destroys its QScreen.
struct AppliedOutputSnapshot
{
    AppliedOutputIdentity identity;
    QPointer<QScreen> liveScreen;
};

}
}

#endif
