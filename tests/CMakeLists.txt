include(ECMAddTests)

# Behavioral tests drive the real compiled application code through the
# lattedock-core object library - no mocks of Latte's own logic, per
# docs/TESTING.md. QT_QPA_PLATFORM=offscreen keeps them headless.

# Pure-core unit tests (docs/QML_EXTRACTION_PLAN.md): one paired test per
# extracted core header, enforced by scripts/coverage-ratchet.sh.
add_subdirectory(units)

ecm_add_test(importerregressiontest.cpp
    TEST_NAME importerregressiontest
    LINK_LIBRARIES lattedock-core Qt6::Test
)
set_tests_properties(importerregressiontest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(importerregressiontest PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/app)

ecm_add_test(storageidremapapplicationtest.cpp
    TEST_NAME storageidremapapplicationtest
    LINK_LIBRARIES lattedock-core Qt6::Test
)
set_tests_properties(storageidremapapplicationtest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(storageidremapapplicationtest PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/app)

ecm_add_test(layershellmappingtest.cpp
    TEST_NAME layershellmappingtest
    LINK_LIBRARIES lattedock-core Qt6::Test
)
set_tests_properties(layershellmappingtest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(layershellmappingtest PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/app)

ecm_add_test(windowinfowraptest.cpp
    TEST_NAME windowinfowraptest
    LINK_LIBRARIES lattedock-core Qt6::Test
)
set_tests_properties(windowinfowraptest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(windowinfowraptest PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/app)

ecm_add_test(indicatorfactoryremovaltest.cpp
    TEST_NAME indicatorfactoryremovaltest
    LINK_LIBRARIES lattedock-core Qt6::Test
)
set_tests_properties(indicatorfactoryremovaltest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(indicatorfactoryremovaltest PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/app)

# Removal parking state machine (71b0d75a): compiles the real containment
# plugin sources (they live in the QML plugin, not lattedock-core) and
# drives them with real Plasma applets built through itemForApplet plus a
# QML harness standing in for the containment main.qml. Needs the devShell's
# LATTE_QML_MODULE_PATH at runtime, like the contract tests.
qt6_add_resources(parkingharness_QRC parkingharness/parkingharness.qrc)
ecm_add_test(layoutmanagerparkingtest.cpp
    ${CMAKE_BINARY_DIR}/containment/plugin/lattetypes.h # AUTOMOC source for Latte::Types::staticMetaObject, like the plugin's own list
    ${CMAKE_SOURCE_DIR}/containment/plugin/layoutmanager.cpp
    ${CMAKE_SOURCE_DIR}/containment/plugin/types.cpp
    ${parkingharness_QRC}
    TEST_NAME layoutmanagerparkingtest
    LINK_LIBRARIES Qt6::Test Qt6::Gui Qt6::Qml Qt6::Quick KF6::ConfigCore KF6::ConfigQml KF6::CoreAddons Plasma::Plasma Plasma::PlasmaQuick
)
set_tests_properties(layoutmanagerparkingtest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(layoutmanagerparkingtest PRIVATE
    ${CMAKE_SOURCE_DIR}/containment
    ${CMAKE_BINARY_DIR}/containment)
ecm_add_test(visualhostwindowtest.cpp
    TEST_NAME visualhostwindowtest
    LINK_LIBRARIES lattedock-core Qt6::Test
)
set_tests_properties(visualhostwindowtest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(visualhostwindowtest PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/app)

# Headless QML checks (Phase 5, docs/TESTING.md): the compile gate stages an
# install and compiles every package QML file in a real engine; the
# interaction harness drives real Latte components through qmltestrunner.
# Both need the devShell toolchain, which is how ctest runs (build-check.sh).
add_test(NAME qmlcompilegate
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/qml-compile-gate.sh)
set_tests_properties(qmlcompilegate PROPERTIES
    ENVIRONMENT "BUILD=${CMAKE_BINARY_DIR}")

add_test(NAME qmlinteraction
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/qml-interaction-tests.sh)
set_tests_properties(qmlinteraction PROPERTIES
    ENVIRONMENT "BUILD=${CMAKE_BINARY_DIR}"
    DEPENDS qmlcompilegate)

# qmllint ratchet (docs/QML_EXTRACTION_PLAN.md step 2.5 point 6): per-file
# curated warning counts (unqualified, missing-type, unresolved-type,
# deprecated, signal-handler-parameters) must match tests/qmllint-baseline
# exactly; the baseline only ever shrinks. Cutover-touched files leave at
# zero (strict-on-touch).
add_test(NAME qmllintgate
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/qmllint-gate.sh)
set_tests_properties(qmllintgate PROPERTIES
    ENVIRONMENT "BUILD=${CMAKE_BINARY_DIR}")

# Effect-rule source scan (family 7 in latte-plasma6-defect-families): no
# autoPaddingEnabled in shipped QML except the literal false. Pure grep, no
# staging or engine needed - fails on the rule regression e3376405 fixed.
add_test(NAME qmleffectrules
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/qml-effect-rules.sh)

# Previews-pipeline contract scan: pins the ten line-level invariants the
# 2026-07-15 hover-lag excavation earned (burst debounce ordering and
# numbers, hide-countdown cancel, direct settle adoption, rootIndex token
# discipline, imperative host sizing, async incubation, cache eviction
# contract, stream gating, no unmap/remap resurrection). Each rule cites
# the commit whose body explains the trap; negative-tested against three
# injected regressions at introduction.
add_test(NAME previewcontractrules
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/preview-contract-rules.sh)

# Upstream contract tests (tests/contracts/README.md): pin the libplasma,
# KSvg and Qt behaviors Latte relies on at the flake-pinned versions, so a
# pin bump that changes semantics fails HERE instead of misbehaving in the
# dock far from the cause.
add_test(NAME qmlcontracts
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/qml-interaction-tests.sh ${CMAKE_SOURCE_DIR}/tests/contracts)
set_tests_properties(qmlcontracts PROPERTIES
    ENVIRONMENT "BUILD=${CMAKE_BINARY_DIR}"
    DEPENDS qmlcompilegate)

# C++ half of the contract suite: the destroyed()-handler type-demotion
# contract cannot be expressed in qmltest (QML cannot observe C++ destructor
# phases), so it runs as a plain Qt test per the contract-suite rules.
ecm_add_test(contracts/destroyedtypedemotiontest.cpp
    TEST_NAME destroyedtypedemotiontest
    LINK_LIBRARIES Qt6::Test
)
set_tests_properties(destroyedtypedemotiontest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# libplasma askDestroy() undo-window signal ordering (71b0d75a): constructs
# real Plasma::Containment/Applet objects offscreen, so it needs libplasma
# itself, not Latte code. The DBus session address is neutered inside the
# test so the "Widget Removed" KNotification can never reach the desktop.
ecm_add_test(contracts/askdestroysignalorderingtest.cpp
    TEST_NAME askdestroysignalorderingtest
    LINK_LIBRARIES Qt6::Test Qt6::Gui Plasma::Plasma KF6::CoreAddons
)
set_tests_properties(askdestroysignalorderingtest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# AppletQuickItem representation-switch contract (9ea29eaa/1aa5238c): builds
# a real applet item through itemForApplet(), with the applet main.qml and
# the expander probe riding the qrc applet path (no package structure
# plugins needed). Needs the devShell's LATTE_QML_MODULE_PATH at runtime for
# the org.kde.plasma.plasmoid import, which ctest has (build-check.sh runs
# inside nix develop); the test fails loudly if it is missing.
qt6_add_resources(repswitch_QRC contracts/repswitch/repswitch.qrc)
ecm_add_test(contracts/representationswitchtest.cpp ${repswitch_QRC}
    TEST_NAME representationswitchtest
    LINK_LIBRARIES Qt6::Test Qt6::Gui Qt6::Qml Qt6::Quick Plasma::Plasma Plasma::PlasmaQuick KF6::CoreAddons
)
set_tests_properties(representationswitchtest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Contract: how Qt Quick parents windows declared inside items (QObject
# resource child, NO transient parent on Qt 6). qmltest cannot read
# QObject::parent() of a window, so it runs as a plain Qt test.
ecm_add_test(contracts/appletwindowparentingtest.cpp
    TEST_NAME appletwindowparentingtest
    LINK_LIBRARIES Qt6::Test Qt6::Quick
)
set_tests_properties(appletwindowparentingtest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
