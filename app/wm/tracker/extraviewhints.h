/*
    SPDX-FileCopyrightText: 2026 Latte Dock contributors
    SPDX-FileCopyrightText: 2026 David Goree <davidgoree2003@gmail.com> (latte-dock-qt6, derived)
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

//! The horizontal-touching-busy-vertical bucket pass (EX-23 in
//! docs/tracking/QML_EXTRACTION_PLAN.md), adopted from David Goree's latte-dock-qt6
//! (app/wm/tracker/extraviewhints.h at 5b58c0a3,
//! github.com/CaptSilver/latte-dock-qt6): an O(views) restructure of our
//! O(n^2) updateExtraViewHints loop with equivalent semantics (their slots pin
//! same-screen bucketing, wrong-location pairing, disabled/not-tracking
//! skips and callback gating). The edge-touch check stays a callback
//! seam because it reads the live view geometry.

#pragma once

#include <functional>

#include <QHash>
#include <QList>
#include <QRect>

#include <Plasma/Plasma>

namespace Latte {
namespace WindowSystem {
namespace Tracker {

struct TrackedViewGeometry {
    int viewKey{0};
    bool enabled{false};
    bool trackingCurrentActivity{false};
    bool isHorizontal{false};
    bool isVertical{false};
    int screenId{-1};
    Plasma::Types::Location location{Plasma::Types::Floating};
    bool isTouchingTopViewAndIsBusy{false};
    bool isTouchingBottomViewAndIsBusy{false};
    QRect absoluteGeometry;
};

namespace ExtraViewHints {

// Returns horKey -> bool: does this horizontal enabled/tracking view touch a busy vertical on same screen?
// O(views): bucket busy verticals by screenId first, then one pass over horizontals.
inline QHash<int,bool> bucketHorizontalTouchingBusyVertical(
    const QList<TrackedViewGeometry> &views,
    const std::function<bool(const TrackedViewGeometry &hor, const TrackedViewGeometry &ver)> &hasEdgeTouch)
{
    // Bucket enabled+tracking vertical views by screenId
    QHash<int, QList<int>> verIndicesByScreen; // screenId -> indices into views
    for (int i = 0; i < views.size(); ++i) {
        const auto &v = views.at(i);
        if (v.isVertical && v.enabled && v.trackingCurrentActivity) {
            verIndicesByScreen[v.screenId].append(i);
        }
    }

    QHash<int,bool> result;
    for (int i = 0; i < views.size(); ++i) {
        const auto &hor = views.at(i);
        if (!hor.isHorizontal || !hor.enabled || !hor.trackingCurrentActivity) {
            continue;
        }
        bool touching = false;
        const auto &verIndices = verIndicesByScreen.value(hor.screenId);
        for (int j : verIndices) {
            const auto &ver = views.at(j);
            bool topTouch = (hor.location == Plasma::Types::TopEdge) && ver.isTouchingTopViewAndIsBusy && hasEdgeTouch(hor, ver);
            bool bottomTouch = (hor.location == Plasma::Types::BottomEdge) && ver.isTouchingBottomViewAndIsBusy && hasEdgeTouch(hor, ver);
            if (topTouch || bottomTouch) {
                touching = true;
                break;
            }
        }
        result[hor.viewKey] = touching;
    }
    return result;
}

} // namespace ExtraViewHints
} // namespace Tracker
} // namespace WindowSystem
} // namespace Latte
