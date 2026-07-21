/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "types.h"

#include <QString>
#include <QtGlobal>

#include <optional>

namespace Latte::Tasks {

enum class MiddleClickRowKind {
    Launcher = 0,
    Task
};

enum class MiddleClickOperation {
    None = 0,
    RequestActivate,
    RequestClose,
    RequestNewInstance,
    RequestToggleMinimized,
    CycleOrActivate,
    RequestToggleGrouping
};

struct OfferedMiddleClickAction {
    Types::TaskAction configuredAction{Types::NoneAction};
    MiddleClickOperation taskOperation{MiddleClickOperation::None};
};

//! The exhaustive mapping for the six TaskAction values offered by the
//! middle-click controls. Both the tasks backend and the D-Bus map parser use
//! this classification, so neither boundary can accept an action/operation
//! pair the production handler does not offer.
inline std::optional<OfferedMiddleClickAction> classifyOfferedMiddleClickAction(int value)
{
    switch (value) {
    case Types::NoneAction:
        return OfferedMiddleClickAction{Types::NoneAction, MiddleClickOperation::None};
    case Types::Close:
        return OfferedMiddleClickAction{Types::Close, MiddleClickOperation::RequestClose};
    case Types::NewInstance:
        return OfferedMiddleClickAction{Types::NewInstance, MiddleClickOperation::RequestNewInstance};
    case Types::ToggleMinimized:
        return OfferedMiddleClickAction{Types::ToggleMinimized, MiddleClickOperation::RequestToggleMinimized};
    case Types::CycleThroughTasks:
        return OfferedMiddleClickAction{Types::CycleThroughTasks, MiddleClickOperation::CycleOrActivate};
    case Types::ToggleGrouping:
        return OfferedMiddleClickAction{Types::ToggleGrouping, MiddleClickOperation::RequestToggleGrouping};
    case Types::PresentWindows:
    case Types::PreviewWindows:
    case Types::HighlightWindows:
    case Types::PreviewAndHighlightWindows:
        return std::nullopt;
    }

    return std::nullopt;
}

inline bool middleClickDispatchPairIsValid(MiddleClickRowKind rowKind,
                                           const OfferedMiddleClickAction &action,
                                           MiddleClickOperation operation)
{
    switch (rowKind) {
    case MiddleClickRowKind::Launcher:
        return operation == MiddleClickOperation::RequestActivate;
    case MiddleClickRowKind::Task:
        return operation == action.taskOperation;
    }

    return false;
}

struct MiddleClickDispatchRecord {
    QString rowIdentity;
    MiddleClickRowKind rowKind{MiddleClickRowKind::Launcher};
    Types::TaskAction configuredAction{Types::NoneAction};
    MiddleClickOperation dispatchedOperation{MiddleClickOperation::None};
    qint64 sequence{0};
};

}
