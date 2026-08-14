/*
 * Synthetic sparse IQ generator.
 *
 * Produces frames with the same layout and scaling the A121 delivers, so the
 * whole DSP chain can be exercised on the host with known ground truth. It
 * models the three things that actually shape the data: a Gaussian range
 * envelope per target, phase rotating at the Doppler frequency, and range walk
 * within a frame. Phase is carried across frames so multi-frame tests see a
 * coherent signal rather than a new random one each time.
 */
#ifndef RP_RADAR_TEST_SYNTH_H
#define RP_RADAR_TEST_SYNTH_H

#include "dsp_types.h"

#define SYNTH_MAX_TARGETS 4u

typedef struct
{
	float range_m;
	float speed_mps; /**< Positive is receding, matching the DSP convention. */
	float amplitude; /**< Peak amplitude in ADC counts. */
	float fwhm_m;    /**< Range envelope width; use the profile's FWHM. */
} synth_target_t;

typedef struct
{
	uint16_t num_points;
	uint16_t sweeps_per_frame;
	float    start_m;
	float    step_m;
	float    sweep_rate_hz;

	/* A large, motionless return standing in for direct leakage and the mat. */
	float clutter_amplitude;
	float clutter_range_m;
	float clutter_fwhm_m;

	float noise_sigma;

	uint64_t rng;
	double   phase[SYNTH_MAX_TARGETS];
} synth_t;

void synth_init(synth_t *synth, uint16_t num_points, uint16_t sweeps_per_frame,
                float start_m, float step_m, float sweep_rate_hz, uint64_t seed);

/**
 * Render one frame. @p targets is indexed positionally; entry i keeps its own
 * phase accumulator across calls, so move a target by updating its range_m
 * between frames rather than reordering the array.
 */
void synth_frame(synth_t *synth, const synth_target_t *targets, uint16_t num_targets,
                 iq16_t *out);

/** Uniform in [0,1). Exposed so tests can be deterministic. */
double synth_uniform(synth_t *synth);

#endif /* RP_RADAR_TEST_SYNTH_H */
