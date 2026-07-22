/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PLACEMENTNORMALIZER_H
#define PLACEMENTNORMALIZER_H

// Qt
#include <QObject>
#include <QVariantMap>

namespace Latte {
namespace Containment {

//! QML boundary for the normalized placement value. The containment passes the
//! current physical output edge and persisted Latte alignment; this boundary
//! translates them to semantic Start/Center/End, invokes the pure transaction,
//! and translates back for the new edge.
//! not final: qmlRegisterType instantiates through a QQmlElement subclass
class PlacementNormalizer : public QObject
{
    Q_OBJECT

public:
    explicit PlacementNormalizer(QObject *parent = nullptr);

public Q_SLOTS:
    //! Returns {accepted, alignment, minLength, maxLength, offset}. Unknown
    //! physical enums and non-finite values are refused loudly and returned
    //! unchanged with accepted=false.
    Q_INVOKABLE QVariantMap normalize(int location, int alignment,
                                      double minLength, double maxLength,
                                      double offset) const;
};

} // namespace Containment
} // namespace Latte

#endif
