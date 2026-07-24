include(ECMAddTests)

# Test-only freedesktop notification endpoint. Nested removal acceptance uses
# it to activate libplasma's real Undo action instead of waiting for or mocking
# the resulting destroyedChanged(false) transition.
add_executable(latte-test-notification-service
    helpers/fakenotificationservice.cpp
)
target_link_libraries(latte-test-notification-service PRIVATE
    Qt6::Core
    Qt6::DBus
)

# Behavioral tests drive the real compiled application code through the
# lattedock-core object library - no mocks of Latte's own logic, per
# docs/reference/TESTING.md. QT_QPA_PLATFORM=offscreen keeps them headless.

# Pure-core unit tests (docs/tracking/QML_EXTRACTION_PLAN.md): one paired test per
# extracted core header, enforced by tests/coverage/coverage-ratchet.sh.
add_subdirectory(units)

# sceneprobe render harness (docs/archive/captsilver-testability-adoption.md P1):
# builds the probe/imgdiff binaries and registers the comparator unit test;
# the render gate itself is scripts/sceneprobe-gate.sh (needs nested kwin).
add_subdirectory(sceneprobe)

# D59 (invalid standalone AppStream identity and stale library provider):
# validate the configured source metadata directly. ECM's global appstreamtest
# reads install_manifest.txt and succeeds without inspecting anything when that
# file is absent, which is the normal development-test state.
find_program(APPSTREAMCLI appstreamcli REQUIRED)
add_test(NAME appstreammetadatatest
    COMMAND ${CMAKE_COMMAND}
        -DAPPSTREAMCLI=${APPSTREAMCLI}
        -DAPPSTREAM_METADATA=${CMAKE_BINARY_DIR}/app/org.kde.latte-dock.appdata.xml
        -P ${CMAKE_CURRENT_SOURCE_DIR}/validate-appstream.cmake)

# Importer's static parser/path helpers over throwaway XDG dirs: fileVersion
# archive classification, nameOfConfigFile (pins the Qt6 remove end-wrap
# fix), layout paths/enumeration, standard-path ordering and the
# multiple-layouts status round trip. uniqueLayoutName lives in
# importerregressiontest.
ecm_add_test(importerlogictest.cpp
    TEST_NAME importerlogictest
    LINK_LIBRARIES lattedock-core Qt6::Test
)
set_tests_properties(importerlogictest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(importerlogictest PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/app)

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

# Both ScreenPool implementations (Latte core and the Plasma-extended
# mirror) against seeded [ScreenConnectors] groups; carries its own main()
# so XDG_CONFIG_HOME points at a temp dir BEFORE QGuiApplication, keeping
# the plasma pool's hardcoded plasmashellrc off the real desktop config.
ecm_add_test(screenpooltest.cpp
    TEST_NAME screenpooltest
    LINK_LIBRARIES lattedock-core Qt6::Test
)
set_tests_properties(screenpooltest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(screenpooltest PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/app)

# The real Windows -> TrackedGeneralInfo -> LastActiveWindow tracker graph
# over a minimal concrete WM: adoption/rejection rules, history pruning
# bounds, the windowRemoved re-selection chain and the request forwarders.
ecm_add_test(lastactivewindowtest.cpp
    TEST_NAME lastactivewindowtest
    LINK_LIBRARIES lattedock-core Qt6::Test
)
set_tests_properties(lastactivewindowtest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(lastactivewindowtest PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/app)

# The windowChanged coalescer: sustained same-window churn has a fixed 150 ms
# delivery deadline, consumers read the latest state at delivery, and a new
# window flushes the pending id before starting its own deadline.
ecm_add_test(windowchangedebouncetest.cpp
    TEST_NAME windowchangedebouncetest
    LINK_LIBRARIES lattedock-core Qt6::Test
)
set_tests_properties(windowchangedebouncetest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(windowchangedebouncetest PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/app)

# SyncedLaunchers' client registry driven over the real object: the
# destroyed-client removal (the d6d57e61 dangling-pointer crash class),
# identity removal precision, and the layout/group filtering query.
ecm_add_test(syncedlaunchersclienttest.cpp
    TEST_NAME syncedlaunchersclienttest
    LINK_LIBRARIES lattedock-core Qt6::Test
)
set_tests_properties(syncedlaunchersclienttest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(syncedlaunchersclienttest PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/app)

# The kglobalshortcutsrc badge parser and the modifier tracker's idle-state
# predicates; carries its own main() so XDG_CONFIG_HOME and the neutered
# DBus address land BEFORE QGuiApplication (the tracker's constructor calls
# KGlobalAccel, which must never reach the desktop's live kglobalacceld).
ecm_add_test(shortcutstest.cpp
    TEST_NAME shortcutstest
    LINK_LIBRARIES lattedock-core Qt6::Test
)
set_tests_properties(shortcutstest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(shortcutstest PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/app)

# The Layouts::Storage singleton over temp .latte KConfig fixtures: the
# view() / updateView() serialization pair, containment enumeration, clone
# and subcontainment handling, the errors/warnings scanners and template
# export/import; carries its own main() so the XDG homes point at a temp dir
# before QGuiApplication.
ecm_add_test(storagetest.cpp
    TEST_NAME storagetest
    LINK_LIBRARIES lattedock-core Qt6::Test
)
set_tests_properties(storagetest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(storagetest PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/app)

# UniversalSettings over a throwaway KSharedConfig with a null corona: the
# signal->saveConfig persistence wiring, guarded setters, documented
# defaults, screen scales and the transient inConfigureAppletsMode contract;
# carries its own main() so XDG_CONFIG_HOME lands before QGuiApplication.
ecm_add_test(universalsettingstest.cpp
    TEST_NAME universalsettingstest
    LINK_LIBRARIES lattedock-core Qt6::Test
)
set_tests_properties(universalsettingstest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(universalsettingstest PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/app)

# The app/tools free helpers: the rect<->string serialization persisted
# screen records ride (incl. the malformed-input refusal) and the XDG path
# resolvers; carries its own main() so both XDG homes are temp dirs.
ecm_add_test(commontoolstest.cpp
    TEST_NAME commontoolstest
    LINK_LIBRARIES lattedock-core Qt6::Test
)
set_tests_properties(commontoolstest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(commontoolstest PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/app)

# The LatteCore QML plugin's engine-free helpers: Environment constants and
# the extras.h inline helpers; compiles environment.cpp directly (it lives
# in the QML plugin, not lattedock-core). The color math is NOT here - it is
# the ColorTools pure core, pinned by tests/units/colortoolstest.cpp.
ecm_add_test(coretoolstest.cpp
    ${CMAKE_SOURCE_DIR}/declarativeimports/core/environment.cpp
    TEST_NAME coretoolstest
    LINK_LIBRARIES Qt6::Test Qt6::Gui Qt6::Qml KF6::CoreAddons
        KF6::IconThemes Plasma::Plasma
)
set_tests_properties(coretoolstest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(coretoolstest PRIVATE
    ${CMAKE_SOURCE_DIR}/declarativeimports/core
    ${CMAKE_BINARY_DIR}/declarativeimports/core)

# D25 (task icons stay stale after icon-theme changes): the real ThemeAwareIcon
# is rendered against two fixture themes that share one icon name but supply
# different solid pixels.
# The test compiles Environment directly because it lives in the core QML
# plugin, then registers that real signal owner with the component's engine.
ecm_add_test(themeawareicontest.cpp
    ${CMAKE_SOURCE_DIR}/declarativeimports/core/environment.cpp
    TEST_NAME themeawareicontest
    LINK_LIBRARIES Qt6::Test Qt6::Gui Qt6::Qml Qt6::Quick
        KF6::CoreAddons KF6::IconThemes
)
set_tests_properties(themeawareicontest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QSG_RHI_BACKEND=software")
target_include_directories(themeawareicontest PRIVATE
    ${CMAKE_SOURCE_DIR}/declarativeimports/core
    ${CMAKE_BINARY_DIR}/declarativeimports/core)
target_compile_definitions(themeawareicontest PRIVATE
    THEMEAWAREICON_QML_PATH="${CMAKE_SOURCE_DIR}/plasmoid/package/contents/ui/components/ThemeAwareIcon.qml"
    ICON_THEME_FIXTURE_ROOT="${CMAKE_SOURCE_DIR}/tests/fixtures/icon-themes")

# The settings-dialog delegate toolkit (Settings::Generic free helpers):
# state predicates, color-group priority, icon/indicator slot geometry incl.
# the RTL flip, and the painters driven into a QImage offscreen.
ecm_add_test(generictoolstest.cpp
    TEST_NAME generictoolstest
    LINK_LIBRARIES lattedock-core Qt6::Test Qt6::Widgets
)
set_tests_properties(generictoolstest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(generictoolstest PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/app)

# The Qt6 Controls-2 rules the config windows ride (final base members, the
# ToolTip attached property, TabBar currentIndex, ListModel get() probing)
# plus source scans of the components controls that depend on them.
ecm_add_test(configcontrolstest.cpp
    TEST_NAME configcontrolstest
    LINK_LIBRARIES Qt6::Test Qt6::Qml
)
set_tests_properties(configcontrolstest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_compile_definitions(configcontrolstest PRIVATE
    CHECKBOX_QML_PATH="${CMAKE_SOURCE_DIR}/declarativeimports/components/CheckBox.qml"
    TEXTFIELD_QML_PATH="${CMAKE_SOURCE_DIR}/declarativeimports/components/TextField.qml"
    ITEMDELEGATE_QML_PATH="${CMAKE_SOURCE_DIR}/declarativeimports/components/ItemDelegate.qml")

# The edit-mode settings audit's QML-handler WIRING harness
# (docs/tracking/edit-mode-settings-audit-plan.md section 2): runs a control's handler
# body against a stub QQmlPropertyMap and runs the Tier B diff predicates
# (tests/units/configsnapshotdiff.h) over the delta. Its own HC3 acceptance
# test - it proves the harness catches a wrong-key/no-op/stray-coupled write
# driven through real QML. Qt6::Qml for the engine/property map, Qt6::Gui for
# the QColor stable-scalar fallback.
ecm_add_test(settingswiringharnesstest.cpp
    TEST_NAME settingswiringharnesstest
    LINK_LIBRARIES Qt6::Test Qt6::Qml Qt6::Gui
)
set_tests_properties(settingswiringharnesstest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(settingswiringharnesstest PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/units) # configsnapshotdiff.h

# SC-F2 per-view settings ledger: schema integrity, stable identities,
# source-coverage mappings and complete linkage to the 1-121 logical catalog.
ecm_add_test(settingsinventorytest.cpp
    TEST_NAME settingsinventorytest
    LINK_LIBRARIES Qt6::Test
)
target_compile_definitions(settingsinventorytest PRIVATE
    REPO_ROOT="${CMAKE_SOURCE_DIR}"
    SETTINGS_INVENTORY_PATH="${CMAKE_SOURCE_DIR}/tests/data/per-view-settings-inventory.json")
target_include_directories(settingsinventorytest PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/units) # settingssourcescanner.h

# SC-O1 (the read-only settings-control D-Bus registry) fixture vertical slice:
# a dedicated test-only QML tree in a real QQuickWindow/QQmlEngine, registered
# exclusively through the production C++ registry API. No production page or
# shared component registration enters this foundation unit.
qt6_add_resources(settingscontrolregistryfixture_QRC
    settingscontrolregistryfixture/settingscontrolregistryfixture.qrc)
ecm_add_test(settingscontrolregistrytest.cpp
    ${settingscontrolregistryfixture_QRC}
    TEST_NAME settingscontrolregistrytest
    LINK_LIBRARIES lattedock-core Qt6::Test Qt6::Gui Qt6::Qml Qt6::Quick
)
set_tests_properties(settingscontrolregistrytest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QSG_RHI_BACKEND=software")
target_include_directories(settingscontrolregistrytest PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/app)

# The public XML and thin Corona delegation cannot be exercised by constructing
# Corona without the full Plasma shell graph. This permanent source guard pins
# both seams and proves controlled missing/bypass mutations red.
ecm_add_test(settingscontrolsourceguardtest.cpp
    TEST_NAME settingscontrolsourceguardtest
    LINK_LIBRARIES Qt6::Test
)
target_compile_definitions(settingscontrolsourceguardtest PRIVATE
    REPO_ROOT="${CMAKE_SOURCE_DIR}")

# The length cluster's real-handler wiring audit (CL-1,
# docs/tracking/edit-mode-settings-audit-plan.md): the same QML-fragment/stub-map pattern
# as settingswiringharnesstest, plus the LengthOffsetClampBridge registered as
# `lengthClamp` (SubConfigView registers it live) so the REAL maxLength ruler /
# slider handlers can be driven. Pins the D15 min-coupling as accepted, the D16
# binding re-sync, and the offset/fine-value P2 rows.
ecm_add_test(lengthhandleraudittest.cpp
    ${CMAKE_SOURCE_DIR}/app/settings/lengthoffsetclampbridge.cpp
    TEST_NAME lengthhandleraudittest
    LINK_LIBRARIES Qt6::Test Qt6::Qml Qt6::Gui
)
set_tests_properties(lengthhandleraudittest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(lengthhandleraudittest PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/units      # configsnapshotdiff.h
    ${CMAKE_SOURCE_DIR}/app/settings     # lengthoffsetclampbridge.h / lengthoffsetclamp.h
    ${CMAKE_BINARY_DIR}/app)             # coretypes.h is generated there

# The Behavior page's real-handler wiring audit (CL-3,
# docs/tracking/edit-mode-settings-audit-plan.md): the same QML-fragment/stub-map pattern
# as settingswiringharnesstest. Pins the Actions/Environment/Items cfg checkboxes
# and the two action combos to their own key (P2, S-d identity round-trips), the
# Location/Alignment/Visibility buttons' `checked` reflection (P3), and the S-a
# finding - the TypeSelection Dock/Panel preset writes solidPanel /
# colorizeTransparentPanels to schema-absent keys nothing reads.
ecm_add_test(behaviorwiringaudittest.cpp
    TEST_NAME behaviorwiringaudittest
    LINK_LIBRARIES Qt6::Test Qt6::Qml Qt6::Gui
)
set_tests_properties(behaviorwiringaudittest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(behaviorwiringaudittest PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/units) # configsnapshotdiff.h

# The Effects page's real-handler wiring audit (CL-4,
# docs/tracking/edit-mode-settings-audit-plan.md): the same QML-fragment/stub-map pattern
# as settingswiringharnesstest, plus a stub latteView.indicator for the ind-class
# indicator controls (they write a C++ property, not a config key). Pins the
# shadow/animation right-key deltas, the S-c durationTime sense verdict, and the
# indicator switch/tab wiring. No extra sources: the handlers are self-contained
# QML fragments over the stub map.
ecm_add_test(effectshandleraudittest.cpp
    TEST_NAME effectshandleraudittest
    LINK_LIBRARIES Qt6::Test Qt6::Qml Qt6::Gui
)
set_tests_properties(effectshandleraudittest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(effectshandleraudittest PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/units) # configsnapshotdiff.h

# The Appearance page's non-length wiring audit (CL-2,
# docs/tracking/edit-mode-settings-audit-plan.md): the same QML-fragment/stub-map pattern
# as settingswiringharnesstest, over the Items/Margins/Colors/Background
# controls. containment/plugin for types.h (the ThemeColors enum values the
# Palette round-trip and its static_asserts pin); the main.xml path for the
# KConfigXT-schema-vs-enum numeric agreement check (the S-b clearance).
ecm_add_test(appearancehandleraudittest.cpp
    TEST_NAME appearancehandleraudittest
    LINK_LIBRARIES Qt6::Test Qt6::Qml Qt6::Gui
)
set_tests_properties(appearancehandleraudittest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(appearancehandleraudittest PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/units          # configsnapshotdiff.h
    ${CMAKE_SOURCE_DIR}/containment/plugin)   # types.h (ThemeColors enum)
target_compile_definitions(appearancehandleraudittest PRIVATE
    THEMECOLORS_MAINXML_PATH="${CMAKE_SOURCE_DIR}/containment/package/contents/config/main.xml"
    APPEARANCE_CONFIG_QML_PATH="${CMAKE_SOURCE_DIR}/shell/package/contents/configuration/pages/AppearanceConfig.qml")

# The Tasks page's real-handler wiring audit (CL-5, the tasks-page settings
# audit + the D10 disposition, docs/tracking/edit-mode-settings-audit-plan.md controls
# 91-121): the same QML-fragment/stub-map pattern as settingswiringharnesstest,
# over the badges/interaction/filters/animations/launchers/scrolling/actions
# controls. plasmoid/plugin for types.h (the TaskAction/TaskScrollAction enum the
# leftClickAction/hoverAction combo mappings and their static_asserts pin). No
# extra sources: the handlers are self-contained QML fragments over the stub map.
ecm_add_test(taskshandleraudittest.cpp
    TEST_NAME taskshandleraudittest
    LINK_LIBRARIES Qt6::Test Qt6::Qml Qt6::Gui
)
set_tests_properties(taskshandleraudittest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(taskshandleraudittest PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/units       # configsnapshotdiff.h
    ${CMAKE_SOURCE_DIR}/plasmoid/plugin)   # types.h (TaskAction enum)

# The shell view wrapper's no-band contract: the legacy backgroundHints
# ternary demonstrably falls through on Plasma 6 (live engine), and the
# shipped Panel.qml keeps its imagePath empty (source scan).
ecm_add_test(panelbackgroundtest.cpp
    TEST_NAME panelbackgroundtest
    LINK_LIBRARIES Qt6::Test Qt6::Qml
)
set_tests_properties(panelbackgroundtest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_compile_definitions(panelbackgroundtest PRIVATE
    PANEL_QML_PATH="${CMAKE_SOURCE_DIR}/shell/package/contents/views/Panel.qml")

# The app/data value-type layer: tables, sentinels, state machines and the
# serialization round trips the settings dialogs and storage ride on.
ecm_add_test(datatypestest.cpp
    TEST_NAME datatypestest
    LINK_LIBRARIES lattedock-core Qt6::Test
)
set_tests_properties(datatypestest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(datatypestest PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/app)

# The pure half of the activity-manager helper: wire-state validation and
# the running filter that layout assignment and activity cycling ride on.
ecm_add_test(activitiesinfotest.cpp
    TEST_NAME activitiesinfotest
    LINK_LIBRARIES lattedock-core Qt6::Test
)
set_tests_properties(activitiesinfotest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(activitiesinfotest PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/app)

# AbstractLayout's [LayoutSettings] persistence round-trip, the guarded
# setter signals it rides on, and the layoutName() parser table.
ecm_add_test(abstractlayouttest.cpp
    TEST_NAME abstractlayouttest
    LINK_LIBRARIES lattedock-core Qt6::Test
)
set_tests_properties(abstractlayouttest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(abstractlayouttest PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/app)

# The reveal-on-screen-edge predicate that arms the wayland edge-ghost
# detector, pinned over the full Types::Visibility domain.
ecm_add_test(visibilityrevealtest.cpp
    TEST_NAME visibilityrevealtest
    LINK_LIBRARIES lattedock-core Qt6::Test
)
set_tests_properties(visibilityrevealtest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(visibilityrevealtest PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/app)

# The wm free helpers: tasktools URL->AppData parsing (launcher/task
# identity) and the SchemeColors .colors parser (dynamic window-scheme
# coloring); carries its own main() so XDG_DATA_HOME is a temp dir for the
# scheme-name resolution fixtures.
ecm_add_test(wmtoolstest.cpp
    TEST_NAME wmtoolstest
    LINK_LIBRARIES lattedock-core Qt6::Test
)
set_tests_properties(wmtoolstest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(wmtoolstest PRIVATE
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
# The containment LayoutManager's plain bookkeeping (option lists, anchor
# properties, masqueraded drag-and-drop indexes) - compiles the plugin
# sources like the parking test; the parking suite carries the scheduled-
# destruction state machine, this one carries everything that needs no
# Plasma stack.
ecm_add_test(layoutmanagertest.cpp
    ${CMAKE_BINARY_DIR}/containment/plugin/lattetypes.h # AUTOMOC source for Latte::Types::staticMetaObject, like the plugin's own list
    ${CMAKE_SOURCE_DIR}/containment/plugin/layoutmanager.cpp
    ${CMAKE_SOURCE_DIR}/containment/plugin/types.cpp
    TEST_NAME layoutmanagertest
    LINK_LIBRARIES Qt6::Test Qt6::Gui Qt6::Qml Qt6::Quick KF6::ConfigCore KF6::ConfigQml KF6::CoreAddons Plasma::Plasma Plasma::PlasmaQuick
)
set_tests_properties(layoutmanagertest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(layoutmanagertest PRIVATE
    ${CMAKE_SOURCE_DIR}/containment
    ${CMAKE_BINARY_DIR}/containment)

# The view-side TasksModel (ordered plasmoid list QML reads as "tasks") and
# the indicator capability Info record: range/count signal discipline and
# guarded setters over bare AppletQuickItems.
ecm_add_test(viewmodelstest.cpp
    TEST_NAME viewmodelstest
    LINK_LIBRARIES lattedock-core Qt6::Test
)
set_tests_properties(viewmodelstest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(viewmodelstest PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/app)

# Plasma 6 shortcuts-host discovery pin (the OWED test from the plan's
# keyboard item): builds a real containment graph through itemForApplet
# with the REAL abilities/PositionShortcuts.qml aliased into the qrc, and
# asserts the discovery walk and all four QML invokable signatures through
# the shipped statics. Needs the devShell's LATTE_QML_MODULE_PATH, like
# the contract tests; REPO_ROOT feeds the abilities.definition import leaf.
qt6_add_resources(shortcutshost_QRC shortcutshost/shortcutshost.qrc)
ecm_add_test(shortcutshosttest.cpp
    ${shortcutshost_QRC}
    TEST_NAME shortcutshosttest
    LINK_LIBRARIES lattedock-core Qt6::Test
)
set_tests_properties(shortcutshosttest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(shortcutshosttest PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/app)
target_compile_definitions(shortcutshosttest PRIVATE
    REPO_ROOT="${CMAKE_SOURCE_DIR}")

ecm_add_test(visualhostwindowtest.cpp
    TEST_NAME visualhostwindowtest
    LINK_LIBRARIES lattedock-core Qt6::Test
)
set_tests_properties(visualhostwindowtest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(visualhostwindowtest PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/app)

# Source guards for small fixes behind object graphs that cannot be built
# offscreen: sidebar assignment, layouts delegation, tracker enablement,
# middle-click dispatch wiring/lifecycle, and the split between immediate strut
# publication and throttled geometry churn.
ecm_add_test(sourceguardtest.cpp
    TEST_NAME sourceguardtest
    LINK_LIBRARIES Qt6::Test
)
set_tests_properties(sourceguardtest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_compile_definitions(sourceguardtest PRIVATE
    REPO_ROOT="${CMAKE_SOURCE_DIR}")

# Dock identity and edit-session boundary wiring. The value-only action policy
# and retarget generations have sanitized unit tests; this pins their use by
# the runtime objects that need a complete Plasma shell to construct.
ecm_add_test(dockidentitycontracttest.cpp
    TEST_NAME dockidentitycontracttest
    LINK_LIBRARIES Qt6::Test
)
set_tests_properties(dockidentitycontracttest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_compile_definitions(dockidentitycontracttest PRIVATE
    REPO_ROOT="${CMAKE_SOURCE_DIR}")

# The details-dialog color-scheme model over planted .colors fixtures:
# enumeration, sorting, the synthetic System Colors row and the row lookup.
ecm_add_test(schemesmodeltest.cpp
    TEST_NAME schemesmodeltest
    LINK_LIBRARIES lattedock-core Qt6::Test
)
set_tests_properties(schemesmodeltest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(schemesmodeltest PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/app)

# The two settings table models (export-template applets, screens dialog):
# population, check-state toggling, defaults and the checkedScreens()
# active-screen exclusion that gates the Screens dialog removal.
ecm_add_test(settingsmodelstest.cpp
    TEST_NAME settingsmodelstest
    LINK_LIBRARIES lattedock-core Qt6::Test
)
set_tests_properties(settingsmodelstest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(settingsmodelstest PRIVATE
    ${CMAKE_SOURCE_DIR}/app
    ${CMAKE_BINARY_DIR}/app)

# Vendored-backend pins (docs/archive/captsilver-testability-adoption.md P3): both
# compile the plasmoid plugin sources directly (they live in the tasks QML
# plugin, not lattedock-core) and serve the plasma-desktop sync pass as
# behavioral tripwires for synced hunks.
ecm_add_test(smartlauncheritemtest.cpp
    ${CMAKE_SOURCE_DIR}/plasmoid/plugin/smartlauncheritem.cpp
    ${CMAKE_SOURCE_DIR}/plasmoid/plugin/smartlauncherbackend.cpp
    TEST_NAME smartlauncheritemtest
    LINK_LIBRARIES Qt6::Test Qt6::Core Qt6::DBus KF6::Service KF6::ConfigCore PW::LibNotificationManager
)
set_tests_properties(smartlauncheritemtest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(smartlauncheritemtest PRIVATE
    ${CMAKE_SOURCE_DIR}/plasmoid/plugin)

set(tasksbackendtest_SRCS tasksbackendtest.cpp ${CMAKE_SOURCE_DIR}/plasmoid/plugin/backend.cpp)
kconfig_add_kcfg_files(tasksbackendtest_SRCS ${CMAKE_SOURCE_DIR}/plasmoid/plugin/kactivitymanagerd_plugins_settings.kcfgc)
ecm_add_test(${tasksbackendtest_SRCS}
    TEST_NAME tasksbackendtest
    LINK_LIBRARIES Qt6::Test Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::DBus
        Plasma::Activities Plasma::ActivitiesStats KSysGuard::ProcessCore
        KF6::Service KF6::ConfigCore KF6::ConfigGui KF6::I18n
        KF6::KIOCore KF6::KIOGui KF6::KIOFileWidgets KF6::Notifications KF6::WindowSystem
)
set_tests_properties(tasksbackendtest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(tasksbackendtest PRIVATE
    ${CMAKE_SOURCE_DIR}/plasmoid/plugin
    ${CMAKE_CURRENT_BINARY_DIR})

# Headless QML checks (Phase 5, docs/reference/TESTING.md): the compile gate stages an
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

# qmllint ratchet (docs/tracking/QML_EXTRACTION_PLAN.md step 2.5 point 6): per-file
# curated warning counts (unqualified, missing-type, unresolved-type,
# deprecated, signal-handler-parameters) must match tests/coverage/qmllint-baseline
# exactly; the baseline only ever shrinks. Cutover-touched files leave at
# zero (strict-on-touch).
add_test(NAME qmllintgate
    COMMAND ${CMAKE_SOURCE_DIR}/tests/coverage/qmllint-gate.sh)
set_tests_properties(qmllintgate PROPERTIES
    ENVIRONMENT "BUILD=${CMAKE_BINARY_DIR}")

# Effect-rule source scans (latte-plasma6-defect-families): rule 1, no
# autoPaddingEnabled in shipped QML except the literal false (fails on the
# rule regression e3376405 fixed); rule 2, every when-gated Binding element
# declares an explicit restoreMode (the Qt6 default resets frozen values -
# semantics pinned by contracts/tst_bindingrestorecontracts.qml). Pure source
# scan, no staging or engine needed.
add_test(NAME qmleffectrules
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/qml-effect-rules.sh)

# Wayland popup-tooltip-surface rule (latte-plasma6-defect-families; issue 3
# of docs/tracking/panel-issues-plan.md): the edit-mode canvas overlay click-target
# controls must not carry an attached QQC2/Controls.ToolTip, whose separate
# Wayland surface maps at the cursor and swallows the press (the
# "Rearrange..." toggle went unclickable in tight edit-mode space). Pins the
# in-window-hint fix in place. Pure source scan, no staging or engine needed.
add_test(NAME qmltooltiprules
    COMMAND ${CMAKE_SOURCE_DIR}/scripts/qml-tooltip-rules.sh)

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

# Contract: the edit-mode detection premise (Applet::setUserConfiguring is a
# synchronous guarded setter at the pin) - the single property Latte's whole
# edit-mode entry/exit detection rides on. The file header names the
# re-audit obligations if a libplasma bump changes the semantics.
ecm_add_test(contracts/userconfiguringcontracttest.cpp
    TEST_NAME userconfiguringcontracttest
    LINK_LIBRARIES Qt6::Test Qt6::Gui Plasma::Plasma KF6::CoreAddons
)
set_tests_properties(userconfiguringcontracttest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Contract: where `expanded` lives at the pin - absent on Plasma::Applet, so
# the Qt5 `Plasmoid.applets[i].expanded = false` loop cannot work on Plasma 6,
# and writable on the graphic item itemForApplet() builds, which is what the
# configure-mode popup collapse routes through (deactivateApplets).
qt6_add_resources(expandapplet_QRC contracts/expandapplet.qrc)
ecm_add_test(contracts/appletsexpandedpropertytest.cpp
    ${expandapplet_QRC}
    TEST_NAME appletsexpandedpropertytest
    LINK_LIBRARIES Qt6::Test Qt6::Gui Qt6::Qml Qt6::Quick KF6::CoreAddons Plasma::Plasma Plasma::PlasmaQuick
)
set_tests_properties(appletsexpandedpropertytest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Contract: the Qt shutdown sequence the Corona teardown design relies on
# (aboutToQuit fires inside exit() while the app is live; execCleanup()
# flushes deferred deletes once; nothing flushes them after exec() returns).
# The header comment in the test file names the re-audit obligations.
ecm_add_test(contracts/quitteardowncontractstest.cpp
    TEST_NAME quitteardowncontractstest
    LINK_LIBRARIES Qt6::Test
)
set_tests_properties(quitteardowncontractstest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Locate a qmltypes file inside the PINNED libplasma's own qml tree (next to
# its .so - true on both a split-tree distro like NixOS, where each package
# carries its own qml, and a merged /usr/lib). The Qt6 QML subdir NAME varies
# per distro: nixpkgs spells it "qt-6/qml", Arch/Fedora/Debian spell it
# "qt6/qml". Search the known spellings rather than hardcoding one, so the
# contracts below pin on every matrix distro instead of only the nix pin.
# A silent skip would un-pin the contract, so a miss is FATAL, not optional.
get_target_property(_plasma_lib_location Plasma::Plasma LOCATION)
get_filename_component(_plasma_libdir "${_plasma_lib_location}" DIRECTORY)
function(latte_find_plasma_qmltypes outvar module_reldir filename contract)
    # find_file caches its result, so a stale hit would survive a libplasma
    # pin bump in a persistent build dir; clear it first to keep the old
    # set()+if(EXISTS) guarantee that every configure re-resolves the path.
    unset("${outvar}" CACHE)
    find_file("${outvar}"
        NAMES "${filename}"
        PATHS "${_plasma_libdir}"
        PATH_SUFFIXES
            "qt-6/qml/${module_reldir}"
            "qt6/qml/${module_reldir}"
            "qml/${module_reldir}"
        NO_DEFAULT_PATH)
    if(NOT ${outvar})
        message(FATAL_ERROR "pinned libplasma qmltypes '${filename}' not found "
            "under ${_plasma_libdir} (qt-6/qml, qt6/qml or qml)/${module_reldir}; "
            "${contract}")
    endif()
    set("${outvar}" "${${outvar}}" PARENT_SCOPE)
endfunction()

# Contract: Plasma 6's ContainmentItem::createApplet takes a QRectF geometry
# hint (Qt5 took a QPoint); a mismatched Q_ARG type makes the alternatives
# swap silently destroy an applet without replacing it.
latte_find_plasma_qmltypes(_plasmoid_qmltypes
    "org/kde/plasma/plasmoid" plasmoidplugin.qmltypes
    "alternativescreateapplettest cannot pin the createApplet geometry contract")
ecm_add_test(contracts/alternativescreateapplettest.cpp
    TEST_NAME alternativescreateapplettest
    LINK_LIBRARIES Qt6::Test
)
set_tests_properties(alternativescreateapplettest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_compile_definitions(alternativescreateapplettest PRIVATE
    PLASMOID_QMLTYPES_PATH="${_plasmoid_qmltypes}"
    REPO_ROOT="${CMAKE_SOURCE_DIR}")

# Contract: the task context menu assigns Latte::Types::PopupPlacement ints
# straight into PlasmaExtras.Menu's placement enum; the values are pinned
# member-for-member against the PINNED libplasma's plasmaextracomponents
# qmltypes, located like the plasmoid qmltypes above and equally must exist.
latte_find_plasma_qmltypes(_plasmaextras_qmltypes
    "org/kde/plasma/extras" plasmaextracomponentsplugin.qmltypes
    "popupplacementcontracttest cannot pin the context menu placement contract")
ecm_add_test(contracts/popupplacementcontracttest.cpp
    ${CMAKE_BINARY_DIR}/app/coretypes.h # AUTOMOC source for Latte::Types::staticMetaObject
    TEST_NAME popupplacementcontracttest
    LINK_LIBRARIES Qt6::Test
)
set_tests_properties(popupplacementcontracttest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(popupplacementcontracttest PRIVATE
    ${CMAKE_BINARY_DIR}/app)
target_compile_definitions(popupplacementcontracttest PRIVATE
    PLASMAEXTRAS_QMLTYPES_PATH="${_plasmaextras_qmltypes}")

# Contract: KF6 KPluginMetaData's single-string ctor resolves a plugin
# LIBRARY, not a metadata file - indicator metadata must load through
# fromJsonFile() or no indicator ever registers and no task dot renders.
ecm_add_test(contracts/indicatormetadatatest.cpp
    TEST_NAME indicatormetadatatest
    LINK_LIBRARIES Qt6::Test KF6::CoreAddons
)
set_tests_properties(indicatormetadatatest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_compile_definitions(indicatormetadatatest PRIVATE
    REPO_ROOT="${CMAKE_SOURCE_DIR}")

# Contract: KF6 KNSCore reads only KPackageStructure from a .knsrc (the KF5
# KPackageType key is dead); without it the Get New Indicators dialog opens
# empty with ConfigFileError.
ecm_add_test(contracts/indicatorsknsrctest.cpp
    TEST_NAME indicatorsknsrctest
    LINK_LIBRARIES Qt6::Test KF6::ConfigCore
)
set_tests_properties(indicatorsknsrctest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_compile_definitions(indicatorsknsrctest PRIVATE
    INDICATORS_KNSRC_PATH="${CMAKE_SOURCE_DIR}/app/latte-indicators.knsrc")

# Contract: Plasma 6 loads no [ActionPlugins] defaults, so the right-click
# menu only exists because of the chain the test header names (shipped
# defaults + GenericLayout re-assert + the KF6 filename-derived plugin id).
# The built plugin's path rides a generator expression so a rename in either
# CMake or metadata fails the test, not the live right-click.
ecm_add_test(contracts/contextmenudefaulttest.cpp
    TEST_NAME contextmenudefaulttest
    LINK_LIBRARIES Qt6::Test Qt6::Gui Plasma::Plasma
)
set_tests_properties(contextmenudefaulttest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_compile_definitions(contextmenudefaulttest PRIVATE
    LATTE_DEFAULTS_PATH="${CMAKE_SOURCE_DIR}/shell/package/contents/defaults"
    GENERICLAYOUT_CPP_PATH="${CMAKE_SOURCE_DIR}/app/layout/genericlayout.cpp"
    CONTEXTMENU_PLUGIN_FILE="$<TARGET_FILE:plasma_containmentactions_lattecontextmenu>"
    CONTEXTMENU_METADATA_JSON="${CMAKE_SOURCE_DIR}/containmentactions/contextmenu/plasma-containmentactions-lattecontextmenu.json")

# The Layouts submenu's QWidgetAction delegate paints text outside its child
# layout. Compile the production delegate and painter helpers directly so Qt 6
# menu measurement is exercised rather than source-scanned.
ecm_add_test(contracts/layoutmenuitemwidgettest.cpp
    ${CMAKE_SOURCE_DIR}/containmentactions/contextmenu/layoutmenuitemwidget.cpp
    ${CMAKE_BINARY_DIR}/containmentactions/contextmenu/generictools.cpp
    TEST_NAME layoutmenuitemwidgettest
    LINK_LIBRARIES Qt6::Test Qt6::Widgets
)
set_tests_properties(layoutmenuitemwidgettest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(layoutmenuitemwidgettest PRIVATE
    ${CMAKE_SOURCE_DIR}/containmentactions/contextmenu
    ${CMAKE_BINARY_DIR}/containmentactions/contextmenu)

# Contract: Plasma 6 removed applet.action(); the edit-mode handle must
# resolve standard actions through applet.plasmoid.internalAction(name) or
# Configure/Remove silently die with a TypeError.
ecm_add_test(contracts/editmodehandleactiontest.cpp
    TEST_NAME editmodehandleactiontest
    LINK_LIBRARIES Qt6::Test
)
set_tests_properties(editmodehandleactiontest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_compile_definitions(editmodehandleactiontest PRIVATE
    CONFIGOVERLAY_QML_PATH="${CMAKE_SOURCE_DIR}/containment/package/contents/ui/editmode/ConfigOverlay.qml")
