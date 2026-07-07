/*
    SPDX-FileCopyrightText: 2016 Smith AR <audoban@openmailbox.org>
    SPDX-FileCopyrightText: 2016 Michail Vourlakos <mvourlakos@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "quickwindowsystem.h"

#include "config-latte-lib.h"

// Qt
#include <QDebug>

// KDE
#include <KWindowSystem>
#if HAVE_X11
#include <KX11Extras>
#endif

namespace Latte {

QuickWindowSystem::QuickWindowSystem(QObject *parent)
    : QObject(parent)
{
    if (KWindowSystem::isPlatformWayland()) {
        //! the wayland compositor is the display server, compositing is unconditional
        m_compositing = true;
    }
#if HAVE_X11
    else {
        connect(KX11Extras::self(), &KX11Extras::compositingChanged
        , this, [&](bool enabled) {
            if (m_compositing == enabled)
                return;

            m_compositing = enabled;
            Q_EMIT compositingChanged();
        });

        m_compositing = KX11Extras::compositingActive();
    }
#endif
}

QuickWindowSystem::~QuickWindowSystem()
{
    qDebug() << staticMetaObject.className() << "destructed";
}

bool QuickWindowSystem::compositingActive() const
{
    return m_compositing;
}

bool QuickWindowSystem::isPlatformWayland() const
{
    return KWindowSystem::isPlatformWayland();
}

bool QuickWindowSystem::isPlatformX11() const
{
    return KWindowSystem::isPlatformX11();
}

} //end of namespace
