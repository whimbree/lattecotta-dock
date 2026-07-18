/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "tools.h"

// local
#include "units/colortools.h"

// These three loudly refuse an invalid QColor at the QML->C++ boundary rather
// than silently returning a wrong brightness: an invalid color arriving here
// is a caller bug worth seeing, not noise to swallow. A startup BURST of such
// refusals used to be expected and benign (root-caused 2026-07-16 with a V4
// caller trace, session-handoff has the method): creation-time QML bindings
// read Kirigami.Theme colors (directly, or through the colorizer/colorPalette
// chain they feed) before the attached PlatformTheme resolved its palette, so
// the getter handed over a default-constructed invalid QColor, self-corrected
// a beat later by the theme's change notify. Those call sites now guard the
// brightness call on color validity (D14, docs/known-defects.md), so this
// boundary is SILENT at startup. Any refusal now is a genuine invalid color
// and deserves a fresh hunt, not a shrug.

namespace Latte{

Tools::Tools(QObject *parent)
    : QObject(parent)
{
}

double Tools::colorBrightness(QColor color)
{
    if (!color.isValid()) {
        qCritical("Tools.colorBrightness: invalid color from QML, returning 0 (dark)");
        return 0.0;
    }

    return ColorTools::colorBrightness(color);
}

double Tools::colorLumina(QColor color)
{
    if (!color.isValid()) {
        qCritical("Tools.colorLumina: invalid color from QML, returning 0 (dark)");
        return 0.0;
    }

    return ColorTools::colorLumina(color);
}

bool Tools::isLight(QColor color, double threshold)
{
    if (!color.isValid()) {
        qCritical("Tools.isLight: invalid color from QML, returning false (dark)");
        return false;
    }

    return ColorTools::isLight(color, threshold);
}

}
