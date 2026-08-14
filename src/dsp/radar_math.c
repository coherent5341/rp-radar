#include "radar_math.h"

#include <math.h>

/*
 * Timing tables, in nanoseconds, transcribed from Acconeer's Exploration Tool
 * (_perf_calc._SAMPLE_DURATIONS_NS / ._POINT_OVERHEAD_DURATIONS_NS).
 * Row = PRF (radar_prf_t order), column = profile 1..5.
 * A zero marks a combination the sensor does not support.
 */
static const float SAMPLE_DURATION_NS[RADAR_PRF_COUNT][5] = {
    /* 19.5 MHz */ {1487.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* 15.6 MHz */ {1795.0f, 1344.0f, 1026.0f, 1026.0f, 1026.0f},
    /* 13.0 MHz */ {2103.0f, 1600.0f, 1231.0f, 1231.0f, 1231.0f},
    /*  8.7 MHz */ {3026.0f, 2369.0f, 1846.0f, 1846.0f, 1846.0f},
    /*  6.5 MHz */ {3949.0f, 3138.0f, 2462.0f, 2462.0f, 2462.0f},
    /*  5.2 MHz */ {4872.0f, 3908.0f, 3077.0f, 3077.0f, 3077.0f},
};

static const float POINT_OVERHEAD_NS[RADAR_PRF_COUNT][5] = {
    /* 19.5 MHz */ {1744.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    /* 15.6 MHz */ {2102.0f, 1612.0f, 1282.0f, 1282.0f, 1282.0f},
    /* 13.0 MHz */ {2462.0f, 1920.0f, 1539.0f, 1539.0f, 1539.0f},
    /*  8.7 MHz */ {3539.0f, 2844.0f, 2308.0f, 2308.0f, 2308.0f},
    /*  6.5 MHz */ {4615.0f, 3766.0f, 3077.0f, 3077.0f, 3077.0f},
    /*  5.2 MHz */ {5692.0f, 4689.0f, 3846.0f, 3846.0f, 3846.0f},
};

static const float PRF_MAX_MEASURABLE_DIST_M[RADAR_PRF_COUNT] = {
    3.1f, 5.1f, 7.0f, 12.7f, 18.5f, 24.3f,
};

static const float ENVELOPE_FWHM_M[5] = {0.04f, 0.07f, 0.14f, 0.19f, 0.32f};

/* Constant RSS adds to every sweep "for good measure". */
#define SWEEP_FIXED_OVERHEAD_S 2.0e-6f
/* RSS keeps a few percent of headroom; mirror it so our estimate stays inside. */
#define SWEEP_HEADROOM 0.98f

static bool profile_index(radar_profile_t profile, uint8_t *index)
{
	if (profile < RADAR_PROFILE_1 || profile > RADAR_PROFILE_5)
	{
		return false;
	}
	*index = (uint8_t)(profile - RADAR_PROFILE_1);
	return true;
}

float radar_max_speed_from_sweep_rate(float sweep_rate_hz)
{
	return sweep_rate_hz * RADAR_PERCEIVED_WAVELENGTH_M / 2.0f;
}

float radar_sweep_rate_for_max_speed(float max_speed_mps, float margin)
{
	if (margin <= 0.0f)
	{
		margin = 1.0f;
	}
	return margin * 2.0f * max_speed_mps / RADAR_PERCEIVED_WAVELENGTH_M;
}

float radar_point_to_meter(int32_t point)
{
	return (float)point * RADAR_BASE_STEP_LENGTH_M;
}

int32_t radar_meter_to_point(float meter)
{
	return (int32_t)lrintf(meter / RADAR_BASE_STEP_LENGTH_M);
}

float radar_envelope_fwhm_m(radar_profile_t profile)
{
	uint8_t idx;

	if (!profile_index(profile, &idx))
	{
		return 0.0f;
	}
	return ENVELOPE_FWHM_M[idx];
}

float radar_prf_max_measurable_distance_m(radar_prf_t prf)
{
	if (prf >= RADAR_PRF_COUNT)
	{
		return 0.0f;
	}
	return PRF_MAX_MEASURABLE_DIST_M[prf];
}

bool radar_sample_duration_s(radar_prf_t prf, radar_profile_t profile, float *duration_s)
{
	uint8_t idx;

	if (prf >= RADAR_PRF_COUNT || !profile_index(profile, &idx))
	{
		return false;
	}

	const float ns = SAMPLE_DURATION_NS[prf][idx];
	if (ns <= 0.0f)
	{
		return false;
	}

	*duration_s = ns * 1e-9f;
	return true;
}

bool radar_point_overhead_duration_s(radar_prf_t prf, radar_profile_t profile, float *duration_s)
{
	uint8_t idx;

	if (prf >= RADAR_PRF_COUNT || !profile_index(profile, &idx))
	{
		return false;
	}

	const float ns = POINT_OVERHEAD_NS[prf][idx];
	if (ns <= 0.0f)
	{
		return false;
	}

	*duration_s = ns * 1e-9f;
	return true;
}

bool radar_sweep_duration_s(radar_prf_t prf, radar_profile_t profile, uint16_t num_points,
                            uint16_t hwaas, float *duration_s)
{
	float sample_s;
	float overhead_s;

	if (num_points == 0u || hwaas == 0u)
	{
		return false;
	}
	if (!radar_sample_duration_s(prf, profile, &sample_s) ||
	    !radar_point_overhead_duration_s(prf, profile, &overhead_s))
	{
		return false;
	}

	const float per_point = ((float)hwaas * sample_s) + overhead_s;

	*duration_s = ((float)num_points * per_point) + SWEEP_FIXED_OVERHEAD_S;
	return true;
}

uint16_t radar_max_hwaas(radar_prf_t prf, radar_profile_t profile, uint16_t num_points,
                         float sweep_rate_hz)
{
	float sample_s;

	if (num_points == 0u || sweep_rate_hz <= 0.0f)
	{
		return 0u;
	}
	if (!radar_sample_duration_s(prf, profile, &sample_s))
	{
		return 0u;
	}

	const float sweep_period_s = 1.0f / sweep_rate_hz;
	const float usable_s       = (sweep_period_s - SWEEP_FIXED_OVERHEAD_S) * SWEEP_HEADROOM;

	if (usable_s <= 0.0f)
	{
		return 0u;
	}

	/*
	 * Acconeer expresses the budget in units of sample_duration and then
	 * subtracts 4 to cover the worst-case per-point overhead.
	 */
	const float budget_samples = usable_s / ((float)num_points * sample_s);
	const float hwaas_f        = floorf(budget_samples - 4.0f);

	if (hwaas_f < 1.0f)
	{
		return 0u;
	}
	if (hwaas_f > (float)RADAR_MAX_HWAAS)
	{
		return (uint16_t)RADAR_MAX_HWAAS;
	}

	return (uint16_t)hwaas_f;
}

uint16_t radar_hwaas_for_sweep_rate(radar_prf_t prf, radar_profile_t profile,
                                    uint16_t num_points, float sweep_rate_hz, float safety)
{
	float sample_s;
	float overhead_s;

	if (num_points == 0u || sweep_rate_hz <= 0.0f)
	{
		return 0u;
	}
	if (safety <= 0.0f || safety > 1.0f)
	{
		safety = 0.95f;
	}
	if (!radar_sample_duration_s(prf, profile, &sample_s) ||
	    !radar_point_overhead_duration_s(prf, profile, &overhead_s))
	{
		return 0u;
	}

	const float sweep_period_s = 1.0f / sweep_rate_hz;
	const float usable_s       = sweep_period_s - SWEEP_FIXED_OVERHEAD_S;

	if (usable_s <= 0.0f)
	{
		return 0u;
	}

	const float per_point_s = (usable_s / (float)num_points) * safety;
	const float hwaas_f     = floorf((per_point_s - overhead_s) / sample_s);

	if (hwaas_f < 1.0f)
	{
		return 0u;
	}
	if (hwaas_f > (float)RADAR_MAX_HWAAS)
	{
		return (uint16_t)RADAR_MAX_HWAAS;
	}

	return (uint16_t)hwaas_f;
}

radar_prf_t radar_select_prf(int32_t end_point, radar_profile_t profile)
{
	const float distance_m = radar_point_to_meter(end_point);

	/*
	 * Walk from the highest PRF (shortest measurement time) downwards and take
	 * the first one whose maximum measurable distance covers the range end.
	 */
	for (uint8_t prf = 0; prf < (uint8_t)RADAR_PRF_COUNT; prf++)
	{
		if (prf == (uint8_t)RADAR_PRF_19_5_MHZ && profile != RADAR_PROFILE_1)
		{
			continue;
		}
		if (distance_m < PRF_MAX_MEASURABLE_DIST_M[prf])
		{
			return (radar_prf_t)prf;
		}
	}

	return RADAR_PRF_5_2_MHZ;
}

bool radar_step_length_valid(uint16_t step_length)
{
	if (step_length == 0u)
	{
		return false;
	}

	const bool divides   = (RADAR_SPARSE_IQ_PPC % step_length) == 0u;
	const bool multiples = (step_length % RADAR_SPARSE_IQ_PPC) == 0u;

	return divides || multiples;
}
