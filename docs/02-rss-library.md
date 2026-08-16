# Getting the Acconeer RSS library

## Why you need it

The A121 has no documented register interface. All communication with the sensor
goes through Acconeer's Radar System Software, a closed-source static library
that speaks the sensor's proprietary SPI protocol and turns the raw returns into
sparse IQ. There is no way around it and nothing to reverse engineer productively
— RSS is the driver.

It is licensed, not redistributable, and therefore not vendored in this
repository. Neither the headers nor the library are here; the build locates your
own copy.

## Which package

Acconeer publishes RSS built for several architectures. From their developer
site (`developer.acconeer.com`, under A121 docs and software), the relevant
listings are:

- A121 Cortex-M0
- A121 Cortex-M4
- **A121 Cortex-M33** ← this one
- A121 ESP32 (what LilyGO's README points at, built for Xtensa)
- Raspberry Pi, STM32CubeIDE, and the module SDKs

The RP2350's ARM cores are Cortex-M33, so take the Cortex-M33 build. The ESP32
package will not link, and neither will the Cortex-M4 one — the object files
carry architecture attributes that the linker checks.

You will need an Acconeer developer account. Unpack the archive somewhere and
point the build at it:

```sh
cmake -S . -B build -DACCONEER_RSS_DIR=/path/to/acconeer_a121_cortex_m33
```

The build looks for `acc_rss_a121.h` and `libacconeer_a121.a`, accepting either
the SDK's own `rss/include` and `rss/lib` layout or a flattened `include`/`lib`
one. If it cannot find them, it says which one is missing.

## Pointing the build at it

Either pass it at configure time:

```sh
cmake -S . -B build -DACCONEER_RSS_DIR=/path/to/acconeer_a121_cortex_m33
```

or set it once in the environment, which is the only option that works with the
Raspberry Pi Pico VS Code extension, since that extension drives CMake itself
and offers no way to add configure arguments:

```powershell
# Windows, permanent for your user account. Restart VS Code afterwards.
setx ACCONEER_RSS_DIR "C:\dev\acconeer_a121_cortex_m33"
```

```sh
# Linux or macOS
export ACCONEER_RSS_DIR=/path/to/acconeer_a121_cortex_m33
```

## Building with the Pico VS Code extension

The extension's build task only runs `ninja -C build`, which needs a
`build.ninja` that CMake's *configure* step produces. If configure has not run
or has failed, the task fails with:

```
ninja: error: loading 'build.ninja': The system cannot find the file specified.
```

That message means "configure has not succeeded", not "ninja is broken". The
two causes are a missing `ACCONEER_RSS_DIR`, above, and a version mismatch in
the extension block at the top of `CMakeLists.txt`:

```cmake
set(sdkVersion 2.1.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
```

Those must match what is installed under `%USERPROFILE%\.pico-sdk`. Look in
`.pico-sdk\sdk`, `.pico-sdk\toolchain` and `.pico-sdk\picotool` to see what you
have. Running "Raspberry Pi Pico: Configure CMake" from the command palette
makes the extension rewrite them for you.

To see why configure actually failed — the extension can be quiet about it —
run it yourself from a terminal in the project folder, substituting your own
versions:

```powershell
cmake -S . -B build -G Ninja `
  -DCMAKE_MAKE_PROGRAM="$env:USERPROFILE/.pico-sdk/ninja/v1.13.2/ninja.exe" `
  -DPICO_SDK_PATH="$env:USERPROFILE/.pico-sdk/sdk/2.1.1" `
  -DPICO_TOOLCHAIN_PATH="$env:USERPROFILE/.pico-sdk/toolchain/14_2_Rel1" `
  -DACCONEER_RSS_DIR="C:/dev/acconeer_a121_cortex_m33"
```

CMake will then say plainly what is missing. Once that succeeds, the extension's
build task works normally, as does `cmake --build build`.

If you have no RSS SDK yet there is no way to build the firmware at all, since
RSS is the sensor driver. The host test suite under `test/` needs neither it nor
the Pico SDK, so that is the part to work on meanwhile.

## The float ABI

This is the one thing likely to bite, so it is worth understanding rather than
guessing at.

ARM has two calling conventions for hardware floating point. Both compute with
the FPU; they differ only in where float arguments and return values live.
`softfp` passes them in the core registers, `hard` passes them in the VFP
registers. Object files built with different conventions cannot be linked
together.

**The Pico SDK builds for Cortex-M33 with `-mfloat-abi=softfp`.** If Acconeer's
Cortex-M33 library was built `hard`, linking fails:

```
libacconeer_a121.a(acc_rss.o): uses VFP register arguments,
    build/ball_speed.elf does not
```

Check which one you have:

```sh
arm-none-eabi-readelf -A libacconeer_a121.a | grep -i 'ABI_VFP_args'
```

A `Tag_ABI_VFP_args: VFP registers` line means hard float. If present, build
everything — the Pico SDK included, since it is compiled from source — with the
same convention, using the toolchain file provided here:

```sh
cmake -S . -B build-hardfp \
      -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain_rp2350_hardfp.cmake \
      -DACCONEER_RSS_DIR=/path/to/a121-cortex-m33-sdk
```

Use a fresh build directory when switching. Mixing objects of both conventions
in one directory produces confusing partial failures.

If instead the library is soft float (`Tag_ABI_VFP_args` absent), the stock Pico
SDK toolchain is already correct and you need do nothing.

## Building without the library

To check that your toolchain and this project's own code are in order before the
SDK arrives:

```sh
cmake -S . -B build-stub -DACCONEER_RSS_DIR=/path/to/headers -DRP_RADAR_RSS_STUB=ON
cmake --build build-stub
```

This links `tools/rss_stub/rss_stub.c`, which defines every RSS symbol the
project references and does nothing. The firmware builds and boots, then reports
that it contains no radar software.

Note what this does and does not establish. It confirms the code compiles and
the Pico SDK links. It says nothing about the float ABI, because the stub is
built with your own flags and so always agrees with them. It still needs the
Acconeer headers — only the compiled library is replaced.

For actually working on the signal processing without hardware, the host test
suite under `test/` is the better tool: it runs the real DSP against synthetic
frames with known ground truth, and needs neither the Pico SDK nor RSS.

## What the port consists of

RSS reaches the hardware through exactly one struct, `acc_hal_a121_t`, which the
application hands over with `acc_rss_hal_register()`. It holds four function
pointers — SPI transfer, allocate, free, log — and a maximum transfer size.
Everything else in the SDK is architecture independent C.

`src/integration/acc_hal_integration_rp2350.c` provides that struct, plus the
board-control functions Acconeer's example applications call directly
(`acc_hal_integration_sensor_enable`, `..._wait_for_sensor_interrupt` and so
on), using the same names and signatures as the reference ports. That is the
whole of the platform port: the ESP32 build LilyGO points at differs from this
one only in the contents of that file and its sibling
`acc_integration_rp2350.c`.
