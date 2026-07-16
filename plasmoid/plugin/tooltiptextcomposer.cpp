/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "tooltiptextcomposer.h"

// local
#include "units/tooltiptext.h"

// Qt
#include <QDebug>

// KDE
#include <KLocalizedString>

// C++
#include <variant>

namespace {
//! the established visitor-composition idiom: std::visit over this stays
//! compile-time exhaustive - a fourth ActivitiesLine alternative fails to
//! build here instead of falling through at runtime
template<class... Ts>
struct Overloaded : Ts... {
    using Ts::operator()...;
};
}

namespace Latte {
namespace Tasks {

TooltipTextComposer::TooltipTextComposer(QObject *parent)
    : QObject(parent)
{
}

QString TooltipTextComposer::composeTitle(const QString &display, const QString &appName) const
{
    return TooltipText::composeTitle(display, appName);
}

QString TooltipTextComposer::composeSubText(const QVariantMap &facts) const
{
    // boundary refusal: a shell drifting a key name must fail loudly here,
    // not compose from silently-defaulted facts
    static const QStringList requiredKeys = {
        QStringLiteral("showOnlyCurrentDesktop"),
        QStringLiteral("showOnlyCurrentActivity"),
        QStringLiteral("desktopCount"),
        QStringLiteral("runningActivityCount"),
        QStringLiteral("onAllVirtualDesktops"),
        QStringLiteral("desktopNames"),
        QStringLiteral("activityIds"),
        QStringLiteral("activityNames"),
        QStringLiteral("currentActivity"),
    };

    for (const QString &key : requiredKeys) {
        if (!facts.contains(key)) {
            qCritical() << "TooltipTextComposer: composeSubText called without fact" << key
                        << "- got keys" << facts.keys();
            return QString();
        }
    }

    TooltipText::SubTextFacts coreFacts;
    coreFacts.showOnlyCurrentDesktop = facts.value(QStringLiteral("showOnlyCurrentDesktop")).toBool();
    coreFacts.showOnlyCurrentActivity = facts.value(QStringLiteral("showOnlyCurrentActivity")).toBool();
    coreFacts.desktopCount = facts.value(QStringLiteral("desktopCount")).toInt();
    coreFacts.runningActivityCount = facts.value(QStringLiteral("runningActivityCount")).toInt();
    coreFacts.onAllVirtualDesktops = facts.value(QStringLiteral("onAllVirtualDesktops")).toBool();
    coreFacts.desktopNames = facts.value(QStringLiteral("desktopNames")).toStringList();
    coreFacts.currentActivityId = facts.value(QStringLiteral("currentActivity")).toString();

    const QVariant activityIds = facts.value(QStringLiteral("activityIds"));
    const QVariant activityNames = facts.value(QStringLiteral("activityNames"));

    if (activityIds.isNull() != activityNames.isNull()) {
        qCritical() << "TooltipTextComposer: activityIds and activityNames must be null together -"
                    << "got" << activityIds << activityNames;
        return QString();
    }

    if (!activityIds.isNull()) {
        const QStringList ids = activityIds.toStringList();
        const QStringList names = activityNames.toStringList();

        if (ids.size() != names.size()) {
            qCritical() << "TooltipTextComposer: activityIds/activityNames length mismatch -"
                        << ids.size() << "ids vs" << names.size() << "names";
            return QString();
        }

        QList<TooltipText::Activity> activities;
        activities.reserve(ids.size());
        for (qsizetype i = 0; i < ids.size(); ++i) {
            activities.append({ids.at(i), names.at(i)});
        }
        coreFacts.activities = std::move(activities);
    }

    const TooltipText::SubTextPlan plan = TooltipText::planSubText(coreFacts);

    QStringList lines;

    if (plan.desktopNamesJoined) {
        lines << i18nc("Comma-separated list of desktops", "On %1", *plan.desktopNamesJoined);
    }

    if (plan.activitiesLine) {
        lines << std::visit(
            Overloaded{
                [](const TooltipText::AvailableOnAllActivities &) {
                    return i18nc("Which virtual desktop a window is currently on",
                                 "Available on all activities");
                },
                [](const TooltipText::AvailableOnActivities &line) {
                    return i18nc("Which activities a window is currently on",
                                 "Available on %1", line.namesJoined);
                },
                [](const TooltipText::AlsoAvailableOnActivities &line) {
                    return i18nc("Activities a window is currently on (apart from the current one)",
                                 "Also available on %1", line.namesJoined);
                },
            },
            *plan.activitiesLine);
    }

    return lines.join(QLatin1Char('\n'));
}

}
}
