set(lattedock-app_SRCS
    ${lattedock-app_SRCS}
    ${CMAKE_CURRENT_SOURCE_DIR}/abstractwindowinterface.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/schemecolors.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/waylandinterface.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/windowinfowrap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tasktools.cpp
)

# the X11 backend is best-effort and optional; its xcb/NETWM dependencies
# only exist when HAVE_X11 is on
if(HAVE_X11)
    set(lattedock-app_SRCS
        ${lattedock-app_SRCS}
        ${CMAKE_CURRENT_SOURCE_DIR}/xwindowinterface.cpp
    )
endif()

set(lattedock-app_SRCS ${lattedock-app_SRCS} PARENT_SCOPE)
