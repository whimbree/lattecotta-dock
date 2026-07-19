/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, derived)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Window-state predicates behind the tracking/dodge decisions (EX-23 in
//! docs/tracking/QML_EXTRACTION_PLAN.md), adopted from David Goree's latte-dock-qt6
//! (app/wm/windowtrackingpredicates.h at 9fba82c8,
//! github.com/CaptSilver/latte-dock-qt6) after diffing against our
//! windowstracker.cpp and abstractwindowinterface.cpp bodies - identical,
//! with the X11 devicePixelRatio screen scaling staying in the adapters (the
//! predicates receive the already-scaled screen geometry).

#pragma once

#include "windowinfowrap.h"

#include <QList>
#include <QRect>

namespace Latte {
namespace WindowSystem {
namespace WindowTrackingPredicates {

inline bool intersects(const WindowInfoWrap &winfo, const QRect &viewAbsoluteGeometry)
{
    return (!winfo.isMinimized() && !winfo.isShaded() && winfo.geometry().intersects(viewAbsoluteGeometry));
}

inline bool isActive(const WindowInfoWrap &winfo)
{
    return (winfo.isValid() && winfo.isActive() && !winfo.isMinimized());
}

inline bool isActiveInViewScreen(const WindowInfoWrap &winfo, const QRect &screenGeometry)
{
    return (winfo.isValid() && winfo.isActive() && !winfo.isMinimized() && screenGeometry.intersects(winfo.geometry()));
}

inline bool isMaximizedInViewScreen(const WindowInfoWrap &winfo, const QRect &screenGeometry)
{
    return (winfo.isValid() && !winfo.isMinimized() && !winfo.isShaded() && winfo.isMaximized() && screenGeometry.intersects(winfo.geometry()));
}

inline bool isIgnored(const QList<WindowId> &ignoredWindows, const WindowId &wid)
{
    return ignoredWindows.contains(wid);
}

inline bool isRegisteredPlasmaIgnored(const QList<WindowId> &plasmaIgnoredWindows, const WindowId &wid)
{
    return plasmaIgnoredWindows.contains(wid);
}

inline bool isWhitelisted(const QList<WindowId> &whitelistedWindows, const WindowId &wid)
{
    return whitelistedWindows.contains(wid);
}

inline bool hasBlockedTracking(const QList<WindowId> &ignoredWindows,
                                const QList<WindowId> &plasmaIgnoredWindows,
                                const QList<WindowId> &whitelistedWindows,
                                const WindowId &wid)
{
    return (!isWhitelisted(whitelistedWindows, wid) &&
            (isRegisteredPlasmaIgnored(plasmaIgnoredWindows, wid) || isIgnored(ignoredWindows, wid)));
}

inline bool shouldRegister(const QList<WindowId> &existingWindows, const WindowId &wid)
{
    return (!wid.isEmpty() && !existingWindows.contains(wid));
}

} // namespace WindowTrackingPredicates
} // namespace WindowSystem
} // namespace Latte
