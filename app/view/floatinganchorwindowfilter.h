/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef FLOATINGANCHORWINDOWFILTER_H
#define FLOATINGANCHORWINDOWFILTER_H

#include <QObject>
#include <QPointer>
#include <QWindow>

namespace Latte::ViewPart::FloatingPopupPresentation {

class AnchorWindowEventFilter
{
public:
    explicit AnchorWindowEventFilter(QObject &observer)
        : m_observer(observer)
    {
    }

    AnchorWindowEventFilter(const AnchorWindowEventFilter &) = delete;
    AnchorWindowEventFilter &operator=(
        const AnchorWindowEventFilter &) = delete;

    ~AnchorWindowEventFilter()
    {
        (void)followWindow(nullptr);
    }

    [[nodiscard]] bool followWindow(QWindow *window)
    {
        if (m_window == window) {
            return false;
        }

        if (m_window) {
            m_window->removeEventFilter(&m_observer);
        }

        m_window = window;
        if (m_window) {
            m_window->installEventFilter(&m_observer);
        }
        return true;
    }

    [[nodiscard]] bool observes(const QObject *object) const
    {
        return object && object == m_window;
    }

private:
    QObject &m_observer;
    QPointer<QWindow> m_window;
};

} // namespace Latte::ViewPart::FloatingPopupPresentation

#endif
