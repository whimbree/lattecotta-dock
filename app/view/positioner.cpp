/*
    SPDX-FileCopyrightText: 2018 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "positioner.h"

// local
#include <coretypes.h>
#include "effects.h"
#include "floatingpanelgeometry.h"
#include "floatingtransition.h"
#include "positionergeometry.h"
#include "originalview.h"
#include "view.h"
#include "visibilitymanager.h"
#include "../lattecorona.h"
#include "../screenpool.h"
#include "../data/screendata.h"
#include "../data/viewdata.h"
#include "../layout/centrallayout.h"
#include "../layouts/manager.h"
#include "../settings/universalsettings.h"
#include "../wm/abstractwindowinterface.h"

// Qt
#include <QDebug>
#include <QScopedValueRollback>

// C++
#include <chrono>

// KDE
#include <KWayland/Client/plasmashell.h>
#include <KWayland/Client/surface.h>
#include <KWindowSystem>

#define RELOCATIONSHOWINGEVENT "viewInRelocationShowing"

using namespace std::chrono_literals;

namespace {
namespace FloatingPanelGeometry = Latte::ViewPart::FloatingPanelGeometry;

constexpr auto RelocationApplyDelay = 100ms;
constexpr int PreserveScreensGroup = -1;

[[nodiscard]] std::string utf8(const QString &value)
{
    const QByteArray bytes = value.toUtf8();
    return std::string(
        bytes.constData(),
        static_cast<std::size_t>(bytes.size()));
}

[[nodiscard]] QString qString(
    const std::string &value)
{
    return QString::fromUtf8(
        value.data(),
        static_cast<qsizetype>(value.size()));
}

[[nodiscard]] std::optional<FloatingPanelGeometry::Edge>
floatingPanelEdge(Plasma::Types::Location location)
{
    switch (location) {
    case Plasma::Types::TopEdge:
        return FloatingPanelGeometry::Edge::Top;
    case Plasma::Types::RightEdge:
        return FloatingPanelGeometry::Edge::Right;
    case Plasma::Types::BottomEdge:
        return FloatingPanelGeometry::Edge::Bottom;
    case Plasma::Types::LeftEdge:
        return FloatingPanelGeometry::Edge::Left;
    default:
        return std::nullopt;
    }
}

[[nodiscard]] FloatingPanelGeometry::PrimaryAxisAlignment
floatingPanelAlignment(Latte::Types::Alignment alignment)
{
    switch (alignment) {
    case Latte::Types::Left:
    case Latte::Types::Top:
        return FloatingPanelGeometry::PrimaryAxisAlignment::Start;
    case Latte::Types::Right:
    case Latte::Types::Bottom:
        return FloatingPanelGeometry::PrimaryAxisAlignment::End;
    default:
        return FloatingPanelGeometry::PrimaryAxisAlignment::Center;
    }
}

[[nodiscard]] QPoint visibilityDisplacedPanelPosition(
    const QRect &stableCanvas,
    FloatingPanelGeometry::Edge edge,
    int slideOffset)
{
    QPoint position = stableCanvas.topLeft();
    const int outwardOffset = qAbs(slideOffset);

    switch (edge) {
    case FloatingPanelGeometry::Edge::Top:
        position.ry() -= outwardOffset;
        break;
    case FloatingPanelGeometry::Edge::Right:
        position.rx() += outwardOffset;
        break;
    case FloatingPanelGeometry::Edge::Bottom:
        position.ry() += outwardOffset;
        break;
    case FloatingPanelGeometry::Edge::Left:
        position.rx() -= outwardOffset;
        break;
    }

    return position;
}
}

namespace Latte {
namespace ViewPart {

Positioner::Positioner(Latte::View *parent)
    : QObject(parent),
      m_view(parent)
{
    m_screenSyncTimer.setSingleShot(true);
    m_screenSyncTimer.setInterval(2000);
    connect(&m_screenSyncTimer, &QTimer::timeout, this, &Positioner::reconsiderScreen);

    //! under X11 it was identified that windows many times especially under screen changes
    //! don't end up at the correct position and size. This timer will enforce repositionings
    //! and resizes every 500ms if the window hasn't end up to correct values and until this
    //! is achieved
    m_validateGeometryTimer.setSingleShot(true);
    m_validateGeometryTimer.setInterval(500);
    connect(&m_validateGeometryTimer, &QTimer::timeout, this, &Positioner::syncGeometry);

    //! syncGeometry() function is costly, so now we make sure that is not executed too often
    m_syncGeometryTimer.setSingleShot(true);
    m_syncGeometryTimer.setInterval(150);
    connect(&m_syncGeometryTimer, &QTimer::timeout, this, &Positioner::immediateSyncGeometry);

    m_corona = qobject_cast<Latte::Corona *>(m_view->corona());

    if (m_corona) {
        connect(m_view, &QWindow::windowTitleChanged, this, &Positioner::updateWaylandId);
        connect(m_corona->wm(), &WindowSystem::AbstractWindowInterface::latteWindowAdded, this, &Positioner::updateWaylandId);

        connect(m_corona->layoutsManager(), &Layouts::Manager::currentLayoutIsSwitching, this, &Positioner::onCurrentLayoutIsSwitching);
        /////

        m_screenSyncTimer.setInterval(qMax(m_corona->universalSettings()->screenTrackerInterval() - 500, 1000));
        connect(m_corona->universalSettings(), &UniversalSettings::screenTrackerIntervalChanged, this, [&]() {
            m_screenSyncTimer.setInterval(qMax(m_corona->universalSettings()->screenTrackerInterval() - 500, 1000));
        });

        connect(m_corona, &Latte::Corona::viewLocationChanged, this, [&]() {
            //! check if an edge has been freed for a primary dock
            //! from another screen
            if (m_view->onPrimary()) {
                m_screenSyncTimer.start();
            }
        });
    }

    init();
}

Positioner::~Positioner()
{
    m_inDelete = true;
    m_placementCompletions.abandonAll();
    slideOutDuringExit();
    m_corona->wm()->unregisterIgnoredWindow(m_trackedWindowId, this);

    m_screenSyncTimer.stop();
    m_validateGeometryTimer.stop();
}

void Positioner::init()
{
    //! seed the teardown-slide edge; it stays fresh through locationChanged
    m_lastLocation = m_view->location();

    //! connections
    connect(this, &Positioner::screenGeometryChanged, this, &Positioner::syncGeometry);

    connect(this, &Positioner::hidingForRelocationStarted, this, &Positioner::updateInRelocationAnimation);
    connect(this, &Positioner::showingAfterRelocationFinished, this, &Positioner::updateInRelocationAnimation);
    connect(this, &Positioner::showingAfterRelocationFinished, this, &Positioner::syncLatteViews);
    connect(this, &Positioner::startupFinished, this, &Positioner::onStartupFinished);

    connect(m_view, &Latte::View::onPrimaryChanged, this, &Positioner::syncLatteViews);

    connect(this, &Positioner::inSlideAnimationChanged, this, [&]() {
        if (!inSlideAnimation()) {
            syncGeometry();
        }
    });

    connect(this, &Positioner::isStickedOnTopEdgeChanged, this, [&]() {
        if (m_view->formFactor() == Plasma::Types::Vertical) {
            syncGeometry();
        }
    });

    connect(this, &Positioner::isStickedOnBottomEdgeChanged, this, [&]() {
        if (m_view->formFactor() == Plasma::Types::Vertical) {
            syncGeometry();
        }
    });

    connect(m_corona->activitiesConsumer(), &KActivities::Consumer::currentActivityChanged, this, [&]() {
        if (m_view->formFactor() == Plasma::Types::Vertical && m_view->layout() && m_view->layout()->isCurrent()) {
            syncGeometry();
        }
    });

    connect(this, &Positioner::slideOffsetChanged, this, [&]() {
        updatePosition(m_lastAvailableScreenRect);
    });

    connect(m_view, &QQuickWindow::xChanged, this, &Positioner::validateDockGeometry);
    connect(m_view, &QQuickWindow::yChanged, this, &Positioner::validateDockGeometry);
    connect(m_view, &QQuickWindow::widthChanged, this, &Positioner::validateDockGeometry);
    connect(m_view, &QQuickWindow::heightChanged, this, &Positioner::validateDockGeometry);
    connect(m_view, &QQuickWindow::screenChanged, this, &Positioner::currentScreenChanged);
    connect(m_view, &QQuickWindow::screenChanged, this, &Positioner::onScreenChanged);

    connect(m_view, &Latte::View::behaveAsPlasmaPanelChanged, this, &Positioner::syncGeometry);
    connect(m_view, &Latte::View::maxThicknessChanged, this, &Positioner::syncGeometry);

    connect(m_view, &Latte::View::behaveAsPlasmaPanelChanged,  this, [&]() {
        if (!m_view->behaveAsPlasmaPanel() && m_slideOffset != 0) {
            m_slideOffset = 0;
            syncGeometry();
        }
    });

    connect(m_view, &Latte::View::offsetChanged, this, [&]() {
        syncGeometry();
    });

    connect(m_view, &Latte::View::locationChanged, this, [&]() {
        //! keep the last known edge available for teardown slides: by the
        //! time the containment emits destroyed() it can no longer be asked
        //! for its location (see slideLocation)
        m_lastLocation = m_view->location();
        updateFormFactor();
        syncGeometry();
    });

    connect(m_view, &Latte::View::editThicknessChanged, this, [&]() {
        syncGeometry();
    });

    connect(m_view, &Latte::View::maxLengthChanged, this, [&]() {
        if (m_view->behaveAsPlasmaPanel()) {
            syncGeometry();
        }
    });

    connect(m_view, &Latte::View::normalThicknessChanged, this, [&]() {
        if (m_view->behaveAsPlasmaPanel()) {
            syncGeometry();
        }
    });

    connect(m_view, &Latte::View::screenEdgeMarginEnabledChanged, this, [&]() {
        syncGeometry();
    });

    connect(m_view, &Latte::View::screenEdgeMarginChanged, this, [&]() {
        syncGeometry();
    });

    connect(m_view, &View::layoutChanged, this, [&]() {
        if (m_nextLayoutName.isEmpty() && m_view->layout() && m_view->formFactor() == Plasma::Types::Vertical) {
            syncGeometry();
        }
    });

    connect(m_view->effects(), &Latte::ViewPart::Effects::drawShadowsChanged, this, [&]() {
        if (!m_view->behaveAsPlasmaPanel()) {
            syncGeometry();
        }
    });

    connect(m_view->effects(), &Latte::ViewPart::Effects::innerShadowChanged, this, [&]() {
        if (m_view->behaveAsPlasmaPanel()) {
            syncGeometry();
        }
    });

    connect(qGuiApp, &QGuiApplication::screenAdded, this, &Positioner::onScreenChanged);
    connect(m_corona->screenPool(), &ScreenPool::primaryScreenChanged, this, &Positioner::onScreenChanged);

    connect(m_view, &Latte::View::visibilityChanged, this, &Positioner::initDelayedSignals);

    initSignalingForLocationChangeSliding();
}

void Positioner::initDelayedSignals()
{
    connect(m_view->visibility(), &ViewPart::VisibilityManager::isHiddenChanged, this, [&]() {
        if (m_view->behaveAsPlasmaPanel() && !m_view->visibility()->isHidden() && qAbs(m_slideOffset)>0) {
            //! ignore any checks to make sure the panel geometry is up-to-date
            immediateSyncGeometry();
        }
    });
}

void Positioner::updateWaylandId()
{
    QString validTitle = m_view->validTitle();
    if (validTitle.isEmpty()) {
        return;
    }

    Latte::WindowSystem::WindowId newId = m_corona->wm()->winIdFor("latte-dock", validTitle);

    if (m_trackedWindowId != newId) {
        if (!m_trackedWindowId.isEmpty()) {
            m_corona->wm()->unregisterIgnoredWindow(m_trackedWindowId, this);
        }

        m_trackedWindowId = newId;
        m_corona->wm()->registerIgnoredWindow(m_trackedWindowId, this);

        Q_EMIT winIdChanged();
    }
}

bool Positioner::inRelocationShowing() const
{
    return m_inRelocationShowing;
}

void Positioner::setInRelocationShowing(bool active)
{
    if (m_inRelocationShowing == active) {
        return;
    }

    m_inRelocationShowing = active;

    if (m_inRelocationShowing) {
        m_view->visibility()->addBlockHidingEvent(RELOCATIONSHOWINGEVENT);
    } else {
        m_view->visibility()->removeBlockHidingEvent(RELOCATIONSHOWINGEVENT);
    }

    Q_EMIT inRelocationShowingChanged();
}

bool Positioner::geometryIsSettled() const
{
    return m_relocationGeneration == m_appliedRelocationGeneration
        && !inRelocationAnimation()
        && !m_inRelocationShowing
        && !m_inSlideAnimation
        && !m_screenSyncTimer.isActive()
        && !m_syncGeometryTimer.isActive()
        && !m_validateGeometryTimer.isActive()
        && m_view
        && m_view->screen() == m_screenToFollow;
}

quint64 Positioner::relocationGeneration() const
{
    return m_relocationGeneration;
}

quint64 Positioner::appliedRelocationGeneration() const
{
    return m_appliedRelocationGeneration;
}

bool Positioner::isOffScreen() const
{
    return (m_view->absoluteGeometry().x()<-500 || m_view->absoluteGeometry().y()<-500);
}

bool Positioner::inStartup() const
{
    return m_inStartup;
}

int Positioner::currentScreenId() const
{
    auto *latteCorona = qobject_cast<Latte::Corona *>(m_view->corona());

    if (latteCorona) {
        return latteCorona->screenPool()->id(m_screenNameToFollow);
    }

    return -1;
}

Latte::WindowSystem::WindowId Positioner::trackedWindowId()
{
    //! wayland ids are compositor uuids and a uuid never parses as a
    //! number, so the Qt5-era 'toInt() <= 0' (id not yet assigned) test
    //! had become constant-true here: under wayland every call lazily
    //! re-resolves the id. Kept unconditional deliberately - the wm only
    //! emits latteWindowAdded for isAcceptableWindow() windows, which
    //! latte's own skip-taskbar windows are not, so this lazy re-resolve
    //! is the reliable path after a surface remap. Tightening it to an
    //! isEmpty() check needs a live-session pass first.
    if (KWindowSystem::isPlatformWayland()) {
        updateWaylandId();
    }

    return m_trackedWindowId;
}

QString Positioner::currentScreenName() const
{
    return m_screenNameToFollow;
}

QScreen *Positioner::assignedScreen() const
{
    return m_screenToFollow;
}

//! the pure core's SlideEdge mirror stays in sync with the window
//! interface's enum by construction: drift fails the build here
static_assert(static_cast<int>(PositionerGeometry::SlideEdge::None) == static_cast<int>(WindowSystem::AbstractWindowInterface::Slide::None));
static_assert(static_cast<int>(PositionerGeometry::SlideEdge::Top) == static_cast<int>(WindowSystem::AbstractWindowInterface::Slide::Top));
static_assert(static_cast<int>(PositionerGeometry::SlideEdge::Left) == static_cast<int>(WindowSystem::AbstractWindowInterface::Slide::Left));
static_assert(static_cast<int>(PositionerGeometry::SlideEdge::Bottom) == static_cast<int>(WindowSystem::AbstractWindowInterface::Slide::Bottom));
static_assert(static_cast<int>(PositionerGeometry::SlideEdge::Right) == static_cast<int>(WindowSystem::AbstractWindowInterface::Slide::Right));

WindowSystem::AbstractWindowInterface::Slide Positioner::slideLocation(Plasma::Types::Location location)
{
    if (location == Plasma::Types::Floating) {
        //! resolve from the cached edge, never from the containment: exit
        //! slides run from teardown paths (GenericLayout::containmentDestroyed,
        //! ~Positioner) where the containment is already inside ~QObject and
        //! its Plasma::Applet state is freed - asking it for location() there
        //! reads freed memory (same destroyed()-handler demotion family as
        //! d6d57e61). The cache is seeded at init() and follows
        //! locationChanged, so it is the same value while the view is alive.
        location = m_lastLocation;
    }

    const auto edge = PositionerGeometry::slideEdge(location);

    if (edge == PositionerGeometry::SlideEdge::None) {
        qDebug() << staticMetaObject.className() << "wrong location";
    }

    //! safe by the static_asserts above: both enums share enumerator order
    return static_cast<WindowSystem::AbstractWindowInterface::Slide>(edge);
}

void Positioner::slideOutDuringExit(Plasma::Types::Location location)
{
    if (m_view->isVisible()) {
        m_corona->wm()->slideWindow(*m_view, slideLocation(location));
        m_view->setVisible(false);
    }
}

void Positioner::slideInDuringStartup()
{
    m_corona->wm()->slideWindow(*m_view, slideLocation(m_view->containment()->location()));
}

void Positioner::onStartupFinished()
{
    if (m_inStartup) {
        m_inStartup = false;
        syncGeometry();
        Q_EMIT isOffScreenChanged();
    }
}

void Positioner::onCurrentLayoutIsSwitching(const QString &layoutName)
{
    if (!m_view || !m_view->layout() || m_view->layout()->name() != layoutName || !m_view->isVisible()) {
        return;
    }

    m_inLayoutUnloading = true;
    slideOutDuringExit();
}

void Positioner::setWindowOnActivities(const Latte::WindowSystem::WindowId &wid, const QStringList &activities)
{
    m_corona->wm()->setWindowOnActivities(wid, activities);
}

void Positioner::syncLatteViews()
{
    if (m_applyingPlacementTransaction) {
        m_layoutSyncDeferredByPlacementTransaction = true;
        return;
    }

    m_layoutSyncDeferredByPlacementTransaction = false;
    if (m_view->layout()) {
        //! This is needed in case the edge there are views that must be deleted
        //! after screen edges changes
        m_view->layout()->syncLatteViewsToScreens();
    }
}

void Positioner::applyOutputPlacement(
    QScreen *const destination,
    const bool followsPrimary)
{
    Q_ASSERT(destination);
    Q_ASSERT(m_applyingPlacementTransaction);
    if (!destination
            || !m_applyingPlacementTransaction) {
        qFatal(
            "Positioner entered output application without a validated placement transaction");
    }

    m_view->setOnPrimary(followsPrimary);

    if (outputPlacementIsNeeded(destination)) {
        if (!setScreenToFollow(destination)) {
            qFatal(
                "Positioner lost its validated output during synchronous placement application");
        }
    } else {
        //! setScreenToFollow() deliberately returns for an unchanged
        //! physical output. The containment must still observe a changed
        //! follow-primary policy as part of this transaction.
        updateContainmentScreen();
    }
}

bool Positioner::outputOwnershipIsNeeded(
    const QScreen *const destination) const
{
    Q_ASSERT(destination);
    return m_screenToFollow != destination
        || m_view->layerShellNeedsOutput(destination);
}

bool Positioner::outputPlacementIsNeeded(
    const QScreen *const destination) const
{
    Q_ASSERT(destination);
    return outputOwnershipIsNeeded(destination)
        || m_view->screen() != destination;
}

void Positioner::updateContainmentScreen()
{
    if (m_view->containment()) {
        m_view->containment()->reactToScreenChange();
    }
}

//! this function updates the dock's associated screen.
//! updateScreenId = true, update also the m_screenNameToFollow
//! updateScreenId = false, do not update the m_screenNameToFollow
//! that way an explicit dock can be shown in another screen when
//! there isnt a tasks dock running in the system and for that
//! dock its first origin screen is stored and that way when
//! that screen is reconnected the dock will return to its original
//! place
bool Positioner::setScreenToFollow(QScreen *scr, bool updateScreenId)
{
    if (!scr || !outputPlacementIsNeeded(scr)) {
        return scr != nullptr;
    }

    const bool changesOutputOwnership =
        m_screenToFollow != scr
        || m_view->layerShellNeedsOutput(scr);
    if (changesOutputOwnership
            && !m_applyingPlacementTransaction
            && m_view->visibility()
            && !m_view->visibility()
                ->beginPlacementTransaction(true)) {
        qCritical() << "Positioner refused output reassignment after reservation retirement failed for"
                    << m_view->validTitle()
                    << "destination=" << scr->name();
        return false;
    }

    qDebug() << "setScreenToFollow() called for screen:" << scr->name() << " update:" << updateScreenId;

    QObject::disconnect(m_screenGeometryConnection);
    m_screenToFollow = scr;

    if (updateScreenId) {
        m_screenNameToFollow = scr->name();
    }

    qDebug() << "adapting to screen...";
    m_view->moveToScreen(
        scr,
        changesOutputOwnership
        ? View::OutputMove::OwnershipTransfer
        : View::OutputMove::ObservationOnly);
    //! A hidden Wayland layer surface can complete setScreen() synchronously
    //! without delivering a later screenChanged edge. Confirm from the
    //! applied QWindow state here; the signal path below remains for
    //! compositor-delayed changes.
    finishPendingScreenPlacementIfApplied();

    updateContainmentScreen();

    m_screenGeometryConnection = connect(scr, &QScreen::geometryChanged, this, &Positioner::screenGeometryChanged);
    syncGeometry();
    m_view->updateAbsoluteGeometry(true);
    qDebug() << "setScreenToFollow() ended...";

    Q_EMIT screenGeometryChanged();
    Q_EMIT currentScreenChanged();
    return true;
}

//! the main function which decides if this dock is at the
//! correct screen
void Positioner::reconsiderScreen()
{
    if (m_inDelete) {
        return;
    }

    qDebug() << "reconsiderScreen() called...";
    qDebug() << "  Delayer  ";

    for (const auto scr : qGuiApp->screens()) {
        qDebug() << "      D, found screen: " << scr->name();
    }

    bool screenExists{false};
    QScreen *primaryScreen{m_corona->screenPool()->primaryScreen()};

    //!check if the associated screen is running
    for (const auto scr : qGuiApp->screens()) {
        if (m_screenNameToFollow == scr->name()
                || (m_view->onPrimary() && scr == primaryScreen)) {
            screenExists = true;
        }
    }

    qDebug() << "dock screen exists  ::: " << screenExists;

    //! 1.a primary dock must be always on the primary screen
    if (m_view->onPrimary() && (m_screenNameToFollow != primaryScreen->name()
                                || outputPlacementIsNeeded(primaryScreen))) {
        //! case 1
        qDebug() << "reached case 1: of updating dock primary screen...";
        setScreenToFollow(primaryScreen);
    } else if (!m_view->onPrimary()) {
        //! 2.an explicit dock must be always on the correct associated screen
        //! there are cases that window manager misplaces the dock, this function
        //! ensures that this dock will return at its correct screen
        for (const auto scr : qGuiApp->screens()) {
            if (scr && scr->name() == m_screenNameToFollow) {
                qDebug() << "reached case 2: updating the explicit screen for dock...";
                setScreenToFollow(scr);
                break;
            }
        }
    }

    syncGeometry();
    qDebug() << "reconsiderScreen() ended...";
}

void Positioner::onScreenChanged(QScreen *scr)
{
    m_screenSyncTimer.start();

    //! this is needed in order to update the struts on screen change
    //! and even though the geometry has been set correctly the offsets
    //! of the screen must be updated to the new ones
    if (m_view->visibility() && m_view->visibility()->mode() == Latte::Types::AlwaysVisible) {
        m_view->updateAbsoluteGeometry(true);
    }
}

void Positioner::syncGeometry()
{
    if (!(m_view->screen() && m_view->containment()) || m_inDelete || m_slideOffset!=0 || inSlideAnimation()) {
        return;
    }

    qDebug() << "syncGeometry() called...";

    if (!m_syncGeometryTimer.isActive()) {
        m_syncGeometryTimer.start();
    }
}

bool Positioner::immediateSyncGeometry()
{
    const bool applied = solveAndApplyGeometry();
    if (applied
            && m_relocationGeneration
                == m_appliedRelocationGeneration
            && !inRelocationAnimation()) {
        if (!m_view->visibility()
                ->publishReservationAfterAppliedPlacement()) {
            qCritical() << "Positioner retained an applied surface without committing its reservation for"
                        << m_view->validTitle();
            if (!m_syncGeometryTimer.isActive()) {
                m_syncGeometryTimer.start();
            }
            return false;
        }
        Q_EMIT placementTransactionCommitted();
        m_view->showAppliedLayerShellPlacement();
    }
    return applied;
}

bool Positioner::solveAndApplyGeometry(
    const bool completesRelocation)
{
    QScreen *const placementScreen = assignedScreen();

    qDebug() << "immediateSyncGeometry() called...";

    if (!placementScreen || !m_view->containment() || m_inDelete) {
        return false;
    }

    //! Geometry belongs to the persistent assignment, not to whichever
    //! output QWindow infers from an intermediate resize. A vertical-to-
    //! horizontal edge change can temporarily put the resized window centre
    //! on an adjacent output before the final LayerShell margins apply.
    if (m_view->screen() != placementScreen
            && !completesRelocation) {
        qDebug() << "Sync Geometry screens inconsistent!!!! ";
        qDebug() << "Sync Geometry screens inconsistent for assigned screen:"
                 << placementScreen->name()
                 << "dock screen:"
                 << (m_view->screen()
                     ? m_view->screen()->name()
                     : QStringLiteral("<none>"));

        if (!m_screenSyncTimer.isActive()) {
            m_screenSyncTimer.start();
        }
        qDebug() << "syncGeometry() ended...";
        return false;
    }

    if (!completesRelocation
            && m_relocationGeneration
                != m_appliedRelocationGeneration) {
        qDebug() << "syncGeometry() retained the previous window and LayerShell placement until transaction completion";
        qDebug() << "syncGeometry() ended...";
        return false;
    }

    const QRect assignedScreenGeometry =
        placementScreen->geometry();
    //! Compute the free screen rectangle for vertical panels only once. This
    //! keeps the costly QRegion operation out of both resize and position.
    QRegion freeRegion;
    QRect maximumRect;
    QRect availableScreenRect = assignedScreenGeometry;

    if (m_inStartup) {
        //! Paint out of screen while preserving the assigned output size.
        availableScreenRect =
            QRect(
                -9999,
                -9999,
                assignedScreenGeometry.width(),
                assignedScreenGeometry.height());
    }

    if (m_view->formFactor() == Plasma::Types::Vertical) {
        const QString layoutName =
            m_view->layout()
            ? m_view->layout()->name()
            : QString();
        auto *const latteCorona =
            qobject_cast<Latte::Corona *>(m_view->corona());
        const int fixedScreen =
            latteCorona->screenPool()->id(
                placementScreen->name());

        QList<Types::Visibility> ignoreModes({
            Latte::Types::AutoHide,
            Latte::Types::SidebarOnDemand,
            Latte::Types::SidebarAutoHide});

        QList<Plasma::Types::Location> ignoreEdges({
            Plasma::Types::LeftEdge,
            Plasma::Types::RightEdge});

        if (m_isStickedOnTopEdge && m_isStickedOnBottomEdge) {
            //! Do not send an empty edge list because that means all edges.
            ignoreEdges << Plasma::Types::TopEdge;
            ignoreEdges << Plasma::Types::BottomEdge;
        } else {
            if (m_isStickedOnTopEdge) {
                ignoreEdges << Plasma::Types::TopEdge;
            }

            if (m_isStickedOnBottomEdge) {
                ignoreEdges << Plasma::Types::BottomEdge;
            }
        }

        const QString activityId =
            m_view->layout()
            ? m_view->layout()->lastUsedActivity()
            : QString();
        if (m_inStartup) {
            freeRegion = availableScreenRect;
        } else {
            freeRegion =
                latteCorona->availableScreenRegionWithCriteria(
                    fixedScreen,
                    activityId,
                    ignoreModes,
                    ignoreEdges);
        }

        //! Startup uses the off-screen geometry so vertical docks do not
        //! shrink before their slide-in.
        maximumRect =
            maximumNormalGeometry(
                m_inStartup
                ? availableScreenRect
                : assignedScreenGeometry);
        const QRegion availableRegion =
            freeRegion.intersected(maximumRect);

        availableScreenRect =
            availableRegion.boundingRect();
        float area = 0;

        //! Pick the largest free rectangle. Scaling to 50x50 cells keeps the
        //! comparison in a compact range without changing its ordering.
        for (const QRect &rect : availableRegion) {
            const float candidateArea =
                static_cast<float>(rect.width() * rect.height())
                / 2500.0F;

            if (candidateArea > area) {
                availableScreenRect = rect;
                area = candidateArea;
            }
        }

    }

    auto solved = solveViewGeometry(
        availableScreenRect,
        assignedScreenGeometry);
    if (!solved) {
        qCritical() << "Positioner refused to mutate a window after geometry"
                       " solving failed for"
                    << m_view->validTitle();
        return false;
    }
    if (m_view->formFactor() == Plasma::Types::Vertical) {
        solved->forcedBorders = solveTopBottomBorders(
            availableScreenRect,
            freeRegion,
            assignedScreenGeometry);
    }

    if (m_inStartup) {
        installStartupGeometry(
            *solved,
            availableScreenRect,
            freeRegion);
        qDebug() << "syncGeometry() solved startup geometry without publishing a mapped placement";
        qDebug() << "syncGeometry() ended...";
        return false;
    }

    if (!m_view->applyPositionedLayerShellGeometry(
            placementScreen,
            solved->surface)) {
        qCritical() << "Positioner could not publish solved geometry for"
                    << m_view->validTitle()
                    << "output=" << placementScreen->name()
                    << "edge=" << m_view->location()
                    << "geometry=" << solved->surface;
        if (!m_syncGeometryTimer.isActive()) {
            m_syncGeometryTimer.start();
        }
        qDebug() << "syncGeometry() ended...";
        return false;
    }

    publishAppliedGeometry(
        *solved,
        assignedScreenGeometry,
        availableScreenRect,
        freeRegion);

    qDebug() << "syncGeometry() calculations for screen:"
             << placementScreen->name()
             << "_" << assignedScreenGeometry;
    qDebug() << "syncGeometry() calculations for edge:"
             << m_view->location();

    qDebug() << "syncGeometry() ended...";
    return true;
}

void Positioner::validateDockGeometry()
{
    if (m_slideOffset==0 && m_view->geometry() != m_validGeometry) {
        m_validateGeometryTimer.start();
    }
}

QRect Positioner::canvasGeometry()
{
    return m_canvasGeometry;
}

QRect Positioner::surfaceGeometry() const
{
    return m_appliedSurfaceGeometry;
}

QRect Positioner::surfaceOutputGeometry() const
{
    return m_appliedOutputGeometry;
}

quint64 Positioner::surfacePlacementGeneration() const
{
    return m_surfacePlacementGeneration;
}

quint64 Positioner::surfaceGeometryPublicationRevision() const
{
    return m_surfaceGeometryPublicationRevision;
}

//! this is used mainly from vertical panels in order to
//! to get the maximum geometry that can be used from the dock
//! based on their alignment type and the location dock
QRect Positioner::maximumNormalGeometry(QRect screenGeometry)
{
    const QRect currentScrGeometry = screenGeometry.isEmpty() ? m_view->screen()->geometry() : screenGeometry;

    return PositionerGeometry::maximumNormalGeometry(m_view->location(),
                                                     m_view->maxNormalThickness(),
                                                     currentScrGeometry);
}

PositionerGeometry::ForcedBorders Positioner::solveTopBottomBorders(
    const QRect &availableScreenRect,
    const QRegion &availableScreenRegion,
    const QRect &assignedScreenGeometry) const
{
    //! whether the top/bottom borders must be drawn too: a one-pixel probe
    //! at each edge of the available area must fit entirely in the free
    //! region (the math lives in the tested PositionerGeometry core)
    return PositionerGeometry::forcedBorders(
        m_view->location(),
        m_view->screenEdgeMargin(),
        assignedScreenGeometry,
        availableScreenRect,
        availableScreenRegion);
}

std::optional<Positioner::SolvedViewGeometry>
Positioner::solveViewGeometry(
    const QRect &availableScreenRect,
    const QRect &assignedScreenGeometry) const
{
    if (availableScreenRect.isEmpty()
            || !assignedScreenGeometry.isValid()
            || m_view->location() == Plasma::Types::Floating) {
        qCritical() << "Positioner refused invalid solved-view inputs"
                    << "available=" << availableScreenRect
                    << "output=" << assignedScreenGeometry
                    << "location=" << m_view->location();
        return std::nullopt;
    }

    SolvedViewGeometry solved;
    solved.canvas = PositionerGeometry::canvasGeometry(
            m_view->location(),
            m_view->formFactor(),
            m_view->editThickness(),
            assignedScreenGeometry,
            availableScreenRect);

    if (m_view->behaveAsPlasmaPanel()) {
        solved.floatingPresentation = solveStablePanelGeometry(
            availableScreenRect,
            assignedScreenGeometry);
        if (!solved.floatingPresentation) {
            return std::nullopt;
        }
        solved.surface =
            solved.floatingPresentation->envelope.value;
        return solved;
    }

    auto inputs = geometryInputs();
    const QSize size = PositionerGeometry::windowSize(
        inputs,
        availableScreenRect,
        assignedScreenGeometry.size());
    inputs.viewWidth = size.width();
    inputs.viewHeight = size.height();
    const QPoint position = PositionerGeometry::dockPosition(
        inputs,
        availableScreenRect);

    solved.surface = m_validGeometry;
    solved.surface.setSize(size);
    if (m_slideOffset == 0
            || m_nextScreenEdge != Plasma::Types::Floating) {
        solved.surface.moveTopLeft(position);
    } else if (m_view->formFactor() == Plasma::Types::Horizontal) {
        solved.surface.moveLeft(position.x());
    } else {
        solved.surface.moveTop(position.y());
    }

    if (!solved.surface.isValid()
            || (!m_inStartup
                && !assignedScreenGeometry.contains(solved.surface))) {
        qCritical() << "Positioner refused Dock geometry outside its output"
                    << "surface=" << solved.surface
                    << "output=" << assignedScreenGeometry;
        return std::nullopt;
    }

    return solved;
}

//! snapshot the View properties the PositionerGeometry core reads (EX-09)
PositionerGeometry::ViewGeometryInputs Positioner::geometryInputs() const
{
    PositionerGeometry::ViewGeometryInputs in;
    in.location = m_view->location();
    in.formFactor = m_view->formFactor();
    in.maxThickness = m_view->maxThickness();
    in.maxNormalThickness = m_view->maxNormalThickness();
    in.editThickness = m_view->editThickness();
    in.viewWidth = m_view->width();
    in.viewHeight = m_view->height();
    return in;
}

std::optional<FloatingPanelGeometry::Solution>
Positioner::solveStablePanelGeometry(
    const QRect &availableScreenRect,
    const QRect &assignedScreenGeometry) const
{
    Q_ASSERT(m_view->behaveAsPlasmaPanel());

    const auto edge = floatingPanelEdge(m_view->location());
    if (!edge.has_value()) {
        qCritical() << "Positioner refused stable panel geometry for a floating edge"
                    << m_view->location();
        return std::nullopt;
    }

    const QRect outputGeometry =
        m_inStartup
        ? QRect(
            -9999,
            -9999,
            assignedScreenGeometry.width(),
            assignedScreenGeometry.height())
        : assignedScreenGeometry;
    const FloatingPanelGeometry::PlacementInputs inputs{
        .outputGeometry = outputGeometry,
        .availablePrimaryGeometry = availableScreenRect,
        .edge = *edge,
        .alignment = floatingPanelAlignment(
            static_cast<Latte::Types::Alignment>(m_view->alignment())),
        .maxLength = m_view->maxLength(),
        .offset = m_view->offset(),
        .panelDepth = m_view->normalThickness(),
        .floatingGap = m_view->isFloatingPanel() ? m_view->screenEdgeMargin() : 0,
    };

    const auto solution = FloatingPanelGeometry::solvePlacement(inputs);
    if (!solution.has_value()) {
        qCritical() << "Positioner could not solve stable panel geometry for"
                    << m_view->validTitle() << "output=" << outputGeometry
                    << "available=" << availableScreenRect
                    << "depth=" << inputs.panelDepth
                    << "gap=" << inputs.floatingGap
                    << "maxLength=" << inputs.maxLength
                    << "offset=" << inputs.offset;
    }

    return solution;
}

void Positioner::applySolvedWindowGeometry(const QRect &surface)
{
    Q_ASSERT(surface.isValid());
    const QSize size = surface.size();

    m_view->setMinimumSize(size);
    m_view->setMaximumSize(size);
    m_view->resize(size);
    m_view->setPosition(surface.topLeft());

    if (m_view->formFactor() == Plasma::Types::Horizontal) {
        Q_EMIT windowSizeChanged();
    }
}

void Positioner::installStartupGeometry(
    const SolvedViewGeometry &solved,
    const QRect &availableScreenRect,
    const QRegion &availableScreenRegion)
{
    m_validGeometry = solved.surface;
    m_lastAvailableScreenRect = availableScreenRect;
    m_lastAvailableScreenRegion = availableScreenRegion;

    FloatingTransition *const transition =
        m_view->floatingTransition();
    const bool transitionChanged =
        transition->installGeometryWithoutNotification(
            solved.floatingPresentation);
    const bool canvasChanged = m_canvasGeometry != solved.canvas;
    m_canvasGeometry = solved.canvas;

    applySolvedWindowGeometry(solved.surface);
    if (canvasChanged) {
        Q_EMIT canvasGeometryChanged();
    }
    if (transitionChanged) {
        transition->publishInstalledGeometryChange();
    }
}

void Positioner::publishAppliedGeometry(
    const SolvedViewGeometry &solved,
    const QRect &assignedScreenGeometry,
    const QRect &availableScreenRect,
    const QRegion &availableScreenRegion)
{
    //! Install every backing value before QWindow, controller, or Positioner
    //! notifications can run. A failed LayerShell application never reaches
    //! this function, so it cannot leak solver scratch as applied state.
    m_validGeometry = solved.surface;
    m_appliedSurfaceGeometry = solved.surface;
    m_appliedOutputGeometry = assignedScreenGeometry;
    m_surfacePlacementGeneration = m_relocationGeneration;
    m_lastAvailableScreenRect = availableScreenRect;
    m_lastAvailableScreenRegion = availableScreenRegion;
    m_view->effects()->setForceTopBorder(
        solved.forcedBorders.top);
    m_view->effects()->setForceBottomBorder(
        solved.forcedBorders.bottom);

    FloatingTransition *const transition =
        m_view->floatingTransition();
    const bool transitionChanged =
        transition->installGeometryWithoutNotification(
            solved.floatingPresentation);
    const bool canvasChanged = m_canvasGeometry != solved.canvas;
    m_canvasGeometry = solved.canvas;

    //! Repeated stable syncs stay cheap in LayerShell::applyViewPlacement,
    //! while this revision records each complete solved-and-applied boundary.
    ++m_surfaceGeometryPublicationRevision;

    applySolvedWindowGeometry(solved.surface);

    //! Reservation geometry and touch authority now consume the same applied
    //! surface, output, and FloatingTransition backing snapshot.
    m_view->updateAbsoluteGeometry(true);

    if (canvasChanged) {
        Q_EMIT canvasGeometryChanged();
    }
    if (transitionChanged) {
        transition->publishInstalledGeometryChange();
    }
    Q_EMIT surfaceGeometryPublicationRevisionChanged();
    Q_EMIT surfaceGeometryCalculated(solved.surface);
}

void Positioner::updatePosition(QRect availableScreenRect)
{
    if (m_view->location() == Plasma::Types::Floating) {
        qWarning() << "wrong location, couldn't update the panel position"
                   << m_view->location();
    }

    if (m_view->behaveAsPlasmaPanel()) {
        const auto edge = floatingPanelEdge(m_view->location());
        if (!edge.has_value() || !m_validGeometry.isValid()) {
            qCritical() << "Positioner refused panel visibility displacement"
                        << "edge=" << m_view->location()
                        << "stableCanvas=" << m_validGeometry;
            return;
        }

        //! Ordinary visibility hiding may move the complete stable envelope.
        //! Floating presentation never reaches this physical-window path.
        m_view->setPosition(visibilityDisplacedPanelPosition(
            m_validGeometry, *edge, m_slideOffset));
        return;
    }

    //! EX-09 (docs/tracking/QML_EXTRACTION_PLAN.md): non-panel placement math
    //! lives in the tested PositionerGeometry core; this adapter keeps the
    //! validGeometry bookkeeping and window application.
    const QPoint position = PositionerGeometry::dockPosition(geometryInputs(), availableScreenRect);

    if (m_slideOffset == 0 || m_nextScreenEdge != Plasma::Types::Floating /*exactly after relocating and changing screen edge*/) {
        //! update valid geometry in normal positioning
        m_validGeometry.moveTopLeft(position);
    } else {
        //! when sliding in/out update only the relevant axis for the screen_edge in
        //! to not mess the calculations and the automatic geometry checkers that
        //! View::Positioner is using.
        if (m_view->formFactor() == Plasma::Types::Horizontal) {
            m_validGeometry.moveLeft(position.x());
        } else {
            m_validGeometry.moveTop(position.y());
        }
    }

    m_view->setPosition(position);

    //! under layer-shell the surface is placed by anchors and margins, so
    //! there is no per-surface setPosition() left to mirror here; the
    //! QWindow::setPosition() above still matters on X11
}

int Positioner::slideOffset() const
{
    return m_slideOffset;
}

void Positioner::setSlideOffset(int offset)
{
    if (m_slideOffset == offset) {
        return;
    }

    m_slideOffset = offset;
    Q_EMIT slideOffsetChanged();
}


void Positioner::updateFormFactor()
{
    if (!m_view->containment())
        return;

    switch (m_view->location()) {
    case Plasma::Types::TopEdge:
    case Plasma::Types::BottomEdge:
        m_view->containment()->setFormFactor(Plasma::Types::Horizontal);
        break;

    case Plasma::Types::LeftEdge:
    case Plasma::Types::RightEdge:
        m_view->containment()->setFormFactor(Plasma::Types::Vertical);
        break;

    default:
        qWarning() << "wrong location, couldn't update the panel position" << m_view->location();
    }
}

void Positioner::onLastRepositionApplyEvent()
{
    Q_ASSERT(!hasPendingPlacementComponents());

    //! The placement generation does not become authoritative until the
    //! complete solution has reached both the assigned QWindow and its
    //! LayerShell object. Keeping the previous generation on failure makes
    //! the incomplete transaction visible to diagnostics and prevents reveal.
    if (!solveAndApplyGeometry(true)) {
        qCritical() << "Positioner retained relocation generation"
                    << m_appliedRelocationGeneration
                    << "after generation"
                    << m_relocationGeneration
                    << "failed its final applied placement";
        scheduleLastRepositionApplyEvent();
        return;
    }

    if (!m_view->visibility()
            ->publishReservationAfterAppliedPlacement()) {
        qCritical() << "Positioner retained relocation generation"
                    << m_appliedRelocationGeneration
                    << "until reservation publication succeeds for"
                    << m_view->validTitle();
        scheduleLastRepositionApplyEvent();
        return;
    }

    if (!m_placementRequests.completeIfCurrent(
            m_relocationGeneration)) {
        qCritical() << "Positioner refused to commit stale relocation generation"
                    << m_relocationGeneration
                    << "for"
                    << m_view->validTitle();
        return;
    }
    m_appliedRelocationGeneration =
        m_relocationGeneration;
    Q_EMIT placementTransactionCommitted();
    completePlacementRequest(
        m_relocationGeneration);
    m_view->showAppliedLayerShellPlacement();

    //! Reservation publication is a direct connection above this point. The
    //! coordinator must own the same output and edge as the live LayerShell
    //! surface before layout synchronization can destroy or reconsider views.
    setInRelocationShowing(true);
    m_view->effects()->setAnimationsBlocked(false);
    Q_EMIT showingAfterRelocationFinished();
    Q_EMIT edgeChanged();

    if (m_repositionFromViewSettingsWindow) {
        m_repositionFromViewSettingsWindow = false;
        m_view->showSettingsWindow();
    }
}

void Positioner::applyUnanimatedPlacementGeneration()
{
    const auto completedGeneration =
        m_relocationGeneration;

    if (hasPendingPlacementComponents()) {
        qCritical() << "Positioner retained unanimated relocation generation"
                    << m_appliedRelocationGeneration
                    << "while generation"
                    << m_relocationGeneration
                    << "still has pending placement components";
        scheduleUnanimatedPlacementApplyEvent();
        return;
    }

    if (!solveAndApplyGeometry(true)) {
        qCritical() << "Positioner retained unanimated relocation generation"
                    << m_appliedRelocationGeneration
                    << "after generation"
                    << m_relocationGeneration
                    << "failed its applied placement";
        scheduleUnanimatedPlacementApplyEvent();
        return;
    }

    if (!m_view->visibility()
            ->publishReservationAfterAppliedPlacement()) {
        qCritical() << "Positioner retained unanimated relocation generation"
                    << m_appliedRelocationGeneration
                    << "until reservation publication succeeds for"
                    << m_view->validTitle();
        scheduleUnanimatedPlacementApplyEvent();
        return;
    }

    if (!m_placementRequests.completeIfCurrent(
            completedGeneration)) {
        qCritical() << "Positioner refused to commit stale unanimated relocation generation"
                    << completedGeneration
                    << "for"
                    << m_view->validTitle();
        return;
    }
    m_appliedRelocationGeneration =
        completedGeneration;
    static_cast<void>(
        invalidateScheduledPlacementCompletionForGeneration(
            m_scheduledPlacementCompletion,
            completedGeneration));
    Q_EMIT placementTransactionCommitted();
    completePlacementRequest(
        completedGeneration);
    m_view->showAppliedLayerShellPlacement();

    if (m_layoutSyncDeferredByPlacementTransaction) {
        syncLatteViews();
    }
}

void Positioner::scheduleLastRepositionApplyEvent()
{
    const auto scheduledGeneration =
        m_relocationGeneration;
    if (m_scheduledPlacementCompletion
            == scheduledGeneration) {
        return;
    }
    m_scheduledPlacementCompletion =
        scheduledGeneration;
    QTimer::singleShot(RelocationApplyDelay, this, [this, scheduledGeneration]() {
        if (m_scheduledPlacementCompletion
                != scheduledGeneration) {
            return;
        }
        m_scheduledPlacementCompletion.reset();
        if (scheduledGeneration
                != m_relocationGeneration) {
            qDebug() << "Ignoring superseded relocation completion generation"
                     << scheduledGeneration << "for view" << m_view->validTitle()
                     << "; current generation is" << m_relocationGeneration;
            return;
        }

        if (hasPendingPlacementComponents()) {
            qCritical() << "Refusing to complete relocation generation"
                        << scheduledGeneration << "for view" << m_view->validTitle()
                        << "while placement changes remain pending";
            return;
        }

        onLastRepositionApplyEvent();
    });
}

void Positioner::scheduleUnanimatedPlacementApplyEvent()
{
    const auto scheduledGeneration =
        m_relocationGeneration;
    if (m_scheduledPlacementCompletion
            == scheduledGeneration) {
        return;
    }
    m_scheduledPlacementCompletion =
        scheduledGeneration;
    QTimer::singleShot(
        RelocationApplyDelay,
        this,
        [this, scheduledGeneration]() {
            if (m_scheduledPlacementCompletion
                    != scheduledGeneration) {
                return;
            }
            m_scheduledPlacementCompletion.reset();
            if (scheduledGeneration
                    != m_relocationGeneration) {
                return;
            }
            applyUnanimatedPlacementGeneration();
        });
}

void Positioner::initSignalingForLocationChangeSliding()
{
    connect(this, &Positioner::hidingForRelocationStarted, this, &Positioner::onHideWindowsForSlidingOut);

    //! SCREEN_EDGE
    connect(m_view, &View::locationChanged, this, [&]() {
        if (m_nextScreenEdge != Plasma::Types::Floating
                && m_view->location()
                    == m_nextScreenEdge) {
            const bool isrelocationlastevent =
                isLastHidingRelocationEvent();
            immediateSyncGeometry();
            m_nextScreenEdge = Plasma::Types::Floating;

            //! make sure that View has been repositioned properly in next screen edge and show view afterwards
            if (isrelocationlastevent) {
                scheduleLastRepositionApplyEvent();
            }
        }
    });

    //! SCREEN
    connect(m_view, &QQuickView::screenChanged, this, [&]() {
        finishPendingScreenPlacementIfApplied();
    });

    //! LAYOUT
    connect(m_view, &View::layoutChanged, this, [&]() {
        if (!m_nextLayoutName.isEmpty()
                && m_view->layout()
                && m_view->layout()->name()
                    == m_nextLayoutName) {
            const bool isrelocationlastevent =
                isLastHidingRelocationEvent();
            m_nextLayoutName = "";

            //! make sure that View has been repositioned properly in next layout and show view afterwards
            if (isrelocationlastevent) {
                scheduleLastRepositionApplyEvent();
            }
        }
    });

    //! APPLY CHANGES
    connect(this, &Positioner::hidingForRelocationFinished, this, [&]() {
        const auto request =
            m_placementRequests.pending();
        if (!request) {
            qCritical() << "Positioner received relocation hide completion without a pending request for"
                        << m_view->validTitle();
            return;
        }
        const auto generation = request->token;
        const PlacementIntent prior =
            currentPlacementIntent();
        const auto prepared =
            preparePlacementApplication(
                *request,
                prior);
        if (!prepared) {
            cancelFailedLayoutRelocation(
                generation,
                prior);
            return;
        }
        const PlacementApplicationPlan plan =
            *prepared;
        const bool applyWithReveal =
            m_repositionIsAnimated;
        //! must be called only if relocation is animated
        if (applyWithReveal) {
            m_repositionIsAnimated = false;
            m_view->effects()->setAnimationsBlocked(true);
        }

        if (!m_view->visibility()->beginPlacementTransaction(
                plan.changesReservationOwnership)) {
            qCritical() << "Positioner cancelled placement after reservation retirement failed for"
                        << m_view->validTitle();
            cancelFailedLayoutRelocation(
                plan.token,
                plan.prior);
            return;
        }
        if (!validatesPlacementApplication(
                plan)) {
            qCritical() << "Positioner cancelled placement after a preflight participant changed during reservation retirement for"
                        << m_view->validTitle();
            cancelFailedLayoutRelocation(
                plan.token,
                plan.prior);
            return;
        }

        {
            Q_ASSERT(!m_applyingPlacementTransaction);
            const QScopedValueRollback applyingPlacement{
                m_applyingPlacementTransaction, true};

            //! LAYOUT
            if (m_placementRequests.isCurrent(
                    plan.token)
                    && plan.movesLayout) {
                const auto moveResult =
                    m_corona->layoutsManager()
                        ->moveView(
                            qString(
                                plan.prior.layoutName),
                            m_view
                                ->containment()
                                ->id(),
                            plan
                                .destinationLayoutName);
                if (!Layouts::Manager::
                        moveWasCommitted(
                            moveResult)) {
                    qCritical() << "Positioner: cancelling refused relocation of containment"
                                << m_view->containment()->id()
                                << "to"
                                << plan.destinationLayoutName;
                    cancelFailedLayoutRelocation(
                        plan.token,
                        plan.prior);
                    return;
                }
                if (moveResult
                        == Layouts::Manager::
                            MoveViewResult::
                                CommittedRecoveryRequired) {
                    qCritical() << "Positioner committed relocation of containment"
                                << m_view->containment()->id()
                                << "with durable cleanup pending";
                }
            }

            //! OUTPUT POLICY AND PHYSICAL OUTPUT
            if (m_placementRequests.isCurrent(
                    plan.token)
                    && plan.appliesOutput) {
                if (!plan.outputScreen) {
                    qCritical() << "Positioner: cancelling placement without a resolved output";
                    cancelFailedLayoutRelocation(
                        plan.token,
                        plan.prior);
                    return;
                }

                if (!m_nextScreenName.isEmpty()) {
                    m_nextScreen =
                        plan.outputScreen;
                }
                applyOutputPlacement(
                    plan.outputScreen,
                    plan.target.followsPrimary);
                if (m_placementRequests.isCurrent(
                        plan.token)) {
                    m_pendingOutputScreen.clear();
                    m_pendingFollowsPrimary.reset();
                    m_pendingOutputOwnershipChange =
                        false;
                }
            }

            //! SCREEN_EDGE
            if (m_placementRequests.isCurrent(
                    plan.token)
                    && plan.movesEdge) {
                m_view->setLocation(
                    static_cast<Plasma::Types::Location>(
                        plan.target.edge));
            }

            //! ALIGNMENT
            if (m_placementRequests.isCurrent(
                    plan.token)
                    && plan.movesAlignment
                    && m_nextAlignment != m_view->alignment()) {
                m_view->setAlignment(
                    static_cast<Latte::Types::Alignment>(
                        plan.target.alignment));
                if (m_placementRequests.isCurrent(
                        plan.token)
                        && m_view->alignment()
                            == static_cast<Latte::Types::Alignment>(
                                plan.target.alignment)) {
                    m_nextAlignment =
                        Latte::Types::NoneAlignment;
                }
            }

            //! SCREENSGROUP
            if (m_placementRequests.isCurrent(
                    plan.token)
                    && plan.movesScreensGroup) {
                auto *const originalView =
                    qobject_cast<Latte::OriginalView *>(m_view);
                Q_ASSERT(originalView);
                if (!originalView) {
                    qFatal(
                        "Positioner lost the validated OriginalView before applying screen-group placement");
                }
                originalView->setScreensGroup(
                    static_cast<Latte::Types::ScreensGroup>(
                        plan.target.screensGroup));
            }
        }

        if (m_placementRequests.isCurrent(
                plan.token)
                && !hasPendingPlacementComponents()) {
            if (applyWithReveal) {
                scheduleLastRepositionApplyEvent();
            }
        }
    });
}

void Positioner::finishPendingScreenPlacementIfApplied()
{
    if (!m_view || !m_nextScreen
            || m_nextScreen != m_view->screen()) {
        return;
    }

    //[1] if panels are not excluded from confirmed geometry check then they are stuck in sliding out end
    //and they do not switch to new screen geometry
    //[2] under wayland view geometry may be delayed to be updated even though the screen has been updated correctly
    const bool geometryConfirmsOutput =
        KWindowSystem::isPlatformWayland()
        || m_view->behaveAsPlasmaPanel()
        || m_nextScreen->geometry().contains(
            m_view->geometry().center());
    if (!geometryConfirmsOutput) {
        return;
    }

    const bool isRelocationLastEvent =
        isLastHidingRelocationEvent();
    m_nextScreen = nullptr;
    m_nextScreenName.clear();

    //! Make sure that View has been repositioned properly in the next screen
    //! and show it afterwards.
    if (isRelocationLastEvent) {
        scheduleLastRepositionApplyEvent();
    }
}

void Positioner::cancelFailedLayoutRelocation(
    const PlacementRequestState::Token token,
    const PlacementIntent &prior)
{
    if (!m_placementRequests
            .cancelToCommittedIfCurrent(
                token,
                prior)) {
        qCritical() << "Positioner refused to cancel stale relocation generation"
                    << token
                    << "for"
                    << m_view->validTitle();
        return;
    }
    m_refusedPlacementRequests.insert(token);
    static_cast<void>(
        m_placementCompletions.complete(
            token,
            PlacementRequestOutcome::Refused));
    projectPendingPlacement(
        *m_placementRequests.pending());

    //! The hide animation already completed. Finish the same generation
    //! through the normal reveal path after making every pending placement
    //! component empty, so geometry settlement and edit-window restoration
    //! retain their ordinary ordering.
    scheduleLastRepositionApplyEvent();
}

void Positioner::completePlacementRequest(
    const PlacementRequestState::Token token)
{
    const bool wasRefused =
        m_refusedPlacementRequests.erase(token)
        > 0;
    if (wasRefused) {
        return;
    }

    static_cast<void>(
        m_placementCompletions.complete(
            token,
            PlacementRequestOutcome::Committed));
}

bool Positioner::inLayoutUnloading()
{
    return m_inLayoutUnloading;
}

bool Positioner::inRelocationAnimation() const
{
    return m_placementRequests.pending().has_value();
}

bool Positioner::hasPendingPlacementComponents() const
{
    return m_nextScreenEdge != Plasma::Types::Floating
        || !m_nextLayoutName.isEmpty()
        || !m_nextScreenName.isEmpty()
        || m_nextAlignment
            != Latte::Types::NoneAlignment
        || m_pendingFollowsPrimary.has_value();
}

bool Positioner::inSlideAnimation() const
{
    return m_inSlideAnimation;
}

void Positioner::setInSlideAnimation(bool active)
{
    if (m_inSlideAnimation == active) {
        return;
    }

    m_inSlideAnimation = active;
    Q_EMIT inSlideAnimationChanged();
}

bool Positioner::isCursorInsideView() const
{
    return m_view->geometry().contains(QCursor::pos(m_screenToFollow));
}

bool Positioner::isStickedOnTopEdge() const
{
    return m_isStickedOnTopEdge;
}

void Positioner::setIsStickedOnTopEdge(bool sticked)
{
    if (m_isStickedOnTopEdge == sticked) {
        return;
    }

    m_isStickedOnTopEdge = sticked;
    Q_EMIT isStickedOnTopEdgeChanged();
}

bool Positioner::isStickedOnBottomEdge() const
{
    return m_isStickedOnBottomEdge;
}

void Positioner::setIsStickedOnBottomEdge(bool sticked)
{
    if (m_isStickedOnBottomEdge == sticked) {
        return;
    }

    m_isStickedOnBottomEdge = sticked;
    Q_EMIT isStickedOnBottomEdgeChanged();
}

void Positioner::updateInRelocationAnimation()
{
    bool inrelocationanimation = inRelocationAnimation();

    if (m_inRelocationAnimation == inrelocationanimation) {
        return;
    }

    m_inRelocationAnimation = inrelocationanimation;
    Q_EMIT inRelocationAnimationChanged();
}

bool Positioner::isLastHidingRelocationEvent() const
{
    int events{0};

    if (!m_nextLayoutName.isEmpty()) {
        events++;
    }

    if (!m_nextScreenName.isEmpty()){
        events++;
    }

    if (m_nextScreenEdge != Plasma::Types::Floating) {
        events++;
    }

    if (m_nextAlignment
            != Latte::Types::NoneAlignment) {
        events++;
    }

    return (events <= 1);
}

PlacementIntent Positioner::currentPlacementIntent() const
{
    const auto *const layout = m_view->layout();
    QScreen *const output = assignedScreen();
    return {
        utf8(layout ? layout->name() : QString()),
        static_cast<int>(m_view->screensGroup()),
        utf8(
            m_view->onPrimary()
            ? Latte::Data::Screen::ONPRIMARYNAME
            : currentScreenName()),
        utf8(
            output
            ? output->name()
            : currentScreenName()),
        m_view->onPrimary(),
        static_cast<int>(m_view->location()),
        static_cast<int>(m_view->alignment()),
    };
}

std::optional<Positioner::PlacementApplicationPlan>
Positioner::preparePlacementApplication(
    const PlacementRequestState::Request &request,
    const PlacementIntent &prior) const
{
    if (!m_placementRequests.isCurrent(
            request.token)) {
        qCritical() << "Positioner refused to prepare stale placement generation"
                    << request.token
                    << "for"
                    << m_view->validTitle();
        return std::nullopt;
    }

    PlacementApplicationPlan plan;
    plan.token = request.token;
    plan.prior = prior;
    plan.target = request.intent;
    plan.destinationLayoutName =
        qString(plan.target.layoutName);
    plan.movesLayout =
        plan.target.layoutName
        != plan.prior.layoutName;
    plan.movesEdge =
        plan.target.edge
        != plan.prior.edge;
    plan.movesAlignment =
        plan.target.alignment
        != plan.prior.alignment;
    plan.movesScreensGroup =
        plan.target.screensGroup
        != plan.prior.screensGroup;

    const QString targetOutputName =
        qString(
            plan.target.resolvedOutputName);
    for (QScreen *const screen :
            qGuiApp->screens()) {
        if (screen
                && screen->name()
                    == targetOutputName) {
            plan.outputScreen = screen;
            break;
        }
    }

    const bool outputPolicyChanges =
        plan.target.followsPrimary
            != plan.prior.followsPrimary
        || plan.target.resolvedOutputName
            != plan.prior.resolvedOutputName;
    const bool outputStateNeedsApplication =
        !plan.outputScreen
        || outputOwnershipIsNeeded(
            plan.outputScreen);
    plan.appliesOutput =
        m_pendingFollowsPrimary.has_value()
        || outputPolicyChanges
        || outputStateNeedsApplication;
    plan.changesReservationOwnership =
        plan.movesEdge
        || (plan.outputScreen
            && outputOwnershipIsNeeded(
                plan.outputScreen));

    if (!validatesPlacementApplication(
            plan)) {
        return std::nullopt;
    }

    return plan;
}

bool Positioner::validatesPlacementApplication(
    const PlacementApplicationPlan &plan) const
{
    if (!m_placementRequests.isCurrent(
            plan.token)) {
        qCritical() << "Positioner refused stale placement preflight generation"
                    << plan.token
                    << "for"
                    << m_view->validTitle();
        return false;
    }
    if (!m_view || !m_view->layout()
            || !m_view->containment()) {
        qCritical() << "Positioner refused placement without live view ownership for generation"
                    << plan.token;
        return false;
    }
    if (currentPlacementIntent()
            != plan.prior) {
        qCritical() << "Positioner refused placement after committed ownership changed during generation"
                    << plan.token
                    << "for"
                    << m_view->validTitle();
        return false;
    }

    if (plan.appliesOutput) {
        const auto screens =
            qGuiApp->screens();
        if (!plan.outputScreen
                || !screens.contains(
                    plan.outputScreen.data())
                || plan.outputScreen->name()
                    != qString(
                        plan.target
                            .resolvedOutputName)) {
            qCritical() << "Positioner refused placement after destination output disappeared for generation"
                        << plan.token
                        << "target="
                        << qString(
                            plan.target
                                .resolvedOutputName);
            return false;
        }
    }

    if (plan.movesLayout
            && !m_corona->layoutsManager()
                ->canMoveView(
                    qString(
                        plan.prior.layoutName),
                    m_view->containment()->id(),
                    plan.destinationLayoutName)) {
        qCritical() << "Positioner refused placement after layout-move preflight failed for generation"
                    << plan.token
                    << "destination="
                    << plan.destinationLayoutName;
        return false;
    }

    if (plan.movesScreensGroup
            && (!m_view->isOriginal()
                || !qobject_cast<
                    Latte::OriginalView *>(
                    m_view))) {
        qCritical() << "Positioner refused screen-group placement without an OriginalView for generation"
                    << plan.token;
        return false;
    }

    return true;
}

void Positioner::projectPendingPlacement(
    const PlacementRequestState::Request &request)
{
    Q_ASSERT(m_placementRequests.isCurrent(
        request.token));
    const PlacementIntent current =
        currentPlacementIntent();
    const PlacementIntent &target = request.intent;

    m_nextLayoutName =
        target.layoutName != current.layoutName
        ? qString(target.layoutName)
        : QString();
    m_nextScreensGroup =
        static_cast<Latte::Types::ScreensGroup>(
            target.screensGroup);

    m_pendingOutputScreen.clear();
    m_pendingFollowsPrimary.reset();
    m_pendingOutputOwnershipChange = false;
    m_nextScreenName.clear();
    m_nextScreen.clear();
    const QString resolvedOutput =
        qString(target.resolvedOutputName);
    QScreen *resolvedScreen{nullptr};
    for (QScreen *const screen :
            qGuiApp->screens()) {
        if (screen
                && screen->name()
                    == resolvedOutput) {
            resolvedScreen = screen;
            break;
        }
    }
    const bool assignedOutputNeedsChange =
        m_screenToFollow != resolvedScreen;
    const bool layerShellOutputNeedsChange =
        resolvedScreen
        && m_view->layerShellNeedsOutput(
            resolvedScreen);
    const bool windowNeedsOutput =
        m_view->screen() != resolvedScreen;
    if (target.followsPrimary
            != current.followsPrimary
            || target.resolvedOutputName
                != current.resolvedOutputName
            || !resolvedScreen
            || assignedOutputNeedsChange
            || layerShellOutputNeedsChange
            || windowNeedsOutput) {
        m_pendingOutputScreen = resolvedScreen;
        m_pendingFollowsPrimary =
            target.followsPrimary;
        m_pendingOutputOwnershipChange =
            assignedOutputNeedsChange
            || layerShellOutputNeedsChange;
        if (windowNeedsOutput) {
            m_nextScreenName =
                resolvedOutput;
        }
    }

    m_nextScreenEdge =
        target.edge != current.edge
        ? static_cast<Plasma::Types::Location>(
            target.edge)
        : Plasma::Types::Floating;
    m_nextAlignment =
        target.alignment != current.alignment
        ? static_cast<Latte::Types::Alignment>(
            target.alignment)
        : Latte::Types::NoneAlignment;
}

void Positioner::setNextScreen(
    const int screensGroup,
    const QString &screenName)
{
    setNextLocation(
        QString(),
        screensGroup,
        screenName,
        Plasma::Types::Floating,
        Latte::Types::NoneAlignment);
}

void Positioner::setNextLayout(
    const QString &layoutName)
{
    setNextLocation(
        layoutName,
        PreserveScreensGroup,
        QString(),
        Plasma::Types::Floating,
        Latte::Types::NoneAlignment);
}

void Positioner::setNextEdge(const int edge)
{
    setNextLocation(
        QString(),
        PreserveScreensGroup,
        QString(),
        edge,
        Latte::Types::NoneAlignment);
}

void Positioner::setNextAlignment(
    const int alignment)
{
    setNextLocation(
        QString(),
        PreserveScreensGroup,
        QString(),
        Plasma::Types::Floating,
        alignment);
}

void Positioner::setNextLocation(
    const QString layoutName,
    const int screensGroup,
    QString screenName,
    const int edge,
    const int alignment)
{
    static_cast<void>(
        requestNextLocation(
            layoutName,
            screensGroup,
            std::move(screenName),
            edge,
            alignment));
}

PlacementSubmission Positioner::requestNextLocation(
    const QString &layoutName,
    const int screensGroup,
    QString screenName,
    const int edge,
    const int alignment,
    PlacementCompletionRegistry::Handler
        completionHandler)
{
    const PlacementIntent committed =
        currentPlacementIntent();
    const PlacementIntent base =
        m_placementRequests.pending()
        ? m_placementRequests.pending()->intent
        : committed;
    PlacementPatch patch;

    if (!layoutName.isEmpty()) {
        auto *const destination =
            m_corona->layoutsManager()
                ->synchronizer()
                ->centralLayout(layoutName);
        if (!destination) {
            qCritical() << "Positioner refused placement in unavailable layout"
                        << layoutName
                        << "for"
                        << m_view->validTitle();
            return {
                PlacementSubmissionStatus::Rejected,
                0,
            };
        }
        patch.layoutName = utf8(layoutName);
    }

    if (screensGroup != PreserveScreensGroup) {
        if (screensGroup
                < Latte::Types::SingleScreenGroup
                || screensGroup
                    > Latte::Types::AllSecondaryScreensGroup
                || (!m_view->isOriginal()
                    && screensGroup
                        != Latte::Types::SingleScreenGroup)) {
            qCritical() << "Positioner refused invalid screen group"
                        << screensGroup
                        << "for"
                        << m_view->validTitle();
            return {
                PlacementSubmissionStatus::Rejected,
                0,
            };
        }

        const auto nextScreensGroup =
            static_cast<Latte::Types::ScreensGroup>(
                screensGroup);
        patch.screensGroup = screensGroup;
        if (nextScreensGroup
                == Latte::Types::AllScreensGroup) {
            screenName =
                Latte::Data::Screen::ONPRIMARYNAME;
        } else if (nextScreensGroup
                == Latte::Types::AllSecondaryScreensGroup) {
            auto *const originalView =
                qobject_cast<Latte::OriginalView *>(
                    m_view);
            Q_ASSERT(originalView);
            const int screenId =
                originalView
                    ->expectedScreenIdFromScreenGroup(
                        nextScreensGroup);
            if (screenId
                    == Latte::ScreenPool::NOSCREENID) {
                qCritical() << "Positioner refused all-secondary placement without an active secondary output for"
                            << m_view->validTitle();
                return {
                    PlacementSubmissionStatus::Rejected,
                    0,
                };
            }
            screenName =
                m_corona->screenPool()
                    ->connector(screenId);
        }
    }

    if (!screenName.isEmpty()) {
        const bool followsPrimary =
            screenName
            == Latte::Data::Screen::ONPRIMARYNAME;
        QScreen *const primaryScreen =
            m_corona->screenPool()
                ->primaryScreen();
        const QString resolvedOutput =
            followsPrimary
            ? (primaryScreen
                ? primaryScreen->name()
                : QString())
            : screenName;
        QScreen *destinationScreen{nullptr};
        for (QScreen *const screen :
                qGuiApp->screens()) {
            if (screen
                    && screen->name()
                        == resolvedOutput) {
                destinationScreen = screen;
                break;
            }
        }
        if (!destinationScreen) {
            qCritical() << "Positioner refused placement on unavailable output"
                        << resolvedOutput
                        << "for"
                        << m_view->validTitle();
            return {
                PlacementSubmissionStatus::Rejected,
                0,
            };
        }
        patch.logicalOutputName =
            utf8(screenName);
        patch.resolvedOutputName =
            utf8(resolvedOutput);
        patch.followsPrimary =
            followsPrimary;
    }

    if (edge != Plasma::Types::Floating) {
        const auto nextEdge =
            static_cast<Plasma::Types::Location>(
                edge);
        if (nextEdge != Plasma::Types::TopEdge
                && nextEdge
                    != Plasma::Types::BottomEdge
                && nextEdge
                    != Plasma::Types::LeftEdge
                && nextEdge
                    != Plasma::Types::RightEdge) {
            qCritical() << "Positioner refused invalid edge"
                        << edge
                        << "for"
                        << m_view->validTitle();
            return {
                PlacementSubmissionStatus::Rejected,
                0,
            };
        }
        patch.edge = edge;
    }

    if (alignment
            != Latte::Types::NoneAlignment) {
        const auto targetEdge =
            static_cast<Plasma::Types::Location>(
                patch.edge.value_or(base.edge));
        const auto normalized =
            Latte::Data::View::
                normalizeAlignmentForEdge(
                    static_cast<
                        Latte::Types::Alignment>(
                        alignment),
                    targetEdge);
        if (normalized
                == Latte::Types::NoneAlignment) {
            qCritical() << "Positioner refused invalid alignment"
                        << alignment
                        << "for edge"
                        << static_cast<int>(
                            targetEdge);
            return {
                PlacementSubmissionStatus::Rejected,
                0,
            };
        }
        patch.alignment =
            static_cast<int>(normalized);
    }

    const std::optional<
        PlacementRequestState::Token>
        supersededToken =
            m_placementRequests.pending()
            ? std::optional{
                m_placementRequests
                    .pending()
                    ->token}
            : std::nullopt;
    const auto submission =
        m_placementRequests.submit(
            committed,
            patch);
    if (!submission.accepted) {
        if (!m_placementRequests.pending()) {
            return {
                PlacementSubmissionStatus::Applied,
                m_appliedRelocationGeneration,
            };
        }

        const auto pendingToken =
            m_placementRequests.pending()->token;
        if (m_refusedPlacementRequests.contains(
                pendingToken)) {
            return {
                PlacementSubmissionStatus::Rejected,
                pendingToken,
            };
        }
        if (completionHandler
                && !m_placementCompletions.watch(
                    pendingToken,
                    std::move(completionHandler))) {
            qFatal(
                "Positioner failed to observe a valid pending placement generation");
        }
        return {
            PlacementSubmissionStatus::
                CompletionExpected,
            pendingToken,
        };
    }

    const auto requestToken =
        submission.request.token;
    if (completionHandler
            && !m_placementCompletions.watch(
                requestToken,
                std::move(completionHandler))) {
        qFatal(
            "Positioner failed to observe a newly accepted placement generation");
    }
    if (supersededToken) {
        m_refusedPlacementRequests.erase(
            *supersededToken);
        static_cast<void>(
            m_placementCompletions.complete(
                *supersededToken,
                PlacementRequestOutcome::
                    Superseded));
    }

    const bool hideAlreadyOwned =
        m_repositionIsAnimated;
    const PlacementIntent &target =
        submission.request.intent;
    bool requiresAnimatedHide =
        target.resolvedOutputName
            != committed.resolvedOutputName
        || target.edge != committed.edge;
    if (target.layoutName
            != committed.layoutName) {
        auto *const origin =
            qobject_cast<CentralLayout *>(
                m_view->layout());
        auto *const destination =
            m_corona->layoutsManager()
                ->synchronizer()
                ->centralLayout(
                    qString(target.layoutName));
        Q_ASSERT(origin);
        Q_ASSERT(destination);
        requiresAnimatedHide =
            requiresAnimatedHide
            || origin->lastUsedActivity()
                != destination
                    ->lastUsedActivity();
    }

    m_relocationGeneration =
        submission.request.token;
    projectPendingPlacement(
        submission.request);

    if (m_view->isOriginal()) {
        auto *const originalView =
            qobject_cast<Latte::OriginalView *>(
                m_view);
        Q_ASSERT(originalView);
        originalView->setNextLocationForClones(
            layoutName,
            edge,
            alignment);
    }

    m_repositionFromViewSettingsWindow =
        m_repositionFromViewSettingsWindow
        || m_view->settingsWindowIsShown();
    m_repositionIsAnimated =
        hideAlreadyOwned
        || requiresAnimatedHide;

    if (requiresAnimatedHide) {
        Q_EMIT hidingForRelocationStarted();
    } else if (!hideAlreadyOwned) {
        Q_EMIT hidingForRelocationFinished();
        applyUnanimatedPlacementGeneration();
    }

    return {
        PlacementSubmissionStatus::
            CompletionExpected,
        requestToken,
    };
}

}
}
