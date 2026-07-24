/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, derived)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef SCREENGEOMETRYCALCULATOR_H
#define SCREENGEOMETRYCALCULATOR_H

// local
#include <coretypes.h>

// Qt
#include <QList>
#include <QRect>
#include <QRegion>

// Plasma
#include <Plasma/Plasma>

namespace Latte {

//! The available-screen geometry math (EX-08 in
//! docs/tracking/QML_EXTRACTION_PLAN.md), extracted from Latte::Corona's
//! availableScreenRect/RegionWithCriteria so it runs over value snapshots
//! instead of live View pointers. The extraction seam and ViewFootprint
//! struct are adopted from David Goree's latte-dock-qt6
//! (app/screengeometrycalculator.h at 6108a3bc,
//! github.com/CaptSilver/latte-dock-qt6); our loop bodies were diffed
//! against theirs and against Qt5 f0ad7b23 before adoption.
//!
//! DIVERGENCE note carried from 1b932ed9: there is deliberately NO
//! self-origin exclusion here or in the shell - a view's own footprint
//! reserves space like any other, because a view's chrome must see the
//! screen area corrected by the view's OWN reserved thickness (upstream
//! d30143f7 excluded the origin view and the settings window opened 99px
//! off-screen on cold starts until the exclusion was reverted).

//! a snapshot of the View properties the geometry math reads
struct ViewFootprint {
    Plasma::Types::Location location{Plasma::Types::Floating};
    Latte::Types::Visibility visibilityMode{Latte::Types::None};

    bool hasVisibility{true};  //!< view->visibility() is non-null
    bool isOffScreen{false};   //!< view->positioner()->isOffScreen()
    bool behaveAsPlasmaPanel{false};

    int normalThickness{0};
    int screenEdgeMargin{0};

    //! The stable dock or panel rectangle that actually occupies the edge.
    //! This is intentionally not the larger masked QWindow canvas.
    QRect occupiedGeometry;
};

namespace ScreenGeometryCalculator {

//! None and NormalWindow never reserve space, regardless of the caller's
//! choices (Qt5-inherited blacklist)
inline void blacklistDefaultModes(QList<Latte::Types::Visibility> &ignoreModes)
{
    if (!ignoreModes.contains(Latte::Types::None)) {
        ignoreModes << Latte::Types::None;
    }

    if (!ignoreModes.contains(Latte::Types::NormalWindow)) {
        ignoreModes << Latte::Types::NormalWindow;
    }
}

//! whether a view carves space out of the screen, after the
//! desktop-startup, edge and visibility-mode filters
inline bool isReserving(const ViewFootprint &view,
                        const QList<Latte::Types::Visibility> &ignoreModes,
                        const QList<Plasma::Types::Location> &ignoreEdges,
                        bool allEdges,
                        bool desktopUse)
{
    const bool inDesktopOffScreenStartup = desktopUse && view.isOffScreen;

    return !inDesktopOffScreenStartup
            && view.occupiedGeometry.isValid()
            && (allEdges || !ignoreEdges.contains(view.location))
            && view.hasVisibility
            && !ignoreModes.contains(view.visibilityMode);
}

//! the screen rect left free of docks: each reserving footprint pushes
//! its edge inward by its applied thickness
inline QRect availableRect(const QRect &startRect,
                           const QRect &screenGeometry,
                           const QList<ViewFootprint> &footprints,
                           QList<Latte::Types::Visibility> ignoreModes,
                           const QList<Plasma::Types::Location> &ignoreEdges,
                           bool desktopUse)
{
    QRect available = startRect;

    if (footprints.isEmpty()) {
        return available;
    }

    blacklistDefaultModes(ignoreModes);
    const bool allEdges = ignoreEdges.isEmpty();

    for (const auto &view : footprints) {
        if (!isReserving(view, ignoreModes, ignoreEdges, allEdges, desktopUse)) {
            continue;
        }

        const int appliedThickness = view.behaveAsPlasmaPanel
                ? view.screenEdgeMargin + view.normalThickness
                : view.normalThickness;

        //! Usually availableScreenRect is used by the desktop, but Latte
        //! has no desktop; only top and bottom need reserving here - left
        //! and right are the ones that dodge other docks (Qt5-inherited)
        switch (view.location) {
        case Plasma::Types::TopEdge:
            if (view.behaveAsPlasmaPanel && desktopUse) {
                //! ignore any real window slide outs in all cases
                available.setTop(qMax(available.top(), screenGeometry.top() + appliedThickness));
            } else {
                available.setTop(qMax(available.top(), view.occupiedGeometry.y() + appliedThickness));
            }
            break;

        case Plasma::Types::BottomEdge:
            if (view.behaveAsPlasmaPanel && desktopUse) {
                available.setBottom(qMin(available.bottom(), screenGeometry.bottom() - appliedThickness));
            } else {
                available.setBottom(qMin(available.bottom(),
                                         view.occupiedGeometry.bottom() - appliedThickness + 1));
            }
            break;

        case Plasma::Types::LeftEdge:
            if (view.behaveAsPlasmaPanel && desktopUse) {
                available.setLeft(qMax(available.left(), screenGeometry.left() + appliedThickness));
            } else {
                available.setLeft(qMax(available.left(),
                                       view.occupiedGeometry.x() + appliedThickness));
            }
            break;

        case Plasma::Types::RightEdge:
            if (view.behaveAsPlasmaPanel && desktopUse) {
                available.setRight(qMin(available.right(), screenGeometry.right() - appliedThickness));
            } else {
                available.setRight(qMin(available.right(),
                                        view.occupiedGeometry.right() - appliedThickness + 1));
            }
            break;

        default:
            break;
        }
    }

    return available;
}

//! the screen region left free of docks: each reserving footprint's
//! occupied strip (alignment/length-aware for docks, the whole window for
//! panel-behaving views) is subtracted
inline QRegion availableRegion(const QRect &startRect,
                               const QRect &screenGeometry,
                               const QList<ViewFootprint> &footprints,
                               QList<Latte::Types::Visibility> ignoreModes,
                               const QList<Plasma::Types::Location> &ignoreEdges,
                               bool desktopUse)
{
    QRegion available = startRect;

    if (footprints.isEmpty()) {
        return available;
    }

    blacklistDefaultModes(ignoreModes);
    const bool allEdges = ignoreEdges.isEmpty();

    for (const auto &view : footprints) {
        if (!isReserving(view, ignoreModes, ignoreEdges, allEdges, desktopUse)) {
            continue;
        }

        const int realThickness = view.normalThickness;

        switch (view.location) {
        case Plasma::Types::TopEdge:
            if (view.behaveAsPlasmaPanel) {
                QRect viewGeometry = view.occupiedGeometry;

                if (desktopUse) {
                    //! ignore any real window slide outs in all cases
                    viewGeometry.moveTop(screenGeometry.top() + view.screenEdgeMargin);
                }

                available -= viewGeometry;
            } else {
                available -= QRect(view.occupiedGeometry.x(),
                                   view.occupiedGeometry.y(),
                                   view.occupiedGeometry.width(),
                                   realThickness);
            }
            break;

        case Plasma::Types::BottomEdge:
            if (view.behaveAsPlasmaPanel) {
                QRect viewGeometry = view.occupiedGeometry;

                if (desktopUse) {
                    viewGeometry.moveTop(screenGeometry.bottom() - view.screenEdgeMargin - viewGeometry.height());
                }

                available -= viewGeometry;
            } else {
                available -= QRect(view.occupiedGeometry.x(),
                                   view.occupiedGeometry.bottom() - realThickness + 1,
                                   view.occupiedGeometry.width(),
                                   realThickness);
            }
            break;

        case Plasma::Types::LeftEdge:
            if (view.behaveAsPlasmaPanel) {
                QRect viewGeometry = view.occupiedGeometry;

                if (desktopUse) {
                    viewGeometry.moveLeft(screenGeometry.left() + view.screenEdgeMargin);
                }

                available -= viewGeometry;
            } else {
                available -= QRect(view.occupiedGeometry.x(),
                                   view.occupiedGeometry.y(),
                                   realThickness,
                                   view.occupiedGeometry.height());
            }
            break;

        case Plasma::Types::RightEdge:
            if (view.behaveAsPlasmaPanel) {
                QRect viewGeometry = view.occupiedGeometry;

                if (desktopUse) {
                    viewGeometry.moveLeft(screenGeometry.right() - view.screenEdgeMargin - viewGeometry.width());
                }

                available -= viewGeometry;
            } else {
                available -= QRect(view.occupiedGeometry.right() - realThickness + 1,
                                   view.occupiedGeometry.y(),
                                   realThickness,
                                   view.occupiedGeometry.height());
            }
            break;

        default:
            break;
        }
    }

    return available;
}

}
}

#endif
