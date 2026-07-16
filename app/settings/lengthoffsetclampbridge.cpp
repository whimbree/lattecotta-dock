/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "lengthoffsetclampbridge.h"

// local
#include "lengthoffsetclamp.h"
#include <coretypes.h>

// Qt
#include <QDebug>

// C++
#include <cmath>
#include <initializer_list>

namespace {

using Latte::Settings::LengthOffsetClamp::Alignment;
using Latte::Settings::LengthOffsetClamp::LengthState;

//! the same total predicate the shipped QML applied with ===: Center and
//! Justify take the centered branches, every other int (edges,
//! NoneAlignment, out-of-domain values from a hand-edited config)
//! behaves as an edge alignment
Alignment alignmentKindOf(int alignment)
{
    const bool centered = (alignment == Latte::Types::Center)
                          || (alignment == Latte::Types::Justify);
    return centered ? Alignment::Centered : Alignment::Edge;
}

//! boundary refusal (qCritical-and-refuse; the parabolic router wrapper
//! is the model): a hand-edited layout config can carry nan/inf in the
//! double keys, and the clamp math would silently propagate it back into
//! the config the way the Qt5 QML did. Refuse the whole call instead -
//! no partial clamping of a corrupt state.
bool refusedNonFinite(const char *function, std::initializer_list<double> values)
{
    for (const double value : values) {
        if (!std::isfinite(value)) {
            qCritical() << "lengthClamp:" << function
                        << "refused: non-finite input reached the clamp -"
                        << "a maxLength/minLength/offset config key holds nan or inf;"
                        << "fix the layout file. Returning the input unchanged.";
            return true;
        }
    }
    return false;
}

QVariantMap toMap(const LengthState &state)
{
    return {{QStringLiteral("maxLength"), state.maxLength},
            {QStringLiteral("minLength"), state.minLength},
            {QStringLiteral("offset"), state.offset}};
}

} // namespace

namespace Latte {
namespace Settings {

LengthOffsetClampBridge::LengthOffsetClampBridge(QObject *parent)
    : QObject(parent)
{
}

QVariantMap LengthOffsetClampBridge::clampMaxLengthByStep(double maxLength, double minLength,
                                                          double offset, double step,
                                                          int alignment) const
{
    if (refusedNonFinite("clampMaxLengthByStep", {maxLength, minLength, offset, step})) {
        return toMap({maxLength, minLength, offset});
    }

    return toMap(LengthOffsetClamp::clampMaxLengthByStep({maxLength, minLength, offset},
                                                         step, alignmentKindOf(alignment)));
}

QVariantMap LengthOffsetClampBridge::clampMaxLengthToValue(double requestedMaxLength,
                                                           double minLength, double offset,
                                                           int alignment) const
{
    if (refusedNonFinite("clampMaxLengthToValue", {requestedMaxLength, minLength, offset})) {
        return toMap({requestedMaxLength, minLength, offset});
    }

    return toMap(LengthOffsetClamp::clampMaxLengthToValue(requestedMaxLength, minLength, offset,
                                                          alignmentKindOf(alignment)));
}

QVariantMap LengthOffsetClampBridge::clampOffset(double maxLength, double requestedOffset,
                                                 int alignment) const
{
    if (refusedNonFinite("clampOffset", {maxLength, requestedOffset})) {
        return {{QStringLiteral("maxLength"), maxLength},
                {QStringLiteral("offset"), requestedOffset}};
    }

    const auto clamped = LengthOffsetClamp::clampOffset(maxLength, requestedOffset,
                                                        alignmentKindOf(alignment));
    return {{QStringLiteral("maxLength"), clamped.maxLength},
            {QStringLiteral("offset"), clamped.offset}};
}

double LengthOffsetClampBridge::offsetSliderFrom(double maxLength, int alignment) const
{
    if (refusedNonFinite("offsetSliderFrom", {maxLength})) {
        return 0.0;
    }

    return LengthOffsetClamp::offsetSliderBounds(maxLength, alignmentKindOf(alignment)).from;
}

double LengthOffsetClampBridge::offsetSliderTo(double maxLength, int alignment) const
{
    if (refusedNonFinite("offsetSliderTo", {maxLength})) {
        return 0.0;
    }

    return LengthOffsetClamp::offsetSliderBounds(maxLength, alignmentKindOf(alignment)).to;
}

}
}
