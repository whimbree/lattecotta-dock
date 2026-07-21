/*
    SPDX-FileCopyrightText: 2016 Smith AR <audoban@openmailbox.org>
    SPDX-FileCopyrightText: 2016 Michail Vourlakos <mvourlakos@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef LATTECORONA_H
#define LATTECORONA_H

// local
#include <coretypes.h>
#include "plasma/quick/configview.h"
#include "layouts/storage.h"
#include "view/panelshadows_p.h"

// Qt
#include <QObject>
#include <QTimer>

// Plasma
#include <Plasma/Corona>

// KDE
#include <KAboutApplicationDialog>

namespace PlasmaQuick {
class SharedQmlEngine;
}

namespace Plasma {
class Corona;
class Containment;
class Types;
}

namespace PlasmaQuick {
class ConfigView;
}

namespace KActivities {
class Consumer;
}

namespace KWayland {
namespace Client {
class PlasmaShell;
}
}

namespace Latte {
class CentralLayout;
class ScreenPool;
class GlobalShortcuts;
class SettingsControlRegistry;
class UniversalSettings;
class View;
class ViewSettingsFactory;
namespace Indicator{
class Factory;
}
namespace Layout{
class GenericLayout;
}
namespace Layouts{
class Manager;
}
namespace PlasmaExtended{
class ScreenGeometries;
class ScreenPool;
class Theme;
}
namespace Templates {
class Manager;
}
namespace WindowSystem{
class AbstractWindowInterface;
}
}

namespace Latte {

class Corona : public Plasma::Corona
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.LatteDock")

public:
    Corona(bool defaultLayoutOnStartup = false,
               QString layoutNameOnStartUp = QString(),
               QString addViewTemplateName = QString(),
               int userSetMemoryUsage = -1,
               QObject *parent = nullptr);
    virtual ~Corona();

    //! the coarse application lifecycle, forward-only. One enum instead of
    //! independent inStartup/inQuit bools so contradictory states
    //! (starting AND quitting) are unrepresentable.
    enum class LifecyclePhase {
        Startup,       //!< constructing and loading layouts
        Running,       //!< layouts loaded, views up
        QuitRequested, //!< a deliberate quit path has started
        Unloaded       //!< containments/layouts unloaded, only deletion remains
    };

    bool inQuit() const;

    //! D-Bus state readback (observability-first): the current lifecycle
    //! phase as a string, pull-queryable by tests and probes instead of
    //! scraping the quit-reason trail in the logs
    QString lifecycleState() const;

    //! plasmashell ShellCorona API shim: plasma-desktop's folder-view
    //! containment binds its isUiReady to corona.isScreenUiReady(screen)
    //! (desktopcontainment main.qml:63). Without the method the binding
    //! throws and stays undefined-falsy FOREVER, so a folder applet
    //! hosted in a dock treats its screen as never ready. plasmashell's
    //! semantics are "desktop view visible, no panels still loading";
    //! the honest equivalent here is "the corona finished loading
    //! layouts" - per-screen granularity has no Latte meaning since
    //! docks are not desktop views.
    Q_INVOKABLE bool isScreenUiReady(int screen) const;

    int numScreens() const override;
    QRect screenGeometry(int id) const override;
    QRegion availableScreenRegion(int id) const override;
    QRect availableScreenRect(int id) const override;

    //! This is a very generic function in order to return the availableScreenRect of specific screen
    //! by calculating only the user specified visibility modes and edges. Empty QLists for both
    //! arguments mean that all choices are accepted in calculations. ignoreExternalPanels means that
    //! external panels should be not considered in the calculations
    QRect availableScreenRectWithCriteria(int id,
                                          QString activityid = QString(),
                                          QList<Types::Visibility> ignoreModes = QList<Types::Visibility>(),
                                          QList<Plasma::Types::Location> ignoreEdges = QList<Plasma::Types::Location>(),
                                          bool ignoreExternalPanels = true,
                                          bool desktopUse = false) const;

    QRegion availableScreenRegionWithCriteria(int id,
                                              QString activityid = QString(),
                                              QList<Types::Visibility> ignoreModes = QList<Types::Visibility>(),
                                              QList<Plasma::Types::Location> ignoreEdges = QList<Plasma::Types::Location>(),
                                              bool ignoreExternalPanels = true,
                                              bool desktopUse = false) const;

    int screenForContainment(const Plasma::Containment *containment) const override;

    //! forwards a view's screen to applet-created transient windows the
    //! moment they show (see eventFilter implementation notes)
    bool eventFilter(QObject *watched, QEvent *event) override;

    KWayland::Client::PlasmaShell *waylandCoronaInterface() const;

    KActivities::Consumer *activitiesConsumer() const;
    GlobalShortcuts *globalShortcuts() const;
    ScreenPool *screenPool() const;
    //! Internal registration surface for settings components as their own
    //! SC-O1 (the read-only settings-control D-Bus registry) follow-up units
    //! land. SC-O1 itself registers fixture controls only.
    SettingsControlRegistry *settingsControlRegistry() const;
    UniversalSettings *universalSettings() const;
    ViewSettingsFactory *viewSettingsFactory() const;
    Layouts::Manager *layoutsManager() const;   
    Templates::Manager *templatesManager() const;

    Indicator::Factory *indicatorFactory() const;

    PlasmaExtended::ScreenPool *plasmaScreenPool() const;
    PlasmaExtended::Theme *themeExtended() const;

    WindowSystem::AbstractWindowInterface *wm() const;

    PanelShadows *dialogShadows() const;

    //! Needs to be called in order to import and load application properly after application
    //! finished all its exit operations
    void importFullConfiguration(const QString &file);

    //! these functions are used from context menu through containmentactions    
    void quitApplication();
    void switchToLayout(QString layout);
    void importLayoutFile(const QString &filepath, const QString &suggestedLayoutName = QString());
    void showSettingsWindow(int page);

    QStringList contextMenuData(const uint &containmentId);
    QStringList viewTemplatesData();

    //! D-Bus state readback (observability-first): a view's applet ids in
    //! layout order, so order assertions (clone sync, drag reorder) are
    //! pull-queryable instead of screenshot-compared. Empty means no view
    //! exists for that containment id (warned in the log).
    QStringList viewAppletsOrder(const uint &containmentId);

    //! D-Bus state readback (observability-first): the live drop-marker
    //! (dndSpacer) visual insert index for one view - >=0 while a drag hovers
    //! the view, -1 when no marker is live. The G3 readback
    //! (docs/tracking/e2e-interaction-test-plan.md): the direct insert(-1)
    //! observability that lets an add/reorder abort assert no orphan
    //! placeholder was stranded. -1 also for a bad containment id (warned).
    int viewDropMarkerIndex(const uint &containmentId);

    //! D-Bus state readback (observability-first): every current view's
    //! placement/visibility/geometry facts as one compact JSON array keyed
    //! on containment id, documented in docs/reference/dbus-observability-interface.md.
    //! The serialization lives in dbusreports.h; this stays a delegation.
    QString viewsData();

    //! D-Bus state readback (observability-first): one view's applets as a
    //! compact JSON array (id, plugin, index, geometry, expanded and
    //! destruction/lockedZoom/coloring flags), documented in
    //! docs/reference/dbus-observability-interface.md. "[]" means no view exists for
    //! that containment id (warned in the log).
    QString viewAppletsData(const uint &containmentId);

    //! D-Bus state readback (observability-first): one view's
    //! windows-tracker facts (touching/maximized/active windows and the
    //! last active window's identity) as one compact JSON object,
    //! documented in docs/reference/dbus-observability-interface.md. "{}" means no
    //! view exists for that containment id (warned in the log).
    QString trackerData(const uint &containmentId);

    //! D-Bus state readback (observability-first): one view's latte-tasks
    //! items (identity by appId/launcherUrl, never window titles) as a
    //! compact JSON array, documented in
    //! docs/reference/dbus-observability-interface.md. "[]" means no view exists for
    //! that containment id (warned) or the view hosts no tasks plasmoid
    //! (legitimate state, not warned).
    QString viewTasksData(const uint &containmentId);

    //! Read-only D-Bus state: live registered settings controls for one view as
    //! compact JSON. Unknown views warn and return []; a known view with no
    //! registered controls returns [] quietly. No setter or execution path.
    QString viewSettingsControlsData(const uint &containmentId);

    //! Read-only D-Bus state: the newest task-icon middle-click dispatch for
    //! one view as compact JSON. The record carries only stable application
    //! identity, launcher/task row kind, configured action, selected operation,
    //! and a process-monotonic sequence. "{}" means no event has been recorded
    //! or the containment id has no view (the latter is warned).
    QString taskMiddleClickDispatchData(const uint &containmentId);

    //! D-Bus state readback (observability-first): one view's colorizer
    //! facts (configured color modes, the decision in force, the measured
    //! background bucket, the published scheme basename) as one compact
    //! JSON object, documented in docs/reference/dbus-observability-interface.md.
    //! "{}" means no view exists for that containment id (warned in the
    //! log).
    QString colorizerData(const uint &containmentId);

    //! D-Bus state readback (observability-first): one view's containment
    //! config VALUES (the settings-panel keys - length/appearance/behavior/
    //! effects) read from the in-process KConfigPropertyMap, plus the live
    //! C++-property half (byPassWM, visibility timers/flags, indicator type)
    //! the edit-mode audit's P3 leg reads, as one compact JSON object
    //! documented in docs/reference/dbus-observability-interface.md. The in-process
    //! read sidesteps the KConfig default-deletion trap (a key at its default
    //! reads as its default, not "missing"). Coarse read of the user's own
    //! dock config (no execution, no secrets). "{}" means no view exists for
    //! that containment id (warned in the log).
    QString viewConfigData(const uint &containmentId);

    //! D-Bus state readback (observability-first): one child applet's config
    //! VALUES (keyed by containment id, applet id, plugin) read from the
    //! applet's in-process KConfigPropertyMap, as one compact JSON object
    //! documented in docs/reference/dbus-observability-interface.md. This is the
    //! D10-class surface the Tasks-page audit diffs to prove whether
    //! tasks.plasmoid.configuration.* writes land anywhere. "{}" means no view
    //! for that containment id, or no applet with that id on the view (both
    //! warned in the log).
    QString appletConfigData(const uint &containmentId, const uint &appletId);

    //! D-Bus state readback (observability-first): every loaded layout
    //! (name, active state, applied activities, view count) plus the
    //! layouts memory mode as one compact JSON object, documented in
    //! docs/reference/dbus-observability-interface.md. Complements switchToLayout.
    QString layoutsData();

    //! D-Bus state readback (observability-first): the ScreenPool's
    //! id<->connector mapping - every known output's Latte screen id,
    //! connector name, geometry, connected state and whether it is the
    //! primary - as one compact JSON array, documented in
    //! docs/reference/dbus-observability-interface.md. This is the queryable
    //! screen<->output topology the multi-output e2e vehicle pins a
    //! per-screen view assignment against; the serialization lives in
    //! dbusreports.h, this stays a delegation.
    QString screensData();

public Q_SLOTS:
    void aboutApplication();
    void activateLauncherMenu();
    void loadDefaultLayout() override;

    void setAutostart(const bool &enabled);

    void addView(const uint &containmentId, const QString &templateId);

    //! D-Bus coarse action (docs/reference/dbus-observability-interface.md): add an
    //! installed plasmoid to a view by plugin id, the deterministic sibling
    //! of a widget-explorer drop. Qt5-faithful end-append. A bad containment
    //! id (no such view) or a plugin id that names no installed plasmoid is
    //! refused loudly with NO applet created (reads-never-construct extended
    //! to this mutator). Readback: viewAppletsData/viewAppletsOrder.
    void addApplet(const uint &containmentId, const QString &pluginId);

    //! D-Bus coarse action (docs/reference/dbus-observability-interface.md): remove an
    //! applet from a view by its instance id, the same "Remove this Widget"
    //! the applet context menu triggers (Applet::destroy()). It rides the
    //! libplasma UNDO WINDOW: the applet lingers with inScheduledDestruction
    //! true until the notification closes or the ~60s fallback fires (the
    //! removeView undo trap). A bad containment id (no such view) or an
    //! applet id that names no applet on the view is refused loudly with NO
    //! applet removed. Readback: viewAppletsData's inScheduledDestruction
    //! flips, and the applet leaves viewAppletsData/viewAppletsOrder once the
    //! undo window ends.
    void removeApplet(const uint &containmentId, const uint &appletId);

    //! D-Bus coarse action (docs/reference/dbus-observability-interface.md): open a
    //! view's widget explorer, the exact "Add Widgets..." context menu action.
    //! It exists as the DRIVING SURFACE for the e2e explorer->containment DND
    //! driver (C-I9/P8): the explorer window carries the AppletDelegate
    //! DragArea that offers the text/x-plasmoidservicename mime, the real drag
    //! source a headless fakepointer press-glide-release drives onto the dock.
    //! A bad containment id (no such view) is refused loudly with no window
    //! shown (reads-never-construct extended to this mutator). No readback of
    //! its own: the explorer window shows up in the compositor dump
    //! (e2e_dumpwins), and a completed drop is observed through
    //! viewAppletsData/viewDropMarkerIndex.
    void showWidgetExplorer(const uint &containmentId);

    void duplicateView(const uint &containmentId);
    void exportViewTemplate(const uint &containmentId);
    void moveViewToLayout(const uint &containmentId, const QString &layoutName);
    void removeView(const uint &containmentId);

    //! D-Bus coarse action (docs/reference/dbus-observability-interface.md): enter or
    //! leave a view's edit mode through the same paths the context menu's
    //! Edit Dock... entry and the settings chrome's close button use
    void setViewEditMode(const uint &containmentId, const bool &editing);

    //! D-Bus coarse action (docs/reference/dbus-observability-interface.md): activate
    //! a view's entry at the given 1-based visual index through the exact
    //! Meta+<index> global-shortcut path
    void activateTaskAt(const uint &containmentId, const int &index);

    //! D-Bus coarse action (docs/reference/dbus-observability-interface.md): set a
    //! view's visibility mode by its viewsData() name, exactly what the
    //! settings Behavior combo does; unknown names are refused loudly
    void setViewVisibilityMode(const uint &containmentId, const QString &mode);

    //! D-Bus coarse action (docs/reference/dbus-observability-interface.md): enter or
    //! leave a view's keyboard-navigation mode through the same
    //! View::enter/exitKeyboardNavigation paths the Meta+Alt+D global
    //! shortcut drives; readback is viewsData()'s keyboardNavigation field
    void setViewKeyboardNavigation(const uint &containmentId, const bool &navigating);

    //! D-Bus coarse action (docs/reference/dbus-observability-interface.md): enter or
    //! leave a view's applet-rearrange (configure-applets) sub-mode, the same
    //! global toggle the settings-window header's rearrange button flips
    //! (HeaderSettings.qml). It is the driving surface for the applet-reorder
    //! e2e family (C-I7/P6): the ConfigOverlay drag machinery is only live
    //! when a view is BOTH in edit mode and this sub-mode, and the sub-mode is
    //! transient (never persisted), so a config seed cannot reach it. Entering
    //! requires the view already in edit mode and is refused loudly otherwise;
    //! readback is viewsData()'s inConfigureAppletsMode field.
    void setViewConfiguringApplets(const uint &containmentId, const bool &configuring);

    void setBackgroundFromBroadcast(QString activity, QString screenName, QString filename);
    void setBroadcastedBackgroundsEnabled(QString activity, QString screenName, bool enabled);
    void showAlternativesForApplet(Plasma::Applet *applet);
    void toggleHiddenState(QString layoutName, QString viewName, QString screenName, int screenEdge);

    //! values are separated with a "-" character
    void windowColorScheme(QString windowIdAndScheme);
    void updateDockItemBadge(QString identifier, QString value);

    void unload();

    //! Plasma 6's availableScreenRect/RegionChanged signals carry the affected
    //! screen id; a views-count change can affect any screen, so broadcast for
    //! every known one
    void notifyAvailableScreenGeometriesChanged();

Q_SIGNALS:
    void configurationShown(PlasmaQuick::ConfigView *configView);
    void viewLocationChanged();
    void raiseViewsTemporaryChanged();
    void availableScreenRectChangedFrom(Latte::View *origin);
    void availableScreenRegionChangedFrom(Latte::View *origin);
    void verticalUnityViewHasFocus();

private Q_SLOTS:
    void alternativesVisibilityChanged(bool visible);
    void load();

    void onAvailableScreenRectChangedFrom(Latte::View *origin);
    void onAvailableScreenRegionChangedFrom(Latte::View *origin);

    void onAboutToQuit();

    void onScreenAdded(QScreen *screen);
    void onScreenRemoved(QScreen *screen);
    void onScreenCountChanged();
    void onScreenGeometryChanged(const QRect &geometry);
    void syncLatteViewsToScreens();

private:
    void setLifecyclePhase(LifecyclePhase phase);

    void cleanConfig();
    void qmlRegisterTypes() const;
    void setupWaylandIntegration();

    bool appletExists(uint containmentId, uint appletId) const;
    bool containmentExists(uint id) const;

    int primaryScreenId() const;

    QStringList containmentsIds();
    QStringList appletsIds();

    Layout::GenericLayout *layout(QString name) const;
    CentralLayout *centralLayout(QString name) const;

private:

    bool m_activitiesStarting{true};
    bool m_defaultLayoutOnStartup{false}; //! this is used to enforce loading the default layout on startup

    LifecyclePhase m_lifecyclePhase{LifecyclePhase::Startup};

    //!it can be used on startup to change memory usage from command line
    int m_userSetMemoryUsage{ -1};

    QString m_layoutNameOnStartUp;
    QString m_startupAddViewTemplateName;
    QString m_importFullConfigurationFile;

    QList<PlasmaQuick::SharedQmlEngine *> m_alternativesObjects;

    QTimer m_viewsScreenSyncTimer;

    KActivities::Consumer *m_activitiesConsumer;
    QPointer<KAboutApplicationDialog> aboutDialog;

    ScreenPool *m_screenPool{nullptr};
    //! Outlives settings factories and layouts so their QObject destruction
    //! synchronously retires every generation before the registry is deleted.
    SettingsControlRegistry *m_settingsControlRegistry{nullptr};
    UniversalSettings *m_universalSettings{nullptr};
    ViewSettingsFactory *m_viewSettingsFactory{nullptr};
    GlobalShortcuts *m_globalShortcuts{nullptr};

    Indicator::Factory *m_indicatorFactory{nullptr};
    Layouts::Manager *m_layoutsManager{nullptr};
    Templates::Manager *m_templatesManager{nullptr};

    PlasmaExtended::ScreenGeometries *m_plasmaGeometries{nullptr};
    PlasmaExtended::ScreenPool *m_plasmaScreenPool{nullptr};
    PlasmaExtended::Theme *m_themeExtended{nullptr};

    WindowSystem::AbstractWindowInterface *m_wm{nullptr};

    PanelShadows *m_dialogShadows{nullptr};

    KWayland::Client::PlasmaShell *m_waylandCorona{nullptr};

    friend class GlobalShortcuts;
    friend class Layouts::Manager;
    friend class Layouts::Storage;
};

}

#endif // LATTECORONA_H
