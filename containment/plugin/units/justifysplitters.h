/*
    SPDX-FileCopyrightText: 2021 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef JUSTIFYSPLITTERS_H
#define JUSTIFYSPLITTERS_H

// Qt
#include <QList>
#include <QtGlobal>

namespace Latte {
namespace Containment {

//! Justify alignment carves the panel into three zones - start (flush to the
//! leading edge), main (centered) and end (flush to the trailing edge) - by
//! dividing an otherwise flat applet-id list with two splitter markers. The
//! two splitter positions are stored 1-based: a splitter at position p is
//! inserted at 0-based index p-1. For a flat list of N applet ids the first
//! splitter's insert index must land in [0, N] (position in [1, N+1]); the
//! second splitter is inserted into the now N+1 long list, so its index must
//! land in [0, N+1] (position in [1, N+2]) and must not precede the first (the
//! main zone holds pos2-pos1-1 >= 0 applets).
//!
//! The compute path (LayoutManager::save) only ever emits positions inside that
//! range: pos1 = startChilds+1, pos2 = startChilds+mainChilds+2, with all three
//! child counts >= 0. So an out-of-range stored value is ALWAYS a stale or
//! migrated config - the schema default is -1, and a config carried over from
//! an older Latte alignment scheme (alignmentUpgraded) has been caught live
//! holding splitterPosition=0. Feeding 0 straight to QList::insert is
//! insert(0-1) = insert(-1): a negative index, undefined behavior that the
//! RelWithDebInfo dock (NDEBUG strips Q_ASSERT) executes silently and
//! inconsistently (one load showed both splitters adjacent, the next dropped
//! the trailing one). This is the sole place a splitter index reaches
//! QList::insert; it repairs an out-of-range pair to the natural centered
//! layout (first splitter before everything, second after everything, every
//! applet in the main zone) so a bad index can never get there. save() then
//! recomputes and persists valid positions from the real placement.
namespace JustifySplitters {

//! 1-based first/second splitter positions.
struct Positions {
    int first;
    int second;
};

//! The centered fallback used when a stored pair is out of range: everything in
//! the main zone. first=1 puts the leading splitter before all applets;
//! second=appletCount+2 puts the trailing splitter after all of them (its
//! insert index appletCount+1 is the tail of the post-first-insert list).
constexpr Positions centeredPositions(int appletCount)
{
    return Positions{1, appletCount + 2};
}

//! A pair is valid when both insert indices are in range for their respective
//! list lengths and the two splitters keep zone order (second strictly after
//! first). appletCount is the length of the flat applet-id list.
constexpr bool positionsAreValid(int first, int second, int appletCount)
{
    return first >= 1
            && second >= first + 1
            && second <= appletCount + 2;
}

//! Whether a repair was needed, and the positions to actually use.
struct RepairResult {
    Positions positions;
    bool wasRepaired;
};

//! Pure: returns the in-range positions to use, repairing an out-of-range pair
//! to the centered layout. Callers that want to be loud about a repair check
//! wasRepaired (the stored config was stale/migrated) and log before acting.
constexpr RepairResult repairPositions(int first, int second, int appletCount)
{
    if (positionsAreValid(first, second, appletCount)) {
        return RepairResult{Positions{first, second}, false};
    }

    return RepairResult{centeredPositions(appletCount), true};
}

//! The flat applet-id list with two splitter markers (splitterId) inserted at
//! the stored positions, repaired to the centered layout first if they are out
//! of range. THIS IS THE ONLY PATH THAT REACHES QList::insert with a splitter
//! index, so no negative or overrun index can ever get there. The Q_ASSERT is a
//! test-build tripwire (QT_FORCE_ASSERTS): it can only fail if the repair above
//! is ever broken, at which point the negative insert would abort here instead
//! of corrupting the layout three subsystems away.
inline QList<int> insertSplitters(const QList<int> &appletOrder,
                                  int first, int second, int splitterId)
{
    const int appletCount = appletOrder.count();
    const Positions positions = repairPositions(first, second, appletCount).positions;

    Q_ASSERT(positionsAreValid(positions.first, positions.second, appletCount));

    QList<int> order = appletOrder;
    order.insert(positions.first - 1, splitterId);
    order.insert(positions.second - 1, splitterId);
    return order;
}

} // namespace JustifySplitters

}
}

#endif // JUSTIFYSPLITTERS_H
