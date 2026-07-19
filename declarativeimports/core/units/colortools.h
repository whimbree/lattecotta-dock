/*
    SPDX-FileCopyrightText: 2018 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef LATTECOLORTOOLSCORE_H
#define LATTECOLORTOOLSCORE_H

// EX-19 ColorLuminance (docs/tracking/QML_EXTRACTION_PLAN.md): the single authority
// for the W3C color brightness/luminance math that previously lived in nine
// copies - three ColorizerTools.js files, four inline QML twins (default
// indicator, LatteIndicator, ShortcutBadge, LatteDockConfiguration) and two
// float C++ copies (Latte::Tools, app commontools). The QML-facing shell is
// the LatteCore Tools singleton (../tools.h); pre-dedup divergences and the
// precision arbitration are recorded in docs/agent-logs/EX-19.md.
//
// Semantics are the Qt5-inherited JS copies' EXACTLY (the dedup spec): all
// math in double, and the QColor entry points read channels through
// QColor::*F() - a float - because QML's color.r IS redF() widened to
// double. That float quantization is pinned behavior: grey #7f7f7f has
// brightness 127.00000002980232, not 127, and the reference table in
// tests/units/colortoolstest.cpp asserts it bit-for-bit against values the
// shipped JS produced. "Simplifying" the QColor entries to integer
// channels (the old C++ copies' reading) would silently shift every
// consumer's value by ~1e-5 relative and break that table.

// Qt
#include <QColor>

#include <cmath>

namespace Latte {
namespace ColorTools {

//! AERT brightness (https://www.w3.org/TR/AERT/#color-contrast), channels
//! in the 0..255 domain. 0 is black, 255 is white; the tree-wide inherited
//! light/dark split compares the result against 127.5.
constexpr double colorBrightnessFromRGB(double r, double g, double b)
{
    Q_ASSERT(r >= 0.0 && r <= 255.0);
    Q_ASSERT(g >= 0.0 && g <= 255.0);
    Q_ASSERT(b >= 0.0 && b <= 255.0);

    return (r * 299 + g * 587 + b * 114) / 1000;
}

//! QML color.r semantics: channels arrive as redF()*255 (float-quantized,
//! see the header comment). Invalid QColor is a producer bug - the QML
//! boundary (Tools singleton) refuses it loudly before reaching here.
inline double colorBrightness(const QColor &color)
{
    Q_ASSERT(color.isValid());

    return colorBrightnessFromRGB(static_cast<double>(color.redF()) * 255,
                                  static_cast<double>(color.greenF()) * 255,
                                  static_cast<double>(color.blueF()) * 255);
}

//! Integer-channel entry for QRgb pixel data (the backgroundcache scanline
//! loop). Exact for 8-bit channels; agrees with the QColor entry to the
//! float-quantization delta (~1e-5 relative), pinned by test.
constexpr double colorBrightness(QRgb rgb)
{
    return colorBrightnessFromRGB(qRed(rgb), qGreen(rgb), qBlue(rgb));
}

//! WCAG 2.0 relative luminance
//! (https://www.w3.org/TR/2008/REC-WCAG20-20081211/#relativeluminancedef),
//! channels in the 0..1 domain. Note the inherited curve is discontinuous
//! at the 0.03928 branch point (the linear value just below it exceeds the
//! power value just above); the boundary rows in the test pin both sides.
inline double colorLuminaFromRGB(double r, double g, double b)
{
    Q_ASSERT(r >= 0.0 && r <= 1.0);
    Q_ASSERT(g >= 0.0 && g <= 1.0);
    Q_ASSERT(b >= 0.0 && b <= 1.0);

    const double rS = (r <= 0.03928) ? (r / 12.92) : std::pow((r + 0.055) / 1.055, 2.4);
    const double gS = (g <= 0.03928) ? (g / 12.92) : std::pow((g + 0.055) / 1.055, 2.4);
    const double bS = (b <= 0.03928) ? (b / 12.92) : std::pow((b + 0.055) / 1.055, 2.4);

    return 0.2126 * rS + 0.7152 * gS + 0.0722 * bS;
}

//! QML color.r semantics, as colorBrightness above.
inline double colorLumina(const QColor &color)
{
    Q_ASSERT(color.isValid());

    return colorLuminaFromRGB(static_cast<double>(color.redF()),
                              static_cast<double>(color.greenF()),
                              static_cast<double>(color.blueF()));
}

//! The tree-wide light/dark split: strictly greater, exactly the inherited
//! `colorBrightness(c) > 127.5` QML sites (127.5 is the AERT midpoint).
inline bool isLight(const QColor &color, double threshold = 127.5)
{
    Q_ASSERT(color.isValid());

    return colorBrightness(color) > threshold;
}

// the endpoints are knowable at compile time - pin them here so a formula
// typo cannot even build (assert-early law; brightness weights sum to 1000)
static_assert(colorBrightnessFromRGB(0, 0, 0) == 0.0);
static_assert(colorBrightnessFromRGB(255, 255, 255) == 255.0);
static_assert(colorBrightness(QRgb(0xff000000)) == 0.0);
static_assert(colorBrightness(QRgb(0xffffffff)) == 255.0);

}
}

#endif
