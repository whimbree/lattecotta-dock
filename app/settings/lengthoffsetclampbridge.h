/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef LENGTHOFFSETCLAMPBRIDGE_H
#define LENGTHOFFSETCLAMPBRIDGE_H

// Qt
#include <QObject>
#include <QVariantMap>

namespace Latte {
namespace Settings {

//! Thin QML boundary over the LengthOffsetClamp core (EX-18): registered
//! as the `lengthClamp` context property in SubConfigView::init(), the
//! existing settings-side registration point that reaches both consumers
//! (PrimaryConfigView hosts pages/AppearanceConfig.qml, CanvasConfigView
//! hosts canvas/maxlength/RulerMouseArea.qml; both share that init). The
//! core decides (lengthoffsetclamp.h); this class maps the raw
//! Latte::Types::Alignment int onto the core's two-valued Alignment,
//! refuses non-finite input loudly (KConfig happily parses "nan" into a
//! double, and the Qt5 QML silently pushed it back into the config), and
//! packs results into maps keyed like the config keys.
class LengthOffsetClampBridge : public QObject
{
    Q_OBJECT

public:
    explicit LengthOffsetClampBridge(QObject *parent = nullptr);

    //! ruler wheel step -> {maxLength, minLength, offset}
    Q_INVOKABLE QVariantMap clampMaxLengthByStep(double maxLength, double minLength,
                                                 double offset, double step,
                                                 int alignment) const;

    //! Maximum slider -> {maxLength, minLength, offset}; the current
    //! maxLength is never read by this path, so it is not a parameter
    Q_INVOKABLE QVariantMap clampMaxLengthToValue(double requestedMaxLength, double minLength,
                                                  double offset, int alignment) const;

    //! Offset slider -> {maxLength, offset}
    Q_INVOKABLE QVariantMap clampOffset(double maxLength, double requestedOffset,
                                        int alignment) const;

    //! the offset slider's reachable range; on refusal both collapse to
    //! 0 (the page disables the offset row when to <= from, so a corrupt
    //! maxLength shows as a disabled control plus the log line)
    Q_INVOKABLE double offsetSliderFrom(double maxLength, int alignment) const;
    Q_INVOKABLE double offsetSliderTo(double maxLength, int alignment) const;
};

}
}

#endif
