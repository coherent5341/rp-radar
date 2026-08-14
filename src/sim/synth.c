#include "synth.h"

#include <math.h>
#include <string.h>

#include "radar_math.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void synth_init(synth_t *synth, uint16_t num_points, uint16_t sweeps_per_frame,
                float start_m, float step_m, float sweep_rate_hz, uint64_t seed)
{
	memset(synth, 0, sizeof(*synth));

	synth->num_points        = num_points;
	synth->sweeps_per_frame  = sweeps_per_frame;
	synth->start_m           = start_m;
	synth->step_m            = step_m;
	synth->sweep_rate_hz     = sweep_rate_hz;
	synth->clutter_amplitude = 6000.0f;
	synth->clutter_range_m   = start_m + (step_m * 2.0f);
	synth->clutter_fwhm_m    = 0.14f;
	synth->noise_sigma       = 40.0f;
	synth->rng               = (seed != 0u) ? seed : 0x9E3779B97F4A7C15ull;
}

/* xorshift64*, chosen so results are identical on host and target. */
static uint64_t next_random(synth_t *synth)
{
	uint64_t x = synth->rng;

	x ^= x >> 12;
	x ^= x << 25;
	x ^= x >> 27;
	synth->rng = x;

	return x * 0x2545F4914F6CDD1Dull;
}

double synth_uniform(synth_t *synth)
{
	return (double)(next_random(synth) >> 11) / 9007199254740992.0;
}

static double gaussian(synth_t *synth)
{
	double u1 = synth_uniform(synth);
	const double u2 = synth_uniform(synth);

	if (u1 < 1e-12)
	{
		u1 = 1e-12;
	}

	return sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

/* Gaussian range envelope normalised to unity at the target's own range. */
static double envelope(double distance_m, double fwhm_m)
{
	if (fwhm_m <= 0.0)
	{
		return 0.0;
	}

	const double sigma = fwhm_m / 2.354820045;
	const double z     = distance_m / sigma;

	return exp(-0.5 * z * z);
}

static int16_t saturate(double v)
{
	if (v > 32767.0)
	{
		return 32767;
	}
	if (v < -32768.0)
	{
		return -32768;
	}

	return (int16_t)lrint(v);
}

void synth_frame(synth_t *synth, const synth_target_t *targets, uint16_t num_targets,
                 iq16_t *out)
{
	const uint16_t np  = synth->num_points;
	const uint16_t spf = synth->sweeps_per_frame;

	if (num_targets > SYNTH_MAX_TARGETS)
	{
		num_targets = SYNTH_MAX_TARGETS;
	}

	for (uint16_t s = 0; s < spf; s++)
	{
		const double t = (double)s / (double)synth->sweep_rate_hz;

		for (uint16_t p = 0; p < np; p++)
		{
			const double point_range = (double)synth->start_m + ((double)p * synth->step_m);

			double re = 0.0;
			double im = 0.0;

			/* Motionless clutter: same contribution in every sweep. */
			re += (double)synth->clutter_amplitude *
			      envelope(point_range - (double)synth->clutter_range_m,
			               (double)synth->clutter_fwhm_m);

			for (uint16_t k = 0; k < num_targets; k++)
			{
				const synth_target_t *tgt = &targets[k];

				/* Range walk inside the frame. */
				const double range_now = (double)tgt->range_m + ((double)tgt->speed_mps * t);
				const double gain =
				    (double)tgt->amplitude * envelope(point_range - range_now, (double)tgt->fwhm_m);

				if (gain < 1e-6)
				{
					continue;
				}

				const double doppler_hz =
				    (double)tgt->speed_mps / (double)RADAR_PERCEIVED_WAVELENGTH_M;
				const double phase = synth->phase[k] + (2.0 * M_PI * doppler_hz * t);

				re += gain * cos(phase);
				im += gain * sin(phase);
			}

			if (synth->noise_sigma > 0.0f)
			{
				re += gaussian(synth) * (double)synth->noise_sigma;
				im += gaussian(synth) * (double)synth->noise_sigma;
			}

			iq16_t *dst = &out[((uint32_t)s * np) + p];

			dst->real = saturate(re);
			dst->imag = saturate(im);
		}
	}

	/* Carry phase so consecutive frames stay coherent. */
	const double frame_duration = (double)spf / (double)synth->sweep_rate_hz;

	for (uint16_t k = 0; k < num_targets; k++)
	{
		const double doppler_hz =
		    (double)targets[k].speed_mps / (double)RADAR_PERCEIVED_WAVELENGTH_M;

		synth->phase[k] = fmod(synth->phase[k] + (2.0 * M_PI * doppler_hz * frame_duration),
		                       2.0 * M_PI);
	}
}
