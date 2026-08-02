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
#include <QString>

namespace Latte {
namespace WindowSystem {
namespace WindowTrackingPredicates {

[[nodiscard]] inline bool allowsSkippedWindowForApplication(
    const QString &applicationId)
{
    return applicationId == QLatin1String("yakuake")
        || applicationId == QLatin1String("krunner");
}

enum class WindowAdmissionState
{
    Unpublished,
    PublishedRejected,
    Accepted,
};

enum class WindowAdmissionAction
{
    None,
    Publish,
    Refresh,
};

struct WindowAdmissionTransition
{
    WindowAdmissionState nextState{WindowAdmissionState::Unpublished};
    WindowAdmissionAction action{WindowAdmissionAction::None};

    friend constexpr bool operator==(const WindowAdmissionTransition &left,
                                     const WindowAdmissionTransition &right) = default;
};

//! KWin object observation outlives temporary tracking rejection. A published
//! row therefore changes validity in place; only an acceptable never-published
//! window needs windowAdded, and only compositor destruction needs
//! windowRemoved. Keeping this table constexpr makes every state transition
//! exhaustively testable without a Wayland connection.
[[nodiscard]] constexpr WindowAdmissionTransition planWindowAdmission(
    WindowAdmissionState current,
    bool isAcceptable) noexcept
{
    switch (current) {
    case WindowAdmissionState::Unpublished:
        return isAcceptable
            ? WindowAdmissionTransition{WindowAdmissionState::Accepted,
                                        WindowAdmissionAction::Publish}
            : WindowAdmissionTransition{};
    case WindowAdmissionState::PublishedRejected:
        return isAcceptable
            ? WindowAdmissionTransition{WindowAdmissionState::Accepted,
                                        WindowAdmissionAction::Refresh}
            : WindowAdmissionTransition{WindowAdmissionState::PublishedRejected,
                                        WindowAdmissionAction::None};
    case WindowAdmissionState::Accepted:
        return WindowAdmissionTransition{
            isAcceptable ? WindowAdmissionState::Accepted
                         : WindowAdmissionState::PublishedRejected,
            WindowAdmissionAction::Refresh};
    }

    return {};
}

inline bool intersects(const WindowInfoWrap &winfo, const QRect &viewAbsoluteGeometry)
{
    return (!winfo.isMinimized() && !winfo.isShaded() && winfo.geometry().intersects(viewAbsoluteGeometry));
}

inline bool matchesExactWindowIdentity(const QString &appId,
                                       const QString &title,
                                       const QString &expectedAppId,
                                       const QString &expectedTitle)
{
    return appId == expectedAppId && title == expectedTitle;
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
