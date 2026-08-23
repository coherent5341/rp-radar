# rp-radar

Acconeer A121 pulsed coherent radar on an RP2350, using a LilyGO T-RADAR
breakout, with two golf demos: a swing tracker and a ball-speed measurement.

The T-RADAR V1.0 is a bare A121 module — an A121, a 24 MHz crystal, an RT9013-18
regulator for the sensor's 1.8 V core, and an 8-pin header. There is no MCU on
it, and every A121 GPIO and the CTRL pin are grounded on the board, so SPI plus
ENABLE and INTERRUPT is the entire interface. That makes it straightforward to
drive from something other than the ESP32 that LilyGO's README points at.

The vendor "demo code" LilyGO links to is Acconeer's A121 ESP32 SDK. Porting it
means replacing one file — the hardware abstraction layer — and leaving RSS, the
service API and the example applications untouched. That port is
`src/integration/acc_hal_integration_rp2350.c`.

## What is here

| Path | |
|---|---|
| `src/integration/` | The RP2350 port: SPI over DMA, chip select, ENABLE, the data-ready interrupt |
| `src/app/radar_session.c` | Sensor bring-up and the frame loop, shared by the applications |
| `src/dsp/` | Range-Doppler processing, tracking and shot detection. No RSS or Pico SDK dependency |
| `src/apps/sparse_iq_example/` | Port of Acconeer's `example_service`. Run this first |
| `src/apps/swing_tracker/` | Demo 1: range and speed of the club and ball, 625 frames/s |
| `src/apps/ball_speed/` | Demo 2: club-head speed, ball speed, smash factor per shot |
| `test/` | Host test suite for the DSP, with synthetic golf shots |
| `tools/` | Serial monitor and plotting |
| `tools/partscan/` | Bench parts list: scans DigiKey and LCSC codes into a database, sorted by component. Python and Docker, unrelated to the firmware |
| `docs/` | Wiring, getting RSS, the radar configuration, running the demos |

## Quick start

```sh
# 1. Prerequisites: arm-none-eabi-gcc, cmake, the Pico SDK, and the Acconeer
#    A121 Cortex-M33 SDK (see docs/02-rss-library.md — it is licensed and
#    cannot be redistributed here).
export PICO_SDK_PATH=/path/to/pico-sdk

# 2. Wire it up: six signals and power. See docs/01-wiring.md.

# 3. Build.
cmake -S . -B build -DACCONEER_RSS_DIR=/path/to/a121-cortex-m33-sdk
cmake --build build

# 4. Flash build/sparse_iq_example.uf2 and check you get sane amplitudes,
#    then move on to swing_tracker.uf2 or ball_speed.uf2.
```

The DSP builds and tests on the host with no hardware and no vendor SDK:

```sh
cmake -S test -B build-test && cmake --build build-test && ./build-test/rp_radar_tests
```

## The two demos, and why they are different

Both measure speed from the same physics — a target moving by half a wavelength
(2.48 mm) turns the measured phase through one full cycle, so radial speed is
just Doppler frequency times 2.48 mm. The Doppler axis is sampled at the sweep
rate, which puts the largest unambiguous speed at `sweep_rate x lambda / 4`. The
sensor cannot sweep quickly and cover a wide range at the same time, and that
one constraint is what separates the demos.

**`swing_tracker` buys coverage.** 16 range points from 0.30 m to 2.10 m, at
20 kHz, giving 625 frames per second. You see where things are as well as how
fast they move: the club head coming down through the range gates, the strike,
the ball leaving, the follow-through. The cost is that the unambiguous window is
only ±24.8 m/s, so a real swing folds. Folded readings are unwrapped using range
walk — a fast target crosses several range bins during a single 1.6 ms frame,
and the slope of that motion is a coarse but alias-free speed measurement, good
enough to pick which alias is real.

**`ball_speed` buys precision.** One range point, and the whole sweep budget
spent on rate: 71 kHz, which puts the folding limit at ±88 m/s. Nothing a golf
club can do to a ball folds, so no unwrapping is needed and the measurement is a
direct frequency reading with 2.7 m/s resolution. The cost is that there is no
range information at all.

`docs/03-radar-config-and-theory.md` works through the arithmetic.

## Status

Verified here:

- The DSP passes 152 checks against synthetic data with known ground truth,
  including an end-to-end golf shot (club 42 m/s, ball 60 m/s, smash 1.43) and
  correct unwrapping of a 60 m/s target from a ±24.8 m/s window.
- Speed estimation error of 0.034 m/s RMS, 0.068 m/s worst, measured over the
  `ball_speed` configuration with the true speed swept across every sub-bin
  position. The 2.75 m/s bin width is the two-target separation limit, not the
  accuracy on a single peak — see the accuracy section of
  `docs/03-radar-config-and-theory.md`, which also explains why alignment, not
  resolution, is what actually limits a real measurement.
- All three applications compile and link clean for Cortex-M33 (v8-M.mainline,
  FPv5/FP-D16) with `-Wall -Wextra -Wshadow`, and produce UF2 images.

Not verified here, because this was written without the hardware to hand:

- **The Doppler sign convention.** Positive speed is taken to mean receding,
  following Acconeer's Exploration Tool. Check it once with a hand moving slowly
  away from the sensor; if it is inverted, build with `-DRADAR_SPEED_SIGN=-1.0f`.
- **Achievable sweep rates.** The configurations are derived from Acconeer's
  published timing tables, and `radar_session` checks them against the sensor's
  own `max_sweep_rate` at startup and warns if it cannot deliver. If it does,
  reduce the point count or the sweep rate.
- **Detection thresholds and the club/ball classification.** These are starting
  points to tune against your own recordings, not tuned constants. The
  amplitude ratio that separates a club head from a ball depends on how the
  sensor is mounted.

## Licensing

This repository is MIT licensed. The Acconeer Radar System Software is not
included, and is not redistributable — download it yourself under Acconeer's
terms. No Acconeer headers or binaries are vendored here; the build locates
your copy.
