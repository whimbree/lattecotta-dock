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
    ExportTemplate,
    MoveToLayout,
    Remove,
};

enum class Role
{
    Original,
    Clone,
};

//! Screen-group clones are implementation members of an original dock. Their
//! lifecycle and placement are controlled by that original, so actions that
//! create, move, export, or destroy an independent dock apply only to original
//! views. An All Screens original is still an original here: duplicating it
//! creates a separate original and that original creates its own clone set.
constexpr bool permits(const Role role, const Action action)
{
    switch (action) {
    case Action::Duplicate:
    case Action::ExportTemplate:
    case Action::MoveToLayout:
    case Action::Remove:
        return role == Role::Original;
    }

    return false;
}

} // namespace ViewActionPolicy
} // namespace Latte
