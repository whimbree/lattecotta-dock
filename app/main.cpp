/*
    SPDX-FileCopyrightText: 2016 Smith AR <audoban@openmailbox.org>
    SPDX-FileCopyrightText: 2016 Michail Vourlakos <mvourlakos@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

// local
#include "config-latte.h"
#include "apptypes.h"
#include "lattecorona.h"
#include "layouts/importer.h"
#include "templates/templatesmanager.h"

// C++
#include <memory>
#include <csignal>

// Qt
#include <QApplication>
#include <QDebug>
#include <QQuickWindow>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDir>
#include <QFile>
#include <QLockFile>
#include <QSharedMemory>
#include <QTextStream>

// KDE
#include <KCrash>
#include <KLocalizedString>
#include <KAboutData>
#include <KDBusService>
#include <KSignalHandler>
#include <KWindowSystem>

//! COLORS
#define CNORMAL  "\e[0m"
#define CIGREEN  "\e[1;32m"
#define CGREEN   "\e[0;32m"
#define CICYAN   "\e[1;36m"
#define CCYAN    "\e[0;36m"
#define CIRED    "\e[1;31m"
#define CRED     "\e[0;31m"

inline void configureAboutData();
inline void detectPlatform(int argc, char **argv);
inline void filterDebugMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg);

QString filterDebugMessageText;
QString filterDebugLogFile;

int main(int argc, char **argv)
{
    //Plasma scales itself to font DPI
    //on X, where we don't have compositor scaling, this generally works fine.
    //also there are bugs on older Qt, especially when it comes to fractional scaling
    //there's advantages to disabling, and (other than small context menu icons) few advantages in enabling

    //On wayland, it's different. Everything is simpler as all coordinates are in the same coordinate system
    //we don't have fractional scaling on the client so don't hit most the remaining bugs and
    //even if we don't use Qt scaling the compositor will try to scale us anyway so we have no choice
    if (!qEnvironmentVariableIsSet("PLASMA_USE_QT_SCALING")) {
        qunsetenv("QT_DEVICE_PIXEL_RATIO");
        QCoreApplication::setAttribute(Qt::AA_DisableHighDpiScaling);
    } else {
        QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    }

    QQuickWindow::setDefaultAlphaBuffer(true);

    //! Latte manages its own autostart (KDBusService single-instance plus
    //! the autostart desktop entry); XSMP session restore would launch a
    //! second copy racing that, and on wayland Qt has no XSMP client at
    //! all. plasmashell disables the session manager the same way
    //! (plasma-workspace 6.6.5 shell/main.cpp). This also means
    //! commitDataRequest/saveStateRequest can never fire, so no handler
    //! for them exists here - session-end teardown arrives as SIGTERM
    //! from systemd instead (see the signal wiring below).
    QCoreApplication::setAttribute(Qt::AA_DisableSessionManager);

    //! Default to the THREADED scenegraph render loop, like plasmashell on
    //! the same systems. The port ran on the basic loop while the effect
    //! audit was open: Qt 6.11's threaded loop raced layer/ShaderEffect
    //! teardown when effect sources churned, and basic made those
    //! corruptions deterministic enough to root-cause. That audit is done -
    //! every effect source is a stable texture provider, shadows are
    //! layer.effect with static padding, transient effects exist only while
    //! visible - and the historical crash recipes (edit-mode toggles,
    //! context menus over churning previews, hover sweeps) run clean on
    //! threaded. The basic loop is not neutral: it serializes every
    //! window's polish/sync/render on the gui thread, measured as
    //! 400-600ms frame hitches while opening the clock's calendar popup
    //! (threaded: p99 6ms, max 41ms on the same interaction). KNOWN
    //! EXCEPTION tracked in the plan: WebEngine-backed applets (comic)
    //! free-run under threaded (~20% cpu per instance); until that is
    //! understood, QSG_RENDER_LOOP=basic remains available as an override.
    if (!qEnvironmentVariableIsSet("QSG_RENDER_LOOP")) {
        qputenv("QSG_RENDER_LOOP", "threaded");
    }

    //! Extra plugin dirs (the staged Latte plugin tree and allow-listed
    //! dependency leaves, see scripts/run-staged.sh) arrive through a
    //! LATTE_-namespaced variable instead of QT_PLUGIN_PATH on purpose:
    //! everything in this process's environment is forwarded verbatim to
    //! applications launched from the dock (KIO's systemd runner copies it
    //! into the transient unit's Environment= property), and a child of a
    //! different Qt build dlopening our pinned plugins is an ABI crash
    //! waiting to happen. A LATTE_ variable is inert for children; feed it
    //! into the process-local library path list instead. addLibraryPath()
    //! prepends, so iterate in reverse to keep the first entry the highest
    //! priority, matching QT_PLUGIN_PATH semantics.
    const QStringList extraPluginPaths = qEnvironmentVariable("LATTE_EXTRA_PLUGIN_PATHS").split(QLatin1Char(':'), Qt::SkipEmptyParts);
    for (auto it = extraPluginPaths.crbegin(); it != extraPluginPaths.crend(); ++it) {
        QCoreApplication::addLibraryPath(*it);
    }

    qputenv("QT_WAYLAND_DISABLE_FIXED_POSITIONS", {});
    const bool qpaVariable = qEnvironmentVariableIsSet("QT_QPA_PLATFORM");
    detectPlatform(argc, argv);
    QApplication app(argc, argv);
    qunsetenv("QT_WAYLAND_DISABLE_FIXED_POSITIONS");

    //! Wayland is the only platform Latte can drive: every view is a
    //! layer-shell surface, so anywhere else nothing anchors, struts
    //! reserve nothing and window tracking no-ops - a dock that "starts"
    //! there is silent wrong behavior, not compatibility (X11 backend
    //! removed 2026-07-17, decision record in the porting plan's Phase 4).
    //! Default-deny rather than enumerating bad platforms, so anything new
    //! is refused until it is deliberately supported; the offscreen QPA is
    //! the one exception - it is the test harness's platform and pretends
    //! to drive no display server. When stderr is not a tty Qt routes the
    //! refusal to the journal, exactly where a failed login autostart gets
    //! looked for.
    if (!KWindowSystem::isPlatformWayland() && app.platformName() != QLatin1String("offscreen")) {
        qCritical() << "Latte requires a Wayland session (the X11 backend was removed;"
                       " KDE's own X11 session ends with Plasma 6.7)."
                    << "Refusing to start on platform" << app.platformName();
        return 1;
    }

    if (!qpaVariable) {
        // don't leak the env variable to processes we start
        qunsetenv("QT_QPA_PLATFORM");
    }


    KLocalizedString::setApplicationDomain("latte-dock");
    app.setWindowIcon(QIcon::fromTheme(QStringLiteral("latte-dock")));
    //protect from closing app when changing to "alternative session" and back
    app.setQuitOnLastWindowClosed(false);

    configureAboutData();

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOptions({
                          {{"r", "replace"}, i18nc("command line", "Replace the current Latte instance.")}
                          , {{"d", "debug"}, i18nc("command line", "Show the debugging messages on stdout.")}
                          , {{"cc", "clear-cache"}, i18nc("command line", "Clear qml cache. It can be useful after system upgrades.")}
                          , {"enable-autostart", i18nc("command line", "Enable autostart for this application")}
                          , {"disable-autostart", i18nc("command line", "Disable autostart for this application")}
                          , {"default-layout", i18nc("command line", "Import and load default layout on startup.")}
                          , {"available-layouts", i18nc("command line", "Print available layouts")}
                          , {"available-dock-templates", i18nc("command line", "Print available dock templates")}
                          , {"available-layout-templates", i18nc("command line", "Print available layout templates")}
                          , {"layout", i18nc("command line", "Load specific layout on startup."), i18nc("command line: load", "layout_name")}
                          , {"import-layout", i18nc("command line", "Import and load a layout."), i18nc("command line: import", "absolute_filepath")}
                          , {"suggested-layout-name", i18nc("command line", "Suggested layout name when importing a layout file"), i18nc("command line: import", "suggested_name")}
                          , {"import-full", i18nc("command line", "Import full configuration."), i18nc("command line: import", "file_name")}
                          , {"add-dock", i18nc("command line", "Add Dock/Panel"), i18nc("command line: add", "template_name")}
                          , {"single", i18nc("command line", "Single layout memory mode. Only one layout is active at any case.")}
                          , {"multiple", i18nc("command line", "Multiple layouts memory mode. Multiple layouts can be active at any time based on Activities running.")}
                      });

    //! START: Hidden options for Developer and Debugging usage
    QCommandLineOption graphicsOption(QStringList() << QStringLiteral("graphics"));
    graphicsOption.setDescription(QStringLiteral("Draw boxes around of the applets."));
    graphicsOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(graphicsOption);

    QCommandLineOption withWindowOption(QStringList() << QStringLiteral("with-window"));
    withWindowOption.setDescription(QStringLiteral("Open a window with much debug information"));
    withWindowOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(withWindowOption);

    QCommandLineOption maskOption(QStringList() << QStringLiteral("mask"));
    maskOption.setDescription(QStringLiteral("Show messages of debugging for the mask (Only useful to devs)."));
    maskOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(maskOption);

    QCommandLineOption timersOption(QStringList() << QStringLiteral("timers"));
    timersOption.setDescription(QStringLiteral("Show messages for debugging the timers (Only useful to devs)."));
    timersOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(timersOption);

    QCommandLineOption spacersOption(QStringList() << QStringLiteral("spacers"));
    spacersOption.setDescription(QStringLiteral("Show visual indicators for debugging spacers (Only useful to devs)."));
    spacersOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(spacersOption);

    QCommandLineOption overloadedIconsOption(QStringList() << QStringLiteral("overloaded-icons"));
    overloadedIconsOption.setDescription(QStringLiteral("Show visual indicators for debugging overloaded applets icons (Only useful to devs)."));
    overloadedIconsOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(overloadedIconsOption);

    QCommandLineOption edgesOption(QStringList() << QStringLiteral("kwinedges"));
    edgesOption.setDescription(QStringLiteral("Show visual window indicators for hidden screen edge windows."));
    edgesOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(edgesOption);

    QCommandLineOption localGeometryOption(QStringList() << QStringLiteral("localgeometry"));
    localGeometryOption.setDescription(QStringLiteral("Show visual window indicators for calculated local geometry."));
    localGeometryOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(localGeometryOption);

    QCommandLineOption layouterOption(QStringList() << QStringLiteral("layouter"));
    layouterOption.setDescription(QStringLiteral("Show visual debug tags for items sizes."));
    layouterOption.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(layouterOption);

    QCommandLineOption filterDebugTextOption(QStringList() << QStringLiteral("debug-text"));
    filterDebugTextOption.setDescription(QStringLiteral("Show only debug messages that contain specific text."));
    filterDebugTextOption.setFlags(QCommandLineOption::HiddenFromHelp);
    filterDebugTextOption.setValueName(i18nc("command line: debug-text", "filter_debug_text"));
    parser.addOption(filterDebugTextOption);

    QCommandLineOption filterDebugInputMask(QStringList() << QStringLiteral("input"));
    filterDebugInputMask.setDescription(QStringLiteral("Show visual window indicators for calculated input mask."));
    filterDebugInputMask.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(filterDebugInputMask);

    QCommandLineOption filterDebugEventSinkMask(QStringList() << QStringLiteral("events-sink"));
    filterDebugEventSinkMask.setDescription(QStringLiteral("Show visual indicators for areas of EventsSink."));
    filterDebugEventSinkMask.setFlags(QCommandLineOption::HiddenFromHelp);
    parser.addOption(filterDebugEventSinkMask);

    QCommandLineOption filterDebugLogCmd(QStringList() << QStringLiteral("log-file"));
    filterDebugLogCmd.setDescription(QStringLiteral("Forward debug output in a log text file."));
    filterDebugLogCmd.setFlags(QCommandLineOption::HiddenFromHelp);
    filterDebugLogCmd.setValueName(i18nc("command line: log-filepath", "filter_log_filepath"));
    parser.addOption(filterDebugLogCmd);
    //! END: Hidden options

    parser.process(app);

    if (parser.isSet(QStringLiteral("enable-autostart"))) {
        Latte::Layouts::Importer::enableAutostart();
    }

    if (parser.isSet(QStringLiteral("disable-autostart"))) {
        Latte::Layouts::Importer::disableAutostart();
        qGuiApp->exit();
        return 0;
    }

    //! print available-layouts
    if (parser.isSet(QStringLiteral("available-layouts"))) {
        QStringList layouts = Latte::Layouts::Importer::availableLayouts();

        if (layouts.count() > 0) {
            qInfo() << i18n("Available layouts that can be used to start Latte:");

            for (const auto &layout : layouts) {
                qInfo() << "     " << layout;
            }
        } else {
            qInfo() << i18n("There are no available layouts, during startup Default will be used.");
        }

        qGuiApp->exit();
        return 0;
    }

    //! print available-layout-templates
    if (parser.isSet(QStringLiteral("available-layout-templates"))) {
        QStringList templates = Latte::Layouts::Importer::availableLayoutTemplates();

        if (templates.count() > 0) {
            qInfo() << i18n("Available layout templates found in your system:");

            for (const auto &templatename : templates) {
                qInfo() << "     " << templatename;
            }
        } else {
            qInfo() << i18n("There are no available layout templates in your system.");
        }

        qGuiApp->exit();
        return 0;
    }

    //! print available-dock-templates
    if (parser.isSet(QStringLiteral("available-dock-templates"))) {
        QStringList templates = Latte::Layouts::Importer::availableViewTemplates();

        if (templates.count() > 0) {
            qInfo() << i18n("Available dock templates found in your system:");

            for (const auto &templatename : templates) {
                qInfo() << "     " << templatename;
            }
        } else {
            qInfo() << i18n("There are no available dock templates in your system.");
        }

        qGuiApp->exit();
        return 0;
    }

    //! choose layout for startup
    bool defaultLayoutOnStartup = false;
    int memoryUsage = -1;
    QString layoutNameOnStartup = "";
    QString addViewTemplateNameOnStartup = "";

    //! --default-layout option
    if (parser.isSet(QStringLiteral("default-layout"))) {
        defaultLayoutOnStartup = true;
    } else if (parser.isSet(QStringLiteral("layout"))) {
        layoutNameOnStartup = parser.value(QStringLiteral("layout"));

        if (!Latte::Layouts::Importer::layoutExists(layoutNameOnStartup)) {
            qInfo() << i18nc("layout missing", "This layout doesn't exist in the system.");
            qGuiApp->exit();
            return 0;
        }
    }

    //! --replace option
    QString username = qgetenv("USER");

    if (username.isEmpty())
        username = qgetenv("USERNAME");

    QLockFile lockFile {QDir::tempPath() + "/latte-dock." + username + ".lock"};

    int timeout {100};

    if (parser.isSet(QStringLiteral("replace")) || parser.isSet(QStringLiteral("import-full"))) {
        qint64 pid{ -1};

        if (lockFile.getLockInfo(&pid, nullptr, nullptr)) {
            kill(static_cast<pid_t>(pid), SIGINT);
            timeout = -1;
        }
    }

    if (!lockFile.tryLock(timeout)) {
        QDBusInterface iface("org.kde.lattedock", "/Latte", "", QDBusConnection::sessionBus());
        bool addview{parser.isSet(QStringLiteral("add-dock"))};
        bool importlayout{parser.isSet(QStringLiteral("import-layout"))};
        bool enableautostart{parser.isSet(QStringLiteral("enable-autostart"))};
        bool disableautostart{parser.isSet(QStringLiteral("disable-autostart"))};

        bool validaction{false};

        if (iface.isValid()) {
            if (addview) {
                validaction = true;
                iface.call("addView", (uint)0, parser.value(QStringLiteral("add-dock")));
                qGuiApp->exit();
                return 0;
            } else if (importlayout) {
                validaction = true;
                QString suggestedname = parser.isSet(QStringLiteral("suggested-layout-name")) ? parser.value(QStringLiteral("suggested-layout-name")) : QString();
                iface.call("importLayoutFile", parser.value(QStringLiteral("import-layout")), suggestedname);
                qGuiApp->exit();
                return 0;
            } else if (enableautostart || disableautostart){
                validaction = true;
            } else {
                // LayoutPage = 0
                iface.call("showSettingsWindow", 0);
            }
        }

        if (!validaction) {
            qInfo() << i18n("An instance is already running!, use --replace to restart Latte");
        }

        qGuiApp->exit();
        return 0;
    }

    //! clear-cache option
    if (parser.isSet(QStringLiteral("clear-cache"))) {
        QDir cacheDir(QDir::homePath() + "/.cache/lattedock/qmlcache");

        if (cacheDir.exists()) {
            cacheDir.removeRecursively();
            qDebug() << "Cache directory found and cleared...";
        }
    }

    //! import-full option
    if (parser.isSet(QStringLiteral("import-full"))) {
        bool imported = Latte::Layouts::Importer::importHelper(parser.value(QStringLiteral("import-full")));

        if (!imported) {
            qInfo() << i18n("The configuration cannot be imported");
            qGuiApp->exit();
            return 0;
        }
    }

    //! import-layout option
    if (parser.isSet(QStringLiteral("import-layout"))) {
        QString suggestedname = parser.isSet(QStringLiteral("suggested-layout-name")) ? parser.value(QStringLiteral("suggested-layout-name")) : QString();
        QString importedLayout = Latte::Layouts::Importer::importLayoutHelper(parser.value(QStringLiteral("import-layout")), suggestedname);

        if (importedLayout.isEmpty()) {
            qInfo() << i18n("The layout cannot be imported");
            qGuiApp->exit();
            return 0;
        } else {
            layoutNameOnStartup = importedLayout;
        }
    }

    //! memory usage option
    if (parser.isSet(QStringLiteral("multiple"))) {
        memoryUsage = (int)(Latte::MemoryUsage::MultipleLayouts);
    } else if (parser.isSet(QStringLiteral("single"))) {
        memoryUsage = (int)(Latte::MemoryUsage::SingleLayout);
    }

    //! add-dock usage option
    if (parser.isSet(QStringLiteral("add-dock"))) {
        QString viewTemplateName = parser.value(QStringLiteral("add-dock"));
        QStringList viewTemplates = Latte::Layouts::Importer::availableViewTemplates();

        if (viewTemplates.contains(viewTemplateName)) {
            if (layoutNameOnStartup.isEmpty()) {
                //! Clean layout template is applied and proper name is used
                QString emptytemplatepath = Latte::Layouts::Importer::layoutTemplateSystemFilePath(Latte::Templates::EMPTYLAYOUTTEMPLATENAME);
                QString suggestedname = parser.isSet(QStringLiteral("suggested-layout-name")) ? parser.value(QStringLiteral("suggested-layout-name")) : viewTemplateName;
                QString importedLayout = Latte::Layouts::Importer::importLayoutHelper(emptytemplatepath, suggestedname);

                if (importedLayout.isEmpty()) {
                    qInfo() << i18n("The layout cannot be imported");
                    qGuiApp->exit();
                    return 0;
                } else {
                    layoutNameOnStartup = importedLayout;
                }
            }

            addViewTemplateNameOnStartup = viewTemplateName;
        }
    }

    //! text filter for debug messages
    if (parser.isSet(QStringLiteral("debug-text"))) {
        filterDebugMessageText = parser.value(QStringLiteral("debug-text"));
    }

    //! log file for debug output
    if (parser.isSet(QStringLiteral("log-file")) && !parser.value(QStringLiteral("log-file")).isEmpty()) {
        filterDebugLogFile = parser.value(QStringLiteral("log-file"));
    }

    //! debug/mask options
    if (parser.isSet(QStringLiteral("debug")) || parser.isSet(QStringLiteral("mask")) || parser.isSet(QStringLiteral("debug-text"))) {
        qInstallMessageHandler(filterDebugMessageOutput);
    } else {
        //! Deviation from upstream's fully-silent handler: Critical and
        //! Fatal always reach stderr (and thus the journal). The tree's
        //! loud-refusal boundaries (qCritical-and-refuse at API borders)
        //! are pointless if production swallows them - the invalid-color
        //! hunt of 2026-07-16 chased a "throwaway-only" defect for a full
        //! session leg because the real config was silently producing the
        //! same refusals. Debug/Info/Warning stay quiet as before.
        const auto criticalOnlyOutput = [](QtMsgType type, const QMessageLogContext &, const QString &msg) {
            if (type == QtCriticalMsg || type == QtFatalMsg) {
                fprintf(stderr, "%s\n", qPrintable(msg));
            }
        };
        qInstallMessageHandler(criticalOnlyOutput);
    }

    //! KJob holds a QEventLoopLocker; with the quit lock enabled, the LAST
    //! locker being destroyed quits the application, so any transient KJob
    //! (KIO from an applet, a KNS download) finishing could take the dock
    //! down with it. plasmashell disables it for the same reason
    //! ("don't let the first KJob terminate us", plasma-workspace 6.6.5).
    QCoreApplication::setQuitLockEnabled(false);

    //! Every quit path must name itself in the log. The dock exited cleanly
    //! ~20s after screen lock/unlock cycles twice (2026-07-10..12) with no
    //! logged trigger, and a raw std::signal handler calling qGuiApp->exit()
    //! was one of three silent candidates (the others: Corona::quitApplication
    //! over D-Bus, and QCoreApplication::quit() exposed on the bus at
    //! /MainApplication by KDBusService). KSignalHandler self-pipes, so the
    //! log call runs on the event loop, not inside the signal handler.
    //! SIGINT is also the --replace handshake (see the kill() above).
    //! SIGTERM is handled since the session-shutdown work: at logout,
    //! systemd tears the user session down with SIGTERM (systemd.kill(5)),
    //! and unhandled that meant immediate death mid-anything - no config
    //! sync, no containment save (the iconSize=78 corruption in my real
    //! layout was written by exactly such a kill during edit mode).
    //! plasmashell quits on SIGTERM the same way. restart-staged.sh still
    //! works: it allows 10s before escalating to SIGKILL and the clean
    //! quit path is well under that.
    //! std::signal(SIGKILL, ...) was dead code - SIGKILL cannot be caught.
    KSignalHandler::self()->watchSignal(SIGINT);
    KSignalHandler::self()->watchSignal(SIGTERM);
    QObject::connect(KSignalHandler::self(), &KSignalHandler::signalReceived, &app, [](int signal) {
        if (signal == SIGTERM) {
            qWarning() << "main: quitting on SIGTERM (session teardown, service stop, or restart-staged)";
        } else {
            qWarning() << "main: quitting on signal" << signal << "(SIGINT: terminal, --replace handshake, or external kill)";
        }
        qGuiApp->quit();
    });

    KCrash::setDrKonqiEnabled(true);
    KCrash::setFlags(KCrash::AutoRestart | KCrash::AlwaysDirectly);

    //! Claim single-instance ownership BEFORE building the Corona. If another
    //! process already owns org.kde.lattedock (e.g. a differently-installed
    //! Latte whose lock file we didn't match), KDBusService::Unique forwards to
    //! it and this instance must exit. Doing that here, before the Corona and
    //! the theme/KSvg singletons it constructs exist, avoids the segfault we
    //! otherwise hit tearing those statics down on the duplicate-exit path
    //! (a static KSvg::Svg destructor -> eraseRenderer()). See REVIEW_NOTES.
    KDBusService service(KDBusService::Unique);

    if (!service.isRegistered()) {
        return 0;
    }

    Latte::Corona corona(defaultLayoutOnStartup, layoutNameOnStartup, addViewTemplateNameOnStartup, memoryUsage);

    return app.exec();
}

inline void filterDebugMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    if (msg.endsWith("QML Binding: Not restoring previous value because restoreMode has not been set.This behavior is deprecated.In Qt < 6.0 the default is Binding.RestoreBinding.In Qt >= 6.0 the default is Binding.RestoreBindingOrValue.")
        || msg.endsWith("QML Binding: Not restoring previous value because restoreMode has not been set.\nThis behavior is deprecated.\nYou have to import QtQml 2.15 after any QtQuick imports and set\nthe restoreMode of the binding to fix this warning.\nIn Qt < 6.0 the default is Binding.RestoreBinding.\nIn Qt >= 6.0 the default is Binding.RestoreBindingOrValue.\n")
        || msg.endsWith("QML Binding: Not restoring previous value because restoreMode has not been set.\nThis behavior is deprecated.\nYou have to import QtQml 2.15 after any QtQuick imports and set\nthe restoreMode of the binding to fix this warning.\nIn Qt < 6.0 the default is Binding.RestoreBinding.\nIn Qt >= 6.0 the default is Binding.RestoreBindingOrValue.")
        || msg.endsWith("QML Connections: Implicitly defined onFoo properties in Connections are deprecated. Use this syntax instead: function onFoo(<arguments>) { ... }")) {
        //! block warnings because they will be needed only after qt6.0 support. Currently Binding.restoreMode can not be supported because
        //! qt5.9 is the minimum supported version.
        return;
    }

    if (!filterDebugMessageText.isEmpty() && !msg.contains(filterDebugMessageText)) {
        return;
    }

    const char *function = context.function ? context.function : "";

    QString typeStr;
    switch (type) {
    case QtDebugMsg:
        typeStr = "Debug";
        break;
    case QtInfoMsg:
        typeStr = "Info";
        break;
    case QtWarningMsg:
        typeStr = "Warning" ;
        break;
    case QtCriticalMsg:
        typeStr = "Critical";
        break;
    case QtFatalMsg:
        typeStr = "Fatal";
        break;
    };

    const char *TypeColor;

    if (type == QtInfoMsg || type == QtWarningMsg) {
        TypeColor = CGREEN;
    } else if (type == QtCriticalMsg || type == QtFatalMsg) {
        TypeColor = CRED;
    } else {
        TypeColor = CIGREEN;
    }

    if (filterDebugLogFile.isEmpty()) {
        qDebug().nospace() << TypeColor << "[" << typeStr.toStdString().c_str() << " : " << CGREEN << QTime::currentTime().toString("h:mm:ss.zz").toStdString().c_str() << TypeColor << "]" << CNORMAL
                          #ifndef QT_NO_DEBUG
                           << CIRED << " [" << CCYAN << function << CIRED << ":" << CCYAN << context.line << CIRED << "]"
                          #endif
                           << CICYAN << " - " << CNORMAL << msg;
    } else {
        QFile logfile(filterDebugLogFile);
        logfile.open(QIODevice::WriteOnly | QIODevice::Append);
        QTextStream logts(&logfile);
        logts << "[" << typeStr.toStdString().c_str() << " : " << QTime::currentTime().toString("h:mm:ss.zz").toStdString().c_str() << "]"
              <<  " - " << msg << Qt::endl;
    }
}

inline void configureAboutData()
{
    KAboutData about(QStringLiteral("lattedock")
                     , QStringLiteral("Latte Dock")
                     , QStringLiteral(VERSION)
                     , i18n("Latte is a dock based on plasma frameworks that provides an elegant and "
                            "intuitive experience for your tasks and plasmoids. It animates its contents "
                            "by using parabolic zoom effect and tries to be there only when it is needed."
                            "\n\n\"Art in Coffee\"")
                     , KAboutLicense::GPL_V2
                     , QStringLiteral("\251 2016-2017 Michail Vourlakos, Smith AR"));

    about.setHomepage(WEBSITE);
    about.setProgramLogo(QIcon::fromTheme(QStringLiteral("latte-dock")));
    about.setDesktopFileName(QStringLiteral("latte-dock"));
    about.setProductName(QByteArray("lattedock"));

    // Authors
    about.addAuthor(QStringLiteral("Michail Vourlakos"), QString(), QStringLiteral("mvourlakos@gmail.com"));
    about.addAuthor(QStringLiteral("Smith AR"), QString(), QStringLiteral("audoban@openmailbox.org"));

    KAboutData::setApplicationData(about);
}

//! used the version provided by PW:KWorkspace
inline void detectPlatform(int argc, char **argv)
{
    if (qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
        return;
    }

    for (int i = 0; i < argc; i++) {
        if (qstrcmp(argv[i], "-platform") == 0 ||
                qstrcmp(argv[i], "--platform") == 0 ||
                QByteArray(argv[i]).startsWith("-platform=") ||
                QByteArray(argv[i]).startsWith("--platform=")) {
            return;
        }
    }

    const QByteArray sessionType = qgetenv("XDG_SESSION_TYPE");

    if (sessionType.isEmpty()) {
        return;
    }

    if (qstrcmp(sessionType, "wayland") == 0) {
        qputenv("QT_QPA_PLATFORM", "wayland");
    }
    //! an x11 session gets no platform hint: Qt resolves xcb on its own and
    //! the isPlatformX11() boundary check after QApplication refuses with the
    //! real message - selecting xcb here would read as supporting it
}
