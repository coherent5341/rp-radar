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

## Resolution is not accuracy

The bin width — 2.75 m/s for `ball_speed`, 1.55 m/s for `swing_tracker` — is
how far apart two targets must be to appear as two peaks. It is *not* how
accurately a single isolated peak can be located, which is much better, because
the peak's shape across neighbouring bins says where inside the bin it sits.

Measured over the shipped `ball_speed` configuration, with the true speed swept
from 30 to 85 m/s in steps that do not divide the bin width so every sub-bin
position is sampled (`test_interpolation_accuracy`):

| estimator | RMS error | worst |
|---|---|---|
| nearest bin | 0.79 m/s | 1.35 m/s |
| parabola on power | 0.224 m/s | 0.33 m/s |
| **parabola on log power** (shipped) | **0.034 m/s** | **0.068 m/s** |

0.034 m/s RMS is 0.06% at a 60 m/s ball speed. For comparison, commercial
launch monitors quote ball speed to about ±0.45 m/s.

The middle row is worth dwelling on, because it explains the choice. Its error
stayed at 0.224 m/s across an SNR sweep from 11000 down to 200 — an error that
does not improve with SNR is bias, not noise. It comes from fitting a parabola
to the wrong shape: a Hann-windowed tone's main lobe is close to Gaussian, and
a Gaussian is exactly a parabola in the *log* domain, not the linear one.
Taking the log first removes the bias for a handful of instructions.

Below roughly SNR 30 the two estimators converge, because noise then dominates
the bias; and below about SNR 14 detection itself fails, since the threshold is
a multiple of the median. At that point the limit is sensitivity, not
interpolation — more HWAAS, a shorter range, or a lower threshold.

The same applies along the range axis. `rd_detect` interpolates range across
neighbouring gates, so reported range is not restricted to the 0.12 m grid: a
target at 0.96 m reads 0.95 m rather than snapping to 0.90 m.

### So what actually limits the measurement

Not the resolution. In rough order of size for a real shot:

1. **Alignment.** Radial speed is `v cos(theta)`. At 20° off the ball's line
   you lose 6%, which at 60 m/s is 3.6 m/s — a hundred times the estimator
   error. This dominates everything else and no amount of signal processing
   fixes it; it is a mounting problem.
2. **The club is not a point.** Its head, hosel and shaft are at different
   radii and so at genuinely different speeds, spreading its return over
   several m/s. Club-head speed from a Doppler peak is really the speed of the
   fastest strong scattering centre, which is about what you want, but expect
   it to be noisier than the ball figure. The ball, being a sphere whose return
   is dominated by the specular point, is the cleaner target of the two.
3. **Dwell.** The ball is in the beam for around 5 ms, so a handful of frames.
   Catching its peak reliably matters more than measuring any one frame better.

Which is why the recommendation is: get the sensor on the target line first,
and only then worry about resolution.

### If you do want more

- **Longer coherent integration.** Resolution is `sweep_rate / N`. Setting
  `PSD_SEGMENTS` to 1 in `ball_speed` doubles N to 128 and halves the bin to
  1.37 m/s, at the cost of the averaging that steadies the peak. Going further
  means longer frames, which is bounded by the ball's ~5 ms dwell — beyond
  about 256 sweeps (3.6 ms) you are integrating over an interval where the
  ball is leaving the beam, and the amplitude taper broadens the peak again.
  Measured RMS at 256 sweeps was 0.007 m/s, which is far below what the
  alignment error makes meaningful.
- **Finer range.** Range resolution is set by the profile envelope, not the
  gate spacing: profile 2 is 0.07 m against profile 3's 0.14 m. It costs loop
  gain, and needs twice the points for the same span, which costs sweep rate.
- **More sensitivity.** HWAAS is the direct knob, and it is what the sweep rate
  is competing for.

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
