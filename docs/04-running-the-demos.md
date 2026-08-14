# Running the demos

Flash the `.uf2` from `build/` by holding BOOTSEL while plugging the board in,
then copying the file to the mass-storage device that appears. Output goes to
the USB CDC serial port (and to UART0 as well, for early boot messages).

## Where to put the sensor

Everything the A121 measures is **radial** — the component of motion along the
line between sensor and target. A ball leaving at speed `v` at an angle `theta`
to that line reads as `v cos(theta)`. At 20° off-line you under-read by 6%; at
30°, by 13%.

So for `ball_speed`, put the sensor on the target line, behind the ball, aimed
down the intended ball flight, roughly 0.65 m back — that is where the single
range gate sits. Raise it to about ball height. The closer the sensor is to the
line the ball actually takes, the closer the reading is to the true speed, and a
consistent mounting matters more than an exact one if you care about comparing
shots to each other.

For `swing_tracker`, the same applies, but the range window runs 0.30 m to
2.10 m so placement is less critical. Down the line still gives the cleanest
separation between the club head's speed and the ball's.

Safety, since this is a radar pointed at a golf ball: put it somewhere a mishit
cannot reach.

## sparse_iq_example

Run this first, every time you change wiring. It prints the applied
configuration and twenty frames of raw sparse IQ.

```
=== A121 sparse IQ example (LilyGO T-RADAR on RP2350) ===
[I/session] RSS a121-x.y.z

  profile           3
  PRF               15.6 MHz
  range             0.300 - 2.100 m in 16 points of 0.120 m
  HWAAS             28
  sweeps per frame  16
  sweep rate        2000 Hz (sensor maximum 6420 Hz)
  frame rate        125.0 Hz (period 8.000 ms)
  unambiguous speed +/- 2.5 m/s
  ...

frame  0  temp  24 C
  sweep 0:   1204  -880i   3310 +1120i ...
  amplitude:     1490     3494 ...
```

Amplitudes should be large where something solid is and should change when you
move a hand through the beam. If they are all zero, or frozen, go back to
`docs/01-wiring.md`.

## swing_tracker

CSV on stdout, one line per record:

```
F,<t_ms>,<detections>,<coarse_mps>,<coarse_conf>
T,<t_ms>,<id>,<class>,<range_m>,<speed_mps>,<amp_db>,<hits>
S,<frames>,<delayed>,<saturated>,<recalibrations>
```

`F` is per frame with at least one detection or track. `T` is one per confirmed
track: its id, its label (`CLUB`, `BALL` or `----`), filtered range, unwrapped
radial speed, smoothed amplitude in dB, and how many frames it has been seen in.
`S` appears every 5000 frames.

```sh
python3 tools/monitor.py /dev/ttyACM0             # live view
python3 tools/monitor.py /dev/ttyACM0 --raw | tee swing.csv
python3 tools/plot_swing.py swing.csv             # needs matplotlib
```

Watch the `S` line. Non-zero `delayed` means the host is not keeping up and
frames are being dropped — raise `TRADAR_SPI_BAUDRATE_HZ`, or reduce
`sweeps_per_frame` or `num_points`. Non-zero `saturated` means the return is
clipping; lower `cfg.receiver_gain` from 16.

### Tuning

In `src/apps/swing_tracker/main.c`:

- `DETECTION_THRESHOLD` (12.0) — peak height over the median of its range bin.
  Raise it if you see spurious tracks in a still room, lower it if the ball is
  missed.
- `MIN_SPEED_MPS` (1.5) — everything slower is treated as the static scene.
- `COARSE_CONFIDENCE_MIN` (0.35) — how concentrated the frame energy must be
  before the range-walk estimate is trusted for unwrapping.

In `tracker_default_config()`:

- `gate_m` — raised to 0.30 m by the application, because at 1.6 ms per frame a
  70 m/s ball moves 0.11 m and a tighter gate would break the track exactly when
  it matters.
- `ball_max_amp_ratio` (0.5) and `ball_min_speed_mps` (15.0) — the club/ball
  labelling. A track is called a ball if it is under half the amplitude of the
  strongest track and moving faster than the threshold. These are starting
  points; the right numbers depend on your mounting, and are best set by
  capturing a few shots and looking at the actual `amp_db` values.

## ball_speed

One block per shot, plus a machine-readable line:

```
V,<t_ms>,<speed_mps>,<snr>
SHOT,<id>,<club>,<ball>,<smash>,<t_impact_ms>,<confidence>,<samples>,<flags>

  shot 1
    club head    41.8 m/s  ( 93.5 mph)
    ball         59.6 m/s  (133.3 mph)
    smash         1.43
    confidence    0.87 over 139 frames
```

`V` lines are the live peak while something is moving faster than 5 m/s, useful
for checking the sensor sees your swing at all before you hit anything.

The detector arms when it sees anything above 12 m/s, records 250 ms, then finds
the largest frame-to-frame jump in peak speed — that is impact, because the ball
leaves at 1.3-1.5x the club-head speed, so the fastest thing in the spectrum
changes discontinuously in one frame. Club speed is read from before that point,
ball speed from the 60 ms after it. An 800 ms cooldown stops the follow-through
triggering a second shot.

### Reading the confidence and the flags

`implausible` means the smash factor fell outside 0.8-1.6. A legal driver tops
out near 1.5 and a wedge sits near 1.0, so anything outside that range is a
measurement problem rather than a remarkable shot. The usual causes:

- the sensor is well off the target line, so both speeds are foreshortened by
  different amounts
- the club was never seen, because the trigger fired late on a fast swing — in
  which case the club speed falls back to the slower of the two peaks just after
  impact, which is lower than the true club-head speed
- the ball was missed and what got reported as the ball is actually the club

Confidence combines how many frames the ball was present for with how strong its
return was, halved if the smash factor is implausible. Below about 0.4, treat the
numbers as suspect.

### Tuning

In `shot_default_config()`:

- `trigger_speed_mps` (12.0) — lower for slower swings, raise if practice
  waggles are triggering captures.
- `impact_jump_ratio` (1.25) — the frame-to-frame speed ratio that counts as
  impact. Lower it for wedges, where smash factor is near 1.0 and the step is
  small; raise it if noise triggers false impacts.
- `ball_window_ms` (60) — how long after impact to look for the ball's peak.
- `min_snr` (10.0) — peaks weaker than this are ignored entirely.

## Sign convention

Positive speed means receding. This follows Acconeer's Exploration Tool, which
maps Doppler frequency to speed as `frequency x perceived_wavelength` and treats
positive as moving away.

**This has not been checked on hardware.** Verify it once: run
`swing_tracker` and move a hand slowly away from the sensor. If the reported
speed is negative, rebuild with

```sh
-DRP_RADAR_DEFINES="RADAR_SPEED_SIGN=-1.0f"
```

`ball_speed` reports speed magnitudes, so it is unaffected either way.
