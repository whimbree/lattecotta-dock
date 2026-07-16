/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef PARABOLICROUTER_H
#define PARABOLICROUTER_H

// local
#include "parabolicmath.h"

// Qt
#include <QVector>
#include <QtGlobal>

// C++
#include <optional>
#include <variant>

namespace Latte {

//! The parabolic scale-propagation ROUTING as one pure walk (EX-02 in
//! docs/QML_EXTRACTION_PLAN.md; the design section there carries the full
//! semantics inventory read out of the distributed chain). The chain this
//! replaces was synchronous per-item signal recursion; the walk computes
//! the identical outcome in one call. Equality-tested against the real
//! shipped chain driven offscreen (parabolicroutertest's table).
//!
//! The result is an EMISSION PLAN for the shells, not a per-item scale
//! table: the shells keep the sglUpdateLower/HigherItemScale signals as
//! their application mechanism, with two receiver arms per item slot -
//! exact match (apply newScales[0] / hand the stack to a bridge client
//! as-received) and the clear-broadcast arm (a [1] emission clears every
//! item beyond its position in the emission's direction). That broadcast
//! arm is what lets ONE clear emission clear items the row model cannot
//! see (the containment's other layouts); the core therefore reports
//! WHERE the clear emission happens instead of expanding it:
//!
//! - actions: the live walk only - every position outward from the entry
//!   is exactly-targeted once while the stack is live; separators/hidden
//!   items are transparent (forward without consuming); normals consume
//!   stack[0]; a bridge client receives the stack AS-RECEIVED and the
//!   live walk STOPS at this level (the client's row routes with this
//!   same core and its overflow re-enters through the existing bridge
//!   surface); an edge spacer targeted with a live stack absorbs
//!   sum of (s-1) over the first spreadSteps entries; a DEAD position
//!   (an item with no connected slots: production case is a
//!   zoom-unsupported lockZoom applet with thin tooltips disabled, so
//!   its ParabolicArea loader never activates) kills the walk outright -
//!   no emission, no clear, recorded in the dead_* harness cases;
//! - clearEmissionPos: when the stack exhausts to the clear-tail [1]
//!   in-row, the position the [1] emission targets (after edge spacers
//!   chain their absorb-0; a spacer exactly targeted by the clear
//!   absorbs 0, spacers further out are untouched - they have no
//!   broadcast arm, the stale-length behavior this leaves at edges is
//!   Qt5-inherited and pinned by the table's stale case). The emission's
//!   exact arm applies 1 at this position (nothing when it is dead) and
//!   its broadcast arm clears everything beyond;
//! - overflow: the stack still traveling when the walk left the row
//!   edge - [1] when a clear-tail chained off the edge through spacers,
//!   the live remainder when the row simply ended. The containment shell
//!   re-emits it at the boundary index (today's chain emits there too:
//!   gap indexes match nobody exactly, only clear broadcasts act); the
//!   plasmoid shell exports it through the bridge.
namespace ParabolicRouter {

enum class ItemKind {
    Normal,      //!< consumes one scale
    Transparent, //!< separator / margins-area separator / hidden: forwards
    EdgeSpacer,  //!< absorbs on exact targeting only
    BridgeClient,//!< receives the stack as-is; live walk stops here
    DeadStop     //!< no connected slots: a live walk dies here silently
};

struct RowItem {
    ItemKind kind = ItemKind::Normal;
    //! spacers only: false when the view alignment makes the spacer inert
    //! (non-centered alignments set length 0 today); the shell owns the
    //! alignment read
    bool absorbing = true;
};

//! per-kind action payloads (step-2.5 type discipline: an ApplyScale
//! cannot carry a handoff stack, a ClientHandoff cannot carry an absorb
//! factor, and no action exists without a position)
struct ApplyScale {
    int pos;
    double scale;
};

struct SpacerAbsorb {
    int pos;
    double factor; //!< length = factor * totals
};

struct ClientHandoff {
    int pos;
    QVector<double> stack; //!< as-received
};

using Action = std::variant<ApplyScale, SpacerAbsorb, ClientHandoff>;

struct RouteResult {
    //! the live walk's applications, in walk order
    QVector<Action> actions;
    //! in-row target of the clear-tail [1] emission, when one fired; the
    //! plasmoid twin additionally exports [1] through the bridge whenever
    //! this holds a value (the chain's sltTrack* forwarded every in-row
    //! clear-tail emission out)
    std::optional<int> clearEmissionPos;
    //! stack remaining when the walk left the row edge
    QVector<double> overflow;
};

inline bool isClearTail(const QVector<double> &stack)
{
    return stack.size() == 1 && stack.first() == 1.0;
}

//! the terminal clear-tail emission targeting pos: edge spacers exactly
//! there absorb 0 and chain one position further; the first non-spacer
//! position (dead or not) is where the [1] emission happens
inline void emitClearTail(const QVector<RowItem> &row, int pos, int step, RouteResult &result)
{
    while (pos >= 0 && pos < row.size() && row[pos].kind == ItemKind::EdgeSpacer) {
        result.actions.append(SpacerAbsorb{pos, 0.0});
        pos += step;
    }

    if (pos < 0 || pos >= row.size()) {
        //! the emission left the row (or the row ended in spacers)
        result.overflow = {1.0};
        return;
    }

    result.clearEmissionPos = pos;
}

//! route a scale stack entering the row at entryPos, traveling by step
//! (+1 toward higher indexes, -1 toward lower). spreadSteps bounds the
//! spacer absorption window ((spread-1)/2, the spacer's hiddenItemsCount).
//! An entryPos outside the row returns the whole stack as overflow (the
//! chain's emission at a gap index: nothing matches exactly).
inline RouteResult routeStack(const QVector<RowItem> &row, int entryPos, int step,
                              QVector<double> stack, int spreadSteps)
{
    //! step=0 is representable and would loop forever; a negative spread
    //! window would silently absorb nothing
    Q_ASSERT(step == 1 || step == -1);
    Q_ASSERT(spreadSteps >= 0);

    RouteResult result;

    if (stack.isEmpty()) {
        return result;
    }

    int pos = entryPos;

    while (pos >= 0 && pos < row.size()) {
        if (stack.isEmpty()) {
            //! the chain drops empty stacks on match (newScales.length<=0);
            //! unreachable from applyParabolicEffect's 1-terminated stacks
            return result;
        }

        if (isClearTail(stack)) {
            emitClearTail(row, pos, step, result);
            return result;
        }

        const RowItem &item = row[pos];

        switch (item.kind) {
        case ItemKind::Normal:
            result.actions.append(ApplyScale{pos, stack.first()});
            stack.removeFirst();
            break;
        case ItemKind::Transparent:
            break;
        case ItemKind::BridgeClient:
            result.actions.append(ClientHandoff{pos, stack});
            return result;
        case ItemKind::EdgeSpacer: {
            double factor = 0.0;
            if (item.absorbing) {
                const int entries = qMin(spreadSteps, static_cast<int>(stack.size()));
                for (int i = 0; i < entries; ++i) {
                    factor += stack.at(i) - 1.0;
                }
            }
            result.actions.append(SpacerAbsorb{pos, factor});
            emitClearTail(row, pos + step, step, result);
            return result;
        }
        case ItemKind::DeadStop:
            //! no slots are connected here: the live stack dies, nothing
            //! beyond is touched (dead_at3_live_dies in the harness)
            return result;
        }

        pos += step;
    }

    //! walk left the row edge with the stack still live
    result.overflow = stack;
    return result;
}

//! both directions from the hovered position, from EX-03's stacks
struct Assignment {
    RouteResult lower;
    RouteResult higher;
};

inline Assignment assignScales(const QVector<RowItem> &row, int hoveredPos,
                               const ParabolicMath::ScaleStacks &stacks, int spreadSteps)
{
    Assignment a;
    a.lower = routeStack(row, hoveredPos - 1, -1, stacks.left, spreadSteps);
    a.higher = routeStack(row, hoveredPos + 1, +1, stacks.right, spreadSteps);
    return a;
}

}
}

#endif
