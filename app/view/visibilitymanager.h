/*
    SPDX-FileCopyrightText: 2016 Smith AR <audoban@openmailbox.org>
    SPDX-FileCopyrightText: 2016 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef VISIBILITYMANAGER_H
#define VISIBILITYMANAGER_H

#include <array>
#include <optional>

// local
#include <coretypes.h>
#include "../plasma/quick/containmentview.h"
#include "helpers/screenspacereservationpublication.h"

// Qt
#include <QObject>
#include <QTimer>

// Plasma
#include <Plasma/Containment>


namespace Latte {
class Corona;
class View;
namespace ViewPart {
class AutoHideScreenEdge;
class FloatingGapWindow;
class ScreenEdgeGhostWindow;
}
namespace WindowSystem {
class AbstractWindowInterface;
}
}

namespace Latte {
namespace ViewPart {

class VisibilityManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hidingIsBlocked READ hidingIsBlocked NOTIFY hidingIsBlockedChanged)

    Q_PROPERTY(Latte::Types::Visibility mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(bool raiseOnDesktop READ raiseOnDesktop WRITE setRaiseOnDesktop NOTIFY raiseOnDesktopChanged)
    Q_PROPERTY(bool raiseOnActivity READ raiseOnActivity WRITE setRaiseOnActivity NOTIFY raiseOnActivityChanged)    
    Q_PROPERTY(bool isHidden READ isHidden WRITE setIsHidden NOTIFY isHiddenChanged)
    Q_PROPERTY(bool isShownFully READ isShownFully WRITE setIsShownFully NOTIFY isShownFullyChanged)
    Q_PROPERTY(bool isBelowLayer READ isBelowLayer NOTIFY isBelowLayerChanged)    
    Q_PROPERTY(bool isSidebar READ isSidebar NOTIFY isSidebarChanged)
    Q_PROPERTY(bool containsMouse READ containsMouse NOTIFY containsMouseChanged)

    //! Floating Gap Window to identify mouse between the screenEdge and the view when it does not accept any other
    //! user input
    Q_PROPERTY(bool isFloatingGapWindowEnabled READ isFloatingGapWindowEnabled WRITE setIsFloatingGapWindowEnabled NOTIFY isFloatingGapWindowEnabledChanged)

    //! KWin Edges Support Options
    Q_PROPERTY(bool enableKWinEdges READ enableKWinEdges WRITE setEnableKWinEdges NOTIFY enableKWinEdgesChanged)
    Q_PROPERTY(bool supportsKWinEdges READ supportsKWinEdges NOTIFY supportsKWinEdgesChanged)

    Q_PROPERTY(int timerShow READ timerShow WRITE setTimerShow NOTIFY timerShowChanged)
    Q_PROPERTY(int timerHide READ timerHide WRITE setTimerHide NOTIFY timerHideChanged)

    //! Struts
    Q_PROPERTY(int strutsThickness READ strutsThickness WRITE setStrutsThickness NOTIFY strutsThicknessChanged)

public:
    enum class ScreenEdgeBackend {
        None,
        ClientGhost,
        KWinAutoHide,
    };

    static const QRect ISHIDDENMASK;

    //! Whether a dock in visibility mode @p mode reveals itself when the
    //! pointer reaches the screen edge. KWin owns that edge for the three
    //! Dodge modes and AutoHide. AlwaysVisible never hides; the cover modes
    //! use their stacking behavior instead of compositor auto-hide.
    static constexpr bool revealsOnScreenEdge(Latte::Types::Visibility mode)
    {
        return mode == Latte::Types::DodgeActive
                || mode == Latte::Types::DodgeMaximized
                || mode == Latte::Types::DodgeAllWindows
                || mode == Latte::Types::AutoHide;
    }

    explicit VisibilityManager(PlasmaQuick::ContainmentView *view);
    virtual ~VisibilityManager();

    Latte::Types::Visibility mode() const;
    void setMode(Latte::Types::Visibility mode);

    void applyActivitiesToHiddenWindows(const QStringList &activities);

    bool raiseOnDesktop() const;
    void setRaiseOnDesktop(bool enable);

    bool raiseOnActivity() const;
    void setRaiseOnActivity(bool enable);

    bool isBelowLayer() const;

    bool isHidden() const;
    void setIsHidden(bool isHidden);

    bool isShownFully() const;
    void setIsShownFully(bool fully);

    bool hidingIsBlocked() const;

    bool containsMouse() const;

    bool isFloatingGapWindowEnabled() const;
    void setIsFloatingGapWindowEnabled(bool enabled);

    int timerShow() const;
    void setTimerShow(int msec);

    int timerHide() const;
    void setTimerHide(int msec);

    bool isSidebar() const;
    bool hasBlockHidingEvent(const QString &type);

    //! KWin Edges Support functions
    bool enableKWinEdges() const;
    void setEnableKWinEdges(bool enable);

    bool supportsKWinEdges() const;
    [[nodiscard]] ScreenEdgeBackend screenEdgeBackend() const;
    [[nodiscard]] bool screenEdgeArmed() const;
    [[nodiscard]] bool screenEdgeRegistered() const;
    [[nodiscard]] bool compositorScreenEdgeSupported() const;

    //! Struts
    int strutsThickness() const;
    void setStrutsThickness(int thickness);

    //! the struts rectangle actually sent to the window manager; empty when
    //! none are published (mode without struts, hidden, or off-screen)
    QRect publishedStruts() const;

    //! Retire old coordinator membership before a placement transaction
    //! changes its LayerShell output or edge. Failure leaves the old placement
    //! and old reservation paired, so the model mutation can be cancelled.
    [[nodiscard]] bool beginPlacementTransaction(
        bool changesReservationOwnership);

    //! Publish the complete LayerShell placement before Positioner commits
    //! its relocation generation, remaps the surface, or begins reveal.
    [[nodiscard]] bool publishReservationAfterAppliedPlacement();

    //! A Plasma removal remains reversible for its Undo window. Retire every
    //! compositor-owned helper before the View leaves the active layout map,
    //! then restore reservation ownership before the canvas is remapped.
    [[nodiscard]] bool suspendForReversibleRemoval();
    [[nodiscard]] bool resumeFromReversibleRemoval();

    //! Used mostly to show / hide Sidebars
    void toggleHiddenState();

public Q_SLOTS:

    Q_INVOKABLE void setViewOnBackLayer();
    Q_INVOKABLE void setViewOnFrontLayer();

    Q_INVOKABLE void addBlockHidingEvent(const QString &type);
    Q_INVOKABLE void removeBlockHidingEvent(const QString &type);

    void initViewFlags();

Q_SIGNALS:
    void mustBeShown();
    void mustBeHide();

    void slideOutFinished();
    void slideInFinished();

    void frameExtentsCleared();
    void modeChanged();
    void raiseOnDesktopChanged();
    void raiseOnActivityChanged();
    void isBelowLayerChanged();
    void isFloatingGapWindowEnabledChanged();
    void isHiddenChanged();
    void isSidebarChanged();
    void isShownFullyChanged();
    void hidingIsBlockedChanged();
    void containsMouseChanged();
    void strutsThicknessChanged();
    void timerShowChanged();
    void timerHideChanged();

    //! KWin Edges Support signals
    void enableKWinEdgesChanged();
    void supportsKWinEdgesChanged();

private Q_SLOTS:
    void saveConfig();
    void restoreConfig();

    void setIsBelowLayer(bool below);

    void onHeadThicknessChanged();
    void onHidingIsBlockedChanged();
    void onIsFloatingGapWindowEnabledChanged();

    void publishFrameExtents(bool forceUpdate = false); //! direct

    //! KWin Edges Support functions
    void updateKWinEdgesSupport();

    void updateSidebarState();

private:
    enum class ReservationUpdateContext {
        Ordinary,
        AppliedPlacement,
    };

    void setContainsMouse(bool contains);

    void raiseView(bool raise);
    void raiseViewTemporarily();

    //! KWin Edges Support functions
    void createAutoHideScreenEdge();
    void createEdgeGhostWindow();
    void deleteAutoHideScreenEdge();
    void deleteEdgeGhostWindow();
    void updateKWinEdgeState();

    //! Floating Gap Support functions
    void createFloatingGapWindow();
    void deleteFloatingGapWindow();
    bool supportsFloatingGap() const;

    bool updateStrutsBasedOnLayoutsAndActivities(
        bool forceUpdate = false,
        ReservationUpdateContext context =
            ReservationUpdateContext::Ordinary);
    void viewEventManager(QEvent *ev);

    void checkMouseInFloatingArea();

    bool windowContainsMouse();

    [[nodiscard]] std::optional<QRect> acceptableStruts();

private Q_SLOTS:
    void dodgeAllWindows();
    void dodgeActive();
    void dodgeMaximized();
    void updateHiddenState();

    void updateStrutsAfterTimer();
    bool isValidMode() const;

private:
    void startTimerHide(const int &msec = 0);

    bool canSetStrut() const;

private:
    WindowSystem::AbstractWindowInterface *m_wm;
    Types::Visibility m_mode{Types::None};
    //! Capacity covers the largest visibility-mode connection set.
    std::array<QMetaObject::Connection, 8> m_connections;

    QTimer m_timerShow;
    QTimer m_timerHide;
    QTimer m_timerPublishFrameExtents;
    //! This timer is very important because it blocks how fast struts are updated.
    //! By using this timer we help the window manager in order to correspond to new
    //! struts (for example changing windows maximized state or geometry) without
    //! creating binding loops between the app and the window manager.
    //! That was reproducible in a floating panel when we were dragging the active window.
    QTimer m_timerBlockStrutsUpdate;

    bool m_isBelowLayer{false};
    bool m_isHidden{false};
    bool m_isFloatingGapWindowEnabled{false};
    bool m_isSidebar{false};
    bool m_isShownFully{false};
    bool m_dragEnter{false};
    bool m_containsMouse{false};
    bool m_raiseTemporarily{false};
    bool m_raiseOnDesktopChange{false};
    bool m_raiseOnActivityChange{false};
    bool m_hideNow{false};
    //! Reservation inputs can change while Positioner owns a compound
    //! placement transaction. Preserve both the dirty state and force intent
    //! so the complete applied boundary replays the normal publication path.
    bool m_reservationUpdateDirty{false};
    bool m_reservationForceUpdatePending{false};
    bool m_suspendedForRemoval{false};

    //! valid on demand sidebar hidden state in order to be checked after slide-ins/outs
    bool m_isRequestedShownSidebarOnDemand{false};

    int m_frameExtentsHeadThicknessGap{0};
    int m_timerHideInterval{700};
    Plasma::Types::Location m_frameExtentsLocation{Plasma::Types::BottomEdge};

    int m_strutsThickness{0};

    QStringList m_blockHidingEvents;

    ScreenSpaceReservationPublicationState m_reservationPublication;
    QRect m_lastMask;

    //! KWin Edges
    bool m_enableKWinEdgesFromUser{true};
    AutoHideScreenEdge *m_autoHideScreenEdge{nullptr};
    ScreenEdgeGhostWindow *m_edgeGhostWindow{nullptr};

    //! Floating Gap
    FloatingGapWindow *m_floatingGapWindow{nullptr};

    Latte::Corona *m_corona{nullptr};
    Latte::View *m_latteView{nullptr};

};

}
}
#endif // VISIBILITYMANAGER_H
