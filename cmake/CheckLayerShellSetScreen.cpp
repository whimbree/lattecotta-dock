/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later

    CMake try_compile probe: does LayerShellQt::Window expose
    setScreen(QScreen*)? The method was added in LayerShellQt 6.6 and later
    removed upstream again, so depending on it unconditionally is a real
    build regression (latte-dock-ng hit exactly that). Compiling this file
    defines LATTE_LAYERSHELL_HAS_SET_SCREEN; the fallback is plain
    QWindow::setScreen().
*/

#include <LayerShellQt/window.h>

int main()
{
    LayerShellQt::Window *w = nullptr;
    w->setScreen(nullptr);
    return 0;
}
