/*
    SPDX-FileCopyrightText: 2018 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef POSITIONER_H
#define POSITIONER_H

//local
#include <coretypes.h>
#include "floatingpanelgeometry.h"
#include "positionergeometry.h"
#include "placementrequeststate.h"
#include "../wm/abstractwindowinterface.h"
#include "../wm/windowinfowrap.h"

// Qt
#include <QObject>
#include <QPointer>
#include <QScreen>
#include <QTimer>

#include <optional>

// Plasma
#include <Plasma/Containment>

namespace Plasma {
class Types;
}

namespace Latte {
class Corona;
class View;
}

namespace Latte {
namespace ViewPart {

class Positioner: public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool inRelocationAnimation READ inRelocationAnimation NOTIFY inRelocationAnimationChanged)
    Q_PROPERTY(bool inRelocationShowing READ inRelocationShowing WRITE setInRelocationShowing NOTIFY inRelocationShowingChanged)
    Q_PROPERTY(bool inSlideAnimation READ inSlideAnimation WRITE setInSlideAnimation NOTIFY inSlideAnimationChanged)

    Q_PROPERTY(bool isOffScreen READ isOffScreen NOTIFY isOffScreenChanged)
    Q_PROPERTY(bool isStickedOnTopEdge READ isStickedOnTopEdge WRITE setIsStickedOnTopEdge NOTIFY isStickedOnTopEdgeChanged)
    Q_PROPERTY(bool isStickedOnBottomEdge READ isStickedOnBottomEdge WRITE setIsStickedOnBottomEdge NOTIFY isStickedOnBottomEdgeChanged)

    Q_PROPERTY(int currentScreenId READ currentScreenId NOTIFY currentScreenChanged)

    Q_PROPERTY(QRect canvasGeometry READ canvasGeometry NOTIFY canvasGeometryChanged)
    Q_PROPERTY(quint64 surfaceGeometryPublicationRevision
                   READ surfaceGeometryPublicationRevision
                   NOTIFY surfaceGeometryPublicationRevisionChanged)

    //! animating window slide
    Q_PROPERTY(int slideOffset READ slideOffset WRITE setSlideOffset NOTIFY slideOffsetChanged)
    Q_PROPERTY(QString currentScreenName READ currentScreenName NOTIFY currentScreenChanged)

public:
    Positioner(Latte::View *parent);
    virtual ~Positioner();

    int currentScreenId() const;
    QString currentScreenName() const;
    [[nodiscard]] QScreen *assignedScreen() const;

    int slideOffset() const;
    void setSlideOffset(int offset);

    bool inLayoutUnloading();
    bool inRelocationAnimation() const;

    bool inRelocationShowing() const;
    void setInRelocationShowing(bool active);

    //! True only after the newest placement generation has applied and all
    //! deferred geometry reconciliation owned by this view has drained.
    bool geometryIsSettled() const;
    quint64 relocationGeneration() const;
    quint64 appliedRelocationGeneration() const;

    bool inSlideAnimation() const;
    void setInSlideAnimation(bool active);

    bool isCursorInsideView() const;

    bool isStickedOnTopEdge() const;
    void setIsStickedOnTopEdge(bool sticked);

    bool isStickedOnBottomEdge() const;
    void setIsStickedOnBottomEdge(bool sticked);

    bool isOffScreen() const;

    //! true from construction until the containment QML reports the end of
    //! its startup sequence (startupFinished); together with isOffScreen()
    //! this is the startup-stranding diagnostic pair - a view stuck with
    //! both set never slid in (the Phase 8 startup-stranding item)
    bool inStartup() const;

    QRect canvasGeometry();
    [[nodiscard]] QRect surfaceGeometry() const;
    [[nodiscard]] quint64 surfaceGeometryPublicationRevision() const;

    bool setScreenToFollow(QScreen *scr, bool updateScreenId = true);
    void setWindowOnActivities(const Latte::WindowSystem::WindowId &wid, const QStringList &activities);

    void reconsiderScreen();

    Latte::WindowSystem::WindowId trackedWindowId();

public Q_SLOTS:
    Q_INVOKABLE void setNextLocation(const QString layoutName, const int screensGroup, QString screenName, int edge, int alignment);
    Q_INVOKABLE void setNextScreen(const int screensGroup, const QString &screenName);
    void setNextLayout(const QString &layoutName);
    Q_INVOKABLE void setNextEdge(int edge);
    Q_INVOKABLE void setNextAlignment(int alignment);
    Q_INVOKABLE void slideInDuringStartup();

    void syncGeometry();

    //! Direct geometry calculations without the ordinary timer. The result is
    //! true only when the solved rectangle reached the assigned LayerShell
    //! output and passed its applied-state postconditions.
    bool immediateSyncGeometry();

    void slideOutDuringExit(Plasma::Types::Location location = Plasma::Types::Floating);

    void initDelayedSignals();
    void updateWaylandId();

Q_SIGNALS:
    void canvasGeometryChanged();
    void currentScreenChanged();
    void edgeChanged();
    void screenGeometryChanged();
    void slideOffsetChanged();
    //! Surface placement and reservation ownership reached one authoritative
    //! output, edge, and generation.
    void placementTransactionCommitted();
    void surfaceGeometryCalculated(const QRect &geometry);
    void surfaceGeometryPublicationRevisionChanged();
    void windowSizeChanged();
    void winIdChanged();

    //! these two signals are used from config ui and containment ui
    //! in order to orchestrate an animated hiding/showing of dock
    //! during changing location
    void hidingForRelocationStarted();
    void hidingForRelocationFinished();
    void showingAfterRelocationFinished();

    void startupFinished(); //called from containment qml end of startup sequence

    void onHideWindowsForSlidingOut();
    void inRelocationAnimationChanged();
    void inRelocationShowingChanged();
    void inSlideAnimationChanged();
    void isOffScreenChanged();
    void isStickedOnTopEdgeChanged();
    void isStickedOnBottomEdgeChanged();

private Q_SLOTS:
    void onScreenChanged(QScreen *screen);
    void onCurrentLayoutIsSwitching(const QString &layoutName);
    void onLastRepositionApplyEvent();
    void onStartupFinished();

    void validateDockGeometry();
    void updateInRelocationAnimation();
    void syncLatteViews();
    void updateContainmentScreen();

private:
    [[nodiscard]] bool applyOutputPlacement(
        QScreen *destination,
        bool followsPrimary);
    [[nodiscard]] bool outputPlacementIsNeeded(
        const QScreen *destination) const;
    void applyUnanimatedPlacementGeneration();
    void cancelFailedLayoutRelocation();
    [[nodiscard]] PlacementIntent currentPlacementIntent() const;
    void finishPendingScreenPlacementIfApplied();
    [[nodiscard]] bool hasPendingPlacementComponents() const;
    void init();
    void initSignalingForLocationChangeSliding();
    void projectPendingPlacement(
        const PlacementRequestState::Request &request);
    void scheduleLastRepositionApplyEvent();
    void scheduleUnanimatedPlacementApplyEvent();

    void updateFormFactor();
    void resizeWindow(
        const QRect &availableScreenRect,
        const QSize &assignedScreenSize);
    void updatePosition(QRect availableScreenRect = QRect());
    void updateCanvasGeometry(
        const QRect &availableScreenRect,
        const QRect &assignedScreenGeometry = QRect());
    [[nodiscard]] bool solveAndApplyGeometry(
        bool completesRelocation = false);
    [[nodiscard]] std::optional<FloatingPanelGeometry::Solution>
    solveStablePanelGeometry(
        const QRect &availableScreenRect,
        const QRect &assignedScreenGeometry) const;
    void applyStablePanelGeometry(
        const FloatingPanelGeometry::Solution &solution);

    void validateTopBottomBorders(
        const QRect &availableScreenRect,
        const QRegion &availableScreenRegion,
        const QRect &assignedScreenGeometry);

    void setCanvasGeometry(const QRect &geometry);

    bool isLastHidingRelocationEvent() const;

    QRect maximumNormalGeometry(QRect screenGeometry = QRect());

    WindowSystem::AbstractWindowInterface::Slide slideLocation(Plasma::Types::Location location);

    //! snapshot of the View properties the PositionerGeometry core reads (EX-09)
    PositionerGeometry::ViewGeometryInputs geometryInputs() const;

private:
    bool m_inDelete{false};
    bool m_inLayoutUnloading{false};
    bool m_inRelocationAnimation{false};
    bool m_inRelocationShowing{false};
    bool m_inSlideAnimation{false};
    bool m_inStartup{true};
    //! Placement signals normally synchronize the layout immediately. Hold
    //! that callback through policy, output, edge, alignment, geometry,
    //! LayerShell application, and reservation publication so no observer
    //! acts on a partial compound placement.
    bool m_applyingPlacementTransaction{false};
    bool m_layoutSyncDeferredByPlacementTransaction{false};

    bool m_isStickedOnTopEdge{false};
    bool m_isStickedOnBottomEdge{false};

    int m_slideOffset{0};

    QRect m_canvasGeometry;
    //! it is used in order to enforce X11 to never miss window geometry
    QRect m_validGeometry;
    quint64 m_surfaceGeometryPublicationRevision{0};
    //! it is used to update geometry calculations without requesting no needed Corona calculations
    QRect m_lastAvailableScreenRect;
    QRegion m_lastAvailableScreenRegion;

    QPointer<Latte::View> m_view;
    QPointer<Latte::Corona> m_corona;

    QString m_screenNameToFollow;
    QPointer<QScreen> m_screenToFollow;
    QMetaObject::Connection m_screenGeometryConnection;
    QTimer m_screenSyncTimer;
    QTimer m_syncGeometryTimer;
    QTimer m_validateGeometryTimer;

    //!used for relocation properties group
    bool m_repositionFromViewSettingsWindow{false};
    bool m_repositionIsAnimated{false};
    quint64 m_relocationGeneration{0};
    quint64 m_appliedRelocationGeneration{0};
    std::optional<quint64>
        m_scheduledPlacementCompletion;
    PlacementRequestState m_placementRequests;

    //! These fields are one current-generation projection of
    //! m_placementRequests. They drive existing Qt change acknowledgements;
    //! no field independently owns requested placement state.
    QString m_nextLayoutName;
    Latte::Types::ScreensGroup m_nextScreensGroup{Latte::Types::SingleScreenGroup};
    QPointer<QScreen> m_pendingOutputScreen;
    std::optional<bool> m_pendingFollowsPrimary;
    bool m_pendingOutputOwnershipChange{false};
    QString m_nextScreenName;
    QPointer<QScreen> m_nextScreen;
    Plasma::Types::Location m_nextScreenEdge{Plasma::Types::Floating};
    Latte::Types::Alignment m_nextAlignment{Latte::Types::NoneAlignment};

    //! last edge the view actually had; the safe source for exit slides on
    //! teardown paths where the containment can no longer be dereferenced
    Plasma::Types::Location m_lastLocation{Plasma::Types::Floating};

    Latte::WindowSystem::WindowId m_trackedWindowId;
};

}
}

#endif
