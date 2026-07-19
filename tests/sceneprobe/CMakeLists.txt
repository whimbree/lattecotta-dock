# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later
#
# sceneprobe render harness (docs/reference/captsilver-testability-adoption.md, P1):
# latte-sceneprobe renders one QML scene offscreen on Vulkan (lavapipe under
# the gate) and applies three failure gates; latte-imgdiff is the standalone
# comparator CLI (P4 reuses it). Only the comparator unit test registers in
# ctest - the render gate itself needs a nested kwin_wayland and runs as
# scripts/sceneprobe-gate.sh, like the other script gates.

find_package(Vulkan REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS GuiPrivate) # rhi/qrhi.h (private API)

add_library(latte-imagecompare STATIC imagecompare.cpp)
target_link_libraries(latte-imagecompare PUBLIC Qt6::Gui)

add_executable(latte-sceneprobe main.cpp)
target_link_libraries(latte-sceneprobe
    Qt6::Gui Qt6::Quick Qt6::Qml Qt6::GuiPrivate Vulkan::Vulkan latte-imagecompare)

add_executable(latte-imgdiff imgdiff_main.cpp)
target_link_libraries(latte-imgdiff Qt6::Gui latte-imagecompare)

include(ECMAddTests)
ecm_add_test(imagecompare_test.cpp
    TEST_NAME sceneprobe_imagecompare
    LINK_LIBRARIES latte-imagecompare Qt6::Test)
set_tests_properties(sceneprobe_imagecompare PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
