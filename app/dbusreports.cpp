/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "dbusreports.h"

// local
#include "layout/genericlayout.h"
#include "view/effects.h"
#include "view/positioner.h"
#include "view/view.h"
#include "view/visibilitymanager.h"

// Plasma
#include <Plasma/Containment>

namespace Latte {
namespace DbusReports {

ViewRecord collectViewRecord(const Latte::View *view, bool inConfigureAppletsMode)
{
    //! views come from Synchronizer::currentViews(), not from outside the
    //! process, and a registered view always carries its containment and
    //! helper ensemble - so these are invariants to assert, not input to
    //! refuse at runtime
    Q_ASSERT(view);
    Q_ASSERT(view->containment());
    Q_ASSERT(view->positioner() && view->visibility() && view->effects());

    ViewRecord record;
    record.containmentId = view->containment()->id();
    //! a view legitimately has no layout for a moment while it moves
    //! between layouts (moveViewToLayout); an empty name reports that state
    record.layout = view->layout() ? view->layout()->name() : QString();
    record.isCloned = view->isCloned();
    //! groupId() is the original's containment id for clones; originals
    //! report -1 per docs/dbus-observability-interface.md
    record.isClonedFrom = record.isCloned ? view->groupId() : -1;
    record.type = view->type();
    record.screen = view->positioner()->currentScreenName();
    record.onPrimary = view->onPrimary();
    record.edge = view->location();
    //! View's alignment surface is int (its QML property type); the value
    //! is stored as Types::Alignment internally, so the cast is lossless
    record.alignment = static_cast<Types::Alignment>(view->alignment());
    record.visibilityMode = view->visibility()->mode();
    record.isHidden = view->visibility()->isHidden();
    record.inStartup = view->positioner()->inStartup();
    record.isOffScreen = view->positioner()->isOffScreen();
    record.absoluteGeometry = view->absoluteGeometry();
    record.localGeometry = view->localGeometry();
    record.screenGeometry = view->screenGeometry();
    record.strutsThickness = view->visibility()->strutsThickness();
    record.publishedStruts = view->visibility()->publishedStruts();
    record.maskRect = view->effects()->mask();
    record.inputMask = view->effects()->inputMask();
    record.editMode = view->inEditMode();
    record.inConfigureAppletsMode = inConfigureAppletsMode;

    return record;
}

QString collectViewsData(const QList<Latte::View *> &views, bool inConfigureAppletsMode)
{
    QList<ViewRecord> records;
    records.reserve(views.count());

    for (const auto view : views) {
        records << collectViewRecord(view, inConfigureAppletsMode);
    }

    return serializeViewRecords(records);
}

}
}
