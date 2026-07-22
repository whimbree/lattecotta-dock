/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

namespace Latte {
namespace ViewActionPolicy {

enum class Action
{
    Duplicate,
    CreateLinked,
    ExportTemplate,
    MoveToLayout,
    Remove,
};

enum class Role
{
    Original,
    ScreenGroupReplica,
    ExplicitLinkedMember,
};

//! Screen-group replicas are implementation members of an original dock.
//! Moving, exporting, or removing one member would escape that relationship,
//! so those actions stay owned by the original. Explicit linked members own
//! placement and removal but cannot move away from the root's layout. Duplicate
//! breaks either relationship, while Create Linked extends it from any member.
constexpr bool permits(const Role role, const Action action)
{
    switch (action) {
    case Action::Duplicate:
    case Action::CreateLinked:
        return true;
    case Action::ExportTemplate:
    case Action::MoveToLayout:
        return role == Role::Original;
    case Action::Remove:
        return role != Role::ScreenGroupReplica;
    }

    return false;
}

} // namespace ViewActionPolicy
} // namespace Latte
