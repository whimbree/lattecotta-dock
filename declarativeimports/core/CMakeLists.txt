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

install(FILES qmldir DESTINATION ${KDE_INSTALL_QMLDIR}/org/kde/latte/core)
