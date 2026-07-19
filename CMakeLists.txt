cmake_minimum_required(VERSION 3.16 FATAL_ERROR)
project(lattedock)

# C++20, verified not assumed (step 2.5 in the extraction plan): the KF6
# pin builds this tree at 20 (checked with full build-check; at the time
# that meant both WITH_X11 variants, since removed - the tree is
# Wayland-only); KDECompilerSettings at level 6.0.0 would only pick
# 17 on its own (it defaults to 20 from level 6.13), so the standard is
# pinned here explicitly. Pure cores are consumed by app targets and use
# this same standard - never fork it per-target.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Always emit build/compile_commands.json so clangd (and any other
# libclang tool) can parse the tree with the real include paths, defines
# and C++ standard the build uses, instead of a standalone best-guess that
# floods every Qt/KF6 include with false "file not found" errors. This is a
# pure side artifact: the Ninja generator writes the JSON but the compile
# rules are byte-for-byte unchanged, so binaries stay identical (verified
# OFF->ON with a diff of the built .so/binaries). Repo-root .clangd points
# clangd at build/ for this file; docs/reference/clangd-setup.md has the contributor
# flow. Ninja/Makefile generators honour this; ours is Ninja.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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

# The headless QML gates (scripts/lib-qml-env.sh) stage the built modules to a
# throwaway prefix and must add the staged qml subdir to the import path
# BEFORE that prefix is installed, so they cannot probe the filesystem for it.
# KDE_INSTALL_QMLDIR is the authoritative subdir but varies by distro Qt6
# layout (lib/qml on nixpkgs, lib/qt6/qml on Arch/Fedora/Debian) and is empty
# in the CMake cache; emit its configure-time value where the harness reads it.
file(WRITE "${CMAKE_BINARY_DIR}/latte-qmldir.txt" "${KDE_INSTALL_QMLDIR}\n")

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
# Phase 2 of docs/tracking/PORTING_PLAN.md deletes these two lines again.
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

find_package(QtWaylandScanner REQUIRED)
find_package(Qt6WaylandClient ${QT_MIN_VERSION} REQUIRED)
find_package(PlasmaWaylandProtocols 1.6 REQUIRED)
find_package(Wayland REQUIRED COMPONENTS Client)

# Detect whether LayerShellQt::Window::setScreen(QScreen*) exists - added in
# LayerShellQt 6.6, later removed upstream again. Fall back to plain
# QWindow::setScreen() when absent (see Phase 4 of docs/tracking/PORTING_PLAN.md).
#
# The raw try_compile re-ran on every configure and adopted whatever that
# run said, so a single configure in a broken environment (system cmake
# outside the devshell, torn toolchain) could silently flip the define and
# layer-shell output pinning would vanish without a word. Two defenses:
# - the only failure accepted as a real "API absent" answer is the
#   compiler reporting the setScreen member missing; any other probe
#   failure (missing header, dead compiler, link failure) is an
#   environment defect and configuration stops loudly.
# - the answer is cached keyed on LayerShellQt_DIR, so a plain
#   reconfigure reuses it and re-pinning layer-shell-qt (a new store
#   path) re-probes automatically.
if(NOT DEFINED CACHE{LATTE_LAYERSHELL_HAS_SET_SCREEN}
        OR NOT "$CACHE{LATTE_LAYERSHELL_SET_SCREEN_PROBED_DIR}" STREQUAL "${LayerShellQt_DIR}")
    try_compile(_latte_layershell_has_setscreen
        SOURCES ${CMAKE_SOURCE_DIR}/cmake/CheckLayerShellSetScreen.cpp
        LINK_LIBRARIES LayerShellQt::Interface Qt6::Core Qt6::Gui
        OUTPUT_VARIABLE _latte_layershell_setscreen_output
    )
    if(NOT _latte_layershell_has_setscreen
            AND NOT _latte_layershell_setscreen_output MATCHES "no member named.*setScreen")
        message(FATAL_ERROR
            "The LayerShellQt::Window::setScreen(QScreen*) probe failed for a reason "
            "other than the member being absent. That is an environment defect - "
            "most likely a non-devshell toolchain (system cmake outside 'nix develop') "
            "or a torn dependency set - and proceeding would silently disable "
            "layer-shell output pinning. Probe output:\n"
            "${_latte_layershell_setscreen_output}")
    endif()
    set(LATTE_LAYERSHELL_HAS_SET_SCREEN ${_latte_layershell_has_setscreen}
        CACHE INTERNAL "LayerShellQt::Window::setScreen(QScreen*) exists in the found layer-shell-qt")
    set(LATTE_LAYERSHELL_SET_SCREEN_PROBED_DIR "${LayerShellQt_DIR}"
        CACHE INTERNAL "LayerShellQt_DIR the setScreen probe last ran against")
endif()
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

# Sanitized-dock build (docs/tracking/ub-catching-plan.md, Prong A). LATTE_SANITIZE=ON
# compiles AND links every target in this tree - the dock and the plugins that
# dlopen into it (containment, core, tasks, the context-menu containment
# action) - with ASan+UBSan, and forces Q_ASSERT live via QT_FORCE_ASSERTS, so
# OUR-code UB and tripped invariants abort with a stack during real driving
# instead of running silently. The motivating bug was an insert(-1) from a
# stale splitter position that vanished a justify splitter in the field with
# no diagnostic (RelWithDebInfo strips Q_ASSERT, ships no sanitizer).
#
# This is a build-system change with blast radius, so it follows the C++17->20
# bump discipline: default OFF, and the normal build is BYTE-UNCHANGED when OFF
# (the block below is skipped entirely - the only always-run additions are an
# inert variable and an OFF option). It is meant for a dedicated out-of-tree
# build (build-asan, scripts/run-asan-dock.sh / start-dock-sanitized.sh), never
# the packaged build (package.nix stays nixpkgs Release) and never the normal
# RelWithDebInfo dev tree. -fno-sanitize-recover=all makes the first finding an
# abort, not a log line.
#
# LATTE_SANITIZE_FLAGS is the SINGLE SOURCE OF TRUTH for the sanitizer flag
# set, shared with the sanitized unit tests (tests/units/CMakeLists.txt reads
# it), so the dock and the tests can never drift on what "sanitized" means.
set(LATTE_SANITIZE_FLAGS -fsanitize=address,undefined -fno-sanitize-recover=all)
option(LATTE_SANITIZE "Build the dock and its plugins with ASan+UBSan and forced Q_ASSERTs (out-of-tree build-asan; never the packaged or normal dev build)" OFF)
if(LATTE_SANITIZE)
    message(STATUS "LATTE_SANITIZE: ON - dock + plugins built with '${LATTE_SANITIZE_FLAGS}' and QT_FORCE_ASSERTS")
    add_compile_options(${LATTE_SANITIZE_FLAGS})
    add_link_options(${LATTE_SANITIZE_FLAGS})
    # RelWithDebInfo defines QT_NO_DEBUG/NDEBUG, which strips Q_ASSERT
    # wholesale; without this our invariant asserts would be dead code exactly
    # in the sanitized build meant to exercise them (the same reason
    # latte_add_unit_test sets it on the test binaries).
    add_compile_definitions(QT_FORCE_ASSERTS)
endif()

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
