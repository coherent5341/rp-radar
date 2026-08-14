#include "range_doppler.h"

#include <math.h>

bool rd_init(rd_t *rd, uint16_t num_points, uint16_t spf, float sweep_rate_hz,
             float start_m, float step_m)
{
	if (rd == NULL || num_points == 0u || num_points > RD_MAX_POINTS)
	{
		return false;
	}

	/* One segment: full frame resolution, which is what a map wants. */
	if (!psd_init(&rd->psd, spf, 1u, sweep_rate_hz, true))
	{
		return false;
	}

	rd->num_points = num_points;
	rd->spf        = spf;
	rd->start_m    = start_m;
	rd->step_m     = step_m;

	return true;
}

void rd_process(rd_t *rd, const iq16_t *frame, float *map)
{
	psd_process(&rd->psd, frame, rd->num_points, map);
}

uint16_t rd_detect(rd_t *rd, const float *map, float threshold_rel, float min_speed_mps,
                   uint16_t guard, rd_det_t *out, uint16_t max_out)
{
	const uint16_t n = rd->psd.seg_len;

	if (max_out == 0u)
	{
		return 0u;
	}

	/*
	 * Mask the bins whose speed magnitude is below min_speed_mps. The speed
	 * axis is monotonic in bin index, so the mask is a single contiguous span
	 * around DC regardless of the sign convention.
	 */
	uint16_t exclude_lo = 0u;
	uint16_t exclude_hi = 0u;

	if (min_speed_mps > 0.0f)
	{
		const float b0 = psd_speed_to_bin(&rd->psd, -min_speed_mps);
		const float b1 = psd_speed_to_bin(&rd->psd, min_speed_mps);
		float       lo = (b0 < b1) ? b0 : b1;
		float       hi = (b0 < b1) ? b1 : b0;

		lo = floorf(lo);
		hi = ceilf(hi) + 1.0f;

		if (lo < 0.0f)
		{
			lo = 0.0f;
		}
		if (hi > (float)n)
		{
			hi = (float)n;
		}

		exclude_lo = (uint16_t)lo;
		exclude_hi = (uint16_t)hi;
	}

	uint16_t count = 0u;

	for (uint16_t point = 0; point < rd->num_points; point++)
	{
		const float *spectrum = &map[(uint32_t)point * n];
		const float  median   = peak_median(spectrum, n, rd->scratch);

		peak_t         peaks[PEAKS_MAX];
		const uint16_t found = peak_find(spectrum, n, median, threshold_rel, guard,
		                                 exclude_lo, exclude_hi, peaks, PEAKS_MAX);

		for (uint16_t p = 0; p < found; p++)
		{
			rd_det_t det;

			det.range_m   = rd->start_m + ((float)point * rd->step_m);
			det.speed_mps = psd_bin_to_speed(&rd->psd, peaks[p].bin);
			det.power     = peaks[p].power;
			det.snr       = peaks[p].snr;
			det.point     = point;
			det.bin       = peaks[p].bin;

			/* Keep the strongest max_out detections across the whole map. */
			uint16_t pos = count;

			while (pos > 0u && out[pos - 1u].power < det.power)
			{
				if (pos < max_out)
				{
					out[pos] = out[pos - 1u];
				}
				pos--;
			}

			if (pos < max_out)
			{
				out[pos] = det;
				if (count < max_out)
				{
					count++;
				}
			}
		}
	}

	return count;
}

float rd_range_walk_speed(rd_t *rd, const iq16_t *frame, uint16_t num_segments,
                          float *out_confidence)
{
	if (out_confidence != NULL)
	{
		*out_confidence = 0.0f;
	}

	if (num_segments < 2u || num_segments > RD_MAX_WALK_SEGMENTS || num_segments > rd->spf)
	{
		return 0.0f;
	}

	const uint16_t seg_len = (uint16_t)(rd->spf / num_segments);

	if (seg_len == 0u)
	{
		return 0.0f;
	}

	float centroid[RD_MAX_WALK_SEGMENTS];
	float weight[RD_MAX_WALK_SEGMENTS];

	float total_energy = 0.0f;
	float peak_share   = 0.0f;

	for (uint16_t seg = 0; seg < num_segments; seg++)
	{
		/*
		 * Per-range-point energy over this block of sweeps, with the block mean
		 * removed so the static scene does not drag the centroid.
		 */
		float energy_sum   = 0.0f;
		float weighted_sum = 0.0f;
		float max_energy   = 0.0f;

		for (uint16_t point = 0; point < rd->num_points; point++)
		{
			float mean_re = 0.0f;
			float mean_im = 0.0f;

			for (uint16_t i = 0; i < seg_len; i++)
			{
				const iq16_t *s =
				    iq_at(frame, rd->num_points, (uint16_t)((seg * seg_len) + i), point);

				mean_re += (float)s->real;
				mean_im += (float)s->imag;
			}
			mean_re /= (float)seg_len;
			mean_im /= (float)seg_len;

			float energy = 0.0f;

			for (uint16_t i = 0; i < seg_len; i++)
			{
				const iq16_t *s =
				    iq_at(frame, rd->num_points, (uint16_t)((seg * seg_len) + i), point);
				const float re = (float)s->real - mean_re;
				const float im = (float)s->imag - mean_im;

				energy += (re * re) + (im * im);
			}

			energy_sum += energy;
			weighted_sum += energy * (float)point;

			if (energy > max_energy)
			{
				max_energy = energy;
			}
		}

		if (energy_sum <= 0.0f)
		{
			return 0.0f;
		}

		centroid[seg] = weighted_sum / energy_sum;
		weight[seg]   = energy_sum;

		total_energy += energy_sum;
		peak_share += max_energy;
	}

	/* Weighted least-squares line fit of centroid against segment index. */
	float sw   = 0.0f;
	float sx   = 0.0f;
	float sy   = 0.0f;
	float sxx  = 0.0f;
	float sxy  = 0.0f;

	for (uint16_t seg = 0; seg < num_segments; seg++)
	{
		const float w = weight[seg];
		const float x = (float)seg;
		const float y = centroid[seg];

		sw += w;
		sx += w * x;
		sy += w * y;
		sxx += w * x * x;
		sxy += w * x * y;
	}

	const float denom = (sw * sxx) - (sx * sx);

	if (denom == 0.0f)
	{
		return 0.0f;
	}

	/* Bins per segment. */
	const float slope = ((sw * sxy) - (sx * sy)) / denom;

	/* Segment duration in seconds. */
	const float segment_s = (float)seg_len / rd->psd.sample_rate_hz;

	if (out_confidence != NULL && total_energy > 0.0f)
	{
		*out_confidence = peak_share / total_energy;
	}

	/*
	 * Increasing range bin means increasing distance, i.e. receding, which is
	 * positive under this file's sign convention.
	 */
	return (slope * rd->step_m) / segment_s;
}

float rd_dealias(float folded_speed, float max_speed, float coarse_speed)
{
	if (max_speed <= 0.0f)
	{
		return folded_speed;
	}

	const float span = 2.0f * max_speed;
	const float k    = roundf((coarse_speed - folded_speed) / span);

	return folded_speed + (k * span);
}
