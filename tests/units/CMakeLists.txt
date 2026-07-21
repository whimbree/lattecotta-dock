# Pure-core unit tests (docs/tracking/QML_EXTRACTION_PLAN.md, section D). Each core
# header under a units/ directory (or an app-subtree capt placement listed in
# app-subtree-units.list) gets exactly one latte_add_unit_test entry here,
# named <header-basename>test. tests/coverage/coverage-ratchet.sh fails build-check
# if a header has no registered pair, so keep the naming mechanical.
include(ECMAddTests)

# Every pure-core test runs under ASan+UBSan (docs/reference/TESTING.md, the step-2.5
# rule). The cores are compiled INTO the test binary (header-only today;
# a .cpp core must be added to the test's SOURCES, never linked from an
# unsanitized object library, or the code under test runs uninstrumented
# and the sanitizers are theater). -fno-sanitize-recover turns any UBSan
# find into a test failure instead of a log line.
#
# The flag set is defined ONCE at the top-level CMakeLists as
# LATTE_SANITIZE_FLAGS (the single source of truth shared with the
# -DLATTE_SANITIZE sanitized-dock build); consume it here so the tests and the
# sanitized dock can never disagree on what "sanitized" means. Refuse loudly if
# it went missing rather than silently building these tests without sanitizers
# (a green test with a violated precondition is the exact failure this guards).
if(NOT LATTE_SANITIZE_FLAGS)
    message(FATAL_ERROR "LATTE_SANITIZE_FLAGS is unset - it must be defined at the top-level CMakeLists before tests/ is added")
endif()

function(latte_add_unit_test _name)
    # extra arguments: additional sources compiled into the binary
    ecm_add_test(${_name}.cpp ${ARGN}
        TEST_NAME ${_name}
        LINK_LIBRARIES Qt6::Test
    )
    set_tests_properties(${_name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    target_compile_options(${_name} PRIVATE ${LATTE_SANITIZE_FLAGS})
    target_link_options(${_name} PRIVATE ${LATTE_SANITIZE_FLAGS})
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

# EX-13 ViewTypeAndBackgroundPredicates: header-only pure predicates plus
# their QML-facing wrapper compiled IN per the sanitized-core rule (the
# wrapper's boundary refusals and enum-int classification run
# instrumented); the wrapper includes the generated plugin/lattetypes.h.
latte_add_unit_test(backgroundstatetest
    ${CMAKE_SOURCE_DIR}/containment/plugin/backgroundstateresolver.cpp)
target_include_directories(backgroundstatetest PRIVATE
    ${CMAKE_BINARY_DIR}/containment) # plugin/lattetypes.h is generated there
# EX-10 MaskInputGeometry: header-only pure rect math plus its QML-facing
# wrapper compiled IN per the sanitized-core rule (the wrapper's boundary
# refusals and sentinel-rect mapping run instrumented); Plasma::Plasma for
# the location enum.
latte_add_unit_test(maskgeometrytest
    ${CMAKE_SOURCE_DIR}/containment/plugin/maskgeometrybridge.cpp)
target_link_libraries(maskgeometrytest
    Plasma::Plasma)

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

# InputMaskFlush (app/view/inputmaskflush.h): the pure "what region to hand
# QWindow::setMask" decision that keeps the window mask wide across a band
# shrink so Qt6 wayland does not clip the vacated region's clearing damage.
# Header-only, QRect-only; Qt6::Test brings QtCore.
latte_add_unit_test(inputmaskflushtest)

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

# WindowId newtype (the WindowId newtype hardening item in
# docs/tracking/PORTING_PLAN.md): header-only id type with the checked X11 parse
# and the loud boundary refusal, pinned incl. compile-time
# non-convertibility.
latte_add_unit_test(windowidtest)
target_include_directories(windowidtest PRIVATE
    ${CMAKE_SOURCE_DIR}/app)

latte_add_unit_test(extraviewhintstest)
target_include_directories(extraviewhintstest PRIVATE
    ${CMAKE_SOURCE_DIR}/app)
target_link_libraries(extraviewhintstest
    Qt6::Gui
    Plasma::Plasma)

# EX-15 WheelAccumulator: header-only pure wheel-delta math (QPoint is
# QtCore; nothing else reaches the core) plus its WheelStepper QML
# boundary compiled IN per the sanitized-core rule (the configuration
# refusals run instrumented).
latte_add_unit_test(wheelaccumulatortest
    ${CMAKE_SOURCE_DIR}/declarativeimports/core/wheelstepper.cpp)

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

# EX-12 ColorizerDecisionCore: header-only pure decision tree; includes the
# containment plugin's Types Q_ENUMs (header-only use, Qt Core suffices).
latte_add_unit_test(colorizerdecisiontest)
target_include_directories(colorizerdecisiontest PRIVATE
    ${CMAKE_SOURCE_DIR}/containment/plugin)
# EX-16 GroupWindowCycler: header-only pure selection math plus its
# QML-facing wrapper compiled IN per the sanitized-core rule (the wrapper's
# boundary refusals and -1 mapping run instrumented); Qt6::Qml for the
# singleton provider's QQmlEngine signature.
latte_add_unit_test(windowcyclertest
    ${CMAKE_SOURCE_DIR}/declarativeimports/core/windowcyclertools.cpp)
target_include_directories(windowcyclertest PRIVATE
    ${CMAKE_SOURCE_DIR}/declarativeimports/core)
target_link_libraries(windowcyclertest
    Qt6::Qml)
# EX-20 BadgeMath: header-only pure core plus its QML-facing wrapper
# compiled IN per the sanitized-core rule (the wrapper's boundary refusals
# and i18nc overflow label run instrumented); KF6::I18n for i18nc.
latte_add_unit_test(badgemathtest
    ${CMAKE_SOURCE_DIR}/declarativeimports/core/badgemathtools.cpp)
target_link_libraries(badgemathtest
    KF6::I18n)
# the wrapper's overflow label asserts against the English msgid; a host
# locale with installed latte catalogs must not translate it away
set_property(TEST badgemathtest APPEND PROPERTY
    ENVIRONMENT "LANGUAGE=C;LC_ALL=C")
# EX-18 LengthOffsetClamp: header-only pure clamp math plus its QML-facing
# bridge compiled IN per the sanitized-core rule (the boundary refusals and
# the alignment-int mapping run instrumented); coretypes.h is generated
# into the build tree and only needs Qt Core.
latte_add_unit_test(lengthoffsetclamptest
    ${CMAKE_SOURCE_DIR}/app/settings/lengthoffsetclampbridge.cpp)
target_include_directories(lengthoffsetclamptest PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/app) # coretypes.h is generated there
# EX-11 LauncherListOps: header-only pure list algebra plus its QML-facing
# wrappers compiled IN per the sanitized-core rule (the boundary refusals
# and QVariant mapping run instrumented).
latte_add_unit_test(launcherlistopstest
    ${CMAKE_SOURCE_DIR}/plasmoid/plugin/launcherlistbridge.cpp)

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

# EX-05 FillLengthDistributor: header-only pure two-pass space distribution.
latte_add_unit_test(filldistributortest)

# Shared RowEntry row-snapshot type family (EX-06 consumes it, EX-14 is
# planned to): header-only plain value type + lookups.
latte_add_unit_test(rowentrytest)

# EX-06 VisibleIndexEngine: header-only pure index math and neighbor
# predicates over RowEntry rows.
latte_add_unit_test(visibleindextest)
# EX-19 ColorLuminance: header-only pure color math. The Tools QML shell
# (tools.cpp) is compiled IN per the sanitized-core rule so its
# loud-refusal boundary runs instrumented; Qt6::Qml only supplies the
# QQmlEngine headers its singleton provider signature names.
latte_add_unit_test(colortoolstest
    ${CMAKE_SOURCE_DIR}/declarativeimports/core/tools.cpp)
target_include_directories(colortoolstest PRIVATE
    ${CMAKE_SOURCE_DIR}/declarativeimports/core)
target_link_libraries(colortoolstest
    Qt6::Gui
    Qt6::Qml)
# EX-21 ScrollOverflowMath: header-only pure scroll math plus its QML-facing
# wrapper compiled IN per the sanitized-core rule (the wrapper's boundary
# refusals run instrumented).
latte_add_unit_test(scrollmathtest
    ${CMAKE_SOURCE_DIR}/plasmoid/plugin/scrolloverflowmath.cpp)

# EX-14 DropEventClassifier: header-only pure classification + routing,
# plus its QML-facing wrapper compiled IN per the sanitized-core rule
# (the boundary refusals and QJSValue predicate marshalling run
# instrumented); Qt6::Qml for QJSEngine/QJSValue.
latte_add_unit_test(dropclassifiertest
    ${CMAKE_SOURCE_DIR}/declarativeimports/core/dropclassifiertools.cpp)
target_link_libraries(dropclassifiertest
    Qt6::Gui
    Qt6::Qml)

# Edit-mode settings audit diff core (tests/units/configsnapshotdiff.h): the
# pure Tier B check (docs/tracking/edit-mode-settings-audit-plan.md section 2) over two
# viewConfigData "config" snapshots. The HC3 acceptance property lives here -
# every rejection direction (stray coupled write, no-op, default-deletion) is
# pinned so the harness cannot green-light a wrong outcome. QJsonObject-only,
# Qt6::Test brings QtCore.
latte_add_unit_test(configsnapshotdifftest)

# viewsData D-Bus serializer (docs/reference/dbus-observability-interface.md): the
# pure ViewRecord -> JSON layer of app/dbusreports.h - enum-name mappings
# and serialization. The live collectors stay in app/dbusreports.cpp
# (three-line field reads off View) and are exercised by the running dock;
# every field name and value type a D-Bus consumer parses is pinned here.
latte_add_unit_test(dbusreportstest)
target_include_directories(dbusreportstest PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/app) # coretypes.h is generated there
target_link_libraries(dbusreportstest
    Qt6::Gui
    Plasma::Plasma)

# SC-O1 (the read-only settings-control D-Bus registry): value-only record,
# validation, deterministic ordering, and compact JSON serialization core.
# The QObject/QQuickItem registry is a separate behavioral test.
latte_add_unit_test(settingscontrolrecordstest)
target_include_directories(settingscontrolrecordstest PRIVATE
    ${CMAKE_SOURCE_DIR}/app)
target_link_libraries(settingscontrolrecordstest
    Qt6::Gui)

# Justify-alignment splitter placement (containment/plugin/units/
# justifysplitters.h): header-only pure list math. The template
# boundary-invariant guard for the UB-catching initiative - the sanitized
# build (QT_FORCE_ASSERTS + ASan) turns the old negative QList::insert into
# an abort, so this test both pins the repaired placement and stays the
# tripwire if the repair is ever removed.
latte_add_unit_test(justifysplitterstest)

# SC-F2 (the source-to-ledger coverage gate): the syntax-aware QML/C++
# settings scanner. Header-only QtCore parsing stays sanitizer-instrumented.
latte_add_unit_test(settingssourcescannertest)
