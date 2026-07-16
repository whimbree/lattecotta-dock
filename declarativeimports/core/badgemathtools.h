/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef LATTECOREBADGEMATHTOOLS_H
#define LATTECOREBADGEMATHTOOLS_H

// Qt
#include <QObject>
#include <QString>
#include <QVariantList>

namespace Latte {

//! Thin QML shell over the BadgeMath pure core (EX-20 in
//! docs/QML_EXTRACTION_PLAN.md, units/badgemath.h), registered as the
//! LatteCore.BadgeMath singleton. It lives in LatteCore rather than the
//! tasks-private plugin because BadgeText.qml is a shared
//! org.kde.latte.components component (the containment's shortcut badges
//! instantiate it too) and must not pull org.kde.latte.private.tasks in.
//!
//! Stateless: the plasmoid root keeps owning its badge record list (QML
//! per-instance state); records cross this boundary as {id, value}
//! variant maps and malformed input is refused loudly, never coerced
//! silently (the router wrapper's refusal model).
class BadgeMathTools final : public QObject
{
    Q_OBJECT

public:
    explicit BadgeMathTools(QObject *parent = nullptr);

    //! the one badge-value parse; identifier only labels the warning.
    //! Malformed values warn and clear the badge (0) - Qt5's visible
    //! result on the D-Bus path.
    Q_INVOKABLE int parseCount(const QString &value, const QString &identifier) const;

    //! TaskItem's re-derive on launcher change: the count stored for
    //! this launcher url, 0 when none is recorded
    Q_INVOKABLE int badgeCountForLauncher(const QVariantList &records, const QString &launcherUrl) const;

    //! updateBadge's record commit: records with the identifier's entry
    //! mutated or appended; a malformed row refuses the whole update
    Q_INVOKABLE QVariantList applyBadge(const QVariantList &records, const QString &identifier,
                                        const QString &value) const;

    //! updateBadge's task filter: launcher url carries identifier.desktop
    Q_INVOKABLE bool taskMatchesBadge(const QString &launcherUrl, const QString &identifier) const;

    //! ProgressOverlay's proportion decision
    Q_INVOKABLE double proportionFor(bool progressVisible, double progress, bool countVisible,
                                     int badgeIndicator) const;

    //! badge canvas arc: twelve o'clock start, clockwise sweep. A
    //! negative proportion is refused loudly with an empty sweep.
    Q_INVOKABLE double arcStartRadian() const;
    Q_INVOKABLE double arcSweepRadian(double proportion) const;

    //! badge ring sizing from the badge height
    Q_INVOKABLE double ringOuterRadius(double height) const;
    Q_INVOKABLE double ringInnerRadius(double height, bool fullCircle) const;

    //! the badge count label: "" for nothing (the shell falls through to
    //! its text mode), locale-grouped digits in range, "9,999+" beyond
    Q_INVOKABLE QString countLabel(int numberValue) const;
};

}

#endif
