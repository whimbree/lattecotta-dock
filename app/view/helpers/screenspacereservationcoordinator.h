/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QObject>
#include <QRect>

#include <Plasma/Plasma>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace Latte {

class Corona;
class View;

namespace ViewPart {

class ScreenSpaceReservation;

struct ScreenSpaceReservationContribution
{
    std::uint64_t persistentDockId;
    int contributionDepth;
};

//! One committed output-edge publisher and every dock contributing to it.
//! The generation is assigned only after the whole projection transaction
//! succeeds, so it never names a partially published state.
struct ScreenSpaceReservationGroupSnapshot
{
    int outputId;
    Plasma::Types::Location edge;
    std::uint64_t generation;
    int publishedDepth;
    std::vector<ScreenSpaceReservationContribution> contributions;
    const ScreenSpaceReservation *publisher;
};

//! One atomic coordinator read. stateGeneration advances after every
//! successful membership, depth, output, edge, geometry or teardown change.
//! An empty groups vector at a newer generation makes last-member teardown
//! observable without retaining a dead publisher record.
struct ScreenSpaceReservationSnapshot
{
    std::uint64_t stateGeneration;
    std::vector<ScreenSpaceReservationGroupSnapshot> groups;
};

//! Owns the single positive-exclusive publisher for each persistent Latte
//! output identity and edge. Views contribute requested depths but retain
//! independent visual and input surfaces.
class ScreenSpaceReservationCoordinator final : public QObject
{
    Q_OBJECT

public:
    explicit ScreenSpaceReservationCoordinator(Corona *corona);
    ~ScreenSpaceReservationCoordinator() override;

    void updateReservation(
        View &view,
        const QRect &strutGeometry,
        Plasma::Types::Location location);
    void removeReservation(View &view);

    [[nodiscard]] std::optional<ScreenSpaceReservationSnapshot>
    snapshot() const;

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace ViewPart
} // namespace Latte
