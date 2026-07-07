cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(lattedock)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(VERSION 0.10.77)
set(AUTHOR "Michail Vourlakos, Smith Ar")
set(EMAIL "mvourlakos@gmail.com, audoban@openmailbox.org")
set(WEBSITE "https://userbase.kde.org/LatteDock")
set(BUG_ADDRESS "https://bugs.kde.org/enter_bug.cgi?product=lattedock")
set(FAQS "https://userbase.kde.org/LatteDock/FAQ")

# Version floors: the oldest versions either reference fork demonstrably
# built against (latte-dock-qt6 targets KF6 6.5). Raise only when code needs
# a newer API, lower only with an actual build against the older version.
set(QT_MIN_VERSION "6.6.0")
set(KF6_MIN_VERSION "6.5.0")
set(PLASMA_MIN_VERSION "6.5.0")
set(KDE_COMPILERSETTINGS_LEVEL "6.0.0")
set(QT_MAJOR_VERSION 6)

set(KF5_LOCALE_PREFIX "")

find_package(ECM ${KF6_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(CheckIncludeFiles)
include(CMakePackageConfigHelpers)
include(ECMOptionalAddSubdirectory)
include(ECMQtDeclareLoggingCategory)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(KDEPackageAppTemplates)
include(WriteBasicConfigVersionFile)

# STUB: KDECompilerSettings at level 6.0.0 enables QT_NO_CAST_FROM_ASCII /
# QT_NO_CAST_FROM_BYTEARRAY, which the inherited Qt5-era code does not satisfy
# yet (~720 bare string literals). Removed here so the Phase 2 compile
# milestone isn't blocked on that churn; the dedicated QStringLiteral pass in
# Phase 2 of docs/PORTING_PLAN.md deletes these two lines again.
remove_definitions(-DQT_NO_CAST_FROM_ASCII)
remove_definitions(-DQT_NO_CAST_FROM_BYTEARRAY)

find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED NO_MODULE
    COMPONENTS Core DBus Gui Qml Quick Widgets)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 ${QT_MIN_VERSION} CONFIG REQUIRED COMPONENTS Test)
endif()

find_package(KF6 ${KF6_MIN_VERSION} REQUIRED COMPONENTS
    Archive Config CoreAddons Crash DBusAddons GlobalAccel GuiAddons
    I18n IconThemes KIO NewStuff Notifications Package Service Svg WindowSystem
    XmlGui)

# Transitively required by Plasma::Plasma's link interface; configure fails
# without it even though nothing here calls it directly.
find_package(KF6KirigamiPlatform ${KF6_MIN_VERSION} REQUIRED)

# De-umbrella'd from the KF5-era Plasma/PlasmaQuick/Activities components.
find_package(Plasma ${PLASMA_MIN_VERSION} REQUIRED)
find_package(PlasmaQuick ${PLASMA_MIN_VERSION} REQUIRED)
find_package(PlasmaActivities ${PLASMA_MIN_VERSION} REQUIRED)
find_package(KWayland ${PLASMA_MIN_VERSION} REQUIRED)
find_package(LayerShellQt ${PLASMA_MIN_VERSION} REQUIRED)

# For the vendored task-manager backend (Phase 6): process/launcher data.
find_package(KSysGuard ${PLASMA_MIN_VERSION} REQUIRED)
find_package(LibNotificationManager ${PLASMA_MIN_VERSION} REQUIRED)

find_package(LibTaskManager ${PLASMA_MIN_VERSION} REQUIRED)
find_package(PlasmaActivitiesStats ${PLASMA_MIN_VERSION} REQUIRED)
add_definitions(-DPLASMA_WORKSPACE_VERSION="${LibTaskManager_VERSION}")

# Best-effort X11 backend: Wayland is the primary target and the only one the
# maintainer runs; X11 must keep compiling but its absence never fails
# configure. Qt6 removed the X11Extras module - native X11 handles come from
# QNativeInterface::QX11Application, and the WId-based KWindowSystem APIs
# from KX11Extras (part of KF6WindowSystem, found above).
option(WITH_X11 "Build the best-effort X11 backend" ON)
set(HAVE_X11 OFF)
if(WITH_X11)
    find_package(X11)
    set_package_properties(X11 PROPERTIES DESCRIPTION "X11 libraries"
        URL "https://www.x.org"
        TYPE OPTIONAL
        PURPOSE "Required for the best-effort X11 backend")
    if(X11_FOUND)
        find_package(XCB MODULE COMPONENTS XCB RANDR SHAPE EVENT)
    endif()
    if(X11_FOUND AND X11_SM_FOUND AND XCB_FOUND)
        set(HAVE_X11 ON)
    else()
        message(STATUS "X11/SM/XCB development files incomplete - building without the X11 backend")
    endif()
endif()

find_package(QtWaylandScanner REQUIRED)
find_package(Qt6WaylandClient ${QT_MIN_VERSION} REQUIRED)
find_package(PlasmaWaylandProtocols 1.6 REQUIRED)
find_package(Wayland REQUIRED COMPONENTS Client)

# Detect whether LayerShellQt::Window::setScreen(QScreen*) exists - added in
# LayerShellQt 6.6, later removed upstream again. Fall back to plain
# QWindow::setScreen() when absent (see Phase 4 of docs/PORTING_PLAN.md).
try_compile(LATTE_LAYERSHELL_HAS_SET_SCREEN
    SOURCES ${CMAKE_SOURCE_DIR}/cmake/CheckLayerShellSetScreen.cpp
    LINK_LIBRARIES LayerShellQt::Interface Qt6::Core Qt6::Gui
)
if(LATTE_LAYERSHELL_HAS_SET_SCREEN)
    add_definitions(-DLATTE_LAYERSHELL_HAS_SET_SCREEN)
    message(STATUS "LayerShellQt::Window::setScreen(QScreen*): available")
else()
    message(STATUS "LayerShellQt::Window::setScreen(QScreen*): not available, using QWindow::setScreen()")
endif()

# Every KF5-minor feature gate in the inherited code tests "KF5 >= 5.x",
# which is unconditionally true on KF6. The gates themselves are removed in
# Phase 2; until then satisfy them all.
set(KF5_VERSION_MINOR "999")

message(STATUS "KF6 VERSION : ${KF6_VERSION}")
message(STATUS "Plasma (libplasma) VERSION : ${Plasma_VERSION}")

include(ECMFindQmlModule)
ecm_find_qmlmodule(QtQuick 2.7)
ecm_find_qmlmodule(QtQuick.Layouts 1.3)
ecm_find_qmlmodule(QtQuick.Controls 2.15)
ecm_find_qmlmodule(org.kde.kquickcontrolsaddons 2.0)
ecm_find_qmlmodule(org.kde.plasma.core 2.0)
ecm_find_qmlmodule(org.kde.plasma.components 3.0)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/app/config-latte.h.cmake
               ${CMAKE_CURRENT_BINARY_DIR}/app/config-latte.h)

#hide warnings
string(REPLACE "-Wall" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
string(REPLACE "-Wdeprecated-declarations" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
string(REPLACE "-Wreorder" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
string(REPLACE "-Wunused-variable" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})
string(REPLACE "-Wunused-parameter" "" CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS})

#add format security check
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wformat -Werror=format-security")

message(STATUS "COMPILER FLAGS : ${CMAKE_CXX_FLAGS}")

# Share Same Core Types between declarativeimports and app
set(CORETYPESHEADER "LIBCORETYPES_H")
configure_file(declarativeimports/coretypes.h.in declarativeimports/core/types.h)
set(CORETYPESHEADER "APPCORETYPES_H")
configure_file(declarativeimports/coretypes.h.in app/coretypes.h)
set(CORETYPESHEADER "LATTEGENERALCONTAINMENTTYPES_H")
configure_file(declarativeimports/coretypes.h.in containment/plugin/lattetypes.h)

# Share App::Settings::GenericTools to containment actions
configure_file(app/settings/generic/generictools.h containmentactions/contextmenu/generictools.h)
configure_file(app/settings/generic/generictools.cpp containmentactions/contextmenu/generictools.cpp)
configure_file(app/data/contextmenudata.h containmentactions/contextmenu/contextmenudata.h)

# subdirectories
add_subdirectory(declarativeimports)
add_subdirectory(indicators)
add_subdirectory(app)
add_subdirectory(containmentactions)
add_subdirectory(containment)
add_subdirectory(icons)
add_subdirectory(plasmoid)
add_subdirectory(shell)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

ki18n_install(po)
