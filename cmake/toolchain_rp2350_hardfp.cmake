#
# Cortex-M33 toolchain for the RP2350, built with -mfloat-abi=hard.
#
# Why this exists
# ---------------
# The Pico SDK's own Cortex-M33 toolchain file uses -mfloat-abi=softfp. Both
# softfp and hard use the FPU for arithmetic; they differ only in how floats are
# passed to and returned from functions — softfp uses the core registers, hard
# uses the VFP registers. Object files built with the two conventions cannot be
# linked together, and Acconeer's Cortex-M33 RSS build may well be hard-float.
#
# The symptom is a link error along the lines of
#
#   libacconeer_a121.a(...): uses VFP register arguments, <target>.elf does not
#
# If you hit that, configure with this toolchain so the whole project — the Pico
# SDK included, since it is built from source — uses the hard-float convention:
#
#   cmake -S . -B build \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain_rp2350_hardfp.cmake \
#         -DACCONEER_RSS_DIR=/path/to/a121-cortex-m33-sdk
#
# Use a clean build directory when switching, or you will link objects of both
# conventions. Verify what a library actually wants with:
#
#   arm-none-eabi-readelf -A libacconeer_a121.a | grep -i 'FP.*use\|ABI_VFP'
#
# This mirrors the SDK's pico_arm_cortex_m33_gcc.cmake, changing only the ABI.
#
set(CMAKE_SYSTEM_PROCESSOR cortex-m33)
set(PICO_DEFAULT_GCC_TRIPLE arm-none-eabi)

set(PICO_COMMON_LANG_FLAGS " -mcpu=cortex-m33 -mthumb -march=armv8-m.main+fp+dsp")
set(PICO_COMMON_LANG_FLAGS "${PICO_COMMON_LANG_FLAGS} -mfloat-abi=hard")
if (NOT PICO_NO_CMSE)
    set(PICO_COMMON_LANG_FLAGS "${PICO_COMMON_LANG_FLAGS} -mcmse")
endif()

include(${PICO_SDK_PATH}/cmake/preload/toolchains/util/pico_arm_gcc_common.cmake)
