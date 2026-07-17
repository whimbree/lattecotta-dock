/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "dbusreports.h"

// local
#include "layout/genericlayout.h"
#include "view/containmentinterface.h"
#include "view/effects.h"
#include "view/positioner.h"
#include "view/view.h"
#include "view/visibilitymanager.h"
#include "view/windowstracker/currentscreentracker.h"
#include "view/windowstracker/windowstracker.h"

// Qt
#include <QQuickItem>

// Plasma
#include <Plasma/Applet>
#include <Plasma/Containment>

// PlasmaQuick
#include <plasmaquick/appletquickitem.h>

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

QString collectAppletsData(const Latte::View *view)
{
    //! same invariant layer as collectViewRecord: the view comes from
    //! Synchronizer::viewForContainment, and a registered view always
    //! carries its extended interface
    Q_ASSERT(view);
    Q_ASSERT(view->extendedInterface());

    auto interface = view->extendedInterface();

    QList<AppletRecord> records;
    const QList<int> order = interface->appletsOrder();
    records.reserve(order.count());

    for (const int id : order) {
        const auto data = interface->appletDataForId(id);

        //! appletsOrder() also carries justify-splitter pseudo-ids that own
        //! no applet data; they are layout artifacts, not applets, so the
        //! report (schema: "per applet") skips them
        if (data.id < 0) {
            continue;
        }

        //! applet data entries always carry their quick item: the tracking
        //! block in onAppletAdded only runs with a non-null AppletQuickItem
        Q_ASSERT(data.applet && data.plasmoid);

        AppletRecord record;
        record.id = data.id;
        record.plugin = data.plugin;
        record.index = interface->indexOfApplet(data.id);
        //! "geometry within the view": the applet item mapped to scene
        //! coordinates, and the scene IS the view window
        record.geometry = data.plasmoid->mapRectToScene(QRectF(0, 0, data.plasmoid->width(), data.plasmoid->height())).toAlignedRect();
        record.isExpanded = interface->appletIsExpanded(data.id);
        //! Plasma::Applet::destroyed() is the scheduled-destruction bit the
        //! interface already forwards as appletInScheduledDestructionChanged
        record.inScheduledDestruction = data.applet->destroyed();
        record.lockedZoom = interface->appletsInLockedZoom().contains(data.id);
        record.colorizingBlocked = interface->appletsDisabledColoring().contains(data.id);

        records << record;
    }

    return serializeAppletRecords(records);
}

QString collectTrackerData(const Latte::View *view)
{
    //! the tracker ensemble is constructed with the view (View::init), so
    //! non-null members are invariants, not runtime input
    Q_ASSERT(view);
    Q_ASSERT(view->containment());
    Q_ASSERT(view->windowsTracker() && view->windowsTracker()->currentScreen());

    auto tracker = view->windowsTracker();
    auto currentScreen = tracker->currentScreen();

    TrackerRecord record;
    record.containmentId = view->containment()->id();
    record.enabled = tracker->enabled();
    record.activeWindowTouching = currentScreen->activeWindowTouching();
    record.activeWindowTouchingEdge = currentScreen->activeWindowTouchingEdge();
    record.activeWindowMaximized = currentScreen->activeWindowMaximized();
    record.existsWindowActive = currentScreen->existsWindowActive();
    record.existsWindowTouching = currentScreen->existsWindowTouching();
    record.existsWindowTouchingEdge = currentScreen->existsWindowTouchingEdge();
    record.existsWindowMaximized = currentScreen->existsWindowMaximized();

    //! a legitimately absent optional: the wm tracker hands out no
    //! LastActiveWindow while the view is untracked (tracking disabled or
    //! not yet registered), and the window it does hand out is invalid
    //! until a first activation lands - both read as "not present"
    auto lastWindow = currentScreen->lastActiveWindow();
    record.lastActiveWindowPresent = lastWindow && lastWindow->isValid();
    //! appName is application identity, deliberately not the window title
    //! (titles are other applications' content - the interface doc records
    //! the rule); an absent window reports an empty name
    record.lastActiveWindowAppName = record.lastActiveWindowPresent ? lastWindow->appName() : QString();

    return serializeTrackerData(record);
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
