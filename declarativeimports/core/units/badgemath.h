/*
    SPDX-FileCopyrightText: 2016 Smith AR <audoban@openmailbox.org>
    SPDX-FileCopyrightText: 2016 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef LATTEUNITSBADGEMATH_H
#define LATTEUNITSBADGEMATH_H

// Qt
#include <QList>
#include <QLocale>
#include <QString>
#include <QtGlobal>

// C++
#include <cmath>
#include <limits>
#include <optional>

namespace Latte {
namespace BadgeMath {

//! Badge identifier parsing, record update semantics, progress-proportion
//! math and the arc/ring geometry the badge canvas draws (EX-20 in
//! docs/QML_EXTRACTION_PLAN.md). Replaces the JS bodies in the plasmoid
//! main.qml (getBadger/updateBadge), ProgressOverlay.qml (proportion) and
//! BadgeText.qml (onPaint sweep, ring sizing, 9999+ label) - the upstream
//! copyright lines above trace to those Qt5 ancestors.
//!
//! The QML shells keep the live state and presentation: the plasmoid root
//! owns the badge record list (per-plasmoid state; a singleton would be
//! shared per-engine, the wrong ownership), the task loop walks live
//! scenegraph children, and the Canvas paints what arcFor/ringRadiiFor
//! computed. This core owns every decision.

//! One badge record as the plasmoid stores it: id is always the
//! ".desktop"-suffixed identifier the update arrived under, value is kept
//! as the raw string the external app published (re-parsed on read, so a
//! malformed publish stays inspectable instead of decaying to a number).
struct BadgeRecord {
    QString id;
    QString value;
};

//! The identifier tail getBadger matched against: everything after the
//! last '/', or the whole identifier when there is none (full launcher
//! urls reduce to their desktop-entry basename).
inline QString launcherTail(const QString &identifier)
{
    const qsizetype lastSlash = identifier.lastIndexOf(QLatin1Char('/'));
    return lastSlash >= 0 ? identifier.mid(lastSlash + 1) : identifier;
}

//! The record key updateBadge stored under: the bare appId from the
//! shortcuts D-Bus path plus the ".desktop" suffix launcher urls carry.
inline QString desktopEntryId(const QString &identifier)
{
    return identifier + QStringLiteral(".desktop");
}

//! getBadger's lookup: the first record whose id is CONTAINED in the
//! identifier's tail. Substring semantics (JS indexOf), not equality -
//! a stored fragment id matches every longer identifier that carries it,
//! exactly as Qt5 behaved.
inline std::optional<qsizetype> findBadgeRecord(const QList<BadgeRecord> &records, const QString &identifier)
{
    const QString tail = launcherTail(identifier);

    for (qsizetype i = 0; i < records.size(); ++i) {
        if (tail.contains(records[i].id)) {
            return i;
        }
    }

    return std::nullopt;
}

//! The one badge-value parse (unifies Qt5's disagreeing Number() and
//! parseInt() paths - recorded in the EX-20 spec section): empty or
//! whitespace-only publishes clear the badge (0), numeric strings
//! truncate toward zero like both Qt5 paths did on well-formed input,
//! and anything else is refused as nullopt for the boundary to report
//! loudly. Deliberately NOT emulated from JS Number(): hex forms and
//! ToInt32's modulo-2^32 wraparound for absurd magnitudes - garbage
//! domain, loud beats faithful there.
inline std::optional<int> parseBadgeCount(const QString &value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return 0;
    }

    bool numeric = false;
    const double parsed = trimmed.toDouble(&numeric); // C locale: no group separators, no hex
    if (!numeric || !std::isfinite(parsed)) {
        return std::nullopt;
    }

    const double truncated = std::trunc(parsed);
    if (truncated < std::numeric_limits<int>::min() || truncated > std::numeric_limits<int>::max()) {
        return std::nullopt;
    }

    return static_cast<int>(truncated);
}

//! updateBadge's record semantics: mutate the record the identifier
//! resolves to, append a fresh one otherwise. Pure over the caller's
//! list; the shell only commits the result when a live task matched
//! (Qt5 updated records inside the task loop - zero matches meant no
//! record, and that stays true).
inline QList<BadgeRecord> applyBadge(QList<BadgeRecord> records, const QString &identifier, const QString &value)
{
    const QString id = desktopEntryId(identifier);

    if (const auto index = findBadgeRecord(records, id)) {
        records[*index].value = value;
    } else {
        records.append(BadgeRecord{id, value});
    }

    return records;
}

//! updateBadge's task filter: does this task's launcher url carry the
//! published identifier's desktop entry (substring, like Qt5).
inline bool taskMatchesBadge(const QString &launcherUrl, const QString &identifier)
{
    return launcherUrl.contains(desktopEntryId(identifier));
}

//! ProgressOverlay's proportion decision: a visible progress wins and
//! maps 0..100 -> 0..1; otherwise a showing count or an externally set
//! badge indicator fills the circle; otherwise nothing. The null
//! smartLauncherItem checks are value-domain and stay in the shell.
inline double proportionFor(bool progressVisible, double progress, bool countVisible, int badgeIndicator)
{
    if (progressVisible) {
        return progress / 100.0;
    }

    if (badgeIndicator > 0 || countVisible) {
        return 1.0;
    }

    return 0.0;
}

struct BadgeArc {
    double startRadian;
    double sweepRadian;
};

//! BadgeText's onPaint sweep: twelve o'clock start, clockwise sweep
//! proportional to the value. Proportion > 1 stays unclamped like Qt5
//! (the canvas wraps); the shipped pipeline cannot produce it because
//! smartlauncheritem.cpp clamps progress to 0..100. Negative proportion
//! is a producer bug - asserted here, refused loudly at the QML boundary
//! by the wrapper. The canvas keeps its 0.01 edge-bleed filler: that is
//! a rendering workaround of the Canvas item, not geometry.
inline BadgeArc arcFor(double proportion)
{
    Q_ASSERT(proportion >= 0.0);

    return BadgeArc{-M_PI / 2.0, 2.0 * M_PI * proportion};
}

struct RingRadii {
    double outer;
    double inner;
};

//! BadgeText's ring sizing: the outer radius is half the badge height,
//! the inner radius hollows a full circle into a 90% ring. A negative
//! height clamps to an empty ring - kept from Qt5 as a deliberate
//! contract, because bindings evaluate transiently during creation
//! before anchors settle and a momentary negative height is a normal
//! state there, not a producer bug.
inline RingRadii ringRadiiFor(double height, bool fullCircle)
{
    const double outer = height < 0.0 ? 0.0 : height / 2.0;
    return RingRadii{outer, fullCircle ? 0.0 : outer * 0.9};
}

//! Which label the badge text shows for a count. The three states are a
//! real enum so "empty label", "zero" and "overflow" cannot be conflated
//! through sentinel strings.
enum class BadgeLabelKind {
    None,     //!< nothing to show; the shell may fall through to its text mode
    Number,   //!< the locale-grouped count
    Overflow, //!< more than 9999: the clamped "9,999+" marker
};

inline BadgeLabelKind classifyBadgeLabel(int numberValue)
{
    if (numberValue > 9999) {
        return BadgeLabelKind::Overflow;
    }

    if (numberValue > 0) {
        return BadgeLabelKind::Number;
    }

    return BadgeLabelKind::None;
}

//! The in-range count rendering Qt5 got from
//! numberValue.toLocaleString(Qt.locale(), 'f', 0): locale grouping, no
//! decimals. Only meaningful for the Number kind - the overflow marker
//! is an i18nc catalog string, which is ambient state the wrapper owns.
inline QString formatBadgeCount(int numberValue, const QLocale &locale)
{
    Q_ASSERT(classifyBadgeLabel(numberValue) == BadgeLabelKind::Number);

    return locale.toString(numberValue);
}

}
}

#endif
