/*
    SPDX-FileCopyrightText: 2016 Smith AR <audoban@openmaibox.org>
    SPDX-FileCopyrightText: 2016 Michail Vourlakos <mvourlakos@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "lattecorona.h"

// local
#include <coretypes.h>
#include <config-latte.h>
#include "alternativeshelper.h"
#include "apptypes.h"
#include "dbusreports.h"
#include "screengeometrycalculator.h"
#include "lattedockadaptor.h"
#include "screenpool.h"
#include "data/generictable.h"
#include "data/layouticondata.h"
#include "declarativeimports/interfaces.h"
#include "declarativeimports/contextmenulayerquickitem.h"
#include "indicator/factory.h"
#include "layout/abstractlayout.h"
#include "layout/centrallayout.h"
#include "layout/genericlayout.h"
#include "layouts/importer.h"
#include "layouts/manager.h"
#include "layouts/synchronizer.h"
#include "shortcuts/globalshortcuts.h"
#include "package/lattepackage.h"
#include "plasma/extended/backgroundcache.h"
#include "plasma/extended/backgroundtracker.h"
#include "plasma/extended/screengeometries.h"
#include "plasma/extended/screenpool.h"
#include "plasma/extended/theme.h"
#include "settings/universalsettings.h"
#include "templates/templatesmanager.h"
#include "tools/commontools.h"
#include "view/originalview.h"
#include "view/view.h"
#include "view/visibilitymanager.h"
#include "view/settings/primaryconfigview.h"
#include "view/settings/viewsettingsfactory.h"
#include "view/windowstracker/windowstracker.h"
#include "view/windowstracker/allscreenstracker.h"
#include "view/windowstracker/currentscreentracker.h"
#include "wm/abstractwindowinterface.h"
#include "wm/schemecolors.h"
#include "wm/waylandinterface.h"
#if HAVE_X11
#include "wm/xwindowinterface.h"
#endif
#include "wm/tracker/lastactivewindow.h"
#include "wm/tracker/schemes.h"
#include "wm/tracker/windowstracker.h"

// Qt
#include <QAction>
#include <QApplication>
#include <QScreen>
#include <QDBusConnection>
#include <QDebug>
#include <QFile>
#include <QFontDatabase>
#include <QQmlContext>
#include <QProcess>
#include <QQuickItem>
#include <QQuickWindow>
#include <QWindow>

// Plasma
#include <Plasma/Plasma>
#include <Plasma/Corona>
#include <Plasma/Containment>
#include <PlasmaQuick/ConfigView>

// KDE
#include <KActionCollection>
#include <KPluginMetaData>
#include <KGlobalAccel>
#include <KLocalizedString>
#include <KPackage/Package>
#include <KPackage/PackageLoader>
#include <KAboutData>
#include <PlasmaActivities/Consumer>
#include <PlasmaQuick/SharedQmlEngine>
#include <KWindowSystem>
#include <KWayland/Client/connection_thread.h>
#include <KWayland/Client/registry.h>
#include <KWayland/Client/plasmashell.h>
#include <KWayland/Client/plasmawindowmanagement.h>

namespace Latte {

Corona::Corona(bool defaultLayoutOnStartup, QString layoutNameOnStartUp, QString addViewTemplateName, int userSetMemoryUsage, QObject *parent)
    : Plasma::Corona(parent),
      m_defaultLayoutOnStartup(defaultLayoutOnStartup),
      m_startupAddViewTemplateName(addViewTemplateName),
      m_userSetMemoryUsage(userSetMemoryUsage),
      m_layoutNameOnStartUp(layoutNameOnStartUp),
      m_activitiesConsumer(new KActivities::Consumer(this)),
      m_screenPool(new ScreenPool(KSharedConfig::openConfig(), this)),
      m_indicatorFactory(new Indicator::Factory(this)),
      m_universalSettings(new UniversalSettings(KSharedConfig::openConfig(), this)),
      m_globalShortcuts(new GlobalShortcuts(this)),
      m_plasmaScreenPool(new PlasmaExtended::ScreenPool(this)),
      m_themeExtended(new PlasmaExtended::Theme(KSharedConfig::openConfig(), this)),
      m_viewSettingsFactory(new ViewSettingsFactory(this)),
      m_templatesManager(new Templates::Manager(this)),
      m_layoutsManager(new Layouts::Manager(this)),
      m_plasmaGeometries(new PlasmaExtended::ScreenGeometries(this)),
      m_dialogShadows(new PanelShadows(this, QStringLiteral("dialogs/background")))
{
    connect(qApp, &QApplication::aboutToQuit, this, &Corona::onAboutToQuit);

    //! watch every window the process shows so applet-created transient
    //! windows can inherit their view's screen (see eventFilter)
    qApp->installEventFilter(this);

    //! create the window manager
    if (KWindowSystem::isPlatformWayland()) {
        m_wm = new WindowSystem::WaylandInterface(this);
    } else {
#if HAVE_X11
        m_wm = new WindowSystem::XWindowInterface(this);
#else
        //! best-effort X11 support was disabled at build time; the wayland
        //! interface degrades to no-ops without a wayland registry
        qWarning() << "Latte was built without X11 support (WITH_X11=OFF) but is not running under wayland; window tracking will not work";
        m_wm = new WindowSystem::WaylandInterface(this);
#endif
    }

    setupWaylandIntegration();

    KPackage::Package package(new Latte::Package(this));

    m_screenPool->load();

    if (!package.isValid()) {
        qWarning() << staticMetaObject.className()
                   << "the package" << package.metadata().rawData() << "is invalid!";
        return;
    } else {
        qDebug() << staticMetaObject.className()
                 << "the package" << package.metadata().rawData() << "is valid!";
    }

    setKPackage(package);
    //! universal settings / extendedtheme must be loaded after the package has been set
    m_universalSettings->load();
    m_themeExtended->load();

    qmlRegisterTypes();

    if (m_activitiesConsumer && (m_activitiesConsumer->serviceStatus() == KActivities::Consumer::Running)) {
        load();
    }

    connect(m_activitiesConsumer, &KActivities::Consumer::serviceStatusChanged, this, &Corona::load);

    m_viewsScreenSyncTimer.setSingleShot(true);
    m_viewsScreenSyncTimer.setInterval(m_universalSettings->screenTrackerInterval());
    connect(&m_viewsScreenSyncTimer, &QTimer::timeout, this, &Corona::syncLatteViewsToScreens);
    connect(m_universalSettings, &UniversalSettings::screenTrackerIntervalChanged, this, [this]() {
        m_viewsScreenSyncTimer.setInterval(m_universalSettings->screenTrackerInterval());
    });

    //! Dbus adaptor initialization
    new LatteDockAdaptor(this);
    QDBusConnection dbus = QDBusConnection::sessionBus();
    dbus.registerObject(QStringLiteral("/Latte"), this);
}

Corona::~Corona()
{
    //! Deliberate teardown order (the session-shutdown plan item). Two Qt
    //! facts drive the shape, both pinned in quitteardowncontractstest:
    //! a deleteLater() posted here is NEVER processed (exec() has returned,
    //! no loop remains to flush it - the previous destructor's pile of
    //! deleteLater() calls silently did nothing), and without explicit
    //! deletes ~QObject's child pass deletes in CONSTRUCTION order, which
    //! puts the screen/theme services ahead of the layouts manager whose
    //! teardown still consumes them (the crash-on-logout class, e.g. the
    //! historical ~Theme null-schemes segfault). Delete consumers before
    //! the services they consume:

    //! chrome and shortcuts consume views and layouts
    delete m_globalShortcuts;
    delete m_viewSettingsFactory;

    //! layouts own the views/containments (already unloaded in
    //! onAboutToQuit on a clean quit; any surviving view destructor still
    //! calls into the wm/theme/indicator services below)
    delete m_layoutsManager;
    delete m_templatesManager;

    //! services the views consumed
    delete m_plasmaGeometries;
    delete m_dialogShadows;
    delete m_indicatorFactory;
    delete m_themeExtended;
    delete m_plasmaScreenPool;
    delete m_universalSettings;
    delete m_screenPool;

    //! the window-system interface goes last: strut and tracking
    //! unregistration from every teardown above lands here
    delete m_wm;

    disconnect(m_activitiesConsumer, &KActivities::Consumer::serviceStatusChanged, this, &Corona::load);
    delete m_activitiesConsumer;

    qDebug() << "Latte Corona - deleted...";

    if (!m_importFullConfigurationFile.isEmpty()) {
        //!NOTE: Restart latte to import the new configuration
        QString importCommand = "latte-dock --import-full \"" + m_importFullConfigurationFile + "\"";
        qDebug() << "Executing Import Full Configuration command : " << importCommand;

        QProcess::startDetached(importCommand);
    }
}

void Corona::onAboutToQuit()
{
    //! the last line of the quit-reason trail: if neither the signal line
    //! (main.cpp) nor the quitApplication line precedes this one in the log,
    //! the event loop was quit from outside the process's own quit paths
    qWarning() << "corona: onAboutToQuit - event loop is exiting";

    setLifecyclePhase(LifecyclePhase::QuitRequested);

    //! Qt 6.11 emits aboutToQuit synchronously inside exit()/quit(), before
    //! the event loop unwinds (quitteardowncontractstest), so the
    //! application is still fully live here: this is the last point where
    //! views can hide, config can sync and containments can save state.
    //! Everything needing a live app happens HERE; ~Corona only deletes,
    //! in dependency order. (The slide-out grace period lives in
    //! quitApplication(), which delays quit() itself by 800ms.)
    m_layoutsManager->synchronizer()->hideAllViews();

    m_viewsScreenSyncTimer.stop();

    if (m_layoutsManager->memoryUsage() == MemoryUsage::SingleLayout) {
        cleanConfig();
    }

    if (m_layoutsManager->memoryUsage() == Latte::MemoryUsage::MultipleLayouts) {
        m_layoutsManager->importer()->setMultipleLayoutsStatus(Latte::MultipleLayouts::Paused);
    }

    qDebug() << "Latte Corona - unload: containments ...";
    m_layoutsManager->unload();

    setLifecyclePhase(LifecyclePhase::Unloaded);
}

void Corona::load()
{
    if (m_activitiesConsumer && (m_activitiesConsumer->serviceStatus() == KActivities::Consumer::Running) && m_activitiesStarting) {
        m_activitiesStarting = false;

        disconnect(m_activitiesConsumer, &KActivities::Consumer::serviceStatusChanged, this, &Corona::load);

        m_templatesManager->init();
        m_layoutsManager->init();

        connect(this, &Corona::availableScreenRectChangedFrom, this, &Corona::onAvailableScreenRectChangedFrom, Qt::UniqueConnection);
        connect(this, &Corona::availableScreenRegionChangedFrom, this, &Corona::onAvailableScreenRegionChangedFrom, Qt::UniqueConnection);
        connect(m_screenPool, &ScreenPool::primaryScreenChanged, this, &Corona::onScreenCountChanged, Qt::UniqueConnection);

        QString loadLayoutName = "";

        if (m_userSetMemoryUsage != -1) {
            MemoryUsage::LayoutsMemory usage = static_cast<MemoryUsage::LayoutsMemory>(m_userSetMemoryUsage);
            m_universalSettings->setLayoutsMemoryUsage(usage);
        }

        if (!m_defaultLayoutOnStartup && m_layoutNameOnStartUp.isEmpty()) {
            if (m_universalSettings->layoutsMemoryUsage() == MemoryUsage::MultipleLayouts) {
                loadLayoutName = "";
            } else {
                loadLayoutName = m_universalSettings->singleModeLayoutName();

                if (!m_layoutsManager->synchronizer()->layoutExists(loadLayoutName)) {
                    //! If chosen layout does not exist, force Default layout loading
                    QString defaultLayoutTemplateName = i18n(Templates::DEFAULTLAYOUTTEMPLATENAME);
                    loadLayoutName = defaultLayoutTemplateName;

                    if (!m_layoutsManager->synchronizer()->layoutExists(defaultLayoutTemplateName)) {
                        //! If Default layout does not exist at all, create it
                        QString path = m_templatesManager->newLayout("", defaultLayoutTemplateName);
                        m_layoutsManager->setOnAllActivities(Layout::AbstractLayout::layoutName(path));
                    }
                }
            }
        } else if (m_defaultLayoutOnStartup) {
            //! force loading a NEW default layout even though a default layout may already exists
            QString newDefaultLayoutPath = m_templatesManager->newLayout("", i18n(Templates::DEFAULTLAYOUTTEMPLATENAME));
            loadLayoutName = Layout::AbstractLayout::layoutName(newDefaultLayoutPath);
            m_universalSettings->setLayoutsMemoryUsage(MemoryUsage::SingleLayout);
        } else {
            loadLayoutName = m_layoutNameOnStartUp;
            m_universalSettings->setLayoutsMemoryUsage(MemoryUsage::SingleLayout);
        }

        m_layoutsManager->loadLayoutOnStartup(loadLayoutName);

        //! load screens signals such screenGeometryChanged in order to support
        //! plasmoid.screenGeometry properly
        for (QScreen *screen : qGuiApp->screens()) {
            onScreenAdded(screen);
        }

        connect(m_layoutsManager->synchronizer(), &Layouts::Synchronizer::initializationFinished, [this]() {
            if (!m_startupAddViewTemplateName.isEmpty()) {
                //! user requested through cmd startup to add view from specific view template and we can add it after the startup
                //! sequence has loaded all required layouts properly
                addView(0, m_startupAddViewTemplateName);
                m_startupAddViewTemplateName = "";
            }

            //! warm the edit-mode chrome once startup has fully settled (the
            //! staggered views land within ~6s; 8s leaves margin), so the
            //! user's FIRST Edit Dock opens on the warm path (~0.5s) instead
            //! of paying ~7s of cold chrome QML instantiation. Skipped when
            //! the user already opened the settings before the timer fired.
            QTimer::singleShot(8000, this, [this]() {
                if (m_viewSettingsFactory->primaryConfigView()) {
                    qDebug() << "chrome warmup: skipped, settings already exist";
                    return;
                }

                auto views = m_layoutsManager->synchronizer()->sortedCurrentViews();

                if (!views.isEmpty()) {
                    qDebug() << "chrome warmup: building the edit chrome hidden...";
                    m_viewSettingsFactory->warmupPrimaryConfigView(views.first());
                } else {
                    qWarning() << "chrome warmup: no views available, warmup skipped";
                }
            });
        });

        setLifecyclePhase(LifecyclePhase::Running);

        connect(qGuiApp, &QGuiApplication::screenAdded, this, &Corona::onScreenAdded, Qt::UniqueConnection);
        connect(qGuiApp, &QGuiApplication::screenRemoved, this, &Corona::onScreenRemoved, Qt::UniqueConnection);
    }
}

void Corona::unload()
{
    qDebug() << "unload: removing containments...";

    while (!containments().isEmpty()) {
        //deleting a containment will remove it from the list due to QObject::destroyed connect in Corona
        //this form doesn't crash, while qDeleteAll(containments()) does
        delete containments().first();
    }
}

void Corona::setupWaylandIntegration()
{
    if (!KWindowSystem::isPlatformWayland()) {
        return;
    }

    using namespace KWayland::Client;

    auto connection = ConnectionThread::fromApplication(this);

    if (!connection) {
        return;
    }

    Registry *registry{new Registry(this)};
    registry->create(connection);

    connect(registry, &Registry::plasmaShellAnnounced, this
            , [this, registry](quint32 name, quint32 version) {
        m_waylandCorona = registry->createPlasmaShell(name, version, this);
    });

    QObject::connect(registry, &KWayland::Client::Registry::plasmaWindowManagementAnnounced,
                     [this, registry](quint32 name, quint32 version) {
        KWayland::Client::PlasmaWindowManagement *pwm = registry->createPlasmaWindowManagement(name, version, this);

        WindowSystem::WaylandInterface *wI = qobject_cast<WindowSystem::WaylandInterface *>(m_wm);

        if (wI) {
            wI->initWindowManagement(pwm);
        }
    });


    QObject::connect(registry, &KWayland::Client::Registry::plasmaVirtualDesktopManagementAnnounced,
                     [this, registry] (quint32 name, quint32 version) {
        KWayland::Client::PlasmaVirtualDesktopManagement *vdm = registry->createPlasmaVirtualDesktopManagement(name, version, this);

        WindowSystem::WaylandInterface *wI = qobject_cast<WindowSystem::WaylandInterface *>(m_wm);

        if (wI) {
            wI->initVirtualDesktopManagement(vdm);
        }
    });


    registry->setup();
    connection->roundtrip();
}

KWayland::Client::PlasmaShell *Corona::waylandCoronaInterface() const
{
    return m_waylandCorona;
}

void Corona::cleanConfig()
{
    auto containmentsEntries = config()->group("Containments");
    bool changed = false;

    for(const auto &cId : containmentsEntries.groupList()) {
        if (!containmentExists(cId.toUInt())) {
            //cleanup obsolete containments
            containmentsEntries.group(cId).deleteGroup();
            changed = true;
            qDebug() << "obsolete containment configuration deleted:" << cId;
        } else {
            //cleanup obsolete applets of running containments
            auto appletsEntries = containmentsEntries.group(cId).group("Applets");

            for(const auto &appletId : appletsEntries.groupList()) {
                if (!appletExists(cId.toUInt(), appletId.toUInt())) {
                    appletsEntries.group(appletId).deleteGroup();
                    changed = true;
                    qDebug() << "obsolete applet configuration deleted:" << appletId;
                }
            }
        }
    }

    if (changed) {
        config()->sync();
        qDebug() << "configuration file cleaned...";
    }
}

bool Corona::containmentExists(uint id) const
{
    for(const auto containment : containments()) {
        if (id == containment->id()) {
            return true;
        }
    }

    return false;
}

bool Corona::appletExists(uint containmentId, uint appletId) const
{
    Plasma::Containment *containment = nullptr;

    for(const auto cont : containments()) {
        if (containmentId == cont->id()) {
            containment = cont;
            break;
        }
    }

    if (!containment) {
        return false;
    }

    for(const auto applet : containment->applets()) {
        if (applet->id() == appletId) {
            return true;
        }
    }

    return false;
}

bool Corona::inQuit() const
{
    return m_lifecyclePhase >= LifecyclePhase::QuitRequested;
}

bool Corona::isScreenUiReady(int screen) const
{
    Q_UNUSED(screen);
    return m_lifecyclePhase >= LifecyclePhase::Running;
}

QString Corona::lifecycleState() const
{
    switch (m_lifecyclePhase) {
    case LifecyclePhase::Startup: return QStringLiteral("startup");
    case LifecyclePhase::Running: return QStringLiteral("running");
    case LifecyclePhase::QuitRequested: return QStringLiteral("quit-requested");
    case LifecyclePhase::Unloaded: return QStringLiteral("unloaded");
    }

    Q_UNREACHABLE();
}

void Corona::setLifecyclePhase(LifecyclePhase phase)
{
    if (m_lifecyclePhase == phase) {
        return;
    }

    //! the lifecycle only moves forward; a backwards request is a defect
    //! at the caller, catch it at the origin
    Q_ASSERT(phase > m_lifecyclePhase);

    m_lifecyclePhase = phase;
    //! qWarning so the transition joins the quit-reason trail, which is
    //! visible without --debug filtering decisions changing
    qWarning() << "corona: lifecycle phase ->" << lifecycleState();
}

KActivities::Consumer *Corona::activitiesConsumer() const
{
    return m_activitiesConsumer;
}

PanelShadows *Corona::dialogShadows() const
{
    return m_dialogShadows;
}

GlobalShortcuts *Corona::globalShortcuts() const
{
    return m_globalShortcuts;
}

ScreenPool *Corona::screenPool() const
{
    return m_screenPool;
}

UniversalSettings *Corona::universalSettings() const
{
    return m_universalSettings;
}

ViewSettingsFactory *Corona::viewSettingsFactory() const
{
    return m_viewSettingsFactory;
}

WindowSystem::AbstractWindowInterface *Corona::wm() const
{
    return m_wm;
}

Indicator::Factory *Corona::indicatorFactory() const
{
    return m_indicatorFactory;
}

Layouts::Manager *Corona::layoutsManager() const
{
    return m_layoutsManager;
}

Templates::Manager *Corona::templatesManager() const
{
    return m_templatesManager;
}

PlasmaExtended::ScreenPool *Corona::plasmaScreenPool() const
{
    return m_plasmaScreenPool;
}

PlasmaExtended::Theme *Corona::themeExtended() const
{
    return m_themeExtended;
}

int Corona::numScreens() const
{
    return qGuiApp->screens().count();
}

QRect Corona::screenGeometry(int id) const
{
    const auto screens = qGuiApp->screens();
    const QScreen *screen{m_screenPool->primaryScreen()};

    QString screenName;

    if (m_screenPool->hasScreenId(id)) {
        screenName = m_screenPool->connector(id);
    }

    for(const auto scr : screens) {
        if (scr->name() == screenName) {
            screen = scr;
            break;
        }
    }

    return screen->geometry();
}

CentralLayout *Corona::centralLayout(QString name) const
{
    CentralLayout *result{nullptr};

    if (!name.isEmpty()) {
        result = m_layoutsManager->synchronizer()->centralLayout(name);
    }

    return result;
}

Layout::GenericLayout *Corona::layout(QString name) const
{
    Layout::GenericLayout *result{nullptr};

    if (!name.isEmpty()) {
        result = m_layoutsManager->synchronizer()->layout(name);
    }

    return result;
}

QRegion Corona::availableScreenRegion(int id) const
{   
    //! ignore modes are added in order for notifications to be placed
    //! in better positioning and not overlap with sidebars or usually hidden views
    QList<Types::Visibility> ignoremodes({Latte::Types::AutoHide,
                                          Latte::Types::SidebarOnDemand,
                                          Latte::Types::SidebarAutoHide});


    return availableScreenRegionWithCriteria(id,
                                             QString(),
                                             ignoremodes);
}

namespace {
//! snapshot the View properties the ScreenGeometryCalculator reads (EX-08);
//! null visibility/positioner become the footprint's explicit flags
Latte::ViewFootprint viewFootprintOf(const Latte::View *view)
{
    Latte::ViewFootprint footprint;
    footprint.location = view->location();
    footprint.formFactor = view->formFactor();
    footprint.alignment = static_cast<Latte::Types::Alignment>(view->alignment());
    footprint.hasVisibility = view->visibility() != nullptr;
    footprint.visibilityMode = footprint.hasVisibility ? view->visibility()->mode() : Latte::Types::None;
    footprint.isOffScreen = view->positioner() && view->positioner()->isOffScreen();
    footprint.behaveAsPlasmaPanel = view->behaveAsPlasmaPanel();
    footprint.normalThickness = view->normalThickness();
    footprint.screenEdgeMargin = view->screenEdgeMargin();
    footprint.maxLength = view->maxLength();
    footprint.offset = view->offset();
    footprint.geometry = view->geometry();
    return footprint;
}
}

QRegion Corona::availableScreenRegionWithCriteria(int id,
                                                  QString activityid,
                                                  QList<Types::Visibility> ignoreModes,
                                                  QList<Plasma::Types::Location> ignoreEdges,
                                                  bool ignoreExternalPanels,
                                                  bool desktopUse) const
{
    const QScreen *screen = m_screenPool->screenForId(id);
    bool inCurrentActivity{activityid.isEmpty()};

    if (!screen) {
        return {};
    }

    const QRect startRect = ignoreExternalPanels ? screen->geometry() : screen->availableGeometry();
    QRegion available = startRect;

    QList<Latte::View *> views;

    if (inCurrentActivity) {
        views = m_layoutsManager->synchronizer()->viewsBasedOnActivityId(m_activitiesConsumer->currentActivity());
    } else {
        views = m_layoutsManager->synchronizer()->viewsBasedOnActivityId(activityid);
    }

    if (views.isEmpty()) {
        return available;
    }

    //! EX-08 (docs/QML_EXTRACTION_PLAN.md): the geometry math lives in the
    //! tested ScreenGeometryCalculator over view-footprint snapshots; this
    //! function keeps the live-graph reads. Deliberately NO self-origin
    //! exclusion (1b932ed9) - see the calculator header.
    QList<ViewFootprint> footprints;

    for (const auto *view : views) {
        if (view && view->containment() && view->screen() == screen) {
            footprints << viewFootprintOf(view);
        }
    }

    return ScreenGeometryCalculator::availableRegion(startRect, screen->geometry(), footprints,
                                                     ignoreModes, ignoreEdges, desktopUse);
}

QRect Corona::availableScreenRect(int id) const
{
    //! ignore modes are added in order for notifications to be placed
    //! in better positioning and not overlap with sidebars or usually hidden views
    QList<Types::Visibility> ignoremodes({Latte::Types::AutoHide,
                                          Latte::Types::SidebarOnDemand,
                                          Latte::Types::SidebarAutoHide});

    return availableScreenRectWithCriteria(id,
                                           QString(),
                                           ignoremodes);
}

QRect Corona::availableScreenRectWithCriteria(int id,
                                              QString activityid,
                                              QList<Types::Visibility> ignoreModes,
                                              QList<Plasma::Types::Location> ignoreEdges,
                                              bool ignoreExternalPanels,
                                              bool desktopUse) const
{
    const QScreen *screen = m_screenPool->screenForId(id);
    bool inCurrentActivity{activityid.isEmpty()};

    if (!screen) {
        return {};
    }

    QRect available = ignoreExternalPanels ? screen->geometry() : screen->availableGeometry();

    QList<Latte::View *> views;

    if (inCurrentActivity) {
        views = m_layoutsManager->synchronizer()->viewsBasedOnActivityId(m_activitiesConsumer->currentActivity());
    } else {
        views = m_layoutsManager->synchronizer()->viewsBasedOnActivityId(activityid);
    }

    if (views.isEmpty()) {
        return available;
    }

    //! EX-08: same seam as availableScreenRegionWithCriteria above
    QList<ViewFootprint> footprints;

    for (const auto *view : views) {
        if (view && view->containment() && view->screen() == screen) {
            footprints << viewFootprintOf(view);
        }
    }

    return ScreenGeometryCalculator::availableRect(available, screen->geometry(), footprints,
                                                   ignoreModes, ignoreEdges, desktopUse);
}

void Corona::onScreenAdded(QScreen *screen)
{
    Q_ASSERT(screen);

    int id = m_screenPool->id(screen->name());

    if (id == -1) {
        m_screenPool->insertScreenMapping(screen->name());
    }

    connect(screen, &QScreen::geometryChanged, this, &Corona::onScreenGeometryChanged);

    Q_EMIT availableScreenRectChanged(m_screenPool->id(screen->name()));
    Q_EMIT screenAdded(m_screenPool->id(screen->name()));

    onScreenCountChanged();
}

void Corona::onScreenRemoved(QScreen *screen)
{
    disconnect(screen, &QScreen::geometryChanged, this, &Corona::onScreenGeometryChanged);
    onScreenCountChanged();
}

void Corona::onScreenCountChanged()
{
    m_viewsScreenSyncTimer.start();
}

void Corona::onScreenGeometryChanged(const QRect &geometry)
{
    Q_UNUSED(geometry);

    QScreen *screen = qobject_cast<QScreen *>(sender());

    if (!screen) {
        return;
    }

    const int id = m_screenPool->id(screen->name());

    if (id >= 0) {
        Q_EMIT screenGeometryChanged(id);
        Q_EMIT availableScreenRegionChanged(id);
        Q_EMIT availableScreenRectChanged(id);
    }
}

void Corona::notifyAvailableScreenGeometriesChanged()
{
    for (QScreen *screen : qGuiApp->screens()) {
        const int id = m_screenPool->id(screen->name());

        if (id >= 0) {
            Q_EMIT availableScreenRectChanged(id);
            Q_EMIT availableScreenRegionChanged(id);
        }
    }
}

void Corona::onAvailableScreenRectChangedFrom(Latte::View *origin)
{
    if (origin && origin->containment()) {
        Q_EMIT availableScreenRectChanged(origin->containment()->screen());
    }
}

void Corona::onAvailableScreenRegionChangedFrom(Latte::View *origin)
{
    if (origin && origin->containment()) {
        Q_EMIT availableScreenRegionChanged(origin->containment()->screen());
    }
}

//! the central functions that updates loading/unloading latteviews
//! concerning screen changed (for multi-screen setups mainly)
void Corona::syncLatteViewsToScreens()
{
    m_layoutsManager->synchronizer()->syncLatteViewsToScreens();
}

int Corona::primaryScreenId() const
{
    return m_screenPool->primaryScreenId();
}

void Corona::quitApplication()
{
    //! part of the quit-reason trail (see main.cpp's signal wiring): callers
    //! are the settings dialog, the D-Bus quitApplication method and
    //! importFullConfiguration - an unexplained clean exit with this line
    //! absent points at QCoreApplication::quit() reached directly (e.g. the
    //! /MainApplication D-Bus object KDBusService registers)
    qWarning() << "corona: quitApplication invoked - beginning deliberate shutdown";

    setLifecyclePhase(LifecyclePhase::QuitRequested);

    //! this code must be called asynchronously because it is called
    //! also from qml (Settings window).
    QTimer::singleShot(300, this, [this]() {
        m_layoutsManager->hideLatteSettingsDialog();
        m_layoutsManager->synchronizer()->hideAllViews();
    });

    //! give the time for the views to hide themselves
    QTimer::singleShot(800, this, [this]() {
        qGuiApp->quit();
    });
}

void Corona::aboutApplication()
{
    if (aboutDialog) {
        aboutDialog->hide();
        aboutDialog->deleteLater();
    }

    aboutDialog = new KAboutApplicationDialog(KAboutData::applicationData());
    connect(aboutDialog.data(), &QDialog::finished, aboutDialog.data(), &QObject::deleteLater);
    m_wm->skipTaskBar(*aboutDialog);
    m_wm->setKeepAbove(WindowSystem::windowIdFromWId(aboutDialog->winId()), true);

    aboutDialog->show();
}

void Corona::loadDefaultLayout()
{
  //disabled
}

//! Resolve the Latte::View hosting a window the process is about to show.
//! Two routes, each pinned by a contract test:
//! - transientParent, when someone assigned one explicitly (libplasma does
//!   for visualParent'd popups, the context menu code does for menus).
//! - the QObject parent chain (Latte::visualHostWindowOf). An applet-created
//!   dialog (a PlasmaCore.Dialog declared in applet QML) is a resource child
//!   of the item that declared it and gets NO transient parent on Qt 6 - the
//!   Qt 5 declared-inside-an-item transient magic now lives in
//!   QQuickWindowQmlImpl only (contracts: appletwindowparentingtest.cpp,
//!   tst_transientwindowcontracts.qml). The declaring item's window() is the
//!   host; when that host is itself an applet popup (a systray member's
//!   dialog, say), its transientParent carries on to the view.
static Latte::View *viewHostingWindow(QWindow *window)
{
    if (auto *view = qobject_cast<Latte::View *>(window->transientParent())) {
        return view;
    }

    QQuickWindow *host = Latte::visualHostWindowOf(window);

    if (!host) {
        return nullptr;
    }

    if (auto *view = qobject_cast<Latte::View *>(host)) {
        return view;
    }

    return qobject_cast<Latte::View *>(host->transientParent());
}

bool Corona::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Show && watched->isWindowType()) {
        QWindow *window = static_cast<QWindow *>(watched);

        //! An applet-created dialog resolves to its view (above), but
        //! nothing forwards the view's SCREEN to it: QWindow::screen()
        //! stays the primary screen until the compositor maps the surface
        //! somewhere, and applets place such dialogs from window->screen()
        //! synchronously right after show(), before any wl_surface.enter
        //! can correct it - so the placement lands on the primary output
        //! regardless of where the dock is (caught live: the comic
        //! applet's full-size viewer centered on the portrait screen while
        //! its dock ran on the landscape one). Forward the screen here, at
        //! Show delivery: filters run before the target window handles the
        //! event and before control returns to the code that positions the
        //! dialog, so every screen-derived placement that follows computes
        //! from the right output.
        if (Latte::View *view = viewHostingWindow(window)) {
            if (view->screen() && window->screen() != view->screen()) {
                window->setScreen(view->screen());
            }
        }
    }

    return Plasma::Corona::eventFilter(watched, event);
}

int Corona::screenForContainment(const Plasma::Containment *containment) const
{
    //FIXME: indexOf is not a proper way to support multi-screen
    // as for environment to environment the indexes change
    // also there is the following issue triggered
    // from latteView adaptToScreen()
    //
    // in a multi-screen environment that
    // primary screen is not set to 0 it was
    // created an endless showing loop at
    // startup (catch-up race) between
    // screen:0 and primaryScreen

    //case in which this containment is child of an applet, hello systray :)
    if (Plasma::Applet *parentApplet = qobject_cast<Plasma::Applet *>(containment->parent())) {
        if (Plasma::Containment *cont = parentApplet->containment()) {
            return screenForContainment(cont);
        } else {
            return -1;
        }
    }

    Plasma::Containment *c = const_cast<Plasma::Containment *>(containment);
    int scrId = m_layoutsManager->synchronizer()->screenForContainment(c);

    if (scrId >= 0) {
        return scrId;
    }

    return containment->lastScreen();
}

void Corona::showAlternativesForApplet(Plasma::Applet *applet)
{
    const QString alternativesQML = kPackage().filePath("appletalternativesui");

    if (alternativesQML.isEmpty()) {
        return;
    }

    Latte::View *latteView =  m_layoutsManager->synchronizer()->viewForContainment(applet->containment());

    PlasmaQuick::SharedQmlEngine *qmlObj{nullptr};

    if (latteView) {
        latteView->setAlternativesIsShown(true);
        qmlObj = new PlasmaQuick::SharedQmlEngine(latteView);
    } else {
        qmlObj = new PlasmaQuick::SharedQmlEngine(this);
    }

    qmlObj->setInitializationDelayed(true);
    qmlObj->setSource(QUrl::fromLocalFile(alternativesQML));

    AlternativesHelper *helper = new AlternativesHelper(applet, qmlObj);
    qmlObj->rootContext()->setContextProperty(QStringLiteral("alternativesHelper"), helper);

    m_alternativesObjects << qmlObj;
    qmlObj->completeInitialization();

    //! Alternative dialog signals
    connect(helper, &QObject::destroyed, this, [latteView]() {
        latteView->setAlternativesIsShown(false);
    });

    connect(qmlObj->rootObject(), SIGNAL(visibleChanged(bool)),
            this, SLOT(alternativesVisibilityChanged(bool)));

    connect(applet, &Plasma::Applet::destroyedChanged, this, [this, qmlObj](bool destroyed) {
        if (!destroyed) {
            return;
        }

        QMutableListIterator<PlasmaQuick::SharedQmlEngine *> it(m_alternativesObjects);

        while (it.hasNext()) {
            PlasmaQuick::SharedQmlEngine *obj = it.next();

            if (obj == qmlObj) {
                it.remove();
                obj->deleteLater();
            }
        }
    });
}

void Corona::alternativesVisibilityChanged(bool visible)
{
    if (visible) {
        return;
    }

    QObject *root = sender();

    QMutableListIterator<PlasmaQuick::SharedQmlEngine *> it(m_alternativesObjects);

    while (it.hasNext()) {
        PlasmaQuick::SharedQmlEngine *obj = it.next();

        if (obj->rootObject() == root) {
            it.remove();
            obj->deleteLater();
        }
    }
}

QStringList Corona::containmentsIds()
{
    QStringList ids;

    for(const auto containment : containments()) {
        ids << QString::number(containment->id());
    }

    return ids;
}

QStringList Corona::appletsIds()
{
    QStringList ids;

    for(const auto containment : containments()) {
        auto applets = containment->config().group("Applets");
        ids << applets.groupList();
    }

    return ids;
}

//! Activate launcher menu through dbus interface
void Corona::activateLauncherMenu()
{
    m_globalShortcuts->activateLauncherMenu();
}

void Corona::windowColorScheme(QString windowIdAndScheme)
{
    int firstSlash = windowIdAndScheme.indexOf("-");
    QString windowIdStr = windowIdAndScheme.mid(0, firstSlash);
    QString schemeStr = windowIdAndScheme.mid(firstSlash + 1);

    if (KWindowSystem::isPlatformWayland()) {
        QTimer::singleShot(200, this, [this, schemeStr]() {
            //! [Wayland Case] - give the time to be informed correctly for the active window id
            //! otherwise the active window id may not be the same with the one triggered
            //! the color scheme dbus signal
            m_wm->schemesTracker()->setColorSchemeForWindow(m_wm->activeWindow(), schemeStr);
        });
    } else {
        m_wm->schemesTracker()->setColorSchemeForWindow(WindowSystem::windowIdFromWId(windowIdStr.toULongLong()), schemeStr);
    }
}

//! update badge for specific view item
void Corona::updateDockItemBadge(QString identifier, QString value)
{
    m_globalShortcuts->updateViewItemBadge(identifier, value);
}

void Corona::setAutostart(const bool &enabled)
{
    m_universalSettings->setAutostart(enabled);
}

void Corona::switchToLayout(QString layout)
{
    if ((layout.startsWith("file:/") || layout.startsWith("/")) && layout.endsWith(".layout.latte")) {
        importLayoutFile(layout);
    } else {
        m_layoutsManager->switchToLayout(layout);
    }
}

void Corona::importLayoutFile(const QString &filepath, const QString &suggestedLayoutName)
{
    bool isFilepathValid = (filepath.startsWith("file:/") || filepath.startsWith("/")) && filepath.endsWith(".layout.latte");

    if (!isFilepathValid) {
        qDebug() << i18n("The layout cannot be imported from file :: ") << filepath;
        return;
    }

    //! Import and load runtime a layout through dbus interface
    //! It can be used from external programs that want to update runtime
    //! the Latte shown layout
    QString layoutPath = filepath;

    //! cleanup layout path
    if (layoutPath.startsWith("file:///")) {
        layoutPath = layoutPath.remove("file://");
    } else if (layoutPath.startsWith("file://")) {
        layoutPath = layoutPath.remove("file:/");
    }

    //! check out layoutpath existence
    if (QFileInfo(layoutPath).exists()) {
        qDebug() << " Layout is going to be imported and loaded from file :: " << layoutPath << " with suggested name :: " << suggestedLayoutName;

        QString importedLayout = m_layoutsManager->importer()->importLayout(layoutPath, suggestedLayoutName);

        if (importedLayout.isEmpty()) {
            qDebug() << i18n("The layout cannot be imported from file :: ") << layoutPath;
        } else {
           m_layoutsManager->switchToLayout(importedLayout, MemoryUsage::SingleLayout);
        }
    } else {
        qDebug() << " Layout from missing file can not be imported and loaded :: " << layoutPath;
    }
}

void Corona::showSettingsWindow(int page)
{
    if (m_lifecyclePhase == LifecyclePhase::Startup) {
        return;
    }

    Settings::Dialog::ConfigurationPage p = Settings::Dialog::LayoutPage;

    if (page >= Settings::Dialog::LayoutPage && page <= Settings::Dialog::PreferencesPage) {
        p = static_cast<Settings::Dialog::ConfigurationPage>(page);
    }

    m_layoutsManager->showLatteSettingsDialog(p);
}

QStringList Corona::contextMenuData(const uint &containmentId)
{
    QStringList data;
    Types::ViewType viewType{Types::DockView};
    auto view = m_layoutsManager->synchronizer()->viewForContainment(containmentId);

    if (view) {
        viewType = view->type();
    }

    data << QString::number((int)m_layoutsManager->memoryUsage()); // Memory Usage
    data << m_layoutsManager->centralLayoutsNames().join(";;"); // All Active layouts
    data << m_layoutsManager->synchronizer()->currentLayoutsNames().join(";;"); // All Current layouts
    data << m_universalSettings->contextMenuActionsAlwaysShown().join(";;");

    QStringList layoutsmenu;

    for(const auto &layoutName : m_layoutsManager->synchronizer()->menuLayouts()) {
        if (m_layoutsManager->synchronizer()->centralLayout(layoutName)
                || m_layoutsManager->memoryUsage() == Latte::MemoryUsage::SingleLayout) {
            QStringList layoutdata;
            Data::LayoutIcon layouticon = m_layoutsManager->iconForLayout(layoutName);
            layoutdata << layoutName;
            layoutdata << QString::number(layouticon.isBackgroundFile);
            layoutdata << layouticon.name;
            layoutsmenu << layoutdata.join("**");
        }
    }

    data << layoutsmenu.join(";;");
    data << (view ? view->layout()->name() : QString());   //Selected View layout*/

    QStringList viewtype;
    viewtype << QString::number((int)viewType); //Selected View type

    if (view && view->isOriginal()) { /*View*/
        auto originalview = qobject_cast<Latte::OriginalView *>(view);
        viewtype << "0";              //original view
        viewtype <<  QString::number(originalview->clonesCount());
    } else if (view && view->isCloned()) {
        viewtype << "1";              //cloned view
        viewtype << "0";              //has no clones
    } else {
        viewtype << "0";              //original view
        viewtype << "0";              //has no clones
    }

    data << viewtype.join(";;");

    return data;
}

QStringList Corona::viewAppletsOrder(const uint &containmentId)
{
    auto view = m_layoutsManager->synchronizer()->viewForContainment(containmentId);

    if (!view || !view->extendedInterface()) {
        qWarning() << "corona: viewAppletsOrder queried for containment" << containmentId << "which has no view";
        return QStringList();
    }

    QStringList order;
    const QList<int> appletIds = view->extendedInterface()->appletsOrder();

    for (const int id : appletIds) {
        order << QString::number(id);
    }

    return order;
}

void Corona::setViewEditMode(const uint &containmentId, const bool &editing)
{
    auto view = m_layoutsManager->synchronizer()->viewForContainment(containmentId);

    if (!view) {
        qWarning() << "corona: setViewEditMode requested for containment" << containmentId << "which has no view";
        return;
    }

    if (editing) {
        //! the same ensemble the context menu's Edit Dock... lands in: that
        //! entry emits configureRequested -> View::showConfigurationInterface,
        //! which TOGGLES the chrome; showSettingsWindow() is the enter-only
        //! wrapper around the identical path (mustBeShown + configuration
        //! interface + window activities), already used by the global
        //! shortcut and by clones redirecting to their original. Idempotent
        //! when the settings chrome is already shown for this view.
        view->showSettingsWindow();
        return;
    }

    //! the same exit the settings chrome's close button uses
    //! (LatteDockConfiguration.qml -> viewConfig.hideConfigWindow()). The
    //! chrome is a shared singleton, so it may only be hidden when it is
    //! parented to THIS view - hiding it otherwise would end another view's
    //! editing session. Deliberately NOT gated on isVisible(): an exit
    //! racing the chrome's deferred show must still cancel it, which
    //! hideConfigWindow's cancelDeferredShow handles.
    auto configView = m_viewSettingsFactory->primaryConfigView();

    if (!view->inEditMode() || !configView || configView->parentView() != view) {
        qWarning() << "corona: setViewEditMode exit requested for containment" << containmentId << "which is not in edit mode";
        return;
    }

    configView->hideConfigWindow();
}

void Corona::activateTaskAt(const uint &containmentId, const int &index)
{
    auto view = m_layoutsManager->synchronizer()->viewForContainment(containmentId);

    if (!view) {
        qWarning() << "corona: activateTaskAt requested for containment" << containmentId << "which has no view";
        return;
    }

    //! the exact Meta+<index> path (docs/dbus-observability-interface.md):
    //! plasma-tasks fallback, wait-for-shown deferral while the dock is
    //! hidden, then the shortcuts host's activateEntryAtIndex - the same
    //! code a left-click on the entry drives. Index is therefore the
    //! 1-based visual entry index the shortcut badges show.
    if (!m_globalShortcuts->activateEntryForView(view, index, static_cast<Qt::Key>(Qt::META))) {
        qWarning() << "corona: activateTaskAt could not activate entry" << index << "on containment" << containmentId;
    }
}

QString Corona::viewAppletsData(const uint &containmentId)
{
    auto view = m_layoutsManager->synchronizer()->viewForContainment(containmentId);

    if (!view) {
        qWarning() << "corona: viewAppletsData queried for containment" << containmentId << "which has no view";
        return QStringLiteral("[]");
    }

    return DbusReports::collectAppletsData(view);
}

QString Corona::trackerData(const uint &containmentId)
{
    auto view = m_layoutsManager->synchronizer()->viewForContainment(containmentId);

    if (!view) {
        qWarning() << "corona: trackerData queried for containment" << containmentId << "which has no view";
        return QStringLiteral("{}");
    }

    return DbusReports::collectTrackerData(view);
}

void Corona::setViewVisibilityMode(const uint &containmentId, const QString &mode)
{
    auto view = m_layoutsManager->synchronizer()->viewForContainment(containmentId);

    if (!view) {
        qWarning() << "corona: setViewVisibilityMode requested for containment" << containmentId << "which has no view";
        return;
    }

    //! D-Bus strings are outside input, so an unknown name (and "none",
    //! which names the unset state rather than a settable mode) is refused
    //! loudly here at the boundary instead of tripping setMode's assert
    const auto visibilityMode = DbusReports::settableVisibilityModeFromName(mode);

    if (!visibilityMode) {
        qWarning() << "corona: setViewVisibilityMode received unknown visibility mode" << mode
                   << "for containment" << containmentId << "- expected one of the visibilityMode names viewsData() reports";
        return;
    }

    //! exactly what the settings Behavior combo does
    //! (latteView.visibility.mode = <mode>); persistence and the
    //! mode-switch machinery stay in their one home, VisibilityManager
    view->visibility()->setMode(*visibilityMode);
}

QString Corona::viewTasksData(const uint &containmentId)
{
    auto view = m_layoutsManager->synchronizer()->viewForContainment(containmentId);

    if (!view) {
        qWarning() << "corona: viewTasksData queried for containment" << containmentId << "which has no view";
        return QStringLiteral("[]");
    }

    return DbusReports::collectTasksData(view);
}

QString Corona::colorizerData(const uint &containmentId)
{
    auto view = m_layoutsManager->synchronizer()->viewForContainment(containmentId);

    if (!view) {
        qWarning() << "corona: colorizerData queried for containment" << containmentId << "which has no view";
        return QStringLiteral("{}");
    }

    return DbusReports::collectColorizerData(view);
}

QString Corona::layoutsData()
{
    return DbusReports::collectLayoutsData(m_layoutsManager);
}

QString Corona::viewsData()
{
    return DbusReports::collectViewsData(m_layoutsManager->synchronizer()->currentViews(),
                                         m_universalSettings->inConfigureAppletsMode());
}

QStringList Corona::viewTemplatesData()
{
    QStringList data;

    Latte::Data::GenericTable<Data::Generic> viewtemplates = m_templatesManager->viewTemplates();

    for(int i=0; i<viewtemplates.rowCount(); ++i) {
        data << viewtemplates[i].name;
        data << viewtemplates[i].id;
    }

    return data;
}

void Corona::addView(const uint &containmentId, const QString &templateId)
{
    if (containmentId <= 0) {
        auto currentlayouts = m_layoutsManager->currentLayouts();
        if (currentlayouts.count() > 0) {
            currentlayouts[0]->newView(templateId);
        }
    } else {
        auto view = m_layoutsManager->synchronizer()->viewForContainment((int)containmentId);
        if (view) {
            view->newView(templateId);
        }
    }
}

void Corona::duplicateView(const uint &containmentId)
{
    auto view = m_layoutsManager->synchronizer()->viewForContainment((int)containmentId);
    if (view) {
        view->duplicateView();
    }
}

void Corona::exportViewTemplate(const uint &containmentId)
{
    auto view = m_layoutsManager->synchronizer()->viewForContainment((int)containmentId);
    if (view) {
        view->exportTemplate();
    }
}

void Corona::moveViewToLayout(const uint &containmentId, const QString &layoutName)
{
    auto view = m_layoutsManager->synchronizer()->viewForContainment((int)containmentId);
    if (view && !layoutName.isEmpty() && view->layout()->name() != layoutName) {
        Latte::Types::ScreensGroup screensgroup{Latte::Types::SingleScreenGroup};

        if (view->isOriginal()) {
            auto originalview = qobject_cast<Latte::OriginalView *>(view);
            screensgroup = originalview->screensGroup();
        }

        view->positioner()->setNextLocation(layoutName, screensgroup, "", Plasma::Types::Floating, Latte::Types::NoneAlignment);
    }
}

void Corona::removeView(const uint &containmentId)
{
    auto view = m_layoutsManager->synchronizer()->viewForContainment((int)containmentId);
    if (view) {
        view->removeView();
    }
}

void Corona::setBackgroundFromBroadcast(QString activity, QString screenName, QString filename)
{
    if (filename.startsWith("file://")) {
        filename = filename.remove(0,7);
    }

    PlasmaExtended::BackgroundCache::self()->setBackgroundFromBroadcast(activity, screenName, filename);
}

void Corona::setBroadcastedBackgroundsEnabled(QString activity, QString screenName, bool enabled)
{
    PlasmaExtended::BackgroundCache::self()->setBroadcastedBackgroundsEnabled(activity, screenName, enabled);
}

void Corona::toggleHiddenState(QString layoutName, QString viewName, QString screenName, int screenEdge)
{
    if (layoutName.isEmpty()) {
        for(auto layout : m_layoutsManager->currentLayouts()) {
            layout->toggleHiddenState(viewName, screenName, (Plasma::Types::Location)screenEdge);
        }
    } else {
        Layout::GenericLayout *gLayout = layout(layoutName);

        if (gLayout) {
            gLayout->toggleHiddenState(viewName, screenName, (Plasma::Types::Location)screenEdge);
        }
    }
}

void Corona::importFullConfiguration(const QString &file)
{
    m_importFullConfigurationFile = file;
    quitApplication();
}

inline void Corona::qmlRegisterTypes() const
{   
    qmlRegisterUncreatableMetaObject(Latte::Settings::staticMetaObject,
                                     "org.kde.latte.private.app",          // import statement
                                     0, 1,                                 // major and minor version of the import
                                     "Settings",                           // name in QML
                                     "Error: only enums of latte app settings");

    qmlRegisterType<Latte::BackgroundTracker>("org.kde.latte.private.app", 0, 1, "BackgroundTracker");
    qmlRegisterType<Latte::Interfaces>("org.kde.latte.private.app", 0, 1, "Interfaces");
    qmlRegisterType<Latte::ContextMenuLayerQuickItem>("org.kde.latte.private.app", 0, 1, "ContextMenuLayer");
    qmlRegisterAnonymousType<QScreen>("latte-dock", 1);
    qmlRegisterAnonymousType<Latte::View>("latte-dock", 1);
    qmlRegisterAnonymousType<Latte::ViewPart::WindowsTracker>("latte-dock", 1);
    qmlRegisterAnonymousType<Latte::ViewPart::TrackerPart::CurrentScreenTracker>("latte-dock", 1);
    qmlRegisterAnonymousType<Latte::ViewPart::TrackerPart::AllScreensTracker>("latte-dock", 1);
    qmlRegisterAnonymousType<Latte::WindowSystem::SchemeColors>("latte-dock", 1);
    qmlRegisterAnonymousType<Latte::WindowSystem::Tracker::LastActiveWindow>("latte-dock", 1);
    qmlRegisterAnonymousType<Latte::Types>("latte-dock", 1);

}

}
