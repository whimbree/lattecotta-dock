/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "visibleindextools.h"

// local
#include "units/visibleindex.h"

// Qt
#include <QDebug>

// C++
#include <optional>
#include <vector>

namespace Latte {

VisibleIndexTools::VisibleIndexTools(QObject *parent)
    : QObject(parent)
{
}

namespace {

//! a malformed row is a collector bug, never something to answer around
//! silently; nullopt here makes every caller refuse with its none-answer
std::optional<RowEntries> toRowEntries(const QVariantList &row)
{
    RowEntries entries;
    entries.reserve(row.size());

    for (const QVariant &variant : row) {
        const QVariantMap map = variant.toMap();
        bool indexOk = false;
        bool subItemsOk = false;
        const int index = map.value(QStringLiteral("index")).toInt(&indexOk);
        const int subItemCount = map.value(QStringLiteral("subItemCount")).toInt(&subItemsOk);

        if (!indexOk || !subItemsOk || index < 0 || subItemCount < 0) {
            qCritical() << "VisibleIndex: malformed row entry" << variant
                        << "- refusing to answer";
            return std::nullopt;
        }

        entries.append(RowEntry{.index = index,
                                .isSeparator = map.value(QStringLiteral("isSeparator")).toBool(),
                                .isHidden = map.value(QStringLiteral("isHidden")).toBool(),
                                .isMarginsSeparator = map.value(QStringLiteral("isMarginsSeparator")).toBool(),
                                .subItemCount = subItemCount});
    }

    return entries;
}

std::optional<VisibleIndex::Direction> toDirection(int direction)
{
    if (direction != VisibleIndexTools::Tail && direction != VisibleIndexTools::Head) {
        qCritical() << "VisibleIndex: invalid direction" << direction
                    << "- refusing to answer";
        return std::nullopt;
    }

    return direction == VisibleIndexTools::Tail ? VisibleIndex::Direction::Tail
                                                : VisibleIndex::Direction::Head;
}

}

int VisibleIndexTools::visibleIndexOf(const QVariantList &row, int actualIndex)
{
    const auto entries = toRowEntries(row);

    if (!entries) {
        return -1;
    }

    return VisibleIndex::visibleIndexOf(*entries, actualIndex).value_or(-1);
}

int VisibleIndexTools::clientVisibleIndexOf(const QVariantList &row, int actualIndex, int hostBase)
{
    const auto entries = toRowEntries(row);

    if (!entries) {
        return -1;
    }

    return VisibleIndex::clientVisibleIndexOf(*entries, actualIndex, hostBase).value_or(-1);
}

bool VisibleIndexTools::belongsAtVisibleIndex(const QVariantList &row, int entryIndex, int visibleIndex)
{
    const auto entries = toRowEntries(row);

    return entries && VisibleIndex::belongsAtVisibleIndex(*entries, entryIndex, visibleIndex);
}

int VisibleIndexTools::firstVisibleIndex(const QVariantList &row)
{
    const auto entries = toRowEntries(row);

    if (!entries) {
        return -1;
    }

    return VisibleIndex::firstVisibleIndex(*entries).value_or(-1);
}

int VisibleIndexTools::lastVisibleIndex(const QVariantList &row)
{
    const auto entries = toRowEntries(row);

    if (!entries) {
        return -1;
    }

    return VisibleIndex::lastVisibleIndex(*entries).value_or(-1);
}

int VisibleIndexTools::countVisibleItems(const QVariantList &row)
{
    const auto entries = toRowEntries(row);

    if (!entries) {
        return 0;
    }

    return VisibleIndex::countVisibleItems(*entries);
}

bool VisibleIndexTools::edgeItemIsSeparator(const QVariantList &row, int direction)
{
    const auto entries = toRowEntries(row);
    const auto dir = toDirection(direction);

    return entries && dir && VisibleIndex::edgeItemIsSeparator(*entries, *dir);
}

QVariantMap VisibleIndexTools::hiddenSkippingNeighbor(const QVariantList &row, int actualIndex, int direction)
{
    QVariantMap none;
    none.insert(QStringLiteral("index"), -1);
    none.insert(QStringLiteral("isSeparator"), false);

    const auto entries = toRowEntries(row);
    const auto dir = toDirection(direction);

    if (!entries || !dir) {
        return none;
    }

    const std::optional<RowEntry> neighbor = VisibleIndex::hiddenSkippingNeighbor(*entries, actualIndex, *dir);

    if (!neighbor) {
        return none;
    }

    QVariantMap out;
    out.insert(QStringLiteral("index"), neighbor->index);
    out.insert(QStringLiteral("isSeparator"), neighbor->isSeparator);
    return out;
}

bool VisibleIndexTools::neighborIsSeparator(const QVariantList &row, int actualIndex, int direction)
{
    const auto entries = toRowEntries(row);
    const auto dir = toDirection(direction);

    return entries && dir && VisibleIndex::neighborIsSeparator(*entries, actualIndex, *dir);
}

bool VisibleIndexTools::neighborIsVisibleSeparator(const QVariantList &row, int actualIndex, int direction)
{
    const auto entries = toRowEntries(row);
    const auto dir = toDirection(direction);

    return entries && dir && VisibleIndex::neighborIsVisibleSeparator(*entries, actualIndex, *dir);
}

bool VisibleIndexTools::isInMarginsArea(const QVariantList &row, int actualIndex)
{
    const auto entries = toRowEntries(row);

    return entries && VisibleIndex::isInMarginsArea(*entries, actualIndex);
}

int VisibleIndexTools::assignedLayoutIndex(const QVariantList &countedChildren, int selfPosition, int beginIndex)
{
    if (selfPosition < 0 || selfPosition >= countedChildren.size() || beginIndex < 0) {
        qCritical() << "VisibleIndex: assignedLayoutIndex out of contract (selfPosition"
                    << selfPosition << "of" << countedChildren.size() << ", beginIndex"
                    << beginIndex << ") - refusing to answer";
        return -1;
    }

    std::vector<bool> counted;
    counted.reserve(static_cast<std::size_t>(countedChildren.size()));

    for (const QVariant &variant : countedChildren) {
        counted.push_back(variant.toBool());
    }

    return VisibleIndex::assignedLayoutIndex(counted, selfPosition, beginIndex).value_or(-1);
}

}
