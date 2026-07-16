# Pure-core unit tests (docs/QML_EXTRACTION_PLAN.md, section D). Each core
# header under a units/ directory (or an app-subtree capt placement listed in
# app-subtree-units.list) gets exactly one ecm_add_test entry here, named
# <header-basename>test. scripts/coverage-ratchet.sh fails build-check if a
# header has no registered pair, so keep the naming mechanical.
include(ECMAddTests)

# EX-22 ActivitySetAlgebra: header-only pure core, no Latte link needed.
ecm_add_test(activitysetalgebratest.cpp
    TEST_NAME activitysetalgebratest
    LINK_LIBRARIES Qt6::Test
)
set_tests_properties(activitysetalgebratest PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
target_include_directories(activitysetalgebratest PRIVATE
    ${CMAKE_SOURCE_DIR}/app)
