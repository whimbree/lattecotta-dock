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

#include <LayerShellQt/window.h>

#include <algorithm>
#include <exception>
#include <iterator>
#include <limits>
#include <map>
#include <ranges>
#include <utility>
#include <vector>

namespace Latte {
namespace ViewPart {

namespace {

[[nodiscard]] QScreen *reservationOutputForView(
    const View &view)
{
    const auto *const layerShell =
        view.layerShellWindow();
    return layerShell
        ? layerShell->screen()
        : nullptr;
}

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

    [[nodiscard]] bool updateReservation(
        View &view,
        const QRect &strutGeometry,
        const Plasma::Types::Location location)
    {
        const auto member = memberFor(view);
        const auto edge = toReservationEdge(location);
        QScreen *const screen =
            reservationOutputForView(view);
        if (!member || !edge || !screen) {
            qCritical() << "reservation coordinator refused incomplete view identity"
                        << "containment="
                        << (view.containment() ? view.containment()->id() : 0)
                        << "location=" << static_cast<int>(location)
                        << "screen=" << screen;
            return false;
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
            return false;
        }

        auto existingRuntime = members.find(*member);
        if (existingRuntime != members.end()
                && existingRuntime->second.view.data() != &view) {
            qCritical() << "reservation coordinator refused duplicate persistent dock identity"
                        << member->value();
            return false;
        }

        bool insertedRuntime = false;
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
            existingRuntime =
                members.emplace(*member, std::move(runtime)).first;
            insertedRuntime = true;
        }

        const ReservationGroupKey group{*output, *edge};
        ScreenSpaceReservationLedger candidate = ledger;
        ReservationLedgerChange change =
            candidate.updateContribution(*member, group, *depth);
        if (!change.changed) {
            //! A compositor output can change geometry without changing this
            //! member's identity or depth. Re-project unchanged policy state
            //! so the publisher follows the new QScreen rectangle.
            change.affectedGroups.push_back(group);
        }

        if (!commitProjectionTransaction(
                std::move(candidate),
                change,
                change.changed)) {
            qCritical() << "reservation coordinator retained the previous committed projection"
                        << "containment=" << member->value()
                        << "outputId=" << outputId
                        << "location=" << static_cast<int>(location);
            if (insertedRuntime) {
                QObject::disconnect(
                    existingRuntime->second.destroyedConnection);
                members.erase(existingRuntime);
            }
            return false;
        }

        return true;
    }

    [[nodiscard]] bool removeReservation(View &view)
    {
        const auto memberEntry = findRuntimeMember(view);
        if (memberEntry == members.end()) {
            //! Removal is intentionally idempotent. Visibility retirement and
            //! View destruction can both release the same contribution, but
            //! an absent runtime must agree with the ledger.
            const auto member = memberFor(view);
            if (member && ledger.findGroup(*member)) {
                qCritical() << "reservation coordinator found ledger state without runtime ownership"
                            << member->value();
                std::terminate();
            }
            return true;
        }

        const ReservationMemberId member = memberEntry->first;
        ScreenSpaceReservationLedger candidate = ledger;
        const ReservationLedgerChange change =
            candidate.removeContribution(member);
        if (!change.changed) {
            qCritical() << "reservation coordinator found runtime ownership without ledger state"
                        << member.value();
            std::terminate();
        }

        auto runtime = members.extract(memberEntry);
        if (!commitProjectionTransaction(
                std::move(candidate),
                change,
                true)) {
            members.insert(std::move(runtime));
            qCritical() << "reservation coordinator retained a contribution after teardown projection failed"
                        << member.value();
            return false;
        }
        QObject::disconnect(runtime.mapped().destroyedConnection);
        return true;
    }

    [[nodiscard]] std::optional<ScreenSpaceReservationSnapshot>
    snapshot() const
    {
        if (!ownershipIsConsistent(ledger)) {
            qCritical() << "reservation coordinator refused an inconsistent ownership snapshot";
            return std::nullopt;
        }

        ScreenSpaceReservationSnapshot result;
        result.stateGeneration = stateGeneration;
        result.groups.reserve(ledger.groupCount());

        for (const ReservationGroupKey group : ledger.groups()) {
            const auto state = ledger.describeGroup(group);
            const auto publisher = publishers.find(group);
            const auto generation = groupGenerations.find(group);
            QScreen *const screen = findScreenForGroup(ledger, group);
            if (!state || publisher == publishers.end()
                    || generation == groupGenerations.end()
                    || !projectionMatches(
                        publisher->second,
                        group,
                        *state,
                        screen)) {
                qCritical() << "reservation coordinator refused an unprojected group snapshot"
                            << group.output.value()
                            << static_cast<int>(group.edge);
                return std::nullopt;
            }

            ScreenSpaceReservationGroupSnapshot groupSnapshot{
                group.output.value(),
                toPlasmaLocation(group.edge),
                generation->second,
                state->maximumDepth.pixels(),
                {},
                publisher->second.surface.get()};
            groupSnapshot.contributions.reserve(
                state->contributions.size());
            std::ranges::transform(
                state->contributions,
                std::back_inserter(groupSnapshot.contributions),
                [](const ReservationContributionState &contribution) {
                    return ScreenSpaceReservationContribution{
                        contribution.member.value(),
                        contribution.depth.pixels()};
                });
            result.groups.push_back(std::move(groupSnapshot));
        }

        return result;
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
        QPointer<QScreen> screen;
    };

    struct PreparedProjection
    {
        ReservationGroupKey group;
        bool active;
        QPointer<QScreen> screen;
        std::unique_ptr<ScreenSpaceReservation> replacement;
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

        ScreenSpaceReservationLedger candidate = ledger;
        const ReservationLedgerChange change =
            candidate.removeContribution(member);
        if (!change.changed) {
            qCritical() << "reservation coordinator received destruction without ledger state"
                        << member.value();
            std::terminate();
        }

        auto retiredRuntime = members.extract(runtime);
        if (!commitProjectionTransaction(
                std::move(candidate),
                change,
                true)) {
            qCritical() << "reservation coordinator could not retire a destroyed member transactionally"
                        << member.value();
            std::terminate();
        }
        QObject::disconnect(
            retiredRuntime.mapped().destroyedConnection);
    }

    [[nodiscard]] QScreen *findScreenForGroup(
        const ScreenSpaceReservationLedger &candidate,
        const ReservationGroupKey group) const
    {
        const auto state = candidate.describeGroup(group);
        if (!state) {
            return nullptr;
        }

        QScreen *resolvedScreen = nullptr;
        for (const ReservationContributionState &contribution
                : state->contributions) {
            const auto runtime = members.find(contribution.member);
            if (runtime == members.end() || !runtime->second.view) {
                qCritical() << "reservation coordinator could not resolve contributor runtime"
                            << contribution.member.value();
                return nullptr;
            }

            QScreen *const screen =
                reservationOutputForView(
                    *runtime->second.view);
            if (!screen
                    || corona->screenPool()->id(screen->name())
                        != group.output.value()) {
                qCritical() << "reservation coordinator found contributor on the wrong output"
                            << contribution.member.value()
                            << group.output.value()
                            << screen;
                return nullptr;
            }

            if (resolvedScreen && resolvedScreen != screen) {
                qCritical() << "reservation coordinator found one output identity backed by multiple QScreens"
                            << group.output.value()
                            << resolvedScreen
                            << screen;
                return nullptr;
            }
            resolvedScreen = screen;
        }

        return resolvedScreen;
    }

    [[nodiscard]] bool memberOwnershipIsConsistent(
        const ScreenSpaceReservationLedger &candidate) const
    {
        if (members.size() != candidate.memberCount()) {
            return false;
        }

        for (const auto &[member, runtime] : members) {
            if (!runtime.view
                    || !candidate.findGroup(member)
                    || !candidate.findContributionDepth(member)) {
                return false;
            }
            const auto runtimeMember = memberFor(*runtime.view);
            if (!runtimeMember || *runtimeMember != member) {
                return false;
            }
        }

        return true;
    }

    [[nodiscard]] bool ownershipIsConsistent(
        const ScreenSpaceReservationLedger &candidate) const
    {
        if (!memberOwnershipIsConsistent(candidate)
                || publishers.size() != candidate.groupCount()
                || groupGenerations.size() != candidate.groupCount()) {
            return false;
        }

        return std::ranges::all_of(
            candidate.groups(),
            [this](const ReservationGroupKey group) {
                return publishers.contains(group)
                    && groupGenerations.contains(group);
            });
    }

    [[nodiscard]] static bool projectionMatches(
        const Publisher &publisher,
        const ReservationGroupKey group,
        const ReservationGroupState &state,
        QScreen *const screen)
    {
        if (!screen
                || !publisher.surface
                || publisher.screen.data() != screen) {
            return false;
        }

        const QRect geometry = reservationGeometry(
            screen->geometry(),
            group.edge,
            state.maximumDepth);
        if (!geometry.isValid()
                || publisher.surface->publishedGeometry() != geometry) {
            return false;
        }

        const auto *const layerShell =
            publisher.surface->layerShellWindow();
        //! LayerShellQt's explicit screen controls the compositor output.
        //! QWindow::screen() can retain the previous output until the first
        //! configure arrives, so it is not a synchronous staging invariant.
        if (!layerShell
                || layerShell->screen() != screen) {
            return false;
        }

        const Plasma::Types::Location location =
            toPlasmaLocation(group.edge);
        const auto expected =
            WindowSystem::LayerShell::reservationPlacement(
                location,
                geometry,
                screen->geometry());
        return layerShell->anchors() == expected.anchors
            && layerShell->exclusiveEdge() == expected.exclusiveEdge
            && layerShell->margins() == expected.margins
            && layerShell->exclusionZone() == expected.exclusiveZone;
    }

    [[nodiscard]] bool prepareProjection(
        const ScreenSpaceReservationLedger &candidate,
        const ReservationGroupKey group,
        PreparedProjection &projection) const
    {
        const auto state = candidate.describeGroup(group);
        if (!state) {
            projection.active = false;
            return true;
        }

        QScreen *const screen =
            findScreenForGroup(candidate, group);
        if (!screen || !screen->geometry().isValid()) {
            qCritical() << "reservation coordinator could not resolve candidate group output"
                        << group.output.value()
                        << static_cast<int>(group.edge);
            return false;
        }

        projection.active = true;
        projection.screen = screen;
        const auto existing = publishers.find(group);
        if (existing != publishers.end()
                && projectionMatches(
                    existing->second,
                    group,
                    *state,
                    screen)) {
            return true;
        }

        const QRect geometry = reservationGeometry(
            screen->geometry(),
            group.edge,
            state->maximumDepth);
        auto replacement =
            std::make_unique<ScreenSpaceReservation>(
                group.output.value(),
                toPlasmaLocation(group.edge));
        if (!replacement->publish(
                screen,
                geometry,
                toPlasmaLocation(group.edge))) {
            qCritical() << "reservation coordinator failed to prepare group projection"
                        << group.output.value()
                        << static_cast<int>(group.edge)
                        << geometry;
            return false;
        }

        Publisher staged{
            std::move(replacement),
            screen};
        if (!projectionMatches(
                staged,
                group,
                *state,
                screen)) {
            qCritical() << "reservation coordinator prepared invalid layer-shell state"
                        << group.output.value()
                        << static_cast<int>(group.edge)
                        << geometry;
            return false;
        }

        projection.replacement =
            std::move(staged.surface);
        return true;
    }

    [[nodiscard]] bool commitProjectionTransaction(
        ScreenSpaceReservationLedger candidate,
        const ReservationLedgerChange &change,
        const bool policyChanged)
    {
        if (!memberOwnershipIsConsistent(candidate)) {
            qCritical() << "reservation coordinator refused a candidate with inconsistent runtime ownership";
            return false;
        }

        std::vector<PreparedProjection> projections;
        projections.reserve(change.affectedGroups.size());
        for (const ReservationGroupKey group : change.affectedGroups) {
            PreparedProjection projection{group, false, nullptr, nullptr};
            if (!prepareProjection(
                    candidate,
                    group,
                    projection)) {
                return false;
            }
            projections.push_back(std::move(projection));
        }

        const bool projectionChanged =
            std::ranges::any_of(
                projections,
                [this](const PreparedProjection &projection) {
                    return projection.replacement
                        || (!projection.active
                            && publishers.contains(projection.group));
                });
        if (!policyChanged && !projectionChanged) {
            return true;
        }

        if (stateGeneration
                == std::numeric_limits<std::uint64_t>::max()) {
            qCritical() << "reservation coordinator exhausted its state generation";
            std::terminate();
        }
        const std::uint64_t nextGeneration =
            stateGeneration + 1;

        ledger = std::move(candidate);
        for (PreparedProjection &projection : projections) {
            if (!projection.active) {
                publishers.erase(projection.group);
                groupGenerations.erase(projection.group);
                continue;
            }

            auto publisher = publishers.find(projection.group);
            if (publisher == publishers.end()) {
                if (!projection.replacement) {
                    qCritical() << "reservation coordinator committed a group without a prepared publisher";
                    std::terminate();
                }
                Publisher next{
                    std::move(projection.replacement),
                    projection.screen};
                publishers.emplace(
                    projection.group,
                    std::move(next));
            } else if (projection.replacement) {
                publisher->second.surface =
                    std::move(projection.replacement);
                publisher->second.screen =
                    projection.screen;
            }
            groupGenerations.insert_or_assign(
                projection.group,
                nextGeneration);
        }
        stateGeneration = nextGeneration;

        if (!ownershipIsConsistent(ledger)) {
            qCritical() << "reservation coordinator committed inconsistent ownership";
            std::terminate();
        }
        for (const PreparedProjection &projection : projections) {
            if (!projection.active) {
                continue;
            }
            const auto state =
                ledger.describeGroup(projection.group);
            const auto publisher =
                publishers.find(projection.group);
            QScreen *const screen =
                findScreenForGroup(ledger, projection.group);
            if (!state || publisher == publishers.end()
                    || !projectionMatches(
                        publisher->second,
                        projection.group,
                        *state,
                        screen)) {
                qCritical() << "reservation coordinator committed an invalid projection"
                            << projection.group.output.value()
                            << static_cast<int>(projection.group.edge);
                std::terminate();
            }
        }

        return true;
    }

    ScreenSpaceReservationCoordinator *const q;
    Corona *const corona;
    ScreenSpaceReservationLedger ledger;
    Members members;
    std::map<ReservationGroupKey, Publisher> publishers;
    std::map<ReservationGroupKey, std::uint64_t> groupGenerations;
    std::uint64_t stateGeneration{0};
};

ScreenSpaceReservationCoordinator::ScreenSpaceReservationCoordinator(
    Corona *const corona)
    : QObject(corona)
    , d(std::make_unique<Private>(this, corona))
{
}

ScreenSpaceReservationCoordinator::~ScreenSpaceReservationCoordinator() =
    default;

bool ScreenSpaceReservationCoordinator::updateReservation(
    View &view,
    const QRect &strutGeometry,
    const Plasma::Types::Location location)
{
    return d->updateReservation(
        view,
        strutGeometry,
        location);
}

bool ScreenSpaceReservationCoordinator::removeReservation(View &view)
{
    return d->removeReservation(view);
}

std::optional<ScreenSpaceReservationSnapshot>
ScreenSpaceReservationCoordinator::snapshot() const
{
    return d->snapshot();
}

} // namespace ViewPart
} // namespace Latte
