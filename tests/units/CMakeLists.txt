# Pure-core unit tests (docs/QML_EXTRACTION_PLAN.md, section D). Each core
# header under a units/ directory (or an app-subtree capt placement listed in
# app-subtree-units.list) gets exactly one latte_add_unit_test entry here,
# named <header-basename>test. scripts/coverage-ratchet.sh fails build-check
# if a header has no registered pair, so keep the naming mechanical.
include(ECMAddTests)

# Every pure-core test runs under ASan+UBSan (docs/TESTING.md, the step-2.5
# rule). The cores are compiled INTO the test binary (header-only today;
# a .cpp core must be added to the test's SOURCES, never linked from an
# unsanitized object library, or the code under test runs uninstrumented
# and the sanitizers are theater). -fno-sanitize-recover turns any UBSan
# find into a test failure instead of a log line.
set(LATTE_UNIT_SANITIZE_FLAGS -fsanitize=address,undefined -fno-sanitize-recover=all)

function(latte_add_unit_test _name)
    # extra arguments: additional sources compiled into the binary
    ecm_add_test(${_name}.cpp ${ARGN}
        TEST_NAME ${_name}
        LINK_LIBRARIES Qt6::Test
    )
    set_tests_properties(${_name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    target_compile_options(${_name} PRIVATE ${LATTE_UNIT_SANITIZE_FLAGS})
    target_link_options(${_name} PRIVATE ${LATTE_UNIT_SANITIZE_FLAGS})
    # RelWithDebInfo defines QT_NO_DEBUG, which strips Q_ASSERT wholesale -
    # without this the cores' invariant asserts would be dead code even
    # here, and the tests would pass with violated preconditions.
    # QT_FORCE_ASSERTS keeps them live in the test binaries only; the
    # shipped dock stays assert-free. Corollary (CLAUDE.md code-clarity
    # law): assert expressions must never carry side effects.
    target_compile_definitions(${_name} PRIVATE QT_FORCE_ASSERTS)
endfunction()

# EX-22 ActivitySetAlgebra: header-only pure core, no Latte link needed.
latte_add_unit_test(activitysetalgebratest)
target_include_directories(activitysetalgebratest PRIVATE
    ${CMAKE_SOURCE_DIR}/app)

# EX-07 StorageIdRemapper: header-only pure id-assignment math.
latte_add_unit_test(storageidremappertest)

# EX-08 ScreenGeometryCalculator: header-only pure geometry math; needs
# the Latte/Plasma type enums (header-only usage, links stay minimal).
latte_add_unit_test(screengeometrycalculatortest)
target_include_directories(screengeometrycalculatortest PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/app) # coretypes.h is generated there
target_link_libraries(screengeometrycalculatortest
    Qt6::Gui
    Plasma::Plasma)

# EX-04 AutoSizeEngine: header-only pure search step, value-type history.
latte_add_unit_test(autosizeenginetest)

# EX-01 PreviewSwitchEngine: header-only pure core, injected clock.
latte_add_unit_test(previewswitchenginetest)

# EX-03 ParabolicMathCore: header-only pure math.
latte_add_unit_test(parabolicmathtest)

# EX-02 ParabolicRouter: header-only pure routing walk.
latte_add_unit_test(parabolicroutertest)

# EX-09 PositionerGeometry: header-only pure sizing/placement math; needs
# the Latte/Plasma type enums like EX-08.
latte_add_unit_test(positionergeometrytest)
target_include_directories(positionergeometrytest PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/app) # coretypes.h is generated there
target_link_libraries(positionergeometrytest
    Qt6::Gui
    Plasma::Plasma)

# EX-23 WindowTrackingPredicates + ExtraViewHints: two header-only cores,
# one spec. The predicates test needs windowinfowrap's types (Qt Gui);
# the hints test needs the Plasma location enum.
latte_add_unit_test(windowtrackingpredicatestest
    ${CMAKE_SOURCE_DIR}/app/wm/windowinfowrap.cpp) # compiled IN, per the
    # sanitized-core rule: WindowInfoWrap's methods are out-of-line
target_include_directories(windowtrackingpredicatestest PRIVATE
    ${CMAKE_SOURCE_DIR}/app)
target_link_libraries(windowtrackingpredicatestest
    Qt6::Gui)

latte_add_unit_test(extraviewhintstest)
target_include_directories(extraviewhintstest PRIVATE
    ${CMAKE_SOURCE_DIR}/app)
target_link_libraries(extraviewhintstest
    Qt6::Gui
    Plasma::Plasma)

# EX-25 PanelBackgroundScan: header-only pure pixel-scan math; QImage and
# QColor need Qt Gui, nothing from KSvg/Plasma reaches the core.
latte_add_unit_test(panelbackgroundscantest)
target_link_libraries(panelbackgroundscantest
    Qt6::Gui)

# EX-24 IconSourceClassifier: header-only pure classification; needs the
# QIcon/QImage variant handlers from Qt Gui.
latte_add_unit_test(iconsourceclassifiertest)
target_link_libraries(iconsourceclassifiertest
    Qt6::Gui)

# EX-17 TooltipTextComposer: header-only pure core plus its QML-facing
# wrapper compiled IN per the sanitized-core rule (the wrapper's boundary
# refusals and i18nc plan-mapping run instrumented); KF6::I18n for i18nc.
latte_add_unit_test(tooltiptexttest
    ${CMAKE_SOURCE_DIR}/plasmoid/plugin/tooltiptextcomposer.cpp)
target_link_libraries(tooltiptexttest
    KF6::I18n)
# the wrapper asserts against English msgids; a host locale with installed
# latte catalogs must not translate them out from under the test
set_property(TEST tooltiptexttest APPEND PROPERTY
    ENVIRONMENT "LANGUAGE=C;LC_ALL=C")
