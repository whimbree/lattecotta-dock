/*
    SPDX-FileCopyrightText: 2013 Sebastian Kügler <sebas@kde.org>
    SPDX-FileCopyrightText: 2016 Smith AR <audoban@openmailbox.org>
    SPDX-FileCopyrightText: 2016 Kai Uwe Broulik <kde@privat.broulik.de>
    SPDX-FileCopyrightText: 2016 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef TOOLTIPTEXT_H
#define TOOLTIPTEXT_H

// Qt
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QtGlobal>

// C++
#include <optional>
#include <variant>

namespace Latte {
namespace TooltipText {

//! Preview tooltip title/subtext derivation (EX-17 in
//! docs/tracking/QML_EXTRACTION_PLAN.md): the KWin <n> counter strip, the appName
//! suffix strip and the desktop/activity subtext decisions that lived as
//! near-twin JS bodies in ToolTipInstance.qml and TaskItem.qml.
//! Latte-authored, NOT part of the plasma-desktop vendored set in
//! plasmoid/plugin (the QML ancestors trace to plasma-desktop's Qt5
//! tooltips, hence the upstream copyright lines above).
//!
//! The QML shells keep the live lookups (desktop/activity id -> name) and
//! the value-domain undefined checks; this core owns every decision. The
//! i18nc catalog lookup is ambient state like a clock read, so it stays
//! OUTSIDE the core: planSubText() returns which lines show and their
//! arguments, and the TooltipTextComposer wrapper maps that plan onto the
//! translated strings (the EX-01 engine/bridge split).

//! Qt5 ran these transforms as JS regexes, whose \s is unicode-aware while
//! PCRE2's default \s is ASCII-only (\d and \W agree between the two).
//! This is JS \s spelled out - the space separators (Zs+Zl+Zp = \p{Z}),
//! the ASCII controls, and the BOM - so the port strips exactly the
//! whitespace Qt5 did (pinned by the NBSP case in the tests).
inline QString jsWhitespaceClass()
{
    return QStringLiteral("[\\p{Z}\\t\\n\\x{0B}\\f\\r\\x{FEFF}]");
}

//! JS String.replace with a non-global regex replaces the FIRST match
//! only; QString::remove(QRegularExpression) is global and would
//! over-strip, so the excision is done by hand.
inline void removeFirstMatch(QString &text, const QRegularExpression &pattern)
{
    const QRegularExpressionMatch match = pattern.match(text);
    if (match.hasMatch()) {
        text.remove(match.capturedStart(), match.capturedLength());
    }
}

//! The window-title transform of ToolTipInstance.qml's generateTitle():
//! save the KWin duplicate-title counter ("title <2>", including whatever
//! trailing non-word characters it carries), strip it, strip a trailing
//! appName plus its separator run, then restore the counter. The
//! non-window branches (genericName, undefined model.display) are
//! value-domain checks that stay in the QML shell.
inline QString composeTitle(const QString &display, const QString &appName)
{
    // KWin appends increasing integers in between pointy brackets to
    // otherwise equal window titles; save <#number> as counter and delete
    // it, to re-append after the appName strip.
    static const QRegularExpression counterAtEnd(QStringLiteral("<\\d+>\\W*$"));
    static const QRegularExpression counterWithLeadingSpace(
        jsWhitespaceClass() + QStringLiteral("*<\\d+>\\W*$"));
    // trailing whitespace and hyphen/em-dash separator runs
    static const QRegularExpression trailingSeparators(
        jsWhitespaceClass() + QStringLiteral("*(?:-|\\x{2014})*") + jsWhitespaceClass() + QStringLiteral("*$"));

    QString text = display;

    const QRegularExpressionMatch counter = counterAtEnd.match(text);
    removeFirstMatch(text, counterWithLeadingSpace);

    // Remove appName from the end. Qt5 built this regex UNESCAPED, so a
    // metachar appName is a pattern ("Node.js" also strips "NodeXjs") -
    // preserved as-is. Where the pattern does not even parse
    // ("Notepad++"), Qt5's JS THREW and broke the whole title binding;
    // skipping the strip and showing the full title is the deliberate
    // lesser evil.
    const QRegularExpression appNameAtEnd(appName + QStringLiteral("$"),
                                          QRegularExpression::CaseInsensitiveOption);
    if (appNameAtEnd.isValid()) {
        removeFirstMatch(text, appNameAtEnd);
    }

    removeFirstMatch(text, trailingSeparators);

    if (counter.hasMatch()) {
        text = text.isEmpty() ? counter.captured()
                              : text + QLatin1Char(' ') + counter.captured();
    }

    // A window title that carried only redundant information (i.e. the
    // appName) is now empty; show an em-dash to indicate that and avoid
    // empty space.
    if (text.isEmpty()) {
        text = QStringLiteral("\u2014");
    }

    return text;
}

//! One activity of the hovered window, id plus its already-resolved name
//! (the QActivities lookup is live session state, so the shell resolves).
//! id and name travel as one value: parallel id/name lists with drifting
//! lengths are unrepresentable past the wrapper boundary.
struct Activity {
    QString id;
    QString name;

    bool operator==(const Activity &) const = default;
};

struct SubTextFacts {
    bool showOnlyCurrentDesktop = false;
    bool showOnlyCurrentActivity = false;
    int desktopCount = 0;
    int runningActivityCount = 0;
    bool onAllVirtualDesktops = false;
    //! the hovered window's desktops, resolved to names in model order;
    //! ids that no longer resolve (desktop just removed) are already
    //! dropped by the shell
    QStringList desktopNames;
    //! nullopt: the model's Activities role is unknown (undefined in QML,
    //! the Qt5 TaskItem body's early-out) - distinct from an empty list,
    //! which means "available on all activities"
    std::optional<QList<Activity>> activities;
    QString currentActivityId;
};

//! the three mutually-exclusive activity lines; the all-activities
//! alternative carries no names and the name-bearing alternatives cannot
//! be empty (planSubText only emits them for a non-empty join), so
//! "Available on all activities: A, B" and "Available on " are
//! unrepresentable
struct AvailableOnAllActivities {
    bool operator==(const AvailableOnAllActivities &) const = default;
};

struct AvailableOnActivities {
    QString namesJoined;

    bool operator==(const AvailableOnActivities &) const = default;
};

struct AlsoAvailableOnActivities {
    QString namesJoined;

    bool operator==(const AlsoAvailableOnActivities &) const = default;
};

using ActivitiesLine = std::variant<AvailableOnAllActivities, AvailableOnActivities, AlsoAvailableOnActivities>;

struct SubTextPlan {
    //! set only when the "On %1" desktop line shows, and never empty: a
    //! desktop line without desktops is unrepresentable
    std::optional<QString> desktopNamesJoined;
    std::optional<ActivitiesLine> activitiesLine;

    bool operator==(const SubTextPlan &) const = default;
};

//! Decides the subtext lines of the shipped generateSubText() twins
//! (ToolTipInstance.qml / TaskItem.qml): whether the desktop line shows
//! and with which names, and which of the three activity lines shows with
//! which names. The wrapper maps the plan onto the i18nc strings.
inline SubTextPlan planSubText(const SubTextFacts &facts)
{
    // production counts are non-negative; a negative count is a caller bug
    Q_ASSERT(facts.desktopCount >= 0);
    Q_ASSERT(facts.runningActivityCount >= 0);

    SubTextPlan plan;

    if (!facts.showOnlyCurrentDesktop
            && facts.desktopCount > 1
            && !facts.onAllVirtualDesktops
            && !facts.desktopNames.isEmpty()) {
        // Qt5 joined with JS [].join(): comma, no space
        plan.desktopNamesJoined = facts.desktopNames.join(QLatin1Char(','));
    }

    if (!facts.activities) {
        return plan;
    }

    const QList<Activity> &activities = *facts.activities;

    if (activities.isEmpty()) {
        if (facts.runningActivityCount > 1) {
            plan.activitiesLine = AvailableOnAllActivities{};
        }
        return plan;
    }

    QStringList otherActivityNames;
    for (const Activity &activity : activities) {
        if (activity.name.isEmpty()) {
            // Qt5 skipped activities whose name lookup came back empty
            continue;
        }
        if (activity.id != facts.currentActivityId) {
            otherActivityNames << activity.name;
        }
    }

    if (!otherActivityNames.isEmpty()) {
        const QString namesJoined = otherActivityNames.join(QStringLiteral(", "));
        if (facts.showOnlyCurrentActivity) {
            plan.activitiesLine = AlsoAvailableOnActivities{namesJoined};
        } else {
            plan.activitiesLine = AvailableOnActivities{namesJoined};
        }
    }

    return plan;
}

//! Accessible-description facts for one task item (the Phase 10 AT-SPI
//! rollout): what the item's group state and badges currently show. The
//! QML shell resolves badge VISIBILITY before calling - the user-config
//! gates (root.showAudioBadge and friends) and the info-vs-progress badge
//! precedence live in TaskIcon.qml as live session state - so this core
//! only decides which announcements a shown state produces and their
//! order.
struct AccessibleDescriptionFacts {
    bool isLauncher = false;
    bool isGroupParent = false;
    int windowsCount = 0;
    //! the audio badge is shown (streams exist, audible, badge enabled)
    bool showsAudioBadge = false;
    //! every stream of the task is muted (the badge draws the muted icon)
    bool isMuted = false;
    //! the progress badge is shown; percent is what it draws
    bool showsProgressBadge = false;
    int progressPercent = 0;
    //! the number the info badge draws; 0 = no info badge shown
    int infoBadgeCount = 0;
};

//! the announcement tokens, one alternative per distinct sentence the
//! wrapper can speak; value-carrying alternatives keep their argument so
//! "a window count without a count" is unrepresentable
struct LauncherAnnouncement {
    bool operator==(const LauncherAnnouncement &) const = default;
};

struct WindowCountAnnouncement {
    int count = 0;

    bool operator==(const WindowCountAnnouncement &) const = default;
};

struct AudioMutedAnnouncement {
    bool operator==(const AudioMutedAnnouncement &) const = default;
};

struct PlayingAudioAnnouncement {
    bool operator==(const PlayingAudioAnnouncement &) const = default;
};

struct ProgressAnnouncement {
    int percent = 0;

    bool operator==(const ProgressAnnouncement &) const = default;
};

struct InfoBadgeAnnouncement {
    int count = 0;

    bool operator==(const InfoBadgeAnnouncement &) const = default;
};

using AccessibleAnnouncement = std::variant<LauncherAnnouncement,
                                            WindowCountAnnouncement,
                                            AudioMutedAnnouncement,
                                            PlayingAudioAnnouncement,
                                            ProgressAnnouncement,
                                            InfoBadgeAnnouncement>;

//! Decides what a screen reader announces about a task beyond its name:
//! kind (launcher), group size, then the badges in the fixed order
//! audio, progress, info - stable state first, transient values last.
//! The wrapper maps each token onto its i18nc string and joins them.
inline QList<AccessibleAnnouncement> planAccessibleDescription(const AccessibleDescriptionFacts &facts)
{
    // production counts are non-negative; a negative value is a caller bug
    Q_ASSERT(facts.windowsCount >= 0);
    Q_ASSERT(facts.progressPercent >= 0);
    Q_ASSERT(facts.infoBadgeCount >= 0);

    QList<AccessibleAnnouncement> plan;

    if (facts.isLauncher) {
        plan << LauncherAnnouncement{};
    }

    if (facts.isGroupParent && facts.windowsCount > 0) {
        plan << WindowCountAnnouncement{facts.windowsCount};
    }

    if (facts.showsAudioBadge) {
        if (facts.isMuted) {
            plan << AudioMutedAnnouncement{};
        } else {
            plan << PlayingAudioAnnouncement{};
        }
    }

    if (facts.showsProgressBadge) {
        plan << ProgressAnnouncement{facts.progressPercent};
    }

    if (facts.infoBadgeCount > 0) {
        plan << InfoBadgeAnnouncement{facts.infoBadgeCount};
    }

    return plan;
}

}
}

#endif
