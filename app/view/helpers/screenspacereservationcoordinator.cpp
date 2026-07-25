/*
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-FileCopyrightText: 2026 Latte Dock contributors

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "screenspacereservationcoordinator.h"

#include "screenspacereservation.h"
#include "screenspacereservationledger.h"
#include "../positioner.h"
#include "../view.h"
#include "../../lattecorona.h"
#include "../../screenpool.h"
#include "../../wm/waylandlayershell.h"

#include <QDebug>
#include <QMetaObject>
#include <QPointer>
#include <QScreen>

#include <map>
#include <ranges>
#include <utility>

namespace Latte {
namespace ViewPart {

namespace {

[[nodiscard]] constexpr std::optional<ReservationEdge> toReservationEdge(
    const Plasma::Types::Location location) noexcept
{
    switch (location) {
    case Plasma::Types::TopEdge:
        return ReservationEdge::Top;
    case Plasma::Types::BottomEdge:
        return ReservationEdge::Bottom;
    case Plasma::Types::LeftEdge:
        return ReservationEdge::Left;
    case Plasma::Types::RightEdge:
        return ReservationEdge::Right;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] constexpr Plasma::Types::Location toPlasmaLocation(
    const ReservationEdge edge) noexcept
{
    switch (edge) {
    case ReservationEdge::Top:
        return Plasma::Types::TopEdge;
    case ReservationEdge::Bottom:
        return Plasma::Types::BottomEdge;
    case ReservationEdge::Left:
        return Plasma::Types::LeftEdge;
    case ReservationEdge::Right:
        return Plasma::Types::RightEdge;
    }

    Q_UNREACHABLE();
}

[[nodiscard]] constexpr bool touchesRequestedEdge(
    const QRect &strutGeometry,
    const QRect &outputGeometry,
    const Plasma::Types::Location location) noexcept
{
    switch (location) {
    case Plasma::Types::TopEdge:
        return strutGeometry.top() == outputGeometry.top();
    case Plasma::Types::BottomEdge:
        return strutGeometry.bottom() == outputGeometry.bottom();
    case Plasma::Types::LeftEdge:
        return strutGeometry.left() == outputGeometry.left();
    case Plasma::Types::RightEdge:
        return strutGeometry.right() == outputGeometry.right();
    default:
        return false;
    }
}

[[nodiscard]] QRect reservationGeometry(
    const QRect &outputGeometry,
    const ReservationEdge edge,
    const ReservationDepth depth)
{
    const int pixels = depth.pixels();
    switch (edge) {
    case ReservationEdge::Top:
        return QRect(outputGeometry.left(), outputGeometry.top(),
                     outputGeometry.width(), pixels);
    case ReservationEdge::Bottom:
        return QRect(
            outputGeometry.left(),
            outputGeometry.bottom() - pixels
                + 1, // +1 is needed in order to not leave a gap at screen_edge.
            outputGeometry.width(),
            pixels);
    case ReservationEdge::Left:
        return QRect(outputGeometry.left(), outputGeometry.top(),
                     pixels, outputGeometry.height());
    case ReservationEdge::Right:
        return QRect(
            outputGeometry.right() - pixels
                + 1, // +1 is needed in order to not leave a gap at screen_edge.
            outputGeometry.top(),
            pixels,
            outputGeometry.height());
    }

    Q_UNREACHABLE();
}

}

class ScreenSpaceReservationCoordinator::Private
{
public:
    explicit Private(
        ScreenSpaceReservationCoordinator *const q,
        Corona *const corona)
        : q(q)
        , corona(corona)
    {
        Q_ASSERT(q);
        Q_ASSERT(corona);
    }

    ~Private()
    {
        for (const auto &[member, runtime] : members) {
            Q_UNUSED(member);
            QObject::disconnect(runtime.destroyedConnection);
        }
        publishers.clear();
    }

    void updateReservation(
        View &view,
        const QRect &strutGeometry,
        const Plasma::Types::Location location)
    {
        const auto member = memberFor(view);
        const auto edge = toReservationEdge(location);
        QScreen *const screen = view.screen();
        if (!member || !edge || !screen) {
            qCritical() << "reservation coordinator refused incomplete view identity"
                        << "containment="
                        << (view.containment() ? view.containment()->id() : 0)
                        << "location=" << static_cast<int>(location)
                        << "screen=" << screen;
            removeReservation(view);
            return;
        }

        const int outputId = corona->screenPool()->id(screen->name());
        const auto output = ReservationOutputId::fromPersistentId(outputId);
        const QRect outputGeometry = screen->geometry();
        const int requestedDepth =
            WindowSystem::LayerShell::exclusiveZoneFor(strutGeometry, location);
        const auto depth = ReservationDepth::fromPixels(requestedDepth);

        if (!output || !corona->screenPool()->hasScreenId(outputId)
                || !outputGeometry.isValid()
                || !strutGeometry.isValid()
                || !outputGeometry.contains(strutGeometry)
                || !touchesRequestedEdge(strutGeometry, outputGeometry, location)
                || !depth) {
            qCritical() << "reservation coordinator refused invalid contribution"
                        << "containment=" << member->value()
                        << "outputId=" << outputId
                        << "location=" << static_cast<int>(location)
                        << "strut=" << strutGeometry
                        << "output=" << outputGeometry;
            removeReservation(view);
            return;
        }

        const auto existingRuntime = members.find(*member);
        if (existingRuntime != members.end()
                && existingRuntime->second.view.data() != &view) {
            qCritical() << "reservation coordinator refused duplicate persistent dock identity"
                        << member->value();
            return;
        }

        if (existingRuntime == members.end()) {
            MemberRuntime runtime;
            runtime.view = &view;
            runtime.destroyedConnection = QObject::connect(
                &view,
                &QObject::destroyed,
                q,
                [this, member = *member, address = &view]() {
                    removeDestroyedReservation(member, address);
                });
            members.emplace(*member, std::move(runtime));
        }

        const ReservationGroupKey group{*output, *edge};
        const ReservationLedgerChange change =
            ledger.updateContribution(*member, group, *depth);
        if (change.changed) {
            for (const ReservationGroupKey affected : change.affectedGroups) {
                reconcileGroup(affected);
            }
        } else {
            //! A compositor output can change geometry without changing this
            //! member's identity or depth. Re-project unchanged policy state
            //! so the publisher follows the new QScreen rectangle.
            reconcileGroup(group);
        }
    }

    void removeReservation(View &view)
    {
        const auto memberEntry = findRuntimeMember(view);
        if (memberEntry == members.end()) {
            return;
        }

        const ReservationMemberId member = memberEntry->first;
        QObject::disconnect(memberEntry->second.destroyedConnection);
        members.erase(memberEntry);

        const ReservationLedgerChange change = ledger.removeContribution(member);
        for (const ReservationGroupKey affected : change.affectedGroups) {
            reconcileGroup(affected);
        }
    }

    [[nodiscard]] std::optional<ScreenSpaceReservationMembership>
    findMembership(const View &view) const
    {
        const auto memberEntry = findRuntimeMember(view);
        if (memberEntry == members.end()) {
            return std::nullopt;
        }

        const auto group = ledger.findGroup(memberEntry->first);
        const auto depth = ledger.findContributionDepth(memberEntry->first);
        if (!group || !depth) {
            qCritical() << "reservation coordinator found runtime membership without ledger state";
            return std::nullopt;
        }

        const auto state = ledger.describeGroup(*group);
        const auto publisher = publishers.find(*group);
        if (!state || publisher == publishers.end()
                || !publisher->second.surface) {
            qCritical() << "reservation coordinator found ledger membership without a publisher";
            return std::nullopt;
        }

        return ScreenSpaceReservationMembership{
            group->output.value(),
            toPlasmaLocation(group->edge),
            depth->pixels(),
            state->maximumDepth.pixels(),
            state->memberCount,
            publisher->second.surface.get()};
    }

private:
    struct MemberRuntime
    {
        QPointer<View> view;
        QMetaObject::Connection destroyedConnection;
    };

    struct Publisher
    {
        std::unique_ptr<ScreenSpaceReservation> surface;
    };

    using Members = std::map<ReservationMemberId, MemberRuntime>;

    [[nodiscard]] static std::optional<ReservationMemberId> memberFor(
        const View &view)
    {
        return view.containment()
                ? ReservationMemberId::fromPersistentDockId(
                    view.containment()->id())
                : std::nullopt;
    }

    [[nodiscard]] Members::iterator findRuntimeMember(const View &view)
    {
        return std::ranges::find_if(
            members,
            [&view](const auto &entry) {
                return entry.second.view.data() == &view;
            });
    }

    [[nodiscard]] Members::const_iterator findRuntimeMember(
        const View &view) const
    {
        return std::ranges::find_if(
            members,
            [&view](const auto &entry) {
                return entry.second.view.data() == &view;
            });
    }

    void removeDestroyedReservation(
        const ReservationMemberId member,
        const View *const address)
    {
        const auto runtime = members.find(member);
        if (runtime == members.end()) {
            qCritical() << "reservation coordinator received destruction for an unregistered member"
                        << member.value();
            return;
        }
        if (runtime->second.view
                && runtime->second.view.data() != address) {
            qCritical() << "reservation coordinator refused destruction from a stale view generation"
                        << member.value();
            return;
        }

        members.erase(runtime);
        const ReservationLedgerChange change =
            ledger.removeContribution(member);
        for (const ReservationGroupKey affected : change.affectedGroups) {
            reconcileGroup(affected);
        }
    }

    [[nodiscard]] QScreen *findScreenForGroup(
        const ReservationGroupKey group) const
    {
        for (const auto &[member, runtime] : members) {
            const auto memberGroup = ledger.findGroup(member);
            if (memberGroup && *memberGroup == group && runtime.view) {
                QScreen *const screen = runtime.view->screen();
                if (screen
                        && corona->screenPool()->id(screen->name())
                            == group.output.value()) {
                    return screen;
                }
            }
        }

        return nullptr;
    }

    void reconcileGroup(const ReservationGroupKey group)
    {
        const auto state = ledger.describeGroup(group);
        if (!state) {
            publishers.erase(group);
            return;
        }

        QScreen *const screen = findScreenForGroup(group);
        if (!screen) {
            qCritical() << "reservation coordinator could not resolve group output"
                        << group.output.value()
                        << static_cast<int>(group.edge);
            return;
        }

        const QRect outputGeometry = screen->geometry();
        const QRect geometry =
            reservationGeometry(outputGeometry, group.edge, state->maximumDepth);
        auto publisher = publishers.find(group);
        if (publisher == publishers.end()) {
            Publisher next;
            next.surface = std::make_unique<ScreenSpaceReservation>(
                group.output.value(),
                toPlasmaLocation(group.edge));
            publisher = publishers.emplace(group, std::move(next)).first;
        }

        if (!publisher->second.surface->publish(
                screen,
                geometry,
                toPlasmaLocation(group.edge))) {
            qCritical() << "reservation coordinator failed to project group"
                        << group.output.value()
                        << static_cast<int>(group.edge)
                        << geometry;
        }
    }

    ScreenSpaceReservationCoordinator *const q;
    Corona *const corona;
    ScreenSpaceReservationLedger ledger;
    Members members;
    std::map<ReservationGroupKey, Publisher> publishers;
};

ScreenSpaceReservationCoordinator::ScreenSpaceReservationCoordinator(
    Corona *const corona)
    : QObject(corona)
    , d(std::make_unique<Private>(this, corona))
{
}

ScreenSpaceReservationCoordinator::~ScreenSpaceReservationCoordinator() =
    default;

void ScreenSpaceReservationCoordinator::updateReservation(
    View &view,
    const QRect &strutGeometry,
    const Plasma::Types::Location location)
{
    d->updateReservation(view, strutGeometry, location);
}

void ScreenSpaceReservationCoordinator::removeReservation(View &view)
{
    d->removeReservation(view);
}

std::optional<ScreenSpaceReservationMembership>
ScreenSpaceReservationCoordinator::findMembership(const View &view) const
{
    return d->findMembership(view);
}

} // namespace ViewPart
} // namespace Latte
