/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "badgemathtools.h"

// local
#include "units/badgemath.h"

// Qt
#include <QVariantMap>

// KDE
#include <KLocalizedString>

namespace Latte {

namespace {

const QLatin1String recordIdKey("id");
const QLatin1String recordValueKey("value");

//! records cross the QML boundary as {id, value} maps; anything else in
//! the list is a caller bug that must surface at this boundary, not
//! three bindings away
std::optional<QList<BadgeMath::BadgeRecord>> recordsFromVariant(const QVariantList &records)
{
    QList<BadgeMath::BadgeRecord> converted;
    converted.reserve(records.size());

    for (const QVariant &row : records) {
        const QVariantMap map = row.toMap();
        if (!map.contains(recordIdKey) || !map.contains(recordValueKey)) {
            qCritical() << "BadgeMath: malformed badge record row" << row
                        << "- expected an {id, value} map; refusing the call";
            return std::nullopt;
        }
        converted.append(BadgeMath::BadgeRecord{map.value(recordIdKey).toString(),
                                                map.value(recordValueKey).toString()});
    }

    return converted;
}

QVariantList recordsToVariant(const QList<BadgeMath::BadgeRecord> &records)
{
    QVariantList out;
    out.reserve(records.size());

    for (const BadgeMath::BadgeRecord &record : records) {
        QVariantMap map;
        map.insert(recordIdKey, record.id);
        map.insert(recordValueKey, record.value);
        out.append(map);
    }

    return out;
}

int countOrRefuseLoudly(const QString &value, const QString &context)
{
    if (const auto count = BadgeMath::parseBadgeCount(value)) {
        return *count;
    }

    qWarning() << "BadgeMath: unparseable badge value" << value << "for" << context
               << "- clearing the badge";
    return 0;
}

}

BadgeMathTools::BadgeMathTools(QObject *parent)
    : QObject(parent)
{
}

int BadgeMathTools::parseCount(const QString &value, const QString &identifier) const
{
    return countOrRefuseLoudly(value, identifier);
}

int BadgeMathTools::badgeCountForLauncher(const QVariantList &records, const QString &launcherUrl) const
{
    const auto converted = recordsFromVariant(records);
    if (!converted) {
        return 0;
    }

    const auto index = BadgeMath::findBadgeRecord(*converted, launcherUrl);
    if (!index) {
        return 0;
    }

    return countOrRefuseLoudly((*converted)[*index].value, launcherUrl);
}

QVariantList BadgeMathTools::applyBadge(const QVariantList &records, const QString &identifier,
                                        const QString &value) const
{
    const auto converted = recordsFromVariant(records);
    if (!converted) {
        return records; // refused (already reported): the stored list stays as it was
    }

    return recordsToVariant(BadgeMath::applyBadge(*converted, identifier, value));
}

bool BadgeMathTools::taskMatchesBadge(const QString &launcherUrl, const QString &identifier) const
{
    return BadgeMath::taskMatchesBadge(launcherUrl, identifier);
}

double BadgeMathTools::proportionFor(bool progressVisible, double progress, bool countVisible,
                                     int badgeIndicator) const
{
    return BadgeMath::proportionFor(progressVisible, progress, countVisible, badgeIndicator);
}

double BadgeMathTools::arcStartRadian() const
{
    return BadgeMath::arcFor(0.0).startRadian;
}

double BadgeMathTools::arcSweepRadian(double proportion) const
{
    if (proportion < 0.0) {
        // boundary refusal, not a clamp: a negative proportion is a producer
        // bug (the shipped pipeline clamps progress to 0..100 upstream) and
        // must surface here instead of painting a backwards arc
        qCritical() << "BadgeMath: negative badge proportion" << proportion
                    << "- refusing with an empty sweep";
        return 0.0;
    }

    return BadgeMath::arcFor(proportion).sweepRadian;
}

double BadgeMathTools::ringOuterRadius(double height) const
{
    return BadgeMath::ringRadiiFor(height, true).outer;
}

double BadgeMathTools::ringInnerRadius(double height, bool fullCircle) const
{
    return BadgeMath::ringRadiiFor(height, fullCircle).inner;
}

QString BadgeMathTools::countLabel(int numberValue) const
{
    switch (BadgeMath::classifyBadgeLabel(numberValue)) {
    case BadgeMath::BadgeLabelKind::Overflow:
        // moved verbatim from BadgeText.qml; the msgid was never extracted
        // into any catalog (no Messages.sh scans declarativeimports/), so
        // this renders the English msgid exactly as the QML call did -
        // recorded in the EX-20 log for a future i18n pass
        return i18nc("Over 9999 new messages, overlay, keep short", "9,999+");
    case BadgeMath::BadgeLabelKind::Number:
        return BadgeMath::formatBadgeCount(numberValue, QLocale());
    case BadgeMath::BadgeLabelKind::None:
        return QString();
    }

    Q_UNREACHABLE();
}

}
