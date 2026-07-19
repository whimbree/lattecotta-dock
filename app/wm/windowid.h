/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

// Qt
#include <QByteArray>
#include <QDebug>
#include <QMetaType>

// C++
#include <optional>
#include <utility>

namespace Latte {
namespace WindowSystem {

//! Window-system id newtype (the WindowId newtype hardening item in
//! docs/tracking/PORTING_PLAN.md). Storage stays the QByteArray that 8e8cdf31
//! chose: wayland carries PlasmaWindow::uuid() bytes verbatim, X11
//! carries the numeric WId as a decimal string, and the empty id is the
//! one no-window value on both platforms. What the newtype adds over the
//! bare alias is boundary discipline - construction only through the
//! named factories (no QByteArray or char* converts in by accident), and
//! the X11-side parse returns std::optional so a malformed id is a
//! type-enforced absence instead of the silent window 0 an ok-flag-less
//! toUInt() produced.
class WindowId
{
public:
    //! the no-window id; isEmpty() answers true
    WindowId() = default;

    static WindowId fromWaylandUuid(const QByteArray &uuid)
    {
        return WindowId(uuid);
    }

    //! 0 is X11 None, not a window: it maps to the empty id
    static WindowId fromX11WId(quint64 wid)
    {
        return wid ? WindowId(QByteArray::number(qulonglong(wid))) : WindowId();
    }

    //! checked X11-side parse: nullopt for the empty id, for bytes that
    //! are not a pure decimal number (e.g. a wayland uuid crossing
    //! backends), for values past quint32, and for a parsed 0 (X11 None
    //! is not a window) - window 0 is unproducible from this type
    std::optional<quint32> toX11WId() const
    {
        if (m_id.isEmpty()) {
            return std::nullopt;
        }

        bool ok{false};
        const quint32 parsed = m_id.toUInt(&ok);

        if (!ok || parsed == 0) {
            return std::nullopt;
        }

        return parsed;
    }

    bool isEmpty() const
    {
        return m_id.isEmpty();
    }

    //! boundary escape hatch: the QML-facing winId property ships these
    //! bytes inside a QVariant (8e8cdf31's decision holds - Qt6 exposes
    //! a QByteArray to QML as an ArrayBuffer), and logging prints them
    const QByteArray &bytes() const
    {
        return m_id;
    }

    //! equality, ordering and hashing are byte-wise - exactly the
    //! QByteArray semantics the QMap/QHash keys and QList lookups relied
    //! on before the newtype, so the substrate swap preserves container
    //! behavior
    friend bool operator==(const WindowId &left, const WindowId &right) = default;

    friend bool operator<(const WindowId &left, const WindowId &right)
    {
        return left.m_id < right.m_id;
    }

    friend size_t qHash(const WindowId &wid, size_t seed = 0)
    {
        return qHash(wid.m_id, seed);
    }

private:
    explicit WindowId(QByteArray id)
        : m_id(std::move(id))
    {
    }

    QByteArray m_id;
};

inline QDebug operator<<(QDebug dbg, const WindowId &wid)
{
    QDebugStateSaver saver(dbg);
    dbg.nospace() << "WindowId(" << wid.bytes() << ")";
    return dbg;
}

//! Parse a WindowId for X11 use at an API boundary. The empty id is the
//! documented no-window value and refuses quietly - a deliberate
//! contract, not a swallowed failure. Malformed non-empty bytes mean a
//! foreign id reached X11 code: that is a defect, refused loudly with
//! the refusing operation named.
inline std::optional<quint32> parseX11WindowId(const WindowId &wid, const char *operation)
{
    if (wid.isEmpty()) {
        return std::nullopt;
    }

    const std::optional<quint32> parsed = wid.toX11WId();

    if (!parsed) {
        qWarning() << operation << "- refusing malformed X11 window id" << wid;
        return std::nullopt;
    }

    return parsed;
}

}
}

Q_DECLARE_METATYPE(Latte::WindowSystem::WindowId)
