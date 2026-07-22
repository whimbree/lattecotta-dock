/*
    SPDX-FileCopyrightText: 2018 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PLACEMENTSTATE_H
#define PLACEMENTSTATE_H

// Qt
#include <QtGlobal>

// C++
#include <algorithm>
#include <cmath>

namespace Latte {
namespace Settings {
namespace PlacementState {

//! This core owns one dock's persisted placement invariant only. It does not
//! consume peer footprints: a bottom dock's alignment currently changes a
//! vertical neighbor's available height and therefore its local AutoSize
//! result. That confirmed cross-dock chain belongs to the forthcoming explicit
//! output-edge solver, together with same-edge rank and cumulative reservation.
//! Adding peer corrections here would preserve the current split reservation
//! model instead of giving it one owner.

//! Physical output edge. The closed enum keeps floating and unknown locations
//! out of the pure placement domain; QML-facing bridges refuse those values.
enum class OutputEdge {
    Top,
    Right,
    Bottom,
    Left
};

//! Alignment along the output edge. Start and End carry the same inward-offset
//! semantics on both axes; physical Left/Right and Top/Bottom names exist only
//! at the boundary.
enum class Alignment {
    Start,
    Center,
    End,
    Justify
};

//! Physical alignment used to translate the semantic state back to Latte's
//! persisted enum. A vertical edge maps Start/End to Top/Bottom; a horizontal
//! edge maps them to Left/Right.
enum class PhysicalAlignment {
    Left,
    Right,
    Top,
    Bottom,
    Center,
    Justify
};

struct RequestedPlacement {
    OutputEdge edge;
    Alignment alignment;
    double minLengthPercent;
    double maxLengthPercent;
    double offsetPercent;
};

struct OutputGeometry {
    double x;
    double y;
    double width;
    double height;
};

struct PrimaryExtent {
    double origin;
    double length;
};

constexpr bool isHorizontal(OutputEdge edge)
{
    return edge == OutputEdge::Top || edge == OutputEdge::Bottom;
}

constexpr Alignment semanticAlignmentOf(PhysicalAlignment alignment)
{
    switch (alignment) {
    case PhysicalAlignment::Left:
    case PhysicalAlignment::Top:
        return Alignment::Start;
    case PhysicalAlignment::Right:
    case PhysicalAlignment::Bottom:
        return Alignment::End;
    case PhysicalAlignment::Center:
        return Alignment::Center;
    case PhysicalAlignment::Justify:
        return Alignment::Justify;
    }

    Q_UNREACHABLE_RETURN(Alignment::Center);
}

constexpr PhysicalAlignment physicalAlignmentFor(OutputEdge edge, Alignment alignment)
{
    switch (alignment) {
    case Alignment::Start:
        return isHorizontal(edge) ? PhysicalAlignment::Left : PhysicalAlignment::Top;
    case Alignment::Center:
        return PhysicalAlignment::Center;
    case Alignment::End:
        return isHorizontal(edge) ? PhysicalAlignment::Right : PhysicalAlignment::Bottom;
    case Alignment::Justify:
        return PhysicalAlignment::Justify;
    }

    Q_UNREACHABLE_RETURN(PhysicalAlignment::Center);
}

//! A placement whose percent values satisfy the on-output invariant. The
//! constructor is private so live consumers cannot accidentally treat an
//! unchecked configuration snapshot as solved placement state.
class NormalizedPlacement
{
public:
    OutputEdge edge() const { return m_edge; }
    Alignment alignment() const { return m_alignment; }
    double minLengthPercent() const { return m_minLengthPercent; }
    double maxLengthPercent() const { return m_maxLengthPercent; }
    double offsetPercent() const { return m_offsetPercent; }

    friend bool operator==(const NormalizedPlacement &, const NormalizedPlacement &) = default;

private:
    friend NormalizedPlacement normalize(RequestedPlacement);

    NormalizedPlacement(OutputEdge edge, Alignment alignment,
                        double minLengthPercent, double maxLengthPercent,
                        double offsetPercent)
        : m_edge(edge),
          m_alignment(alignment),
          m_minLengthPercent(minLengthPercent),
          m_maxLengthPercent(maxLengthPercent),
          m_offsetPercent(offsetPercent)
    {
    }

    OutputEdge m_edge;
    Alignment m_alignment;
    double m_minLengthPercent;
    double m_maxLengthPercent;
    double m_offsetPercent;
};

//! The single placement normalization transaction. Maximum and minimum are
//! first brought into their persisted percent domains, then the offset is
//! bounded against the effective maximum. Edge offsets are always inward and
//! therefore non-negative; centered offsets may move in either direction.
//!
//! DEVIATION from Qt5: its alignment-change path wrote the new physical
//! alignment without revalidating offset, so a negative Center offset became a
//! negative edge margin. This maintained continuation treats that reachable
//! off-output placement as an upstream defect and normalizes the whole state at
//! every entry point.
inline NormalizedPlacement normalize(RequestedPlacement requested)
{
    Q_ASSERT(std::isfinite(requested.minLengthPercent)
             && std::isfinite(requested.maxLengthPercent)
             && std::isfinite(requested.offsetPercent));

    const double minimum = std::clamp(requested.minLengthPercent, 0.0, 100.0);
    const double maximumFloor = requested.alignment == Alignment::Justify ? 1.0
                                                                          : std::max(1.0, minimum);
    const double maximum = std::clamp(requested.maxLengthPercent, maximumFloor, 100.0);
    const double freeLength = 100.0 - maximum;
    const bool centered = requested.alignment == Alignment::Center
            || requested.alignment == Alignment::Justify;
    const double offset = centered
            ? std::clamp(requested.offsetPercent, -freeLength / 2.0, freeLength / 2.0)
            : std::clamp(requested.offsetPercent, 0.0, freeLength);

    return {requested.edge, requested.alignment, minimum, maximum, offset};
}

inline RequestedPlacement requestedFrom(const NormalizedPlacement &placement)
{
    return {placement.edge(), placement.alignment(), placement.minLengthPercent(),
            placement.maxLengthPercent(), placement.offsetPercent()};
}

//! Resolve the normalized percent state into the absolute primary-axis extent
//! consumed by backgrounds and applet layouts. Output origins remain part of
//! the calculation so secondary outputs are not accidentally treated as if
//! they started at (0,0).
inline PrimaryExtent solvePrimaryExtent(const NormalizedPlacement &placement,
                                        const OutputGeometry &output)
{
    Q_ASSERT(std::isfinite(output.x) && std::isfinite(output.y)
             && std::isfinite(output.width) && std::isfinite(output.height));
    Q_ASSERT(output.width > 0.0 && output.height > 0.0);

    const double outputOrigin = isHorizontal(placement.edge()) ? output.x : output.y;
    const double outputLength = isHorizontal(placement.edge()) ? output.width : output.height;
    const double length = outputLength * placement.maxLengthPercent() / 100.0;
    const double offset = outputLength * placement.offsetPercent() / 100.0;
    double origin = outputOrigin;

    switch (placement.alignment()) {
    case Alignment::Start:
        origin += offset;
        break;
    case Alignment::End:
        origin += outputLength - length - offset;
        break;
    case Alignment::Center:
    case Alignment::Justify:
        origin += (outputLength - length) / 2.0 + offset;
        break;
    }

    return {origin, length};
}

inline bool liesWithinOutput(const PrimaryExtent &extent, OutputEdge edge,
                             const OutputGeometry &output)
{
    Q_ASSERT(std::isfinite(extent.origin) && std::isfinite(extent.length));

    const double outputOrigin = isHorizontal(edge) ? output.x : output.y;
    const double outputLength = isHorizontal(edge) ? output.width : output.height;
    constexpr double epsilon = 0.000001;

    return extent.length >= 0.0
            && extent.origin >= outputOrigin - epsilon
            && extent.origin + extent.length <= outputOrigin + outputLength + epsilon;
}

} // namespace PlacementState
} // namespace Settings
} // namespace Latte

#endif
