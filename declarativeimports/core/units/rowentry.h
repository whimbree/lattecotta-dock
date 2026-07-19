/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef ROWENTRY_H
#define ROWENTRY_H

// Qt
#include <QVector>

// C++
#include <optional>

namespace Latte {

//! The shared row-snapshot type family (docs/tracking/QML_EXTRACTION_PLAN.md,
//! section E "shared enums first" note): one entry per indexed item of a
//! dock row, gathered by the QML collector Bindings that read live
//! children. EX-06 VisibleIndexEngine consumes it now; EX-14
//! DropEventClassifier's insertIndexAt is planned to share it. It lands
//! in its own header so a future consumer never has to include an
//! unrelated engine.
//!
//! Data patterns the collectors guarantee (and the engines assert):
//! - every entry in a row has index >= 0 (the collectors filter the
//!   transient index -1 items out, matching the Qt5-era separators[]/
//!   hidden[] collectors' index>=0 rule);
//! - indexes need not be contiguous (the containment's three layouts
//!   deliberately leave a gap before endLayout's high beginIndex);
//! - subItemCount is the number of visible sub-items a multi-item applet
//!   expands to (a tasks applet reports its visible tasks); plain items
//!   are 1, and an empty multi-item applet is legitimately 0.
struct RowEntry {
    //! -1 mirrors the QML items' own "not yet indexed" default; a row
    //! never contains such an entry
    int index{-1};
    bool isSeparator{false};
    bool isHidden{false};
    bool isMarginsSeparator{false};
    int subItemCount{1};
};

using RowEntries = QVector<RowEntry>;

//! bounds-checked lookup: rows are keyed by the entries' own index field,
//! not by list position; a missing index is a legitimate absence (row
//! gaps, off-edge neighbors), never an error
inline std::optional<RowEntry> entryAt(const RowEntries &entries, int index)
{
    for (const RowEntry &entry : entries) {
        if (entry.index == index) {
            return entry;
        }
    }

    return std::nullopt;
}

//! a visible item contributes to visible counting; separators, margins
//! separators and hidden items do not (IndexerPrivate's Qt5-era skip rule)
inline bool isVisibleItem(const RowEntry &entry)
{
    return !entry.isSeparator && !entry.isHidden && !entry.isMarginsSeparator;
}

}

#endif
