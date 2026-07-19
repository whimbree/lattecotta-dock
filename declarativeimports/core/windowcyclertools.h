/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef LATTECOREWINDOWCYCLERTOOLS_H
#define LATTECOREWINDOWCYCLERTOOLS_H

// Qt
#include <QJSEngine>
#include <QObject>
#include <QQmlEngine>
#include <QVariant>
#include <QVariantList>

namespace Latte {

//! Thin QML shell over the WindowCycler pure core (EX-16 in
//! docs/tracking/QML_EXTRACTION_PLAN.md, units/windowcycler.h). The shells
//! (plasmoid SubWindows.qml, containment loaders/Tasks.qml, plasmoid
//! tools.js) feed value snapshots and apply the returned selection through
//! the live tasksModel request slots. Every "none" result is -1 here -
//! the QML boundary keeps the -1 convention the shells always used; the
//! core beneath is optional-based.
class WindowCyclerTools final : public QObject
{
    Q_OBJECT

public:
    explicit WindowCyclerTools(QObject *parent = nullptr);

public Q_SLOTS:
    //! windows: [{winId, isActive, isMinimized}] in group order. winId is
    //! opaque (string on wayland, number on X11 - stringified here, the
    //! 8e8cdf31 family). lastActiveWinId: the group's last-active winId,
    //! or -1 for none (the Qt5 sentinel the shells keep).
    //! Returns the index to activate, -1 when there is nothing to act on.
    Q_INVOKABLE int selectNext(const QVariantList &windows, const QVariant &lastActiveWinId) const;
    Q_INVOKABLE int selectPrevious(const QVariantList &windows, const QVariant &lastActiveWinId) const;
    //! the index whose minimized state a group toggle flips; -1 when every
    //! window is minimized (a normal state, unlike the two above)
    Q_INVOKABLE int selectMinimizeTarget(const QVariantList &windows, const QVariant &lastActiveWinId) const;

    //! entries: [{isLauncher, isStartup, isGroupParent, childCount}] in
    //! bar order. Returns the flat cycle stops as [{row, childRow}] with
    //! childRow -1 for a plain task's stop.
    Q_INVOKABLE QVariantList flattenTasksForCycling(const QVariantList &entries) const;

    //! the stop adjacent to activeIndex (-1 for "active not in the list")
    //! among count stops, wrapping at both ends; -1 only when count is 0
    Q_INVOKABLE int selectAdjacentTask(int count, int activeIndex, bool next) const;
};

static QObject *windowcyclertools_qobject_singletontype_provider(QQmlEngine *engine, QJSEngine *scriptEngine)
{
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)

// NOTE: QML engine is the owner of this resource
    return new WindowCyclerTools;
}

}

#endif
