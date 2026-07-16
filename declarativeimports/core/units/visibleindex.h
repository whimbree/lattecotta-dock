/*
    SPDX-FileCopyrightText: 2020 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef VISIBLEINDEX_H
#define VISIBLEINDEX_H

// local
#include "rowentry.h"

// Qt
#include <QtGlobal>

// C++
#include <algorithm>
#include <optional>
#include <vector>

namespace Latte {

//! Visible-index mapping and separator/hidden neighbor predicates over an
//! item row (EX-06 in docs/QML_EXTRACTION_PLAN.md), extracted from four
//! hand-maintained QML twins that had already drifted structurally: the
//! containment IndexerPrivate.qml/Indexer.qml pair, the client abilities
//! Indexer.qml, and the AppletItem.qml/BasicItem.qml while-loop neighbor
//! walks. The QML keeps its collector Bindings (they read live children)
//! and its live bridge reads (client delegation, row-edge fallbacks) and
//! asks here for every verdict.
//!
//! Row semantics (rowentry.h): entries are keyed by their own index
//! field; indexes may have gaps (the containment's endLayout starts at a
//! deliberately high beginIndex); an absent index is "nothing there" -
//! not hidden, not a separator - exactly how the Qt5 walks treated an
//! integer no membership array contained.
namespace VisibleIndex {

enum class Direction {
    Tail, //! toward lower indexes
    Head  //! toward higher indexes
};

//! the collector contract (rowentry.h): rows never carry unindexed
//! entries or negative sub-item counts. Asserted at every engine entry
//! point (QT_FORCE_ASSERTS in the unit tests) so a collector bug fails at
//! the defect instead of three subsystems away; the QML wrapper
//! additionally refuses malformed rows loudly at runtime, since that is
//! the boundary where outside input arrives.
inline bool isWellFormedRow(const RowEntries &entries)
{
    return std::all_of(entries.cbegin(), entries.cend(), [](const RowEntry &entry) {
        return entry.index >= 0 && entry.subItemCount >= 0;
    });
}

//! how many visible items sit strictly before actualIndex, expanding
//! multi-item applets to their visible sub-item counts
//! (IndexerPrivate.qml's visibleItemsBeforeCount summed over the three
//! layouts; the client twin is the same sum with every subItemCount at 1)
inline int countVisibleItemsBefore(const RowEntries &entries, int actualIndex)
{
    Q_ASSERT(isWellFormedRow(entries));

    int visibleItems = 0;

    for (const RowEntry &entry : entries) {
        if (entry.index < actualIndex && isVisibleItem(entry)) {
            visibleItems += entry.subItemCount;
        }
    }

    return visibleItems;
}

//! the 1-based visible index of an entry, nullopt when it owns none.
//! Qt5 subtlety preserved: a margins-area separator is skipped when
//! counting OTHERS' positions yet still owns a visible index itself
//! (Qt5's visibleIndex vetoed only separators[] and hidden[] membership).
//! Deliberate deviation from Qt5: an index present in NO entry owns
//! nothing (nullopt). Qt5 answered 1 + countBefore for it - a phantom
//! position that let an index -1 splitter probe or a transient re-index
//! window hand callers a real-looking badge slot.
inline std::optional<int> visibleIndexOf(const RowEntries &entries, int actualIndex)
{
    Q_ASSERT(isWellFormedRow(entries));

    const std::optional<RowEntry> entry = entryAt(entries, actualIndex);

    if (!entry || entry->isSeparator || entry->isHidden) {
        return std::nullopt;
    }

    return countVisibleItemsBefore(entries, actualIndex) + 1;
}

//! the client twin's visible index (client Indexer.qml's visibleIndex):
//! the host's visible index of the containing applet replaces the
//! client's own "+1 self slot", so the applet's first visible sub-item
//! answers the applet's own visible index. hostBase arrives from a live
//! bridge read; a standalone client (no bridge) passes Qt5's -1 and keeps
//! the shipped twin's answers, faithful even where they look odd.
inline std::optional<int> clientVisibleIndexOf(const RowEntries &entries, int actualIndex, int hostBase)
{
    const std::optional<int> relative = visibleIndexOf(entries, actualIndex);

    if (!relative) {
        return std::nullopt;
    }

    return hostBase + *relative - 1;
}

//! whether visibleIndex falls on the entry at entryIndex, spanning
//! multi-item applets across their visible sub-items
//! (IndexerPrivate.qml's visibleIndexBelongsAtApplet). An entry owning no
//! visible index matches nothing - Qt5's -1 base fell through to the
//! range branch and let a HIDDEN multi-item applet claim visible indexes
//! 0..count-2 (base -1 + count), misrouting shortcut activation to an
//! invisible applet; the optional base makes that state unrepresentable.
//! An empty multi-item applet still matches its exact base: Qt5's
//! exact-match branch fired before the range check, and the reference
//! generator recorded the shipped QML answering true there.
inline bool belongsAtVisibleIndex(const RowEntries &entries, int entryIndex, int visibleIndex)
{
    Q_ASSERT(isWellFormedRow(entries));

    if (visibleIndex < 0) {
        return false;
    }

    const std::optional<int> base = visibleIndexOf(entries, entryIndex);

    if (!base) {
        return false;
    }

    //! base has a value, so the entry exists
    const int span = std::max(1, entryAt(entries, entryIndex)->subItemCount);

    return visibleIndex >= *base && visibleIndex < *base + span;
}

//! lowest visible-item index, nullopt when nothing is visible (the
//! client Indexer.qml firstVisibleItemIndex binding; -1 at the QML
//! boundary)
inline std::optional<int> firstVisibleIndex(const RowEntries &entries)
{
    Q_ASSERT(isWellFormedRow(entries));

    std::optional<int> lowest;

    for (const RowEntry &entry : entries) {
        if (isVisibleItem(entry) && (!lowest || entry.index < *lowest)) {
            lowest = entry.index;
        }
    }

    return lowest;
}

//! highest visible-item index, nullopt when nothing is visible
inline std::optional<int> lastVisibleIndex(const RowEntries &entries)
{
    Q_ASSERT(isWellFormedRow(entries));

    std::optional<int> highest;

    for (const RowEntry &entry : entries) {
        if (isVisibleItem(entry) && (!highest || entry.index > *highest)) {
            highest = entry.index;
        }
    }

    return highest;
}

//! how many entries are visible items (the client twin's
//! visibleItemsCount: entries, not expanded sub-items)
inline int countVisibleItems(const RowEntries &entries)
{
    Q_ASSERT(isWellFormedRow(entries));

    return static_cast<int>(std::count_if(entries.cbegin(), entries.cend(), isVisibleItem));
}

//! whether a non-hidden separator sits outside the visible run's edge:
//! before the first visible item (Tail; the client twin's
//! firstTailItemIsSeparator) or after the last one (Head;
//! lastHeadItemIsSeparator). Qt5 asymmetry preserved: with no visible
//! items both bounds collapse to -1, so Tail scans nothing while Head
//! scans the whole row - a separator-only row answers false/true.
inline bool edgeItemIsSeparator(const RowEntries &entries, Direction direction)
{
    Q_ASSERT(isWellFormedRow(entries));

    const bool towardTail = (direction == Direction::Tail);
    const int bound = towardTail ? firstVisibleIndex(entries).value_or(-1)
                                 : lastVisibleIndex(entries).value_or(-1);

    return std::any_of(entries.cbegin(), entries.cend(), [towardTail, bound](const RowEntry &entry) {
        const bool outsideBound = towardTail ? entry.index < bound : entry.index > bound;
        return outsideBound && entry.isSeparator && !entry.isHidden;
    });
}

//! the nearest neighbor entry with every hidden entry walked over
//! (AppletItem.qml's tail/head walk). Resolves to nullopt when the walk
//! lands where no entry is - off the row's tail edge, past its head, or
//! on a between-layouts gap index. The containment shell still asks a
//! live client bridge for the landing entry's own edge state when it
//! manages sub-indexed items; this only resolves WHO the neighbor is.
inline std::optional<RowEntry> hiddenSkippingNeighbor(const RowEntries &entries, int actualIndex, Direction direction)
{
    Q_ASSERT(isWellFormedRow(entries));

    const int step = (direction == Direction::Tail) ? -1 : 1;

    //! terminates: each pass either returns, or steps over an existing
    //! hidden entry - and there are finitely many entries
    for (int at = actualIndex + step; at >= 0; at += step) {
        const std::optional<RowEntry> entry = entryAt(entries, at);

        if (!entry) {
            return std::nullopt;
        }

        if (!entry->isHidden) {
            return entry;
        }
    }

    return std::nullopt; //! walked off the tail edge
}

//! whether the nearest non-hidden neighbor is a separator, hidden
//! separators invisible (the verdict half of AppletItem.qml's
//! tail/headAppletIsSeparator and of BasicItem.qml's
//! tail/headItemIsVisibleSeparator). A separator asks nothing about its
//! own neighbors (the Qt5 self guard), and a missing self owns no
//! neighbors either.
inline bool neighborIsVisibleSeparator(const RowEntries &entries, int actualIndex, Direction direction)
{
    Q_ASSERT(isWellFormedRow(entries));

    const std::optional<RowEntry> self = entryAt(entries, actualIndex);

    if (!self || self->isSeparator) {
        return false;
    }

    const std::optional<RowEntry> neighbor = hiddenSkippingNeighbor(entries, actualIndex, direction);

    return neighbor && neighbor->isSeparator;
}

//! whether the nearest neighbor is a separator when only hidden
//! NON-separators are walked over: a hidden separator stops the walk and
//! still counts (BasicItem.qml's tail/headItemIsSeparator rule - a
//! separator hidden by settings still shapes its neighbors' spacing)
inline bool neighborIsSeparator(const RowEntries &entries, int actualIndex, Direction direction)
{
    Q_ASSERT(isWellFormedRow(entries));

    const std::optional<RowEntry> self = entryAt(entries, actualIndex);

    if (!self || self->isSeparator) {
        return false;
    }

    const int step = (direction == Direction::Tail) ? -1 : 1;

    for (int at = actualIndex + step; at >= 0; at += step) {
        const std::optional<RowEntry> entry = entryAt(entries, at);

        if (!entry) {
            return false; //! a gap or the head edge: nothing there to be a separator
        }

        if (!entry->isHidden || entry->isSeparator) {
            return entry->isSeparator;
        }
    }

    return false; //! walked off the tail edge
}

//! whether the entry sits inside the margins area: an odd number of
//! margins-area separators before it, each one toggling the area along
//! the row (AppletItem.qml's inMarginsArea - its Qt5 comment said "even"
//! but the shipped test was parity 1, kept here). A margins separator is
//! never itself inside.
inline bool isInMarginsArea(const RowEntries &entries, int actualIndex)
{
    Q_ASSERT(isWellFormedRow(entries));

    const std::optional<RowEntry> self = entryAt(entries, actualIndex);

    if (self && self->isMarginsSeparator) {
        return false;
    }

    const auto precedesAsMarginsSeparator = [actualIndex](const RowEntry &entry) {
        return entry.isMarginsSeparator && entry.index < actualIndex;
    };
    const auto count = std::count_if(entries.cbegin(), entries.cend(), precedesAsMarginsSeparator);

    return count % 2 == 1;
}

//! index assignment from a layout child scan (AppletItem.qml's
//! checkIndex, the fe63a63e semantics: parabolic edge spacers and
//! internal view splitters are uncounted): beginIndex + the rank of self
//! among counted children, nullopt when self is itself uncounted (such
//! children keep index -1)
inline std::optional<int> assignedLayoutIndex(const std::vector<bool> &countedChildren, int selfPosition, int beginIndex)
{
    Q_ASSERT(selfPosition >= 0 && selfPosition < static_cast<int>(countedChildren.size()));
    Q_ASSERT(beginIndex >= 0);

    if (!countedChildren[static_cast<std::size_t>(selfPosition)]) {
        return std::nullopt;
    }

    const auto begin = countedChildren.cbegin();
    const int rank = static_cast<int>(std::count(begin, begin + selfPosition, true));

    return beginIndex + rank;
}

}
}

#endif
