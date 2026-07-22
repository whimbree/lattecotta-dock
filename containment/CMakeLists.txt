add_definitions(-DTRANSLATION_DOMAIN=\"plasma_applet_org.kde.latte.containment\")

plasma_install_package(package org.kde.latte.containment)

set(containment_SRCS
    plugin/lattetypes.h
    plugin/types.cpp
    plugin/autosizestepper.cpp
    plugin/filldistributortools.cpp
    plugin/colorizerdecider.cpp
    plugin/backgroundstateresolver.cpp
    plugin/maskgeometrybridge.cpp
    plugin/placementnormalizer.cpp
    plugin/layoutmanager.cpp
    plugin/lattecontainmentplugin.cpp
)

add_library(lattecontainmentplugin SHARED ${containment_SRCS})

target_link_libraries(lattecontainmentplugin
                      Qt6::Core
                      Qt6::Qml
                      Qt6::Quick
                      KF6::ConfigQml
                      Plasma::Plasma
                      Plasma::PlasmaQuick)

target_include_directories(lattecontainmentplugin PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/app)

install(TARGETS lattecontainmentplugin DESTINATION ${KDE_INSTALL_QMLDIR}/org/kde/latte/private/containment)

# plugins.qmltypes feeds qmllint/tooling type resolution (the step-2.5
# qmllint ratchet); regenerate after registering new types in
# plugin/lattecontainmentplugin.cpp:
#   qmlplugindump -nonrelocatable org.kde.latte.private.containment 0.1
# (offscreen, with QML2_IMPORT_PATH at the staged tree - the gate fails
# on new unresolved-type warnings if this file goes stale)
install(FILES plugin/qmldir plugin/plugins.qmltypes DESTINATION ${KDE_INSTALL_QMLDIR}/org/kde/latte/private/containment)
