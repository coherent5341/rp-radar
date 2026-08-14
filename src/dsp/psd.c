#include "psd.h"

#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

bool psd_init(psd_t *psd, uint16_t sweeps_per_frame, uint16_t num_segments,
              float sweep_rate_hz, bool remove_mean)
{
	if (psd == NULL || num_segments == 0u || sweeps_per_frame == 0u || sweep_rate_hz <= 0.0f)
	{
		return false;
	}

	const uint16_t seg_len = (uint16_t)(sweeps_per_frame / num_segments);

	if (!fft_size_supported(seg_len))
	{
		return false;
	}

	if (!fft_init(&psd->fft, seg_len))
	{
		return false;
	}

	psd->seg_len        = seg_len;
	psd->num_segments   = num_segments;
	psd->sweeps_used    = (uint16_t)(seg_len * num_segments);
	psd->sample_rate_hz = sweep_rate_hz;
	psd->remove_mean    = remove_mean;
	psd->speed_sign     = RADAR_SPEED_SIGN;

	/*
	 * Periodic Hann, matching scipy.signal.get_window("hann", n) with
	 * sym=False, which is what scipy.signal.welch uses internally.
	 */
	float win_sq_sum = 0.0f;
	for (uint16_t i = 0; i < seg_len; i++)
	{
		const double phase = (2.0 * M_PI * (double)i) / (double)seg_len;
		const float  w     = (float)(0.5 * (1.0 - cos(phase)));

		psd->win[i] = w;
		win_sq_sum += w * w;
	}

	psd->scale = (win_sq_sum > 0.0f) ? (1.0f / (sweep_rate_hz * win_sq_sum)) : 1.0f;

	return true;
}

void psd_process(psd_t *psd, const iq16_t *frame, uint16_t num_points, float *out)
{
	const uint16_t seg_len = psd->seg_len;
	const uint16_t half    = (uint16_t)(seg_len / 2u);
	const float    avg     = 1.0f / (float)psd->num_segments;

	for (uint16_t point = 0; point < num_points; point++)
	{
		float *spectrum = &out[(uint32_t)point * seg_len];

		memset(spectrum, 0, sizeof(float) * seg_len);

		for (uint16_t seg = 0; seg < psd->num_segments; seg++)
		{
			const uint16_t first_sweep = (uint16_t)(seg * seg_len);

			/* Gather this segment's samples for this range point. */
			for (uint16_t i = 0; i < seg_len; i++)
			{
				const iq16_t *s = iq_at(frame, num_points, (uint16_t)(first_sweep + i), point);

				psd->scratch[i].re = (float)s->real;
				psd->scratch[i].im = (float)s->imag;
			}

			if (psd->remove_mean)
			{
				float mean_re = 0.0f;
				float mean_im = 0.0f;

				for (uint16_t i = 0; i < seg_len; i++)
				{
					mean_re += psd->scratch[i].re;
					mean_im += psd->scratch[i].im;
				}
				mean_re /= (float)seg_len;
				mean_im /= (float)seg_len;

				for (uint16_t i = 0; i < seg_len; i++)
				{
					psd->scratch[i].re -= mean_re;
					psd->scratch[i].im -= mean_im;
				}
			}

			for (uint16_t i = 0; i < seg_len; i++)
			{
				psd->scratch[i].re *= psd->win[i];
				psd->scratch[i].im *= psd->win[i];
			}

			fft_forward(&psd->fft, psd->scratch);

			/*
			 * Accumulate straight into fftshifted order: output index i holds
			 * transform bin (i + seg_len/2) mod seg_len, so index 0 is
			 * -fs/2 and index seg_len/2 is DC.
			 */
			for (uint16_t i = 0; i < seg_len; i++)
			{
				const uint16_t src = (uint16_t)((i + half) % seg_len);
				const float    re  = psd->scratch[src].re;
				const float    im  = psd->scratch[src].im;

				spectrum[i] += ((re * re) + (im * im));
			}
		}

		const float norm = psd->scale * avg;
		for (uint16_t i = 0; i < seg_len; i++)
		{
			spectrum[i] *= norm;
		}
	}
}

float psd_bin_to_speed(const psd_t *psd, float shifted_bin)
{
	const float half      = (float)psd->seg_len / 2.0f;
	const float frequency = (shifted_bin - half) * psd->sample_rate_hz / (float)psd->seg_len;

	return psd->speed_sign * frequency * RADAR_PERCEIVED_WAVELENGTH_M;
}

float psd_speed_to_bin(const psd_t *psd, float speed_mps)
{
	const float half      = (float)psd->seg_len / 2.0f;
	const float frequency = (psd->speed_sign * speed_mps) / RADAR_PERCEIVED_WAVELENGTH_M;

	return (frequency * (float)psd->seg_len / psd->sample_rate_hz) + half;
}

float psd_max_speed(const psd_t *psd)
{
	return radar_max_speed_from_sweep_rate(psd->sample_rate_hz);
}

float psd_speed_resolution(const psd_t *psd)
{
	return (psd->sample_rate_hz / (float)psd->seg_len) * RADAR_PERCEIVED_WAVELENGTH_M;
}
