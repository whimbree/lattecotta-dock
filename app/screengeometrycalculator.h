/*
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
//! docs/QML_EXTRACTION_PLAN.md), extracted from Latte::Corona's
//! availableScreenRect/RegionWithCriteria so it runs over value snapshots
//! instead of live View pointers (capt's proven seam, latte-dock-qt6
//! screengeometrycalculator at 81384003; our loop bodies were diffed
//! against theirs and against Qt5 f0ad7b23 before adoption).
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
    Plasma::Types::FormFactor formFactor{Plasma::Types::Planar};
    Latte::Types::Alignment alignment{Latte::Types::Center};
    Latte::Types::Visibility visibilityMode{Latte::Types::None};

    bool hasVisibility{true};  //!< view->visibility() is non-null
    bool isOffScreen{false};   //!< view->positioner()->isOffScreen()
    bool behaveAsPlasmaPanel{false};

    int normalThickness{0};
    int screenEdgeMargin{0};
    float maxLength{1.0f};
    float offset{0.0f};

    QRect geometry; //!< the view window geometry
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
                available.setTop(qMax(available.top(), view.geometry.y() + appliedThickness));
            }
            break;

        case Plasma::Types::BottomEdge:
            if (view.behaveAsPlasmaPanel && desktopUse) {
                available.setBottom(qMin(available.bottom(), screenGeometry.bottom() - appliedThickness));
            } else {
                available.setBottom(qMin(available.bottom(), view.geometry.y() + view.geometry.height() - appliedThickness));
            }
            break;

        case Plasma::Types::LeftEdge:
            if (view.behaveAsPlasmaPanel && desktopUse) {
                available.setLeft(qMax(available.left(), screenGeometry.left() + appliedThickness));
            } else {
                available.setLeft(qMax(available.left(), view.geometry.x() + appliedThickness));
            }
            break;

        case Plasma::Types::RightEdge:
            if (view.behaveAsPlasmaPanel && desktopUse) {
                available.setRight(qMin(available.right(), screenGeometry.right() - appliedThickness));
            } else {
                available.setRight(qMin(available.right(), view.geometry.x() + view.geometry.width() - appliedThickness));
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

        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;

        switch (view.formFactor) {
        case Plasma::Types::Horizontal:
            if (view.behaveAsPlasmaPanel) {
                w = view.geometry.width();
                x = view.geometry.x();
            } else {
                w = view.maxLength * view.geometry.width();
                const int offsetW = view.offset * view.geometry.width();

                switch (view.alignment) {
                case Latte::Types::Left:
                    x = view.geometry.x() + offsetW;
                    break;

                case Latte::Types::Center:
                case Latte::Types::Justify:
                    x = (view.geometry.center().x() - w / 2) + 1 + offsetW;
                    break;

                case Latte::Types::Right:
                    x = view.geometry.right() + 1 - w - offsetW;
                    break;

                default:
                    break;
                }
            }
            break;
        case Plasma::Types::Vertical:
            if (view.behaveAsPlasmaPanel) {
                h = view.geometry.height();
                y = view.geometry.y();
            } else {
                h = view.maxLength * view.geometry.height();
                const int offsetH = view.offset * view.geometry.height();

                switch (view.alignment) {
                case Latte::Types::Top:
                    y = view.geometry.y() + offsetH;
                    break;

                case Latte::Types::Center:
                case Latte::Types::Justify:
                    y = (view.geometry.center().y() - h / 2) + 1 + offsetH;
                    break;

                case Latte::Types::Bottom:
                    y = view.geometry.bottom() - h - offsetH;
                    break;

                default:
                    break;
                }
            }
            break;
        default:
            break;
        }

        switch (view.location) {
        case Plasma::Types::TopEdge:
            if (view.behaveAsPlasmaPanel) {
                QRect viewGeometry = view.geometry;

                if (desktopUse) {
                    //! ignore any real window slide outs in all cases
                    viewGeometry.moveTop(screenGeometry.top() + view.screenEdgeMargin);
                }

                available -= viewGeometry;
            } else {
                y = view.geometry.y();
                available -= QRect(x, y, w, realThickness);
            }
            break;

        case Plasma::Types::BottomEdge:
            if (view.behaveAsPlasmaPanel) {
                QRect viewGeometry = view.geometry;

                if (desktopUse) {
                    viewGeometry.moveTop(screenGeometry.bottom() - view.screenEdgeMargin - viewGeometry.height());
                }

                available -= viewGeometry;
            } else {
                y = view.geometry.bottom() - realThickness + 1;
                available -= QRect(x, y, w, realThickness);
            }
            break;

        case Plasma::Types::LeftEdge:
            if (view.behaveAsPlasmaPanel) {
                QRect viewGeometry = view.geometry;

                if (desktopUse) {
                    viewGeometry.moveLeft(screenGeometry.left() + view.screenEdgeMargin);
                }

                available -= viewGeometry;
            } else {
                x = view.geometry.x();
                available -= QRect(x, y, realThickness, h);
            }
            break;

        case Plasma::Types::RightEdge:
            if (view.behaveAsPlasmaPanel) {
                QRect viewGeometry = view.geometry;

                if (desktopUse) {
                    viewGeometry.moveLeft(screenGeometry.right() - view.screenEdgeMargin - viewGeometry.width());
                }

                available -= viewGeometry;
            } else {
                x = view.geometry.right() - realThickness + 1;
                available -= QRect(x, y, realThickness, h);
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
