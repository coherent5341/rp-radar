#
# Host build of the portable DSP layer plus its tests.
#
# Deliberately independent of the firmware build: it needs neither the Pico SDK
# nor the Acconeer RSS library, so the signal processing can be developed and
# regression-tested on a laptop and in CI.
#
#   cmake -S test -B build-test
#   cmake --build build-test
#   ./build-test/rp_radar_tests
#
cmake_minimum_required(VERSION 3.16)
project(rp_radar_tests C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(DSP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src/dsp)
set(SIM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../src/sim)

add_executable(rp_radar_tests
    main.c
    test_dsp.c
    test_rd.c
    test_shot.c
    ${SIM_DIR}/synth.c
    ${DSP_DIR}/fft.c
    ${DSP_DIR}/psd.c
    ${DSP_DIR}/peaks.c
    ${DSP_DIR}/radar_math.c
    ${DSP_DIR}/range_doppler.c
    ${DSP_DIR}/tracker.c
    ${DSP_DIR}/shot.c
)

target_include_directories(rp_radar_tests PRIVATE ${DSP_DIR} ${SIM_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_options(rp_radar_tests PRIVATE
    -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion -O2 -g
)

find_library(M_LIB m)
if(M_LIB)
    target_link_libraries(rp_radar_tests PRIVATE ${M_LIB})
endif()

enable_testing()
add_test(NAME dsp COMMAND rp_radar_tests)
