/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef FLOATINGMASKHANDSHAKE_H
#define FLOATINGMASKHANDSHAKE_H

#include <QRect>
#include <QtGlobal>

#include <mutex>
#include <utility>

namespace Latte::ViewPart::FloatingMaskHandshake {

class RenderBridge
{
public:
    void publish(quint64 generation)
    {
        const std::scoped_lock lock{m_mutex};
        if (!m_closed) {
            m_nextGeneration = generation;
        }
    }

    [[nodiscard]] bool synchronizeForFrame()
    {
        const std::scoped_lock lock{m_mutex};
        if (m_closed) {
            return false;
        }

        m_synchronizedGeneration = m_nextGeneration;
        return true;
    }

    template<typename Post>
    [[nodiscard]] bool afterFrame(Post &&post)
    {
        const std::scoped_lock lock{m_mutex};
        if (m_closed) {
            return false;
        }

        // Keep the mutex through posting. close() therefore waits until an
        // in-flight render callback has stopped using its Effects receiver,
        // while late callbacks retain only this shared bridge and refuse.
        std::forward<Post>(post)(m_synchronizedGeneration);
        return true;
    }

    void close()
    {
        const std::scoped_lock lock{m_mutex};
        m_closed = true;
    }

private:
    std::mutex m_mutex;
    quint64 m_nextGeneration{0};
    quint64 m_synchronizedGeneration{0};
    bool m_closed{false};
};

class State
{
public:
    [[nodiscard]] quint64 arm(const QRect &exactMask)
    {
        m_presentationOwnsInput = true;
        m_pending = true;
        m_exactMask = exactMask;
        return ++m_generation;
    }

    [[nodiscard]] quint64 adoptExact(const QRect &exactMask)
    {
        m_presentationOwnsInput = true;
        m_pending = false;
        m_exactMask = exactMask;
        return ++m_generation;
    }

    [[nodiscard]] quint64 transferToLegacy()
    {
        m_presentationOwnsInput = false;
        m_pending = false;
        m_exactMask = {};
        return ++m_generation;
    }

    [[nodiscard]] bool canCollapse(quint64 submittedGeneration,
                                   const QRect &currentExactMask) const
    {
        return m_presentationOwnsInput
            && m_pending
            && submittedGeneration == m_generation
            && currentExactMask == m_exactMask;
    }

    [[nodiscard]] quint64 generationForNextFrame() const
    {
        return m_generation;
    }

    [[nodiscard]] quint64 generation() const
    {
        return m_generation;
    }

    [[nodiscard]] bool pending() const
    {
        return m_pending;
    }

    void complete()
    {
        m_pending = false;
    }

private:
    quint64 m_generation{0};
    QRect m_exactMask;
    bool m_presentationOwnsInput{false};
    bool m_pending{false};
};

} // namespace Latte::ViewPart::FloatingMaskHandshake

#endif
