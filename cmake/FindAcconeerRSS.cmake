#
# Locates the Acconeer A121 Radar System Software.
#
# RSS is closed source and licensed, so it is not — and cannot be — vendored
# here. Download the "A121 Cortex-M33" SDK from Acconeer's developer site and
# point ACCONEER_RSS_DIR at it:
#
#   cmake -S . -B build -DACCONEER_RSS_DIR=/path/to/acconeer_a121_cortex_m33
#
# The directory needs to contain the headers (acc_rss_a121.h and friends) and
# libacconeer_a121.a. Both the SDK's own layout (rss/include, rss/lib) and a
# flattened include/lib layout are accepted.
#
# Defines the imported target acconeer::rss.
#
# See docs/02-rss-library.md, in particular the note about the float ABI: the
# Pico SDK builds softfp by default and the vendor library may be hard, which
# shows up as a link error rather than as anything subtle.
#

set(ACCONEER_RSS_DIR "" CACHE PATH "Root of the unpacked Acconeer A121 Cortex-M33 SDK")

# Fall back to the environment when no -D was given. The Raspberry Pi Pico VS
# Code extension drives CMake itself and offers no way to add configure
# arguments, so setting ACCONEER_RSS_DIR once as a user environment variable is
# the only way to make that workflow find the SDK.
if(NOT ACCONEER_RSS_DIR AND DEFINED ENV{ACCONEER_RSS_DIR})
    set(ACCONEER_RSS_DIR "$ENV{ACCONEER_RSS_DIR}")
    message(STATUS "Using ACCONEER_RSS_DIR from the environment: ${ACCONEER_RSS_DIR}")
endif()

# Windows paths arrive with backslashes, which CMake treats as escapes.
file(TO_CMAKE_PATH "${ACCONEER_RSS_DIR}" ACCONEER_RSS_DIR)

option(RP_RADAR_RSS_STUB
       "Link a local stub instead of the real RSS library. Builds and links, but \
does nothing at runtime; useful only to check the toolchain and this project's \
own code compile." OFF)

find_path(ACCONEER_RSS_INCLUDE_DIR
    NAMES acc_rss_a121.h
    HINTS
        ${ACCONEER_RSS_DIR}
        ${ACCONEER_RSS_DIR}/include
        ${ACCONEER_RSS_DIR}/rss/include
        ${ACCONEER_RSS_DIR}/rss
    NO_DEFAULT_PATH
    NO_CMAKE_FIND_ROOT_PATH
)

#
# A missing SDK must not fail configure.
#
# Failing here would leave no build.ninja behind, and the Pico VS Code
# extension's build task only runs ninja, so the user would see nothing but
# "loading 'build.ninja': The system cannot find the file specified" with no
# hint as to the real cause. Report it instead and let the top-level
# CMakeLists turn it into a build-time error that actually says what is wrong.
#
set(ACCONEER_RSS_FOUND TRUE)
set(ACCONEER_RSS_PROBLEM "")

if(NOT ACCONEER_RSS_INCLUDE_DIR)
    set(ACCONEER_RSS_FOUND FALSE)
    if(ACCONEER_RSS_DIR)
        set(ACCONEER_RSS_PROBLEM
            "ACCONEER_RSS_DIR is set to '${ACCONEER_RSS_DIR}' but acc_rss_a121.h is not under it")
    else()
        set(ACCONEER_RSS_PROBLEM "ACCONEER_RSS_DIR is not set")
    endif()
endif()

if(ACCONEER_RSS_FOUND AND NOT RP_RADAR_RSS_STUB)
    find_library(ACCONEER_RSS_LIBRARY
        NAMES acconeer_a121 libacconeer_a121
        HINTS
            ${ACCONEER_RSS_DIR}
            ${ACCONEER_RSS_DIR}/lib
            ${ACCONEER_RSS_DIR}/rss/lib
        NO_DEFAULT_PATH
        NO_CMAKE_FIND_ROOT_PATH
    )

    if(NOT ACCONEER_RSS_LIBRARY)
        set(ACCONEER_RSS_FOUND FALSE)
        set(ACCONEER_RSS_PROBLEM
            "found the RSS headers at ${ACCONEER_RSS_INCLUDE_DIR} but no "
            "libacconeer_a121.a alongside them -- check you downloaded the "
            "Cortex-M33 build, not the ESP32, Cortex-M0 or Cortex-M4 one")
    endif()
endif()

if(ACCONEER_RSS_FOUND)
    add_library(acconeer_rss_iface INTERFACE)
    target_include_directories(acconeer_rss_iface INTERFACE ${ACCONEER_RSS_INCLUDE_DIR})

    if(RP_RADAR_RSS_STUB)
        message(WARNING
            "RP_RADAR_RSS_STUB is on: linking a do-nothing stub. The firmware will "
            "build but will not talk to a sensor.")

        add_library(acconeer_rss STATIC ${CMAKE_CURRENT_LIST_DIR}/../tools/rss_stub/rss_stub.c)
        target_link_libraries(acconeer_rss PUBLIC acconeer_rss_iface)
    else()
        add_library(acconeer_rss INTERFACE)
        target_link_libraries(acconeer_rss INTERFACE acconeer_rss_iface ${ACCONEER_RSS_LIBRARY})

        message(STATUS "Acconeer RSS library: ${ACCONEER_RSS_LIBRARY}")
    endif()

    add_library(acconeer::rss ALIAS acconeer_rss)
    message(STATUS "Acconeer RSS headers: ${ACCONEER_RSS_INCLUDE_DIR}")
endif()
