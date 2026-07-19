/*
    SPDX-FileCopyrightText: 2016 Smith AR <audoban@openmailbox.org>
    SPDX-FileCopyrightText: 2016-2018 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef LATTEWINDOWCYCLER_H
#define LATTEWINDOWCYCLER_H

// Qt
#include <QList>
#include <QString>
#include <QtGlobal>

// C++
#include <optional>

namespace Latte {
namespace WindowCycler {

//! Cycle-target selection over window/task snapshots (EX-16 in
//! docs/tracking/QML_EXTRACTION_PLAN.md). Replaces the hand-mirrored QML/JS bodies
//! that drifted apart per copy (unguarded WinIdList reads, a dead taskList
//! reference): the group-window family below is SubWindows.qml's three
//! cycle functions, the flat-task family is the containment loader's and
//! plasmoid tools.js's activateNextPrevTask. Semantics match the Qt5
//! bodies at f0ad7b23 exactly, except that an empty list yields nullopt
//! instead of Qt5's invalid-index activation request (the spec's recorded
//! deviation; shells warn instead of firing a request at nothing).

//! One window of a task group. winId is OPAQUE (8e8cdf31): wayland ids are
//! UUID strings, X11 ids are decimal strings - never ints in here; the
//! QML boundary stringifies.
struct GroupWindow {
    QString winId;
    bool isActive = false;
    bool isMinimized = false;
};

//! The window AFTER the first active one, wrapping past the end. With no
//! active window the ladder falls to the last-active id's window (stale or
//! absent id falls to the first window). nullopt only for an empty list.
inline std::optional<int> selectNext(const QList<GroupWindow> &windows,
                                     const std::optional<QString> &lastActiveWinId)
{
    if (windows.isEmpty()) {
        return std::nullopt;
    }

    for (int i = 0; i < windows.size(); ++i) {
        if (windows[i].isActive) {
            return (i + 1 < windows.size()) ? i + 1 : 0;
        }
    }

    if (lastActiveWinId) {
        for (int i = 0; i < windows.size(); ++i) {
            if (windows[i].winId == *lastActiveWinId) {
                return i;
            }
        }
    }

    return 0;
}

//! The window BEFORE the last active one, wrapping past the start. The
//! backward scans mirror Qt5: with several active windows the LAST one
//! decides, and the last-active fallback takes the last id match.
inline std::optional<int> selectPrevious(const QList<GroupWindow> &windows,
                                         const std::optional<QString> &lastActiveWinId)
{
    if (windows.isEmpty()) {
        return std::nullopt;
    }

    for (int i = windows.size() - 1; i >= 0; --i) {
        if (windows[i].isActive) {
            return (i > 0) ? i - 1 : windows.size() - 1;
        }
    }

    if (lastActiveWinId) {
        for (int i = windows.size() - 1; i >= 0; --i) {
            if (windows[i].winId == *lastActiveWinId) {
                return i;
            }
        }
    }

    return 0;
}

//! The window whose minimized state a group toggle should flip: the last
//! active one; else the last-active id's window IF it is not minimized
//! (a minimized match falls through - Qt5's ladder); else the last
//! non-minimized window. nullopt when every window is minimized (or the
//! list is empty): nothing to toggle is a normal state here, unlike the
//! activation selectors above.
inline std::optional<int> selectMinimizeTarget(const QList<GroupWindow> &windows,
                                               const std::optional<QString> &lastActiveWinId)
{
    for (int i = windows.size() - 1; i >= 0; --i) {
        if (windows[i].isActive) {
            return i;
        }
    }

    if (lastActiveWinId) {
        for (int i = windows.size() - 1; i >= 0; --i) {
            if (windows[i].winId == *lastActiveWinId && !windows[i].isMinimized) {
                return i;
            }
        }
    }

    for (int i = windows.size() - 1; i >= 0; --i) {
        if (!windows[i].isMinimized) {
            return i;
        }
    }

    return std::nullopt;
}

//! One row of the task bar as the flat-cycle flattening sees it.
struct TaskEntry {
    bool isLauncher = false;
    bool isStartup = false;
    bool isGroupParent = false;
    //! model rows under a group parent; meaningless (0) otherwise
    int childCount = 0;
};

//! A cycle stop: a bar row, plus the child row when the stop is one window
//! of a group parent. A plain task cannot carry a child row - the optional
//! makes that unrepresentable.
struct TaskPosition {
    int row = 0;
    std::optional<int> childRow;

    bool operator==(const TaskPosition &) const = default;
};

//! Expands the bar into the flat list activation cycles over: launchers
//! and startups drop out, a group parent contributes one stop per child
//! window (none while childless), a plain task contributes itself.
inline QList<TaskPosition> flattenTasksForCycling(const QList<TaskEntry> &entries)
{
    QList<TaskPosition> positions;

    for (int row = 0; row < entries.size(); ++row) {
        const TaskEntry &entry = entries[row];
        //! a negative child count is a shell bug (model rowCount cannot be
        //! negative); the QML boundary refuses it loudly before it gets here
        Q_ASSERT(entry.childCount >= 0);

        if (entry.isLauncher || entry.isStartup) {
            continue;
        }

        if (entry.isGroupParent) {
            for (int child = 0; child < entry.childCount; ++child) {
                positions.append(TaskPosition{row, child});
            }
        } else {
            positions.append(TaskPosition{row, std::nullopt});
        }
    }

    return positions;
}

enum class CycleDirection {
    Next,
    Previous
};

//! The stop adjacent to the active one in the flat cycle list: next wraps
//! past the end to the first stop, previous wraps past the start to the
//! last. No active stop means the first one (Qt5's initialized-target
//! behavior). nullopt only for an empty list.
inline std::optional<int> selectAdjacentTask(int count,
                                             const std::optional<int> &activeIndex,
                                             CycleDirection direction)
{
    //! preconditions, not input handling: the shells derive both values
    //! from the same list, so a violation is a shell bug (the QML boundary
    //! refuses out-of-range input loudly before it gets here)
    Q_ASSERT(count >= 0);
    Q_ASSERT(!activeIndex || (*activeIndex >= 0 && *activeIndex < count));

    if (count == 0) {
        return std::nullopt;
    }

    if (!activeIndex) {
        return 0;
    }

    if (direction == CycleDirection::Next) {
        return (*activeIndex < count - 1) ? *activeIndex + 1 : 0;
    }

    return (*activeIndex > 0) ? *activeIndex - 1 : count - 1;
}

}
}

#endif
