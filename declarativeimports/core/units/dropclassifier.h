/*
    SPDX-FileCopyrightText: 2016 Smith AR <audoban@openmailbox.org>
    SPDX-FileCopyrightText: 2016-2019 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef DROPCLASSIFIER_H
#define DROPCLASSIFIER_H

// Qt
#include <QList>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtGlobal>

// C++
#include <algorithm>
#include <cmath>
#include <concepts>
#include <optional>

namespace Latte {

//! Drag/drop mime classification and the drop decision ladders (EX-14 in
//! docs/tracking/QML_EXTRACTION_PLAN.md), extracted from the containment's
//! DragDropArea.qml, the plasmoid's taskslayout/MouseHandler.qml and
//! tools.js insertIndexAt. The two consumers matched the same mime
//! formats with SUBTLY different accept sets (newline-split name lines
//! vs whole-payload comparison; presence vs first-position format
//! checks); one header now owns both rule sets side by side so they can
//! no longer drift apart silently. The QML shells keep only scene
//! effects (spacer parenting, launcher messages, model moves) and pass
//! every verdict question here, where a decision cannot be silently
//! disconnected the way a Qt6 DropArea `function onDrop` member is
//! (the b474adad lesson).
//!
//! All bodies are Qt5-faithful (f0ad7b23 read function by function; the
//! port's shipped bodies are byte-identical to Qt5). Faithful oddities
//! are documented at their functions and pinned by tests, not "fixed".
namespace DropClassifier {

//! the task-manager drag payload every taskbar button drag carries
inline const QLatin1StringView TaskButtonItemFormat{"application/x-orgkdeplasmataskmanager_taskbuttonitem"};
//! the widget-explorer applet drag payload (newline-separated plugin ids)
inline const QLatin1StringView PlasmoidServiceNameFormat{"text/x-plasmoidservicename"};
inline const QLatin1StringView AudobanSeparatorId{"audoban.applet.separator"};
inline const QLatin1StringView LatteSeparatorId{"org.kde.latte.separator"};
inline const QLatin1StringView LatteTasksId{"org.kde.latte.plasmoid"};

//! a predicate deciding whether a dragged url is an application entry
//! (a .desktop file / launcher); the live implementations consult C++
//! (extendedInterface/backend isApplication), so the classifier takes
//! the answer as an injected callable - the EX-23 callback seam shape,
//! invoked synchronously, never stored
template<typename Predicate>
concept IsApplicationUrlPredicate = std::predicate<Predicate, const QUrl &>;

//! pure value snapshot of a drag's mime payload, marshalled once per
//! event by the QML wrapper from the event's QMimeData. hasUrls and
//! urls are BOTH carried: QMimeData::hasUrls() is text/uri-list format
//! presence while urls() is parsed content, and the shipped decisions
//! read both (guards on hasUrls, every() over urls).
struct MimeSnapshot {
    QStringList formats;
    //! the "text/x-plasmoidservicename" payload as text; possibly
    //! several newline-separated plugin ids
    QString plasmoidServiceData;
    bool hasUrls{false};
    QList<QUrl> urls;
};

inline bool containsTaskButtonFormat(const MimeSnapshot &mime)
{
    return mime.formats.indexOf(TaskButtonItemFormat) >= 0;
}

//! Qt5 subtlety, shared by both consumers: the plasmoid-service checks
//! test `formats.indexOf(...) === 0` - the service format must be the
//! formats list's FIRST entry, not merely present
inline bool firstFormatIsPlasmoidService(const MimeSnapshot &mime)
{
    return mime.formats.indexOf(PlasmoidServiceNameFormat) == 0;
}

//! the containment's name rule (View::mimeContainsPlasmoid): the format
//! must be PRESENT (any position) and any newline-separated payload
//! line may match
inline bool namesPlasmoidService(const MimeSnapshot &mime, QLatin1StringView name)
{
    if (!mime.formats.contains(PlasmoidServiceNameFormat)) {
        return false;
    }

    const QStringList lines = mime.plasmoidServiceData.split(QLatin1Char('\n'), Qt::SkipEmptyParts);

    return lines.contains(name);
}

//! the plasmoid's separator rule (MouseHandler.isDroppingSeparator):
//! the WHOLE payload compared against the two separator ids, and the
//! service format must be the FIRST formats entry. A multi-line payload
//! containing a separator line therefore matches in the containment but
//! not here - faithful asymmetry, pinned by tests.
inline bool wholePayloadNamesSeparator(const MimeSnapshot &mime)
{
    const bool isSeparatorName = mime.plasmoidServiceData == AudobanSeparatorId
            || mime.plasmoidServiceData == LatteSeparatorId;

    return firstFormatIsPlasmoidService(mime) && isSeparatorName;
}

//! the shared launchers rule (both consumers carry the identical body):
//! for a drag that has urls OR is not a first-format plasmoid drag,
//! every dragged url must be an application. Qt5 quirk preserved:
//! every() over an EMPTY url list is vacuously TRUE, so a no-url,
//! non-plasmoid drag (e.g. plain text) classifies as "only launchers" -
//! downstream that resolves to a no-op url drop, exactly as in Qt5.
template<IsApplicationUrlPredicate Predicate>
bool droppingOnlyLaunchers(const MimeSnapshot &mime, const Predicate &isApplicationUrl)
{
    if (mime.hasUrls || !firstFormatIsPlasmoidService(mime)) {
        return std::all_of(mime.urls.cbegin(), mime.urls.cend(), isApplicationUrl);
    }

    return false;
}

//! the containment's five independent observations (DragDropArea
//! onDragEnter); unlike the plasmoid flags these are NOT mutually
//! exclusive - e.g. a task drag also answers onlyLaunchers=true through
//! the vacuous every() - and the enter ladder resolves the precedence
struct ContainmentDragFlags {
    bool isTask{false};
    bool isPlasmoid{false};
    bool isSeparator{false};
    bool isLatteTasks{false};
    bool onlyLaunchers{false};
};

template<IsApplicationUrlPredicate Predicate>
ContainmentDragFlags classifyContainmentDrag(const MimeSnapshot &mime, const Predicate &isApplicationUrl)
{
    return ContainmentDragFlags{
        .isTask = containsTaskButtonFormat(mime),
        //! isPlasmoid additionally requires NO urls; isSeparator and
        //! isLatteTasks (the name rule) do not - faithful asymmetry
        .isPlasmoid = !mime.hasUrls && firstFormatIsPlasmoidService(mime),
        .isSeparator = namesPlasmoidService(mime, AudobanSeparatorId)
                || namesPlasmoidService(mime, LatteSeparatorId),
        .isLatteTasks = namesPlasmoidService(mime, LatteTasksId),
        .onlyLaunchers = droppingOnlyLaunchers(mime, isApplicationUrl),
    };
}

//! the plasmoid's flags (MouseHandler onDragEnter) with their faithful
//! precedence chain: a task move vetoes launchers and separator;
//! recognized launchers veto the files flag
struct TasksDropFlags {
    bool movingTask{false};
    bool droppingOnlyLaunchers{false};
    bool droppingSeparator{false};
    bool droppingFiles{false};
};

template<IsApplicationUrlPredicate Predicate>
TasksDropFlags classifyTasksDrag(const MimeSnapshot &mime, const Predicate &isApplicationUrl)
{
    TasksDropFlags flags;
    flags.movingTask = containsTaskButtonFormat(mime);
    flags.droppingOnlyLaunchers = !flags.movingTask && droppingOnlyLaunchers(mime, isApplicationUrl);
    flags.droppingSeparator = !flags.movingTask && wholePayloadNamesSeparator(mime);
    flags.droppingFiles = !flags.droppingOnlyLaunchers && mime.hasUrls;

    return flags;
}

//! MouseHandler's eventIsAccepted rule: anything the tasks area knows
//! how to receive
inline bool tasksDragAccepts(const TasksDropFlags &flags)
{
    return flags.movingTask || flags.droppingSeparator || flags.droppingOnlyLaunchers || flags.droppingFiles;
}

//! ---- containment decision ladders (DragDropArea's three handlers) ----
//! Reject/Ignore answers mean the QML performs its Qt5 refusal
//! (event.ignore()+clearInfo at enter, plain return elsewhere); the
//! scene effects for the other answers stay in the shell.

enum class ContainmentEnterAction {
    Reject, //!< task drag, immutable containment, or view not fully shown
    ShowAddLaunchersMessage, //!< a tasks applet steals launcher drops
    InsertSpacer
};

inline ContainmentEnterAction containmentDragEnterAction(const ContainmentDragFlags &flags,
                                                         bool immutable, bool viewShownFully,
                                                         bool hasStealingApplet)
{
    if (flags.isTask || immutable || !viewShownFully) {
        return ContainmentEnterAction::Reject;
    }

    if (hasStealingApplet && flags.onlyLaunchers) {
        return ContainmentEnterAction::ShowAddLaunchersMessage;
    }

    return ContainmentEnterAction::InsertSpacer;
}

//! the move ladder never re-checks immutable/viewShownFully - a
//! rejected enter never confirmed the drag, so move only re-routes the
//! already-accepted kinds (faithful to Qt5's onDragMove)
enum class ContainmentMoveAction {
    LeaveUnchanged, //!< task drag: keep current state, no re-routing
    ShowAddLaunchersMessage,
    InsertSpacer
};

inline ContainmentMoveAction containmentDragMoveAction(const ContainmentDragFlags &flags,
                                                       bool hasStealingApplet)
{
    if (flags.isTask) {
        return ContainmentMoveAction::LeaveUnchanged;
    }

    if (hasStealingApplet && flags.onlyLaunchers) {
        return ContainmentMoveAction::ShowAddLaunchersMessage;
    }

    return ContainmentMoveAction::InsertSpacer;
}

enum class ContainmentDropAction {
    Ignore, //!< task drag or view not fully shown; the spacer is deliberately NOT hidden (Qt5 returned early)
    AddLaunchersToStealingApplet,
    ProcessMime //!< hand the mime to the containment (with the dnd-spacer masquerade substitution in the shell)
};

inline ContainmentDropAction containmentDropAction(const ContainmentDragFlags &flags,
                                                   bool viewShownFully, bool hasStealingApplet)
{
    if (flags.isTask || !viewShownFully) {
        return ContainmentDropAction::Ignore;
    }

    if (hasStealingApplet && flags.onlyLaunchers) {
        return ContainmentDropAction::AddLaunchersToStealingApplet;
    }

    return ContainmentDropAction::ProcessMime;
}

//! ---- plasmoid drop ladder (MouseHandler onDrop) ----

enum class TasksDropAction {
    Ignore, //!< unrecognized drag: clear flags + event.ignore()
    AddSeparator, //!< internal separator at separatorDropPosition()
    DropUrls, //!< launchers or files: emit urlsDropped
    LeaveUnchanged //!< an internal task move released here ("Reject internal drops")
};

inline TasksDropAction tasksDropAction(const TasksDropFlags &flags)
{
    if (!tasksDragAccepts(flags)) {
        return TasksDropAction::Ignore;
    }

    if (flags.droppingSeparator) {
        return TasksDropAction::AddSeparator;
    }

    if (flags.droppingOnlyLaunchers || flags.droppingFiles) {
        return TasksDropAction::DropUrls;
    }

    return TasksDropAction::LeaveUnchanged;
}

//! where an internal separator lands: the hovered task's index when one
//! is hovered AND indexed, else the row head (Qt5's onDrop ternary)
inline int separatorDropPosition(std::optional<int> hoveredItemIndex)
{
    return (hoveredItemIndex && *hoveredItemIndex >= 0) ? *hoveredItemIndex : 0;
}

//! ---- insert index (tools.js insertIndexAt, the plasmoid's only copy) ----
//!
//! Faithful to Qt5 including two quirks the tests pin:
//! - `above && above.itemIndex` is a JS TRUTHINESS test: a hovered item
//!   at index 0 falls through to the stripe math (which answers 0 for
//!   any point inside the first step, so the difference only shows at
//!   the exact row origin), and a transient index -1 is truthy and
//!   returned as-is;
//! - the stripe math `ceil(distance/step) - 1` answers -1 at distance 0
//!   and lower before the target origin (mapToItem can hand negative
//!   coordinates); TasksModel::move rejects negative targets, so those
//!   answers are inert downstream - preserved, not clamped.
inline int insertIndexAt(std::optional<int> aboveItemIndex, QPointF positionInTarget,
                         Qt::Orientation orientation, qreal itemStep)
{
    //! metrics.totals.length is >= icon size on any live dock; a
    //! non-positive step means the caller read a torn-down metrics
    //! object and the wrapper refuses before reaching here
    Q_ASSERT(itemStep > 0);

    if (aboveItemIndex && *aboveItemIndex != 0) {
        return *aboveItemIndex;
    }

    const qreal distance = (orientation == Qt::Vertical) ? positionInTarget.y() : positionInTarget.x();

    return static_cast<int>(std::ceil(distance / itemStep)) - 1;
}

//! ---- drag-move routing (MouseHandler onDragMove's suppression + move ladder) ----

//! the dragged task's relevant state; absent for external drags
struct DragSourceState {
    int itemIndex{-1};
    bool isLauncher{false};
    //! object identity `dragSource === above`, computed where identity
    //! lives (the QML delegate references)
    bool isAbove{false};
};

//! the delegate under the cursor; absent when the pointer is over
//! empty row space
struct AboveItemState {
    int itemIndex{-1};
    //! whether the delegate's model data (`.m`) is attached; isLauncher
    //! is only meaningful when it is
    bool hasModelData{false};
    bool isLauncher{false};
};

struct TasksDragMoveSnapshot {
    std::optional<DragSourceState> dragSource;
    std::optional<AboveItemState> above;
    bool hasIgnoredItem{false};
    //! `above === ignoredItem` in the shell. JS loose == semantics ride
    //! along for free: when both are null the comparison is TRUE, which
    //! makes SuppressRepeatTarget fire for an external drag over empty
    //! row space (see decideTasksDragMove)
    bool aboveIsIgnored{false};
    //! `hoveredItem === above`
    bool aboveIsHovered{false};
    bool sortIsManual{false};
    //! insert inputs for the reorder branch
    QPointF positionInTarget;
    Qt::Orientation orientation{Qt::Horizontal};
    qreal itemStep{0};
};

enum class TasksDragMoveAction {
    SuppressRepeatTarget, //!< the EITM-fork repeat-target rule: return before anything else
    ReorderDragSource, //!< tasksModel.move(dragSource, moveTo) + record above as ignored + restart the ignore timer
    KeepOrder, //!< manual-sort move branch reached, but the target is the source itself or its current index
    HoverAbove, //!< external drag entered a new delegate: hover it + restart the activation timer
    ClearHover, //!< external drag left all delegates: clear hover + stop the timer
    None //!< no branch fired (e.g. external drag still over the hovered delegate)
};

struct TasksDragMoveDecision {
    TasksDragMoveAction action{TasksDragMoveAction::None};
    //! engaged exactly when action == ReorderDragSource
    std::optional<int> moveTo;
};

//! The Qt5 onDragMove ladder, verbatim branch order. Data pattern worth
//! knowing (pinned by tests): for EXTERNAL drags ignoredItem is only
//! ever null (the ignore bookkeeping is exclusive to internal moves),
//! so over empty row space `null == null` fires SuppressRepeatTarget
//! and ClearHover is unreachable until an internal move sets
//! ignoredItem - hover clears on dragLeave instead. Faithful.
inline TasksDragMoveDecision decideTasksDragMove(const TasksDragMoveSnapshot &snapshot)
{
    //! shell computes aboveIsIgnored as `above === ignoredItem`; both
    //! absent must therefore answer true (JS null == null) ...
    Q_ASSERT(snapshot.above || snapshot.hasIgnoredItem || snapshot.aboveIsIgnored);
    //! ... and a PRESENT above can only be the ignored item when one exists
    Q_ASSERT(!(snapshot.aboveIsIgnored && snapshot.above) || snapshot.hasIgnoredItem);

    if (!snapshot.dragSource && snapshot.aboveIsIgnored) {
        return {TasksDragMoveAction::SuppressRepeatTarget, std::nullopt};
    }

    if (snapshot.dragSource && snapshot.dragSource->isLauncher
            && snapshot.above && snapshot.above->hasModelData && !snapshot.above->isLauncher
            && snapshot.aboveIsIgnored) {
        return {TasksDragMoveAction::SuppressRepeatTarget, std::nullopt};
    }

    if (snapshot.sortIsManual && snapshot.dragSource && !snapshot.hasIgnoredItem) {
        const std::optional<int> aboveIndex = snapshot.above
                ? std::optional<int>{snapshot.above->itemIndex}
                : std::nullopt;
        const int insertAt = insertIndexAt(aboveIndex, snapshot.positionInTarget,
                                           snapshot.orientation, snapshot.itemStep);

        if (!snapshot.dragSource->isAbove && snapshot.dragSource->itemIndex != insertAt) {
            return {TasksDragMoveAction::ReorderDragSource, insertAt};
        }

        return {TasksDragMoveAction::KeepOrder, std::nullopt};
    }

    if (!snapshot.dragSource && snapshot.above && !snapshot.aboveIsHovered) {
        return {TasksDragMoveAction::HoverAbove, std::nullopt};
    }

    if (!snapshot.above) {
        return {TasksDragMoveAction::ClearHover, std::nullopt};
    }

    return {TasksDragMoveAction::None, std::nullopt};
}

}
}

#endif
