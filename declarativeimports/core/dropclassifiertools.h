/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef LATTECOREDROPCLASSIFIERTOOLS_H
#define LATTECOREDROPCLASSIFIERTOOLS_H

// Qt
#include <QJSEngine>
#include <QJSValue>
#include <QObject>
#include <QQmlEngine>
#include <QVariantMap>

class QMimeData;

namespace Latte {

//! Thin QML shell over the DropClassifier pure core (EX-14 in
//! docs/tracking/QML_EXTRACTION_PLAN.md, units/dropclassifier.h). The drag
//! handlers of the containment DragDropArea and the plasmoid
//! MouseHandler hand their event's QMimeData (a DeclarativeMimeData IS
//! one) plus trivially-read live state here and apply the returned
//! verdicts as scene effects.
//!
//! Boundary conventions (the router wrapper's model): null mimeData, a
//! non-callable isApplication predicate, a malformed drag-move snapshot
//! and a non-positive itemStep are caller bugs and are refused loudly
//! (qCritical + the inert answer). A predicate that throws in JS is
//! surfaced as a qWarning and answers "not an application" for that
//! url, never silently swallowed.
class DropClassifierTools final : public QObject
{
    Q_OBJECT

public:
    //! mirror of DropClassifier::ContainmentEnterAction
    //! (value names prefixed per ladder: all enums share the QML
    //! singleton's flat name scope)
    enum ContainmentEnterAction {
        EnterReject = 0,
        EnterShowAddLaunchersMessage,
        EnterInsertSpacer
    };
    Q_ENUM(ContainmentEnterAction)

    //! mirror of DropClassifier::ContainmentMoveAction
    enum ContainmentMoveAction {
        MoveLeaveUnchanged = 0,
        MoveShowAddLaunchersMessage,
        MoveInsertSpacer
    };
    Q_ENUM(ContainmentMoveAction)

    //! mirror of DropClassifier::ContainmentDropAction
    enum ContainmentDropAction {
        DropIgnore = 0,
        DropAddLaunchersToStealingApplet,
        DropProcessMime
    };
    Q_ENUM(ContainmentDropAction)

    //! mirror of DropClassifier::TasksDropAction
    enum TasksDropAction {
        TasksDropIgnore = 0,
        TasksDropAddSeparator,
        TasksDropUrls,
        TasksDropLeaveUnchanged
    };
    Q_ENUM(TasksDropAction)

    //! mirror of DropClassifier::TasksDragMoveAction
    enum TasksDragMoveAction {
        MoveTaskSuppressRepeatTarget = 0,
        MoveTaskReorder,
        MoveTaskKeepOrder,
        MoveTaskHoverAbove,
        MoveTaskClearHover,
        MoveTaskNone
    };
    Q_ENUM(TasksDragMoveAction)

    explicit DropClassifierTools(QObject *parent = nullptr);

public Q_SLOTS:
    //! {isTask, isPlasmoid, isSeparator, isLatteTasks, onlyLaunchers}
    Q_INVOKABLE QVariantMap classifyContainmentDrag(QMimeData *mimeData, const QJSValue &isApplicationUrl);
    //! {movingTask, droppingOnlyLaunchers, droppingSeparator, droppingFiles}
    Q_INVOKABLE QVariantMap classifyTasksDrag(QMimeData *mimeData, const QJSValue &isApplicationUrl);

    Q_INVOKABLE int containmentDragEnterAction(bool isTask, bool onlyLaunchers, bool immutable,
                                               bool viewShownFully, bool hasStealingApplet);
    Q_INVOKABLE int containmentDragMoveAction(bool isTask, bool onlyLaunchers, bool hasStealingApplet);
    Q_INVOKABLE int containmentDropAction(bool isTask, bool onlyLaunchers, bool viewShownFully,
                                          bool hasStealingApplet);

    Q_INVOKABLE bool tasksDragAccepts(bool movingTask, bool droppingOnlyLaunchers,
                                      bool droppingSeparator, bool droppingFiles);
    Q_INVOKABLE int tasksDropAction(bool movingTask, bool droppingOnlyLaunchers,
                                    bool droppingSeparator, bool droppingFiles);
    Q_INVOKABLE int separatorDropPosition(bool hasHoveredItem, int hoveredItemIndex);

    //! snapshot keys (missing/mistyped keys are refused loudly):
    //! dragSource: null | {itemIndex, isLauncher, isAbove}
    //! above:      null | {itemIndex, hasModelData, isLauncher}
    //! hasIgnoredItem, aboveIsIgnored, aboveIsHovered, sortIsManual: bool
    //! posX, posY, itemStep: real; vertical: bool
    //! answer: {action: TasksDragMoveAction, moveTo: int (only for MoveTaskReorder)}
    Q_INVOKABLE QVariantMap decideTasksDragMove(const QVariantMap &snapshot);
};

static QObject *dropclassifiertools_qobject_singletontype_provider(QQmlEngine *engine, QJSEngine *scriptEngine)
{
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)

// NOTE: QML engine is the owner of this resource
    return new DropClassifierTools;
}

}

#endif
