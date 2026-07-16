configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config-latte-lib.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config-latte-lib.h)

set(lattecoreplugin_SRCS
    lattecoreplugin.cpp
    dialog.cpp
    environment.cpp
    iconitem.cpp
    parabolicmathtools.cpp
    parabolicroutertools.cpp
    quickwindowsystem.cpp
    tools.cpp
    types.h
    visibleindextools.cpp
    windowresizehandler.cpp
)

# dialog.cpp uses PlasmaQuick's PlasmaShellWaylandIntegration, whose public
# header includes the generated qwayland-plasma-shell.h; generate it here the
# same way libplasma does internally
ecm_add_qtwayland_client_protocol(lattecoreplugin_SRCS
    PROTOCOL ${PLASMA_WAYLAND_PROTOCOLS_DIR}/plasma-shell.xml
    BASENAME plasma-shell
)

add_library(lattecoreplugin SHARED ${lattecoreplugin_SRCS})

target_link_libraries(lattecoreplugin
    Qt6::Quick
    Qt6::Qml
    Qt6::WaylandClient
    KF6::CoreAddons
    KF6::ConfigCore
    KF6::Svg
    KF6::WindowSystem
    Plasma::Plasma
    Plasma::PlasmaQuick
    KF6::IconThemes
)

install(TARGETS lattecoreplugin DESTINATION ${KDE_INSTALL_QMLDIR}/org/kde/latte/core)

# plugins.qmltypes feeds qmllint/tooling type resolution (the step-2.5
# qmllint ratchet); regenerate after registering new types in
# lattecoreplugin.cpp:
#   qmlplugindump -nonrelocatable org.kde.latte.core 0.2
# (offscreen, with QML2_IMPORT_PATH at the staged tree - the gate fails
# on new unresolved-type warnings if this file goes stale)
install(FILES qmldir plugins.qmltypes DESTINATION ${KDE_INSTALL_QMLDIR}/org/kde/latte/core)
