/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, derived)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! Pure pixel-scanline math behind PanelBackground's plasma-theme
//! analysis (EX-25 in docs/tracking/QML_EXTRACTION_PLAN.md): max opacity of the
//! center element, corner roundness from mask or shadow corners, and
//! the shadow band's size and color. The bodies are Michail's Qt5
//! algorithms, extracted along the seam from David Goree's latte-dock-qt6
//! (app/plasma/extended/panelbackgroundscan.h at 15a317ff,
//! github.com/CaptSilver/latte-dock-qt6) after diffing their unit against
//! our panelbackground.cpp.
//! Deliberate divergences from the Qt5 bodies, all pinned by
//! tests/units/panelbackgroundscantest.cpp:
//! - the corner loops keep the exclusive row bound of 39c6517d (Qt5
//!   read scanLine(height()), one row past the image);
//! - widths come from the image instead of the CENTERWIDTH constant,
//!   so the functions are total over any image the tests construct;
//! - empty images are refused loudly with a neutral result instead of
//!   dereferencing a null scanline (see refusesEmptyImage).
//! Every KSvg/Theme concern stays in the PanelBackground adapter:
//! element lookup, corner/border id choice, hasShadow gating, the
//! theme-declared shadow size, signal emission.

#pragma once

// Qt
#include <QColor>
#include <QImage>
#include <QtGlobal>

// C++
#include <optional>

namespace Latte {
namespace PlasmaExtended {
namespace PanelBackgroundScan {

//! Which corner image the caller fetched. The adapter maps the panel
//! location to it: BottomEdge/RightEdge panels analyze the theme's
//! topleft corner, TopEdge/LeftEdge panels the bottomright one.
enum class Corner {
    TopLeft,
    BottomRight
};

//! Scan direction for the shadow border image. Horizontal panels
//! (Bottom/Top edge) scan column 0 down all rows; vertical panels
//! (Left/Right edge) scan row 0 across all columns.
enum class BandOrientation {
    Horizontal,
    Vertical
};

//! Result of measuring a panel border's shadow band. color is nullopt
//! when the border has no pixel with alpha > 0 - the "no shadow drawn"
//! state is unrepresentable as a color value, the adapter decides what
//! an absent color means (Qt5 keeps the previous one).
struct ShadowBand {
    int size{0};
    std::optional<QColor> color;
};

//! Theme SVGs are outside input: an element can be missing or
//! zero-sized (a mask theme may ship mask-topleft without
//! mask-bottomright), which hands the scans a null image whose
//! scanLine() is nullptr. Refuse that loudly here instead of
//! dereferencing it; the callers return their neutral result.
inline bool refusesEmptyImage(const QImage &img, const char *scan)
{
    if (!img.isNull()) {
        return false;
    }

    qCritical("PanelBackgroundScan: %s received an empty image (missing or zero-sized theme element) - returning the neutral result", scan);
    return true;
}

//! qAlpha() is only meaningful on premultiplied ARGB data; KSvg
//! usually hands that over already, so this converts only when needed.
inline QImage ensurePremultiplied(const QImage &img)
{
    if (img.format() == QImage::Format_ARGB32_Premultiplied) {
        return img;
    }

    return img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
}

//! Average alpha over the first min(2, height) rows of the center
//! element. Two rows because Oxygen's center carries different opacity
//! levels in the same element and a single row misreads it. The result
//! is floored at 0.01: a fully transparent background that only draws
//! a border must not report 0% opacity, or every later calculation
//! hands back a fully transparent plasma svg.
inline float measureMaxOpacity(const QImage &center)
{
    if (refusesEmptyImage(center, "measureMaxOpacity")) {
        return 0.01f;
    }

    const QImage img = ensurePremultiplied(center);
    const int rows = qMin(2, img.height());

    float alphasum{0};

    for (int row = 0; row < rows; ++row) {
        //! constScanLine everywhere in this unit: img shares data with the
        //! caller's image, and the non-const scanLine() would detach (deep
        //! copy) the whole image on the first call
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(row));

        for (int col = 0; col < img.width(); ++col) {
            alphasum += ((float)qAlpha(line[col]) / (float)255);
        }
    }

    return qMax(0.01f, alphasum / (float)(rows * img.width()));
}

//! Count the roundness lines of a mask corner: how many rows taper off
//! the corner's base line. Returns 0 for a square corner, for a corner
//! whose outermost point is not fully transparent (no roundness at
//! all), or for an empty image.
inline int measureRoundnessFromMaskCorner(const QImage &corner, Corner which)
{
    if (refusesEmptyImage(corner, "measureRoundnessFromMaskCorner")) {
        return 0;
    }

    const QImage img = ensurePremultiplied(corner);

    const int baseRow = (which == Corner::TopLeft ? img.height() - 1 : 0);
    const int baseCol = (which == Corner::TopLeft ? img.width() - 1 : 0);

    int baseLineLength = 0;
    int roundnessLines = 0;

    if (which == Corner::TopLeft) {
        //! TOPLEFT corner
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(baseRow));
        const QRgb basePoint = line[baseCol];

        const QRgb *isRoundedLine = reinterpret_cast<const QRgb *>(img.constScanLine(0));
        const QRgb isRoundedPoint = isRoundedLine[0];

        //! If there is roundness, if that point is not fully transparent then
        //! there is no roundness
        if (qAlpha(isRoundedPoint) == 0) {
            if (qAlpha(basePoint) > 0) {
                //! calculate the mask baseLine length
                for (int c = baseCol; c >= 0; --c) {
                    const QRgb point = line[c];

                    if (qAlpha(point) > 0) {
                        baseLineLength++;
                    } else {
                        break;
                    }
                }
            }

            if (baseLineLength > 0) {
                int headLimitR = baseRow;
                int tailLimitR = baseRow;

                for (int r = baseRow - 1; r >= 0; --r) {
                    const QRgb *rline = reinterpret_cast<const QRgb *>(img.constScanLine(r));
                    const QRgb fpoint = rline[baseCol];

                    if (qAlpha(fpoint) == 0) {
                        //! a line that is not part of the roundness because its first pixel is fully transparent
                        break;
                    }

                    headLimitR = r;
                }

                const int c = qMax(0, img.width() - baseLineLength);

                for (int r = baseRow - 1; r >= 0; --r) {
                    const QRgb *rline = reinterpret_cast<const QRgb *>(img.constScanLine(r));
                    const QRgb point = rline[c];

                    if (qAlpha(point) != 255) {
                        tailLimitR = r;
                        break;
                    }
                }

                if (headLimitR != tailLimitR) {
                    roundnessLines = tailLimitR - headLimitR + 1;
                }
            }
        }
    } else {
        //! BOTTOMRIGHT CORNER
        //! it should be TOPRIGHT corner in that case
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(baseRow));
        const QRgb basePoint = line[baseCol];

        const QRgb *isRoundedLine = reinterpret_cast<const QRgb *>(img.constScanLine(img.height() - 1));
        const QRgb isRoundedPoint = isRoundedLine[img.width() - 1];

        //! If there is roundness, if that point is not fully transparent then
        //! there is no roundness
        if (qAlpha(isRoundedPoint) == 0) {
            if (qAlpha(basePoint) > 0) {
                //! calculate the mask baseLine length
                for (int c = baseCol; c < img.width(); ++c) {
                    const QRgb point = line[c];

                    if (qAlpha(point) > 0) {
                        baseLineLength++;
                    } else {
                        break;
                    }
                }
            }

            if (baseLineLength > 0) {
                int headLimitR = 0;
                int tailLimitR = 0;

                //! r stays < height(): scanLine() rows end at height()-1 and these
                //! loops read the row before their break checks (39c6517d)
                for (int r = baseRow + 1; r < img.height(); ++r) {
                    const QRgb *rline = reinterpret_cast<const QRgb *>(img.constScanLine(r));
                    const QRgb fpoint = rline[baseCol];

                    if (qAlpha(fpoint) == 0) {
                        //! a line that is not part of the roundness because its first pixel is not transparent
                        break;
                    }

                    headLimitR = r;
                }

                const int c = baseLineLength - 1;

                for (int r = baseRow + 1; r < img.height(); ++r) {
                    const QRgb *rline = reinterpret_cast<const QRgb *>(img.constScanLine(r));
                    const QRgb point = rline[c];

                    if (qAlpha(point) != 255) {
                        tailLimitR = r;
                        break;
                    }
                }

                if (headLimitR != tailLimitR) {
                    roundnessLines = headLimitR - tailLimitR + 1;
                }
            }
        }
    }

    return roundnessLines;
}

//! Count the roundness lines of a shadow corner.
//! 1.  The caller chose which corner shadow based on panel location
//! 2.  For that corner discover the maxOpacity (most solid shadow point) and
//!     how many pixels (distance) it is to the most solid point, that is
//!     called [baseLineLength]
//! 3.  After [2] the algorithm for each next line calculates the maxOpacity
//!     for that line and how many points are needed to reach there. If the
//!     points to reach the line max opacity are shorter than baseLineLength
//!     then that line is considered part of the roundness
//! 3.1 Avoid zig-zag cases such as the Air plasma theme case. When the shadow
//!     is not following a straight line until reaching the rounded part the
//!     algorithm is considering as valid roundness only the last part of the
//!     discovered roundness and ignores all the previous.
//! 4.  Calculating the lines that are shorter than the baseline provides
//!     the discovered roundness
inline int measureRoundnessFromShadowCorner(const QImage &corner, Corner which)
{
    if (refusesEmptyImage(corner, "measureRoundnessFromShadowCorner")) {
        return 0;
    }

    const QImage img = ensurePremultiplied(corner);

    const int baseRow = (which == Corner::TopLeft ? img.height() - 1 : 0);
    const int baseCol = (which == Corner::TopLeft ? img.width() - 1 : 0);

    int baseLineLength = 0;
    int roundnessLines = 0;

    if (which == Corner::TopLeft) {
        //! TOPLEFT corner
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(baseRow));
        const QRgb basePoint = line[baseCol];

        int baseShadowMaxOpacity = 0;

        if (qAlpha(basePoint) == 0) {
            //! calculate the shadow maxOpacity in the base line
            //! and number of pixels to reach there
            for (int c = baseCol; c >= 0; --c) {
                const QRgb point = line[c];

                if (qAlpha(point) > baseShadowMaxOpacity) {
                    baseShadowMaxOpacity = qAlpha(point);
                    baseLineLength = (baseCol - c + 1);
                }
            }
        }

        if (baseLineLength > 0) {
            for (int r = baseRow - 1; r >= 0; --r) {
                const QRgb *rline = reinterpret_cast<const QRgb *>(img.constScanLine(r));
                const QRgb fpoint = rline[baseCol];

                if (qAlpha(fpoint) != 0) {
                    //! a line that is not part of the roundness because its first pixel is not transparent
                    break;
                }

                int transPixels = 0;
                int rowMaxOpacity = 0;

                for (int c = baseCol; c >= 0; --c) {
                    const QRgb point = rline[c];

                    if (qAlpha(point) > rowMaxOpacity) {
                        rowMaxOpacity = qAlpha(point);
                        continue;
                    }
                }

                for (int c = baseCol; c >= (baseCol - baseLineLength + 1); --c) {
                    const QRgb point = rline[c];

                    if (qAlpha(point) != rowMaxOpacity) {
                        transPixels++;
                        continue;
                    }

                    if (transPixels != baseLineLength) {
                        roundnessLines++;
                        break;
                    }
                }

                if (transPixels == baseLineLength) {
                    //! 3.1 avoid zig-zag shadows Air plasma theme case
                    roundnessLines = 0;
                }
            }
        }
    } else {
        //! BOTTOMRIGHT CORNER
        //! it should be TOPRIGHT corner in that case
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(baseRow));
        const QRgb basePoint = line[baseCol];

        int baseShadowMaxOpacity = 0;

        if (qAlpha(basePoint) == 0) {
            //! calculate the base line transparent pixels
            for (int c = baseCol; c < img.width(); ++c) {
                const QRgb point = line[c];

                if (qAlpha(point) > baseShadowMaxOpacity) {
                    baseShadowMaxOpacity = qAlpha(point);
                    baseLineLength = c + 1;
                }
            }
        }

        if (baseLineLength > 0) {
            //! r stays < height() for the same scanLine bound reason as the
            //! mask corner loops (39c6517d)
            for (int r = baseRow + 1; r < img.height(); ++r) {
                const QRgb *rline = reinterpret_cast<const QRgb *>(img.constScanLine(r));
                const QRgb fpoint = rline[baseCol];

                if (qAlpha(fpoint) != 0) {
                    //! a line that is not part of the roundness because its first pixel is not transparent
                    break;
                }

                int transPixels = 0;
                int rowMaxOpacity = 0;

                for (int c = baseCol; c < img.width(); ++c) {
                    const QRgb point = rline[c];

                    if (qAlpha(point) > rowMaxOpacity) {
                        rowMaxOpacity = qAlpha(point);
                        baseLineLength = c + 1;
                    }
                }

                for (int c = baseCol; c < baseLineLength; ++c) {
                    const QRgb point = rline[c];

                    if (qAlpha(point) != rowMaxOpacity) {
                        transPixels++;
                        continue;
                    }

                    if (transPixels != baseLineLength) {
                        roundnessLines++;
                        break;
                    }
                }

                if (transPixels == baseLineLength) {
                    //! 3.1 avoid zig-zag shadows Air plasma theme case
                    roundnessLines = 0;
                }
            }
        }
    }

    return roundnessLines;
}

//! Measure a border image's shadow band: the size is the span between
//! the first and last visible pixel along the scan axis (the svg's
//! declared element size may pad fully transparent edges, so the
//! adapter takes the max of this and the theme-declared size), and the
//! color is the most opaque pixel found anywhere in the border. The
//! color components are read from the premultiplied data as-is - Qt5
//! behavior, kept.
inline ShadowBand measureShadowBand(const QImage &border, BandOrientation orientation)
{
    if (refusesEmptyImage(border, "measureShadowBand")) {
        return {};
    }

    const QImage img = ensurePremultiplied(border);

    int firstPixel{-1};
    int lastPixel{-1};

    if (orientation == BandOrientation::Horizontal) {
        for (int y = 0; y < img.height(); ++y) {
            const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(y));
            const QRgb pixel = line[0];

            if (qAlpha(pixel) > 0) {
                if (firstPixel < 0) {
                    firstPixel = y;
                }
                lastPixel = y;
            }
        }
    } else {
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(0));

        for (int x = 0; x < img.width(); ++x) {
            const QRgb pixel = line[x];

            if (qAlpha(pixel) > 0) {
                if (firstPixel < 0) {
                    firstPixel = x;
                }
                lastPixel = x;
            }
        }
    }

    ShadowBand band;
    band.size = (firstPixel >= 0 ? qMax(0, lastPixel - firstPixel + 1) : 0);

    //! find maximum shadow color applied
    int maxopacity{0};

    for (int r = 0; r < img.height(); ++r) {
        const QRgb *line = reinterpret_cast<const QRgb *>(img.constScanLine(r));

        for (int c = 0; c < img.width(); ++c) {
            const QRgb pixel = line[c];

            if (qAlpha(pixel) > maxopacity) {
                maxopacity = qAlpha(pixel);
                QColor color(pixel);
                color.setAlpha(qMin(255, maxopacity));
                band.color = color;
            }
        }
    }

    return band;
}

} // namespace PanelBackgroundScan
} // namespace PlasmaExtended
} // namespace Latte
