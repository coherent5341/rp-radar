#
# rp-radar — LilyGO T-RADAR (Acconeer A121) on an RP2350.
#
#   cmake -S . -B build -DACCONEER_RSS_DIR=/path/to/a121-cortex-m33-sdk
#   cmake --build build
#
# Produces three UF2 images in build/: sparse_iq_example, swing_tracker and
# ball_speed. The host-side DSP tests are a separate project under test/ and
# need neither the Pico SDK nor RSS.
#
cmake_minimum_required(VERSION 3.13)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
# The extension rewrites the three version numbers below to match what it has
# installed, and pico-vscode.cmake then supplies PICO_SDK_PATH,
# PICO_TOOLCHAIN_PATH and picotool. Without this block the extension's configure
# step has no SDK to point at, so no build.ninja is produced and the build task
# fails with "loading 'build.ninja': The system cannot find the file specified".
#
# If configure complains that a version is missing, check what you actually have
# under ~/.pico-sdk (%USERPROFILE%\.pico-sdk on Windows) and correct these, or
# re-run "Raspberry Pi Pico: Configure CMake" and let the extension set them.
#
# Building from the command line instead? This block is a no-op when
# pico-vscode.cmake is absent, so PICO_SDK_PATH works as usual.
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================

# RP2350, Cortex-M33. Override with -DPICO_BOARD=... for a different RP2350
# board; the pin assignment lives in src/board/t_radar_pins.h either way.
set(PICO_PLATFORM rp2350 CACHE STRING "Pico platform")
set(PICO_BOARD pico2 CACHE STRING "Pico board")

include(cmake/pico_sdk_import.cmake)

project(rp_radar C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

pico_sdk_init()

include(cmake/FindAcconeerRSS.cmake)

#
# Without RSS there is no sensor driver and so no firmware to build. Configure
# still succeeds, deliberately: a hard failure here would leave no build.ninja,
# and the Pico VS Code extension's build task runs only ninja, so the sole
# symptom would be "loading 'build.ninja': The system cannot find the file
# specified" -- which says nothing about the actual cause. Generating a target
# that fails loudly puts the explanation where the user is already looking.
#
if(NOT ACCONEER_RSS_FOUND)
    message(WARNING
        "Acconeer RSS not found, so no firmware targets will be created "
        "(${ACCONEER_RSS_PROBLEM}). The build will stop with instructions. "
        "See docs/02-rss-library.md.")

    add_custom_target(rss_missing ALL
        COMMAND ${CMAKE_COMMAND} -E cmake_echo_color --red --bold
                "The Acconeer A121 RSS library is required to build this firmware."
        COMMAND ${CMAKE_COMMAND} -E echo
                "  Problem: ${ACCONEER_RSS_PROBLEM}"
        COMMAND ${CMAKE_COMMAND} -E echo
                "  RSS is the A121's driver and is closed source, so it cannot be"
        COMMAND ${CMAKE_COMMAND} -E echo
                "  shipped here. Download the Cortex-M33 build from Acconeer, then"
        COMMAND ${CMAKE_COMMAND} -E echo
                "  either set the ACCONEER_RSS_DIR environment variable and"
        COMMAND ${CMAKE_COMMAND} -E echo
                "  reconfigure, or pass -DACCONEER_RSS_DIR=... to cmake."
        COMMAND ${CMAKE_COMMAND} -E echo
                "  Details: docs/02-rss-library.md"
        COMMAND ${CMAKE_COMMAND} -E echo
                "  No sensor SDK yet? The host DSP tests need neither it nor the"
        COMMAND ${CMAKE_COMMAND} -E echo
                "  Pico SDK: cmake -S test -B build-test && cmake --build build-test"
        COMMAND ${CMAKE_COMMAND} -E false
        VERBATIM
    )

    return()
endif()

# --- Portable signal processing -------------------------------------------
# No Pico SDK and no RSS dependency, so the same sources build for the host
# test suite. Keep it that way.
add_library(rp_radar_dsp STATIC
    src/dsp/fft.c
    src/dsp/psd.c
    src/dsp/peaks.c
    src/dsp/radar_math.c
    src/dsp/range_doppler.c
    src/dsp/tracker.c
    src/dsp/shot.c
)
target_include_directories(rp_radar_dsp PUBLIC src/dsp)

# --- Platform port and sensor session -------------------------------------
add_library(rp_radar_port STATIC
    src/integration/acc_hal_integration_rp2350.c
    src/integration/acc_integration_rp2350.c
    src/app/radar_session.c
)
target_include_directories(rp_radar_port PUBLIC
    src/board
    src/integration
    src/app
)
target_link_libraries(rp_radar_port PUBLIC
    rp_radar_dsp
    acconeer::rss
    pico_stdlib
    hardware_spi
    hardware_dma
    hardware_gpio
)

# Pin overrides and tuning knobs, e.g.
#   cmake ... -DRP_RADAR_DEFINES="TRADAR_PIN_ENABLE=14;TRADAR_SPI_BAUDRATE_HZ=8000000"
set(RP_RADAR_DEFINES "" CACHE STRING "Extra compile definitions, semicolon separated")
if(RP_RADAR_DEFINES)
    target_compile_definitions(rp_radar_port PUBLIC ${RP_RADAR_DEFINES})
    target_compile_definitions(rp_radar_dsp PUBLIC ${RP_RADAR_DEFINES})
endif()

target_compile_options(rp_radar_dsp PRIVATE -Wall -Wextra -Wshadow)
target_compile_options(rp_radar_port PRIVATE -Wall -Wextra -Wshadow)

# --- Applications ----------------------------------------------------------
function(add_radar_app name)
    add_executable(${name} src/apps/${name}/main.c)
    target_link_libraries(${name} PRIVATE rp_radar_port)
    target_compile_options(${name} PRIVATE -Wall -Wextra -Wshadow)

    # USB CDC carries the data streams; UART stays on for early boot messages
    # and for when USB is not enumerated.
    pico_enable_stdio_usb(${name} 1)
    pico_enable_stdio_uart(${name} 1)

    pico_add_extra_outputs(${name})
endfunction()

add_radar_app(sparse_iq_example)
add_radar_app(swing_tracker)
add_radar_app(ball_speed)
