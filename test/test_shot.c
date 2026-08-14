#include <math.h>
#include <string.h>

#include "peaks.h"
#include "psd.h"
#include "shot.h"
#include "synth.h"
#include "test_util.h"

/* The ball speed demo's operating point. */
#define BS_POINTS 1u
#define BS_SPF 128u
#define BS_SEGMENTS 2u
#define BS_SWEEP_RATE 71000.0f
#define BS_SEG_LEN 64u
#define BS_RANGE_M 0.80f

static iq16_t g_frame[BS_SPF * BS_POINTS];
static float  g_spectrum[BS_SEG_LEN];
static float  g_scratch[BS_SEG_LEN];

/* Run one frame through the chain the firmware uses and hand it to the detector. */
static bool step(shot_t *shot, psd_t *psd, synth_t *synth, const synth_target_t *targets,
                 uint16_t num_targets, uint32_t t_ms, shot_result_t *out)
{
	synth_frame(synth, targets, num_targets, g_frame);
	psd_process(psd, g_frame, BS_POINTS, g_spectrum);

	const float median = peak_median(g_spectrum, BS_SEG_LEN, g_scratch);

	peak_t         peaks[PEAKS_MAX];
	const uint16_t found =
	    peak_find(g_spectrum, BS_SEG_LEN, median, 10.0f, 2u, 0u, 0u, peaks, PEAKS_MAX);

	return shot_push(shot, t_ms, peaks, found, psd, out);
}

void test_shot(void)
{
	psd_t psd;
	CHECK(psd_init(&psd, BS_SPF, BS_SEGMENTS, BS_SWEEP_RATE, true));
	CHECK(psd.seg_len == BS_SEG_LEN);

	/* 60 m/s must be inside the unambiguous interval for this demo to work. */
	CHECK(psd_max_speed(&psd) > 80.0f);

	shot_cfg_t cfg;
	shot_default_config(&cfg);

	shot_t shot;
	shot_init(&shot, &cfg);
	CHECK(shot_state(&shot) == SHOT_STATE_IDLE);

	synth_t synth;
	synth_init(&synth, BS_POINTS, BS_SPF, BS_RANGE_M, 0.30f, BS_SWEEP_RATE, 5150u);
	synth.clutter_range_m   = BS_RANGE_M;
	synth.clutter_amplitude = 6000.0f;
	synth.noise_sigma       = 60.0f;

	const float frame_period_ms = 1000.0f * (float)BS_SPF / BS_SWEEP_RATE;

	/*
	 * Index 0 is always the club and index 1 always the ball, so each keeps its
	 * own phase accumulator across frames.
	 */
	synth_target_t targets[2] = {
	    {.range_m = BS_RANGE_M, .speed_mps = 12.0f, .amplitude = 5000.0f, .fwhm_m = 0.32f},
	    {.range_m = BS_RANGE_M, .speed_mps = 60.0f, .amplitude = 1500.0f, .fwhm_m = 0.32f},
	};

	const uint16_t impact_frame = 50u;
	shot_result_t  result;
	bool           completed = false;

	for (uint16_t frame = 0; frame < 200u && !completed; frame++)
	{
		const uint32_t t_ms = (uint32_t)lrintf((float)frame * frame_period_ms);
		uint16_t       num_targets;

		if (frame < impact_frame)
		{
			/* Downswing: club accelerating from 12 to 42 m/s. */
			targets[0].speed_mps =
			    12.0f + (30.0f * (float)frame / (float)(impact_frame - 1u));
			targets[0].amplitude = 5000.0f;
			num_targets          = 1u;
		}
		else if (frame < (impact_frame + 6u))
		{
			/* Impact: ball away at 60 m/s, club slowed to 30 m/s, both in beam. */
			targets[0].speed_mps = 30.0f;
			targets[0].amplitude = 4000.0f;
			targets[1].speed_mps = 60.0f;
			targets[1].amplitude = 1500.0f;
			num_targets          = 2u;
		}
		else if (frame < 90u)
		{
			/* Follow-through: club only, decelerating and leaving the beam. */
			targets[0].speed_mps = 30.0f - (18.0f * (float)(frame - impact_frame - 6u) / 33.0f);
			targets[0].amplitude = 4000.0f;
			num_targets          = 1u;
		}
		else
		{
			num_targets = 0u;
		}

		completed = step(&shot, &psd, &synth, targets, num_targets, t_ms, &result);
	}

	CHECK(completed);

	if (completed)
	{
		CHECK(result.valid);
		CHECK_NEAR(result.club_speed_mps, 42.0, 3.0);
		CHECK_NEAR(result.ball_speed_mps, 60.0, 3.0);
		CHECK_NEAR(result.smash_factor, 60.0 / 42.0, 0.12);
		CHECK(!result.implausible);
		CHECK(result.confidence > 0.5f);
		CHECK(result.id == 1u);
		CHECK(result.num_samples > 50u);

		/* Impact should land on the frame where the ball appeared. */
		const uint32_t expected_ms = (uint32_t)lrintf((float)impact_frame * frame_period_ms);
		CHECK(result.t_impact_ms >= (expected_ms - 6u));
		CHECK(result.t_impact_ms <= (expected_ms + 6u));
	}

	/* The detector must be holding off rather than immediately re-arming. */
	CHECK(shot_state(&shot) == SHOT_STATE_COOLDOWN);

	/* Quiet frames past the cooldown return it to idle. */
	uint32_t t_ms = result.t_impact_ms + 100u;

	for (uint16_t i = 0; i < 800u && shot_state(&shot) != SHOT_STATE_IDLE; i++)
	{
		shot_result_t ignored;

		t_ms += (uint32_t)lrintf(frame_period_ms);
		(void)step(&shot, &psd, &synth, targets, 0u, t_ms, &ignored);
	}

	CHECK(shot_state(&shot) == SHOT_STATE_IDLE);
}

void test_shot_no_trigger(void)
{
	psd_t psd;
	CHECK(psd_init(&psd, BS_SPF, BS_SEGMENTS, BS_SWEEP_RATE, true));

	shot_cfg_t cfg;
	shot_default_config(&cfg);

	shot_t shot;
	shot_init(&shot, &cfg);

	synth_t synth;
	synth_init(&synth, BS_POINTS, BS_SPF, BS_RANGE_M, 0.30f, BS_SWEEP_RATE, 4004u);
	synth.clutter_range_m   = BS_RANGE_M;
	synth.clutter_amplitude = 6000.0f;
	synth.noise_sigma       = 60.0f;

	/* A practice waggle: real movement, but nowhere near the trigger speed. */
	synth_target_t slow = {
	    .range_m = BS_RANGE_M, .speed_mps = 3.0f, .amplitude = 5000.0f, .fwhm_m = 0.32f};

	const float frame_period_ms = 1000.0f * (float)BS_SPF / BS_SWEEP_RATE;
	bool        completed       = false;

	for (uint16_t frame = 0; frame < 150u && !completed; frame++)
	{
		shot_result_t result;
		const uint32_t t_ms = (uint32_t)lrintf((float)frame * frame_period_ms);

		completed = step(&shot, &psd, &synth, &slow, 1u, t_ms, &result);
	}

	CHECK(!completed);
	CHECK(shot_state(&shot) == SHOT_STATE_IDLE);
}
