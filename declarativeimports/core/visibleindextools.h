/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef LATTECOREVISIBLEINDEXTOOLS_H
#define LATTECOREVISIBLEINDEXTOOLS_H

// Qt
#include <QJSEngine>
#include <QObject>
#include <QQmlEngine>
#include <QVariantList>
#include <QVariantMap>

namespace Latte {

//! Thin QML shell over the VisibleIndex pure core (EX-06 in
//! docs/QML_EXTRACTION_PLAN.md, units/visibleindex.h). The indexer
//! twins' collector Bindings feed row snapshots - lists of {index,
//! isSeparator, isHidden, isMarginsSeparator, subItemCount} - and every
//! verdict comes from the core; the QML keeps only its live reads
//! (client bridges, row-edge fallbacks).
//!
//! Boundary conventions: a malformed row entry is a collector bug and is
//! refused loudly (qCritical + the function's none-answer), the router
//! wrapper's model. Query indexes are NOT validated: probing -1 (an
//! unindexed spacer) or a gap index is a legitimate "owns nothing"
//! question the shells ask on purpose. "None" answers keep QML's
//! -1-means-none convention at this boundary only.
class VisibleIndexTools final : public QObject
{
    Q_OBJECT

public:
    //! mirrors VisibleIndex::Direction for the QML shells
    enum Direction {
        Tail = 0,
        Head
    };
    Q_ENUM(Direction)

    explicit VisibleIndexTools(QObject *parent = nullptr);

public Q_SLOTS:
    //! 1-based visible index, -1 when the entry owns none
    Q_INVOKABLE int visibleIndexOf(const QVariantList &row, int actualIndex);
    //! the client twin's composition: hostBase replaces the "+1 self slot"
    Q_INVOKABLE int clientVisibleIndexOf(const QVariantList &row, int actualIndex, int hostBase);
    Q_INVOKABLE bool belongsAtVisibleIndex(const QVariantList &row, int entryIndex, int visibleIndex);
    Q_INVOKABLE int firstVisibleIndex(const QVariantList &row);
    Q_INVOKABLE int lastVisibleIndex(const QVariantList &row);
    Q_INVOKABLE int countVisibleItems(const QVariantList &row);
    Q_INVOKABLE bool edgeItemIsSeparator(const QVariantList &row, int direction);
    //! the landing entry of the all-hidden-skipped walk as {index,
    //! isSeparator}, index -1 when the walk resolves to nothing; the
    //! containment shell keys its live client delegation off index
    Q_INVOKABLE QVariantMap hiddenSkippingNeighbor(const QVariantList &row, int actualIndex, int direction);
    Q_INVOKABLE bool neighborIsSeparator(const QVariantList &row, int actualIndex, int direction);
    Q_INVOKABLE bool neighborIsVisibleSeparator(const QVariantList &row, int actualIndex, int direction);
    Q_INVOKABLE bool isInMarginsArea(const QVariantList &row, int actualIndex);
    //! checkIndex rank math: countedChildren are bools, -1 when self is
    //! uncounted (edge spacers / internal view splitters keep index -1)
    Q_INVOKABLE int assignedLayoutIndex(const QVariantList &countedChildren, int selfPosition, int beginIndex);
};

static QObject *visibleindextools_qobject_singletontype_provider(QQmlEngine *engine, QJSEngine *scriptEngine)
{
    Q_UNUSED(engine)
    Q_UNUSED(scriptEngine)

// NOTE: QML engine is the owner of this resource
    return new VisibleIndexTools;
}

}

#endif
