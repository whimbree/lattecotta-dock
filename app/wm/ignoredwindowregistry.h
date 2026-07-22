/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include "windowid.h"

#include <QHash>
#include <QList>
#include <QSet>

namespace Latte {
namespace WindowSystem {

//! Owner-aware ignored-window state. Window tracking changes only when the
//! first owner registers or the last owner unregisters, so one runtime object
//! cannot make a window trackable while another still owns the exclusion.
class IgnoredWindowRegistry
{
public:
    using OwnerToken = quintptr;

    enum class Change
    {
        None,
        BecameIgnored,
        BecameTrackable,
    };

    [[nodiscard]] Change registerOwner(const WindowId &window, const OwnerToken owner)
    {
        if (window.isEmpty() || owner == 0) {
            return Change::None;
        }

        auto &owners = m_owners[window];
        if (owners.contains(owner)) {
            return Change::None;
        }

        const bool firstOwner = owners.isEmpty();
        owners.insert(owner);

        if (firstOwner) {
            m_windows.append(window);
            return Change::BecameIgnored;
        }

        return Change::None;
    }

    [[nodiscard]] Change unregisterOwner(const WindowId &window, const OwnerToken owner)
    {
        auto entry = m_owners.find(window);
        if (entry == m_owners.end() || owner == 0 || !entry->remove(owner)) {
            return Change::None;
        }

        if (!entry->isEmpty()) {
            return Change::None;
        }

        m_owners.erase(entry);
        m_windows.removeAll(window);
        return Change::BecameTrackable;
    }

    [[nodiscard]] Change clear(const WindowId &window)
    {
        if (!m_owners.remove(window)) {
            return Change::None;
        }

        m_windows.removeAll(window);
        return Change::BecameTrackable;
    }

    [[nodiscard]] bool contains(const WindowId &window) const
    {
        return m_owners.contains(window);
    }

    const QList<WindowId> &windows() const
    {
        return m_windows;
    }

private:
    QHash<WindowId, QSet<OwnerToken>> m_owners;
    QList<WindowId> m_windows;
};

} // namespace WindowSystem
} // namespace Latte
