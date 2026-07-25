/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PANELSHADOWSTATE_H
#define PANELSHADOWSTATE_H

#include <QHash>
#include <QMargins>

#include <KSvg/FrameSvg>

#include <optional>

namespace Latte::ViewPart::PanelShadowState {

struct State {
    KSvg::FrameSvg::EnabledBorders enabledBorders{
        KSvg::FrameSvg::AllBorders};
    QMargins extraPadding;

    friend bool operator==(const State &, const State &) = default;
};

enum class Update {
    Inserted,
    Changed,
    Unchanged,
};

template<typename Window>
class Registry
{
public:
    [[nodiscard]] Update update(Window window, const State &state)
    {
        auto existing = m_states.find(window);
        if (existing == m_states.end()) {
            m_states.insert(window, state);
            return Update::Inserted;
        }
        if (existing.value() == state) {
            return Update::Unchanged;
        }

        existing.value() = state;
        return Update::Changed;
    }

    [[nodiscard]] std::optional<State> stateFor(Window window) const
    {
        const auto existing = m_states.constFind(window);
        return existing == m_states.cend()
            ? std::nullopt
            : std::optional<State>{existing.value()};
    }

    [[nodiscard]] bool remove(Window window)
    {
        return m_states.remove(window) != 0;
    }

    [[nodiscard]] bool isEmpty() const
    {
        return m_states.isEmpty();
    }

    [[nodiscard]] const QHash<Window, State> &states() const
    {
        return m_states;
    }

private:
    QHash<Window, State> m_states;
};

} // namespace Latte::ViewPart::PanelShadowState

#endif
