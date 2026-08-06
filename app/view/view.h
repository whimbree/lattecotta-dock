/*
    SPDX-FileCopyrightText: 2016 Smith AR <audoban@openmailbox.org>
    SPDX-FileCopyrightText: 2016 Michail Vourlakos <mvourlakos@gmail.com>
    SPDX-FileCopyrightText: 2026 Bree Spektor
    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef VIEW_H
#define VIEW_H

// local
#include <coretypes.h>
#include "containmentinterface.h"
#include "effects.h"
#include "floatingtransition.h"
#include "parabolic.h"
#include "positioner.h"
#include "eventssink.h"
#include "visibilitymanager.h"
#include "windowtouchtracker.h"
#include "indicator/indicator.h"
#include "settings/primaryconfigview.h"
#include "windowstracker/windowstracker.h"
#include "../data/viewdata.h"
#include "../shortcuts/globalshortcuts.h"
#include "../layout/genericlayout.h"
#include "../plasma/quick/containmentview.h"
#include "../plasma/quick/configview.h"

// C++
#include <array>

// Qt
#include <QQuickView>
#include <QMenu>
#include <QMetaObject>
#include <QMimeData>
#include <QScreen>
#include <QPointer>
#include <QTimer>


namespace Plasma {
class Types;
class Corona;
class Containment;
}

namespace PlasmaQuick {
class AppletQuickItem;
}

namespace LayerShellQt {
class Window;
}

namespace Latte {
class Corona;
class Interfaces;
class GenericLayout;
class OriginalView;
namespace ViewActionPolicy {
enum class Role;
}
}

namespace Latte {

class View : public PlasmaQuick::ContainmentView
{
    Q_OBJECT

    Q_PROPERTY(int groupId READ groupId NOTIFY groupIdChanged)
    Q_PROPERTY(Latte::Types::ViewType type READ type WRITE setType NOTIFY typeChanged)

    Q_PROPERTY(bool alternativesIsShown READ alternativesIsShown NOTIFY alternativesIsShownChanged)
    Q_PROPERTY(bool behaveAsPlasmaPanel READ behaveAsPlasmaPanel WRITE setBehaveAsPlasmaPanel NOTIFY behaveAsPlasmaPanelChanged)
    Q_PROPERTY(bool floatingGapConfigured READ floatingGapConfigured NOTIFY floatingGapConfiguredChanged)
    Q_PROPERTY(bool floatingPanelConfigured READ isFloatingPanel NOTIFY floatingPanelConfiguredChanged)
    Q_PROPERTY(bool byPassWM READ byPassWM WRITE setByPassWM NOTIFY byPassWMChanged)
    Q_PROPERTY(bool canRemove READ canRemove NOTIFY canRemoveChanged)
    Q_PROPERTY(bool containsDrag READ containsDrag NOTIFY containsDragChanged)
    Q_PROPERTY(bool inSettingsAdvancedMode READ inSettingsAdvancedMode NOTIFY inSettingsAdvancedModeChanged)

    Q_PROPERTY(bool inEditMode READ inEditMode NOTIFY inEditModeChanged)
    Q_PROPERTY(bool linkedEditHighlight READ linkedEditHighlight NOTIFY linkedEditHighlightChanged)
    Q_PROPERTY(bool isPreferredForShortcuts READ isPreferredForShortcuts WRITE setIsPreferredForShortcuts NOTIFY isPreferredForShortcutsChanged)
    Q_PROPERTY(bool keyboardNavigationIsActive READ keyboardNavigationIsActive NOTIFY keyboardNavigationIsActiveChanged)
    Q_PROPERTY(bool onPrimary READ onPrimary WRITE setOnPrimary NOTIFY onPrimaryChanged)
    Q_PROPERTY(bool screenEdgeMarginEnabled READ screenEdgeMarginEnabled WRITE setScreenEdgeMarginEnabled NOTIFY screenEdgeMarginEnabledChanged)

    //! values to be used from Smart surrounding Views
    Q_PROPERTY(bool isTouchingBottomViewAndIsBusy READ isTouchingBottomViewAndIsBusy WRITE setIsTouchingBottomViewAndIsBusy NOTIFY isTouchingBottomViewAndIsBusyChanged)
    Q_PROPERTY(bool isTouchingTopViewAndIsBusy READ isTouchingTopViewAndIsBusy WRITE setIsTouchingTopViewAndIsBusy NOTIFY isTouchingTopViewAndIsBusyChanged)

    Q_PROPERTY(int alignment READ alignment WRITE setAlignment NOTIFY alignmentChanged)
    Q_PROPERTY(int fontPixelSize READ fontPixelSize WRITE setFontPixelSize NOTIFY fontPixelSizeChanged)
    Q_PROPERTY(int x READ x NOTIFY xChanged)
    Q_PROPERTY(int y READ y NOTIFY yChanged)
    Q_PROPERTY(int width READ width NOTIFY widthChanged)
    Q_PROPERTY(int height READ height NOTIFY heightChanged)
    Q_PROPERTY(int editThickness READ editThickness NOTIFY editThicknessChanged)
    Q_PROPERTY(int maxThickness READ maxThickness WRITE setMaxThickness NOTIFY maxThicknessChanged)
    Q_PROPERTY(int normalThickness READ normalThickness WRITE setNormalThickness NOTIFY normalThicknessChanged)
    Q_PROPERTY(int maxNormalThickness READ maxNormalThickness WRITE setMaxNormalThickness NOTIFY maxNormalThicknessChanged)
    Q_PROPERTY(int headThicknessGap READ headThicknessGap WRITE setHeadThicknessGap NOTIFY headThicknessGapChanged)
    Q_PROPERTY(int screenEdgeMargin READ screenEdgeMargin WRITE setScreenEdgeMargin NOTIFY screenEdgeMarginChanged)

    Q_PROPERTY(float maxLength READ maxLength WRITE setMaxLength NOTIFY maxLengthChanged)
    Q_PROPERTY(float offset READ offset WRITE setOffset NOTIFY offsetChanged)

    Q_PROPERTY(QString name READ name NOTIFY nameChanged)

    Q_PROPERTY(QQuickItem *colorizer READ colorizer WRITE setColorizer NOTIFY colorizerChanged)
    Q_PROPERTY(QQuickItem *metrics READ metrics WRITE setMetrics NOTIFY metricsChanged)

    Q_PROPERTY(QVariantList containmentActions READ containmentActions NOTIFY containmentActionsChanged)

    Q_PROPERTY(Latte::Layout::GenericLayout *layout READ layout WRITE setLayout NOTIFY layoutChanged)
    Q_PROPERTY(Latte::ViewPart::Effects *effects READ effects NOTIFY effectsChanged)
    Q_PROPERTY(Latte::ViewPart::ContainmentInterface *extendedInterface READ extendedInterface NOTIFY extendedInterfaceChanged)
    Q_PROPERTY(Latte::ViewPart::FloatingTransition *floatingTransition
                   READ floatingTransition CONSTANT)
    Q_PROPERTY(Latte::ViewPart::Indicator *indicator READ indicator NOTIFY indicatorChanged)
    Q_PROPERTY(Latte::ViewPart::Parabolic *parabolic READ parabolic NOTIFY parabolicChanged)
    Q_PROPERTY(Latte::ViewPart::Positioner *positioner READ positioner NOTIFY positionerChanged)
    Q_PROPERTY(Latte::ViewPart::EventsSink *sink READ sink NOTIFY sinkChanged)
    Q_PROPERTY(Latte::ViewPart::VisibilityManager *visibility READ visibility NOTIFY visibilityChanged)
    Q_PROPERTY(Latte::ViewPart::WindowTouchTracker *windowTouchTracker
                   READ windowTouchTracker CONSTANT)
    Q_PROPERTY(Latte::ViewPart::WindowsTracker *windowsTracker READ windowsTracker NOTIFY windowsTrackerChanged)

    Q_PROPERTY(Latte::Interfaces *interfacesGraphicObj READ interfacesGraphicObj WRITE setInterfacesGraphicObj NOTIFY interfacesGraphicObjChanged)

    Q_PROPERTY(QRect absoluteGeometry READ absoluteGeometry NOTIFY absoluteGeometryChanged)
    Q_PROPERTY(QRect localGeometry READ localGeometry WRITE setLocalGeometry NOTIFY localGeometryChanged)
    Q_PROPERTY(QRect screenGeometry READ screenGeometry NOTIFY screenGeometryChanged)
    Q_PROPERTY(quint64 layerShellConfigureRequestRevision
                   READ layerShellConfigureRequestRevision
                   NOTIFY layerShellConfigureRequestRevisionChanged)

public:
    enum class OutputMove
    {
        ObservationOnly,
        OwnershipTransfer,
    };

    View(Plasma::Corona *corona, QScreen *targetScreen = nullptr, bool byPassX11WM = false);
    virtual ~View();

    void init(Plasma::Containment *plasma_containment = nullptr);

    //! must run after init() and before the first show(): LayerShellQt can
    //! only turn a not-yet-created surface into a layer surface
    void setupWaylandLayerShell();
    [[nodiscard]] const LayerShellQt::Window *layerShellWindow() const;
    //! Apply one complete Positioner solution to the explicitly assigned
    //! output. LayerShellQt is the synchronous output authority because
    //! QWindow::screen() may follow a compositor configure asynchronously.
    //! Success also requires the requested anchors, margins, and
    //! non-reserving visual policy.
    [[nodiscard]] bool layerShellNeedsOutput(
        const QScreen *screen) const;
    [[nodiscard]] bool applyPositionedLayerShellGeometry(
        QScreen *assignedScreen,
        const QRect &geometry);
    //! Remap a surface hidden by moveToScreen() only after reservation
    //! ownership has moved to the same applied output and edge.
    void showAppliedLayerShellPlacement();
    [[nodiscard]] bool publishScreenSpaceReservation(
        const QRect &strutGeometry,
        Plasma::Types::Location location);
    [[nodiscard]] bool clearScreenSpaceReservation();
    [[nodiscard]] bool suspendForReversibleRemoval();
    [[nodiscard]] bool resumeFromReversibleRemoval();

    Types::ViewType type() const;
    void setType(Types::ViewType type);

    bool alternativesIsShown() const;
    void setAlternativesIsShown(bool show);

    bool inDelete() const;
    bool inReadyState() const;

    bool onPrimary() const;
    void setOnPrimary(bool flag);

    bool behaveAsPlasmaPanel() const;
    void setBehaveAsPlasmaPanel(bool behavior);

    bool containsDrag() const;
    bool containsMouse() const;

    [[nodiscard]] virtual bool canRemove() const;
    [[nodiscard]] virtual bool canMoveToLayout() const;

    bool byPassWM() const;
    void setByPassWM(bool bypass);

    bool inEditMode() const;
    [[nodiscard]] bool linkedEditHighlight() const;

    [[nodiscard]] bool floatingGapConfigured() const;
    [[nodiscard]] bool isFloatingPanel() const;

    virtual bool isPreferredForShortcuts() const;
    void setIsPreferredForShortcuts(bool preferred);

    //! keyboard navigation mode: the window temporarily takes keyboard
    //! focus (layer-shell OnDemand) so the containment QML can traverse
    //! and activate dock items from the keyboard. Continuation feature -
    //! upstream Qt5 Latte had no such mode; with the mode off the window
    //! stays focus-refusing exactly as before.
    bool keyboardNavigationIsActive() const;
    [[nodiscard]] bool containmentAcceptsInput() const;
    [[nodiscard]] bool ownsPanelFocusSession() const;
    Q_INVOKABLE void enterKeyboardNavigation();
    Q_INVOKABLE void exitKeyboardNavigation();
    Q_INVOKABLE void toggleKeyboardNavigation();

    bool inSettingsAdvancedMode() const;

    bool isTouchingBottomViewAndIsBusy() const;
    void setIsTouchingBottomViewAndIsBusy(bool touchAndBusy);

    bool isTouchingTopViewAndIsBusy() const;
    void setIsTouchingTopViewAndIsBusy(bool touchAndBusy);

    bool screenEdgeMarginEnabled() const;
    void setScreenEdgeMarginEnabled(bool enabled); 

    virtual int groupId() const;

    int fontPixelSize() const;
    void setFontPixelSize(int size);

    int editThickness() const;

    int maxThickness() const;
    void setMaxThickness(int thickness);

    int normalThickness() const;
    void setNormalThickness(int thickness);

    int maxNormalThickness() const;
    void setMaxNormalThickness(int thickness);

    int headThicknessGap() const;
    void setHeadThicknessGap(int thickness);

    int screenEdgeMargin() const;
    void setScreenEdgeMargin(int margin);

    int alignment() const;
    void setAlignment(int alignment);

    float maxLength() const;
    void setMaxLength(float length);

    float offset() const;
    void setOffset(float offset);

    QRect absoluteGeometry() const;
    QRect screenGeometry() const;

    QRect localGeometry() const;
    void setLocalGeometry(const QRect &geometry);
    [[nodiscard]] quint64 layerShellConfigureRequestRevision() const;

    QString validTitle() const;

    QString name() const;
    void setName(const QString &newname);

    bool isOnActivity(const QString &activity) const;
    bool isOnAllActivities() const;

    QStringList activities() const;
    void setActivities(const QStringList &ids);

    bool settingsWindowIsShown() const;
    void showSettingsWindow();

    //! open this view's widget explorer, the exact "Add Widgets..." context
    //! menu action (Menu::requestWidgetExplorer -> showAddWidgetsInterface ->
    //! the showWidgetExplorer slot below). The public coarse entry the D-Bus
    //! showWidgetExplorer action drives so the e2e DND driver has a real drag
    //! source surface (the explorer's AppletDelegate DragArea) to press on.
    void openWidgetExplorer();

    QQuickItem *colorizer() const;
    void setColorizer(QQuickItem *colorizer);

    QQuickItem *metrics() const;
    void setMetrics(QQuickItem *metrics);

    virtual bool isCloned() const = 0; //means that this view is a clone of an original view
    virtual bool isOriginal() const = 0; //means that this view is an original view that can be autocloned to other screens
    virtual bool isSingle() const = 0; //means that this view is not related to clones and screen groups in any way
    [[nodiscard]] bool ownsOutputPlacement() const;
    virtual Latte::Types::ScreensGroup screensGroup() const = 0;
    virtual Latte::Data::View::LinkPlacement linkPlacement() const = 0;
    ViewActionPolicy::Role actionRole() const;

    //! The runtime view that owns this view's shared edit session. Original
    //! views own themselves; screen-group clones resolve to their original.
    virtual Latte::View *configurationTargetView();

    //! The persistent root for applet synchronization. Unlike
    //! configurationTargetView(), explicit linked members resolve to the root
    //! here while keeping their own edit presentation.
    virtual Latte::View *relationshipRootView();

    //! Relationship-aware structural applet mutations. Applet instances stay
    //! containment-local, while linked views route the transaction through
    //! their direct root so every member observes one authoritative change.
    Q_INVOKABLE virtual bool addApplet(const QString &pluginId);
    Q_INVOKABLE virtual bool removeApplet(int appletId);
    Q_INVOKABLE virtual void synchronizeDroppedApplet(QObject *mimeData, int x, int y);

    QVariantList containmentActions() const;

    QQuickView *configView() const;

    virtual Latte::Data::View data() const;

    ViewPart::Effects *effects() const;   
    ViewPart::FloatingTransition *floatingTransition() const;
    ViewPart::ContainmentInterface *extendedInterface() const;
    virtual ViewPart::Indicator *indicator() const;
    ViewPart::Parabolic *parabolic() const;
    ViewPart::Positioner *positioner() const;
    ViewPart::EventsSink *sink() const;
    ViewPart::VisibilityManager *visibility() const;
    ViewPart::WindowTouchTracker *windowTouchTracker() const;
    ViewPart::WindowsTracker *windowsTracker() const;

    Latte::Interfaces *interfacesGraphicObj() const;
    void setInterfacesGraphicObj(Latte::Interfaces *ifaces);

    Layout::GenericLayout *layout() const;
    void setLayout(Layout::GenericLayout *layout);


    //! release grab and restore mouse state
    void unblockMouse(int x, int y);

    virtual void reconsiderScreen();

    //! Move the view window to @p nextScreen. An ownership transfer remaps
    //! even when Qt has already observed the destination because Latte or
    //! LayerShell can still own the source output.
    void moveToScreen(
        QScreen *nextScreen,
        OutputMove outputMove);

    //! these are signals that create crashes, such a example is the availableScreenRectChanged from corona
    //! when its containment is destroyed
    void disconnectSensitiveSignals();

    //! used from ViewSettingsFactory in order to move Configuration Windows to different View
    void releaseConfigView();

public Q_SLOTS:
    Q_INVOKABLE void newView(const QString &templateFile);
    Q_INVOKABLE void removeView();
    Q_INVOKABLE void duplicateView();
    Q_INVOKABLE void createLinkedViewOnScreen(const QString &screenName, int edge);
    Q_INVOKABLE void exportTemplate();

    void createLinkedView(int targetScreenId, Plasma::Types::Location targetEdge);
    void reloadRuntimeView();

    Q_INVOKABLE bool mimeContainsPlasmoid(QMimeData *mimeData, QString name);

    void updateAbsoluteGeometry(bool bypassChecks = false);

    Q_INVOKABLE bool isHighestPriorityView();
    Q_INVOKABLE QAction *action(const QString &name);

protected Q_SLOTS:
    void showConfigurationInterface(Plasma::Applet *applet) override;
    void showWidgetExplorer(const QPointF &point);

protected:
    bool event(QEvent *ev) override;

Q_SIGNALS:
    void eventTriggered(QEvent *ev);
    void mousePressed(const QPoint pos, const int button);
    void mouseReleased(const QPoint pos, const int button);
    void wheelScrolled(const QPoint pos, const QPoint angleDelta, const int buttons);

    void activitiesChanged();
    void alternativesIsShownChanged();
    void alignmentChanged();
    void behaveAsPlasmaPanelChanged();
    void byPassWMChanged();
    void canRemoveChanged();
    void colorizerChanged();
    void configWindowGeometryChanged(); // is called from config windows
    void containmentActionsChanged();
    void containsDragChanged();
    void dockLocationChanged();
    void editThicknessChanged();
    void effectsChanged();
    void extendedInterfaceChanged();
    void fontPixelSizeChanged();
    void floatingGapConfiguredChanged();
    void floatingPanelConfiguredChanged();
    void forcedShown(); //[workaround] forced shown to avoid a KWin issue that hides windows when closing activities
    void geometryChanged();
    void groupIdChanged();
    void widthChanged();
    void headThicknessGapChanged();
    void heightChanged();
    void inEditModeChanged();
    void indicatorChanged();
    void inSettingsAdvancedModeChanged();
    void interfacesGraphicObjChanged();
    void isPreferredForShortcutsChanged();
    void keyboardNavigationIsActiveChanged();
    void linkedEditHighlightChanged();
    void isTouchingBottomViewAndIsBusyChanged();
    void isTouchingTopViewAndIsBusyChanged();
    void layoutChanged();
    void localGeometryChanged();
    void maxLengthChanged();
    void maxThicknessChanged();
    void metricsChanged();
    void normalThicknessChanged();
    void maxNormalThicknessChanged();
    void nameChanged();
    void offsetChanged();
    void onPrimaryChanged();
    void parabolicChanged();
    void positionerChanged();
    void screenEdgeMarginChanged();
    void screenEdgeMarginEnabledChanged();
    void screenGeometryChanged();

    void sinkChanged();
    void typeChanged();
    void visibilityChanged();
    void windowsTrackerChanged();
    void xChanged();
    void yChanged();

    void absoluteGeometryChanged(const QRect &geometry);
    void layerShellConfigureRequestRevisionChanged();

    void indicatorPluginChanged(const QString &indicatorId);
    void indicatorPluginRemoved(const QString &indicatorId);
    void userRequestedViewType(const int &type);

    //! are used to trigger the Corona relevant signals and in that
    //! way we can disable any such signaling all together, e.g. through disconnectSensitiveSignals()
    void availableScreenRectChangedFrom(Latte::View *origin);
    void availableScreenRegionChangedFrom(Latte::View *origin);

protected:
    QPointer<Latte::Corona> m_corona;

private Q_SLOTS:
    void applyActivitiesToWindows();
    void availableScreenRectChangedFromSlot(View *origin);
    void hideWindowsForSlidingOut();
    void preferredViewForShortcutsChangedSlot(Latte::View *view);
    void releaseGrab();
    void updateTransientWindowsTracking();
    void statusChanged(Plasma::Types::ItemStatus);

    void addTransientWindow(QWindow *window);
    void removeTransientWindow(const bool &visible);

    //! workaround in order for top panels to be always on top
    void topViewAlwaysOnTop();
    void verticalUnityViewHasFocus();

    //!workaround for when kwin hides view when an activity is closing
    void showHiddenViewFromActivityStopping();

    void restoreConfig();
    void saveConfig();

private:
    enum class PanelFocusSessionDisposition
    {
        RestoreApplication,
        KeepCurrentApplication,
    };

    enum class TemplateImportRelationship
    {
        Preserve,
        IndependentSnapshot,
    };

    void createViewFromTemplate(const QString &templateFile, TemplateImportRelationship relationship);
    void initSignalingForLocationChangeSliding();
    void reanchorLayerShell();
    //! true when this view's window covers the whole screen along its length
    //! axis (a horizontal masked dock: PositionerGeometry::windowSize gives it
    //! the full screen width). The layer-shell anchoring anchors both length
    //! edges in that case so the compositor never re-centres it (see
    //! LayerShell::anchorsFor). Vertical masked docks are sized to the available
    //! region, not the screen, so they do not qualify.
    bool windowSpansScreenLength() const;
    //! The stable canvas remains anchored flush with its output edge.
    //! Floating presentation is internal, so this is always zero.
    int layerShellEdgeMargin() const;
    void updateAppletContainsMethod();
    void updateWindowTouchTriggerGeometry();

    //! apply the window's keyboard-focus stance (Qt flags + layer-shell
    //! keyboard interactivity) computed from the containment status and
    //! the keyboard-navigation mode; the single writer for both knobs
    void applyKeyboardFocusPolicy(bool takesFocus);
    [[nodiscard]] bool panelFocusIsRequested() const;
    [[nodiscard]] bool ensurePanelFocusSessionStarted();
    void endPanelFocusSession(PanelFocusSessionDisposition disposition);
    void relinquishPanelFocusSessionOnRemoval();
    void applyPanelFocusPolicy();
    void cancelPanelFocusSessionOnFocusLoss();
    void leaveKeyboardNavigation();

    void setContainsDrag(bool contains);
    void setLinkedEditHighlight(bool highlighted);
    void updateLinkedEditHighlightVisibilityBlocker();

private:
    Plasma::Containment *containmentById(uint id);

    bool m_alternativesIsShown{false};
    bool m_behaveAsPlasmaPanel{false};
    bool m_byPassWM{true};
    bool m_containsDrag{false};
    bool m_containsMouse{false};
    bool m_inDelete{false};
    bool m_isPreferredForShortcuts{false};
    bool m_keyboardNavigationIsActive{false};
    bool m_containmentAcceptsInput{false};
    bool m_ownsPanelFocusSession{false};
    bool m_linkedEditHighlight{false};
    quint64 m_layerShellConfigureRequestRevision{0};
    bool m_onPrimary{true};
    bool m_screenEdgeMarginEnabled{false};

    bool m_isTouchingBottomViewAndIsBusy{false};
    bool m_isTouchingTopViewAndIsBusy{false};

    bool m_layerShellConfigured{false};
    //! A visible mapped surface stays hidden between QWindow retarget and the
    //! complete LayerShell placement. It is remapped only after output,
    //! anchors, margins, and geometry pass their joint postcondition.
    bool m_showAfterLayerShellPlacement{false};
    bool m_suspendedForRemoval{false};
    //! Non-owning. LayerShellQt parents the attached state to this QWindow.
    LayerShellQt::Window *m_layerShellWindow{nullptr};

    int m_fontPixelSize{ -1};
    int m_maxThickness{256};
    int m_normalThickness{256};
    int m_maxNormalThickness{256};
    int m_headThicknessGap{0};
    int m_screenEdgeMargin{-1};
    float m_maxLength{1};
    float m_offset{0};

    Types::Alignment m_alignment{Types::Center};
    Types::ViewType m_type{Types::DockView};

    QRect m_localGeometry;
    QRect m_absoluteGeometry;

    QString m_name;

    QStringList m_activities;

    //! HACK: In order to avoid crashes when the View is added and removed
    //! immediately during startup
    QTimer m_initLayoutTimer;

    //! HACK: Timers in order to handle KWin faulty
    //! behavior that hides Views when closing Activities
    //! with no actual reason
    QTimer m_visibleHackTimer1;
    QTimer m_visibleHackTimer2;

    QTimer m_releaseGrabTimer;
    int m_releaseGrab_x;
    int m_releaseGrab_y;
    bool m_floatingEventProjectionPending{false};

    Layout::GenericLayout *m_layout{nullptr};

    QQuickItem *m_colorizer{nullptr};
    QQuickItem *m_metrics{nullptr};

    QPointer<PlasmaQuick::ConfigView> m_appletConfigView;
    QPointer<ViewPart::PrimaryConfigView> m_primaryConfigView;

    QPointer<ViewPart::Effects> m_effects;
    QPointer<ViewPart::FloatingTransition> m_floatingTransition;
    QPointer<ViewPart::WindowTouchTracker> m_windowTouchTracker;
    QPointer<ViewPart::Indicator> m_indicator;
    QPointer<ViewPart::ContainmentInterface> m_interface;
    QPointer<ViewPart::Parabolic> m_parabolic;
    QPointer<ViewPart::Positioner> m_positioner;
    QPointer<ViewPart::EventsSink> m_sink;
    QPointer<ViewPart::VisibilityManager> m_visibility;
    QPointer<ViewPart::WindowsTracker> m_windowsTracker;

    QPointer<Latte::Interfaces> m_interfacesGraphicObj;

    //! Connections to release and bound for the assigned layout
    QList<QMetaObject::Connection> connectionsLayout;

    //! track transientWindows
    QList<QWindow *> m_transientWindows;

    friend class Latte::OriginalView;

};

}

#endif
