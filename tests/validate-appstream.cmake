# SPDX-FileCopyrightText: 2026 Latte Dock contributors
# SPDX-FileCopyrightText: 2026 Bree Spektor
# SPDX-License-Identifier: GPL-2.0-or-later

if(NOT DEFINED APPSTREAMCLI OR NOT EXISTS "${APPSTREAMCLI}")
    message(FATAL_ERROR "APPSTREAMCLI does not name an executable: ${APPSTREAMCLI}")
endif()
if(NOT DEFINED APPSTREAM_METADATA OR NOT EXISTS "${APPSTREAM_METADATA}")
    message(FATAL_ERROR "configured AppStream metadata is missing: ${APPSTREAM_METADATA}")
endif()

execute_process(
    COMMAND "${APPSTREAMCLI}" validate --no-net "${APPSTREAM_METADATA}"
    RESULT_VARIABLE validation_status
    OUTPUT_VARIABLE validation_output
    ERROR_VARIABLE validation_error)
if(NOT validation_status EQUAL 0)
    message(FATAL_ERROR
        "appstreamcli rejected ${APPSTREAM_METADATA} with status ${validation_status}:\n"
        "${validation_output}${validation_error}")
endif()

file(READ "${APPSTREAM_METADATA}" metadata)
string(REGEX REPLACE "[ \t\r\n]+" " " metadata_compact "${metadata}")

function(assert_exactly_one expression description)
    string(REGEX MATCHALL "${expression}" matches "${metadata_compact}")
    list(LENGTH matches match_count)
    if(NOT match_count EQUAL 1)
        message(FATAL_ERROR
            "AppStream metadata must contain exactly one ${description}; found ${match_count}")
    endif()
endfunction()

assert_exactly_one("<component type=\"desktop-application\">"
    "desktop-application component root")
assert_exactly_one("<id>org[.]kde[.]latte-dock</id>"
    "org.kde.latte-dock component ID")
assert_exactly_one("<launchable type=\"desktop-id\">org[.]kde[.]latte-dock[.]desktop</launchable>"
    "org.kde.latte-dock.desktop launchable")
assert_exactly_one("<replaces> <id>org[.]kde[.]latte-dock[.]desktop</id> </replaces>"
    "migration from the released org.kde.latte-dock.desktop component ID")

if(metadata_compact MATCHES "<extends[ >]")
    message(FATAL_ERROR "standalone AppStream metadata must not extend another component")
endif()
if(metadata_compact MATCHES "liblatte2plugin[.]so")
    message(FATAL_ERROR "AppStream metadata advertises removed liblatte2plugin.so")
endif()

assert_exactly_one("<provides> <binary>latte-dock</binary> </provides>"
    "provides block containing only the latte-dock binary")
