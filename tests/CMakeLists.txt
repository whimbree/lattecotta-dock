include(ECMAddTests)

# Behavioral tests drive the real compiled application code through the
# lattedock-core object library - no mocks of Latte's own logic, per
# docs/TESTING.md. QT_QPA_PLATFORM=offscreen keeps them headless.

ecm_add_test(importerregressiontest.cpp
    TEST_NAME importerregressiontest
    LINK_LIBRARIES lattedock-core Qt6::Test
)
set_tests_properties(importerregressiontest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(importerregressiontest PRIVATE
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

# Effect-rule source scan (family 7 in latte-plasma6-defect-families): no
# autoPaddingEnabled in shipped QML except the literal false. Pure grep, no
# staging or engine needed - fails on the rule regression e3376405 fixed.
add_test(NAME qmleffectrules
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/qml-effect-rules.sh)

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

# Contract: how Qt Quick parents windows declared inside items (QObject
# resource child, NO transient parent on Qt 6). qmltest cannot read
# QObject::parent() of a window, so it runs as a plain Qt test.
ecm_add_test(contracts/appletwindowparentingtest.cpp
    TEST_NAME appletwindowparentingtest
    LINK_LIBRARIES Qt6::Test Qt6::Quick
)
set_tests_properties(appletwindowparentingtest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
