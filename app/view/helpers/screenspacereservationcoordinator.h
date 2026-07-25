/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#pragma once

#include <QObject>
#include <QRect>

#include <Plasma/Plasma>

#include <cstddef>
#include <memory>
#include <optional>

namespace Latte {

class Corona;
class View;

namespace ViewPart {

class ScreenSpaceReservation;

//! One view's projection into the shared output-edge publisher.
struct ScreenSpaceReservationMembership
{
    int outputId;
    Plasma::Types::Location edge;
    int contributionDepth;
    int publishedDepth;
    std::size_t memberCount;
    const ScreenSpaceReservation *publisher;
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

    [[nodiscard]] std::optional<ScreenSpaceReservationMembership>
    findMembership(const View &view) const;

private:
    class Private;
    std::unique_ptr<Private> d;
};

} // namespace ViewPart
} // namespace Latte
