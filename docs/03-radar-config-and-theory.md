# How the measurement works, and how the configurations were chosen

## Sparse IQ

A frame of A121 sparse IQ is a matrix: `sweeps_per_frame` sweeps, each of
`num_points` complex samples, one per range gate. Range gates are spaced
`step_length x 2.5 mm` apart starting at `start_point x 2.5 mm`.

Amplitude tells you something is there. Phase tells you how it moves. Over one
sweep the phase is fixed; across sweeps it rotates at the Doppler frequency, and
that is where all speed information lives.

## Speed from phase

Radar is a two-way path, so a target moving by half a wavelength changes the
round trip by a full wavelength and turns the measured phase through 2π. At the
A121's 60.5 GHz the free-space wavelength is 4.955 mm, so the relevant quantity
is half of it:

```
perceived wavelength = lambda / 2 = 2.4776 mm
speed = doppler_frequency x 2.4776 mm
```

Doppler frequency comes from transforming the frame down the sweep axis. Sampled
at the sweep rate, the spectrum spans ±sweep_rate/2, which puts the largest
unambiguous speed at

```
max_speed = sweep_rate x lambda / 4 = sweep_rate x 1.2388 mm
```

and the resolution of a transform of length N at

```
resolution = (sweep_rate / N) x 2.4776 mm
```

These are `radar_max_speed_from_sweep_rate()` and `psd_speed_resolution()`.

## The constraint that shapes everything

Sweep rate is not free. A sweep measures every point in turn, and each point
costs `hwaas x sample_duration + point_overhead`, where the durations depend on
the PRF and the profile. Acconeer publishes the tables; they are transcribed in
`radar_math.c`. For profile 3 at PRF 15.6 MHz a sample takes 1026 ns and each
point adds 1282 ns of overhead, so

```
sweep_duration ~= num_points x (hwaas x 1026 ns + 1282 ns) + 2 us
```

More points means a slower sweep rate means a lower unambiguous speed. More
HWAAS means more averaging and better SNR, and also a slower sweep rate. There
is one budget and three things competing for it: **range coverage, speed
ceiling, and sensitivity.** The two demos resolve that competition differently,
and that is the entire reason there are two.

A golf ball leaves a driver at up to about 80 m/s and a club head arrives at
30-55 m/s. Measuring 80 m/s without folding needs

```
sweep_rate = 2 x 80 / 0.0024776 = 64.6 kHz
```

At that rate the whole sweep must fit in 15.5 µs, which allows one point with
generous averaging, or two with none. There is no configuration that both
reaches 80 m/s and covers two metres of range.

## Demo 2: ball_speed

Take the speed ceiling and give up range.

| | |
|---|---|
| profile | 5 |
| range | one point at 0.65 m |
| step length | 120 (unused with one point) |
| PRF | 15.6 MHz |
| HWAAS | derived, about 7 |
| sweeps per frame | 128, in 2 averaged segments of 64 |
| sweep rate | 71 kHz |
| frame rate | 555 Hz (1.80 ms) |
| unambiguous speed | ±88 m/s |
| speed resolution | 2.75 m/s |

Profile 5 earns its place twice. It has the highest radar loop gain of the five,
which a golf ball's small return needs; and its envelope is 0.32 m wide at half
power, against profile 3's 0.14 m. That width is what gives a single range point
useful dwell time — a 60 m/s ball crosses 0.32 m in 5.3 ms, so it is present for
about three frames rather than one. The cost is the direct leakage, which forces
the range start beyond twice the envelope width, hence 0.65 m.

Spending the whole budget on one point rather than two buys roughly 8 dB from
the extra HWAAS. Two points would extend the covered depth and so the dwell, at
that cost. Either is defensible; if your ball detections are marginal, try
`cfg.num_points = 2` in `radar_session_ball_speed_config()` and compare.

`continuous_sweep_mode` and `double_buffering` are both on. Continuous sweep
mode makes sweep timing uniform across frame boundaries as well as within a
frame, and double buffering lets the sensor keep sweeping while the host reads
the previous frame out. Between them there is no dead time — which matters when
the event you are trying to catch lasts five milliseconds.

## Demo 1: swing_tracker

Take the range coverage and unwrap the speed afterwards.

| | |
|---|---|
| profile | 3 |
| range | 0.30 m to 2.10 m, 16 points |
| step length | 48 (0.12 m) |
| PRF | 15.6 MHz |
| HWAAS | derived, 1 |
| sweeps per frame | 32 |
| sweep rate | 20 kHz |
| frame rate | 625 Hz (1.60 ms) |
| unambiguous speed | ±24.8 m/s |
| speed resolution | 1.55 m/s |

Profile 3's 0.14 m envelope is sampled every 0.12 m, so the range window has no
gaps, and starting at 0.30 m keeps clear of that profile's direct leakage.

±24.8 m/s does not cover a golf swing, so measurements fold: a 60 m/s ball reads
as 60 − 49.6 = 10.4 m/s. Two independent things unwrap it.

**Range walk.** A fast target crosses range bins during the frame itself. At
70 m/s it covers 0.112 m in the 1.6 ms a frame takes, close to one 0.12 m bin.
Splitting the frame into four blocks of sweeps, taking the amplitude-weighted
range centroid of each and fitting a line gives speed directly, with no aliasing
— coarse, but the aliases are 49.6 m/s apart, so being right to within 25 m/s is
enough to choose between them. `rd_range_walk_speed()` and `rd_dealias()`.

**Track continuity.** Once a target is being tracked, its own speed is a much
better guide than any single-frame estimate: a club head cannot change speed by
49.6 m/s in 1.6 ms. So the tracker unwraps against the track's current speed and
falls back to range walk only for detections that do not match an existing
track. `speed_measurement()` in `tracker.c`.

Range walk is only computed when the strongest detection is fast enough to
possibly be folded, and is only trusted when the energy is concentrated enough
that the centroid means something — with two comparable returns it sits between
them. That is the confidence value it returns.

## Detection

Peaks are tested against the **median** of the spectrum, not the mean. A handful
of large bins barely moves the median, so a strong club-head return does not
raise the threshold that the much weaker ball return has to clear — which is
precisely the situation a few milliseconds after impact, with both in the beam
at once. The threshold is a multiple of that median; 10-12 works on synthetic
data and is the right first thing to tune on real recordings.

Before the transform, each segment's complex mean is subtracted. The static
scene — direct leakage, the mat, the tee — is identical in every sweep, so it
lives entirely at zero Doppler; removing the mean removes it, instead of leaving
a large peak whose window sidelobes spread across the band. `test_psd` checks
both halves of that claim.

## Choosing your own configuration

`radar_math.h` has the pieces:

- `radar_sweep_rate_for_max_speed(speed, 1.1f)` — required rate for a speed
  ceiling, with Acconeer's 10% margin so the peak stays clear of the fold
- `radar_hwaas_for_sweep_rate(prf, profile, points, rate, 0.95f)` — largest
  averaging that fits, returning 0 if the rate is unreachable at that point count
- `radar_select_prf(end_point, profile)` — highest PRF covering the range end
- `radar_step_length_valid()` — step must divide or be a multiple of 24

`radar_session_open()` derives PRF and HWAAS from the rest of the configuration,
then compares what you asked for against the sensor's own reported
`max_sweep_rate` and warns if it cannot deliver. Believe the sensor: if that
warning appears, the speed axis is wrong, and the fix is fewer points, less
HWAAS, or a lower sweep rate.

Note that `radar_max_hwaas()` also exists and reproduces Acconeer's own
conservative estimate, which charges four sample durations of overhead per point
where the published tables say about 1.25. It rejects configurations the sensor
handles comfortably — including the swing tracker's — which is why the derivation
uses the table-based variant and leans on the runtime check for the truth.
