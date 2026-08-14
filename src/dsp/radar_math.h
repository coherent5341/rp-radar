/*
 * A121 physical constants and configuration arithmetic.
 *
 * The tables and formulas here mirror the ones Acconeer publishes in the
 * Exploration Tool (acconeer.exptool.a121._perf_calc and .algo._utils) so that
 * the firmware can pick a legal, timing-feasible sensor configuration on its
 * own instead of hard-coding magic numbers. Enumerations deliberately use the
 * same numeric values as the RSS enums; radar_session.c asserts that at compile
 * time.
 */
#ifndef RP_RADAR_MATH_H
#define RP_RADAR_MATH_H

#include <stdbool.h>
#include <stdint.h>

/* --- Physical constants ------------------------------------------------- */

#define RADAR_SPEED_OF_LIGHT_MPS 299792458.0f
/** A121 carrier. */
#define RADAR_FREQUENCY_HZ 60.5e9f
/** Free-space wavelength, ~4.9553 mm. */
#define RADAR_WAVELENGTH_M (RADAR_SPEED_OF_LIGHT_MPS / RADAR_FREQUENCY_HZ)
/**
 * Half the wavelength, ~2.4776 mm. Radar is a two-way path, so a target moving
 * by lambda/2 advances the measured phase by a full turn. Speed is therefore
 * simply doppler_frequency * RADAR_PERCEIVED_WAVELENGTH_M.
 */
#define RADAR_PERCEIVED_WAVELENGTH_M (RADAR_WAVELENGTH_M / 2.0f)
/** Distance between adjacent range points at step_length = 1. */
#define RADAR_BASE_STEP_LENGTH_M 0.0025f

/** Sparse IQ points per cycle; step_length must divide or be a multiple of it. */
#define RADAR_SPARSE_IQ_PPC 24u
#define RADAR_MAX_HWAAS 511u
/** num_points * sweeps_per_frame must not exceed the sensor buffer. */
#define RADAR_MAX_FRAME_ELEMENTS 4095u

typedef enum
{
	RADAR_PRF_19_5_MHZ = 0, /* profile 1 only */
	RADAR_PRF_15_6_MHZ,
	RADAR_PRF_13_0_MHZ,
	RADAR_PRF_8_7_MHZ,
	RADAR_PRF_6_5_MHZ,
	RADAR_PRF_5_2_MHZ,
	RADAR_PRF_COUNT
} radar_prf_t;

typedef enum
{
	RADAR_PROFILE_1 = 1,
	RADAR_PROFILE_2,
	RADAR_PROFILE_3,
	RADAR_PROFILE_4,
	RADAR_PROFILE_5
} radar_profile_t;

/* --- Doppler / speed ---------------------------------------------------- */

/**
 * Largest unambiguous radial speed for a given sweep rate, i.e. the Nyquist
 * edge: sweep_rate/2 Hz of Doppler maps to sweep_rate*lambda/4 m/s. Speeds
 * beyond this fold back into the measured interval.
 */
float radar_max_speed_from_sweep_rate(float sweep_rate_hz);

/**
 * Inverse of radar_max_speed_from_sweep_rate with an oversampling margin.
 * @p margin of 1.1 (Acconeer's choice) keeps the peak 10% clear of the edge,
 * where the Hann main lobe would otherwise straddle the fold.
 */
float radar_sweep_rate_for_max_speed(float max_speed_mps, float margin);

/* --- Range -------------------------------------------------------------- */

/** Convert a range point index to metres. */
float radar_point_to_meter(int32_t point);
/** Convert metres to the nearest range point index. */
int32_t radar_meter_to_point(float meter);
/** Half-power envelope width of a profile, in metres. */
float radar_envelope_fwhm_m(radar_profile_t profile);
/** Maximum measurable distance for a PRF, in metres. */
float radar_prf_max_measurable_distance_m(radar_prf_t prf);

/* --- Sweep timing ------------------------------------------------------- */

/**
 * Per-sample and per-point-overhead durations, in seconds. Both return false
 * for the combinations the sensor does not support (PRF 19.5 MHz above
 * profile 1).
 */
bool radar_sample_duration_s(radar_prf_t prf, radar_profile_t profile, float *duration_s);
bool radar_point_overhead_duration_s(radar_prf_t prf, radar_profile_t profile, float *duration_s);

/**
 * Estimated duration of one sweep. Model:
 *   num_points * (hwaas * sample_duration + point_overhead) + fixed overhead.
 * The result is an estimate; the authoritative figure is max_sweep_rate in the
 * processing metadata that RSS returns once the config is applied.
 */
bool radar_sweep_duration_s(radar_prf_t prf, radar_profile_t profile, uint16_t num_points,
                            uint16_t hwaas, float *duration_s);

/**
 * Largest HWAAS that still fits inside one sweep period at @p sweep_rate_hz.
 * Uses Acconeer's deliberately conservative formulation, so a configuration
 * accepted here has margin against the sensor's own timing check. Returns 0
 * when even HWAAS 1 does not fit, which means the sweep rate is unreachable
 * with that many points.
 */
uint16_t radar_max_hwaas(radar_prf_t prf, radar_profile_t profile, uint16_t num_points,
                         float sweep_rate_hz);

/**
 * Largest HWAAS that fits, using the per-point timing tables directly rather
 * than Acconeer's worst-case approximation.
 *
 * radar_max_hwaas() charges four sample durations of overhead per point, but
 * the published tables put the real figure nearer 1.25, so the conservative
 * form rejects configurations the sensor handles comfortably — including the
 * swing tracker's 16 points at 20 kHz. This variant uses the measured overhead
 * and applies @p safety (0.95 is a sensible default) as the margin instead.
 *
 * Both are estimates. The authoritative number is max_sweep_rate from the
 * processing metadata, which radar_session checks once the config is applied.
 */
uint16_t radar_hwaas_for_sweep_rate(radar_prf_t prf, radar_profile_t profile,
                                    uint16_t num_points, float sweep_rate_hz, float safety);

/**
 * Highest PRF whose maximum measurable distance covers @p end_point.
 * PRF 19.5 MHz is excluded for profiles other than 1, as the sensor requires.
 */
radar_prf_t radar_select_prf(int32_t end_point, radar_profile_t profile);

/** True if @p step_length divides, or is a multiple of, the sparse IQ PPC. */
bool radar_step_length_valid(uint16_t step_length);

#endif /* RP_RADAR_MATH_H */
