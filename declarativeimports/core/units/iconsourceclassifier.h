/*
    SPDX-FileCopyrightText: 2012 Marco Martin <mart@kde.org>
    SPDX-FileCopyrightText: 2014 David Edmundson <davidedmudnson@kde.org>
    SPDX-FileCopyrightText: 2016 Smith AR <audoban@openmailbox.org>
    SPDX-FileCopyrightText: 2016 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, derived)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef ICONSOURCECLASSIFIER_H
#define ICONSOURCECLASSIFIER_H

// Qt
#include <QIcon>
#include <QImage>
#include <QLatin1String>
#include <QString>
#include <QUrl>
#include <QVariant>

namespace Latte {

//! IconItem's source-routing decisions as pure functions (EX-24 in
//! docs/tracking/QML_EXTRACTION_PLAN.md), adopted from David Goree's latte-dock-qt6
//! (declarativeimports/core/iconsourceclassifier.h at ad74a34a,
//! github.com/CaptSilver/latte-dock-qt6) after diffing their ancestor
//! ladder against our iconitem.cpp and the Qt5 ancestor (f0ad7b23) - all
//! three agree.
//! iconitem.cpp keeps the KSvg/KIconLoader resolution work and asks here
//! which branch a source variant takes, so a misrouting bug fails a unit
//! test here instead of rendering as a silently wrong icon.
namespace IconSourceClassifier {

enum class SourceKind {
    LocalFile,     //!< a file:// url; load the pointed-at image from disk
    SvgOrIconName, //!< a non-empty name; resolve via theme svg, icon loader, then QIcon
    Icon,          //!< a nameless QIcon; use it directly
    Image,         //!< a QImage; use it directly
    Clear,         //!< nothing usable; reset every resolved member
};

//! Derive the name a source variant routes by: toString(), but a QIcon
//! created with QIcon::fromTheme() carries its theme name and that wins.
//! classify() calls this rather than re-deriving, so a named QIcon can
//! never misroute to the Icon branch - the two derivations cannot drift.
inline QString sourceName(const QVariant &source)
{
    QString name = source.toString();

    if (source.canConvert<QIcon>() && !source.value<QIcon>().name().isEmpty()) {
        name = source.value<QIcon>().name();
    }

    return name;
}

//! Classify a source variant into the branch IconItem::setSource() takes:
//! the if/else ladder reduced to an enum. Named sources branch on locality;
//! nameless ones try QIcon before QImage; anything else clears the icon.
//! Total over QVariant - every input maps to exactly one SourceKind.
inline SourceKind classify(const QVariant &source)
{
    const QString name = sourceName(source);

    if (!name.isEmpty()) {
        return QUrl(name).isLocalFile() ? SourceKind::LocalFile : SourceKind::SvgOrIconName;
    }

    if (source.canConvert<QIcon>()) {
        return SourceKind::Icon;
    }

    if (source.canConvert<QImage>()) {
        return SourceKind::Image;
    }

    return SourceKind::Clear;
}

//! The setLastValidSourceName() guard: true means do NOT record this as
//! the last valid name. Empty names carry no information and
//! application-x-executable is the generic placeholder a task shows while
//! its real icon is still unknown - remembering it would replace a real
//! last-known icon with the placeholder.
inline bool isFilteredSourceName(const QString &name)
{
    return name.isEmpty() || name == QLatin1String("application-x-executable");
}

//! IconItem's resolved-source state as a value struct: which of the three
//! resolution members (QIcon, KSvg::Svg, QImage) is currently non-null.
struct ResolvedIcon {
    bool hasIcon{false};
    bool hasSvg{false};
    bool hasImage{false};

    //! IconItem::isValid(): any successful resolution makes the item valid.
    constexpr bool isValid() const
    {
        return hasIcon || hasSvg || hasImage;
    }
};

}
}

#endif
