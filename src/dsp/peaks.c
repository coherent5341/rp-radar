#include "peaks.h"

#include <math.h>
#include <string.h>

/* Partial sort: places the k-th smallest element of x[0..n) at index k. */
static void quickselect(float *x, uint16_t n, uint16_t k)
{
	uint16_t lo = 0;
	uint16_t hi = (uint16_t)(n - 1u);

	while (lo < hi)
	{
		const float pivot = x[(uint16_t)(lo + ((hi - lo) / 2u))];
		uint16_t    i     = lo;
		uint16_t    j     = hi;

		while (i <= j)
		{
			while (x[i] < pivot)
			{
				i++;
			}
			while (x[j] > pivot)
			{
				if (j == 0u)
				{
					break;
				}
				j--;
			}

			if (i <= j)
			{
				const float tmp = x[i];

				x[i] = x[j];
				x[j] = tmp;
				i++;
				if (j == 0u)
				{
					break;
				}
				j--;
			}
		}

		if (k <= j)
		{
			hi = j;
		}
		else if (k >= i)
		{
			lo = i;
		}
		else
		{
			break;
		}
	}
}

float peak_median(const float *x, uint16_t n, float *scratch)
{
	if (n == 0u)
	{
		return 0.0f;
	}

	memcpy(scratch, x, sizeof(float) * n);
	const uint16_t mid = (uint16_t)((n - 1u) / 2u);

	quickselect(scratch, n, mid);

	return scratch[mid];
}

float peak_parabolic(const float *y, uint16_t i, uint16_t n)
{
	if (i == 0u || i + 1u >= n)
	{
		return (float)i;
	}

	const float y1 = y[i - 1u];
	const float y2 = y[i];
	const float y3 = y[i + 1u];

	/* Vertex of the parabola through the three samples, in bins from i. */
	const float denom = (y1 - (2.0f * y2)) + y3;

	if (denom == 0.0f)
	{
		return (float)i;
	}

	float delta = (0.5f * (y1 - y3)) / denom;

	/* A well-formed peak puts the vertex inside the central bin. */
	if (delta > 0.5f)
	{
		delta = 0.5f;
	}
	else if (delta < -0.5f)
	{
		delta = -0.5f;
	}

	return (float)i + delta;
}

float peak_parabolic_log(const float *y, uint16_t i, uint16_t n)
{
	if (i == 0u || i + 1u >= n)
	{
		return (float)i;
	}

	/* Floor rather than clamp: a zero bin should sit far below, not alongside. */
	const float y1 = logf(y[i - 1u] + 1e-30f);
	const float y2 = logf(y[i] + 1e-30f);
	const float y3 = logf(y[i + 1u] + 1e-30f);

	const float denom = (y1 - (2.0f * y2)) + y3;

	if (denom == 0.0f)
	{
		return (float)i;
	}

	float delta = (0.5f * (y1 - y3)) / denom;

	if (delta > 0.5f)
	{
		delta = 0.5f;
	}
	else if (delta < -0.5f)
	{
		delta = -0.5f;
	}

	return (float)i + delta;
}

uint16_t peak_find(const float *spectrum, uint16_t n, float median, float threshold_rel,
                   uint16_t guard, uint16_t exclude_lo, uint16_t exclude_hi,
                   peak_t *out, uint16_t max_out)
{
	if (n < 3u || max_out == 0u)
	{
		return 0u;
	}

	const float threshold = median * threshold_rel;
	uint16_t    count     = 0u;

	for (uint16_t i = 1u; i + 1u < n; i++)
	{
		if (exclude_lo < exclude_hi && i >= exclude_lo && i < exclude_hi)
		{
			continue;
		}

		/*
		 * Strict on one side only, so a flat-topped peak is reported once
		 * rather than not at all.
		 */
		if (spectrum[i] < spectrum[i - 1u] || spectrum[i] <= spectrum[i + 1u])
		{
			continue;
		}
		if (spectrum[i] <= threshold)
		{
			continue;
		}

		/*
		 * Insertion sort by descending power, with non-maximum suppression:
		 * a candidate too close to an accepted stronger peak is dropped, and
		 * accepting a candidate evicts weaker neighbours within the guard.
		 */
		bool suppressed = false;

		for (uint16_t k = 0; k < count; k++)
		{
			const float separation = (out[k].bin > (float)i) ? (out[k].bin - (float)i)
			                                                 : ((float)i - out[k].bin);

			if (separation <= (float)guard && out[k].power >= spectrum[i])
			{
				suppressed = true;
				break;
			}
		}

		if (suppressed)
		{
			continue;
		}

		/* Drop weaker peaks inside the guard band of this stronger one. */
		uint16_t write = 0u;
		for (uint16_t k = 0; k < count; k++)
		{
			const float separation = (out[k].bin > (float)i) ? (out[k].bin - (float)i)
			                                                 : ((float)i - out[k].bin);

			if (separation <= (float)guard)
			{
				continue;
			}
			out[write++] = out[k];
		}
		count = write;

		peak_t candidate;

		candidate.bin   = peak_parabolic_log(spectrum, i, n);
		candidate.power = spectrum[i];
		candidate.snr   = (median > 0.0f) ? (spectrum[i] / median) : 0.0f;

		uint16_t pos = count;
		while (pos > 0u && out[pos - 1u].power < candidate.power)
		{
			if (pos < max_out)
			{
				out[pos] = out[pos - 1u];
			}
			pos--;
		}

		if (pos < max_out)
		{
			out[pos] = candidate;
			if (count < max_out)
			{
				count++;
			}
		}
	}

	return count;
}
