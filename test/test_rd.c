#include <string.h>

#include "range_doppler.h"
#include "synth.h"
#include "test_util.h"
#include "tracker.h"

/* The swing tracker's operating point. */
#define TRK_POINTS 16u
#define TRK_SPF 32u
#define TRK_SWEEP_RATE 20000.0f
#define TRK_START_M 0.30f
#define TRK_STEP_M 0.12f

static iq16_t g_frame[TRK_SPF * TRK_POINTS];
static float  g_map[TRK_POINTS * TRK_SPF];

/* True if some detection sits near the given range and speed. */
static bool has_detection(const rd_det_t *dets, uint16_t n, float range_m, float speed_mps,
                          float range_tol, float speed_tol)
{
	for (uint16_t i = 0; i < n; i++)
	{
		if (fabsf(dets[i].range_m - range_m) <= range_tol &&
		    fabsf(dets[i].speed_mps - speed_mps) <= speed_tol)
		{
			return true;
		}
	}

	return false;
}

void test_range_doppler(void)
{
	rd_t rd;

	CHECK(rd_init(&rd, TRK_POINTS, TRK_SPF, TRK_SWEEP_RATE, TRK_START_M, TRK_STEP_M));
	CHECK_NEAR(psd_max_speed(&rd.psd), 24.776, 0.01);
	CHECK_NEAR(psd_speed_resolution(&rd.psd), 1.5485, 0.001);

	synth_t synth;
	synth_init(&synth, TRK_POINTS, TRK_SPF, TRK_START_M, TRK_STEP_M, TRK_SWEEP_RATE, 777u);
	synth.clutter_range_m   = 0.54f;
	synth.clutter_amplitude = 8000.0f;
	synth.noise_sigma       = 50.0f;

	/* Club head closing at 8 m/s at 0.9 m, ball receding at 14 m/s at 1.5 m. */
	const synth_target_t targets[2] = {
	    {.range_m = 0.90f, .speed_mps = -8.0f, .amplitude = 5000.0f, .fwhm_m = 0.14f},
	    {.range_m = 1.50f, .speed_mps = 14.0f, .amplitude = 1600.0f, .fwhm_m = 0.14f},
	};

	synth_frame(&synth, targets, 2u, g_frame);
	rd_process(&rd, g_frame, g_map);

	rd_det_t       dets[8];
	const uint16_t found = rd_detect(&rd, g_map, 12.0f, 2.0f, 2u, dets, 8u);

	CHECK(found >= 2u);
	CHECK(has_detection(dets, found, 0.90f, -8.0f, 0.13f, 2.0f));
	CHECK(has_detection(dets, found, 1.50f, 14.0f, 0.13f, 2.0f));

	/* Detections come back strongest first, and the club is the stronger return. */
	if (found >= 1u)
	{
		CHECK_NEAR(dets[0].range_m, 0.90, 0.13);
		CHECK_NEAR(dets[0].speed_mps, -8.0, 2.0);
	}

	/* Raising the speed mask above the club's speed must drop it. */
	const uint16_t masked = rd_detect(&rd, g_map, 12.0f, 10.0f, 2u, dets, 8u);
	CHECK(masked >= 1u);
	CHECK(!has_detection(dets, masked, 0.90f, -8.0f, 0.13f, 2.0f));
	CHECK(has_detection(dets, masked, 1.50f, 14.0f, 0.13f, 2.0f));
}

void test_dealias(void)
{
	const float max_speed = 24.776f;
	const float span      = 2.0f * max_speed;

	/* A 70 m/s target folds to 70 - span. */
	const float folded = 70.0f - span;

	CHECK_NEAR(folded, 20.448, 0.01);
	CHECK_NEAR(rd_dealias(folded, max_speed, 65.0f), 70.0, 0.01);
	CHECK_NEAR(rd_dealias(folded, max_speed, 75.0f), 70.0, 0.01);

	/* The coarse estimate only has to land within half a fold of the truth. */
	CHECK_NEAR(rd_dealias(folded, max_speed, 46.0f), 70.0, 0.01);
	CHECK_NEAR(rd_dealias(folded, max_speed, 94.0f), 70.0, 0.01);

	/* A slow target must be left alone. */
	CHECK_NEAR(rd_dealias(12.0f, max_speed, 10.0f), 12.0, 0.01);

	/* Symmetry for approaching targets. */
	CHECK_NEAR(rd_dealias(-folded, max_speed, -65.0f), -70.0, 0.01);

	/* Without a folding period there is nothing to unwrap. */
	CHECK_NEAR(rd_dealias(20.0f, 0.0f, 70.0f), 20.0, 1e-6);
}

void test_range_walk(void)
{
	rd_t rd;

	CHECK(rd_init(&rd, TRK_POINTS, TRK_SPF, TRK_SWEEP_RATE, TRK_START_M, TRK_STEP_M));

	synth_t synth;
	synth_init(&synth, TRK_POINTS, TRK_SPF, TRK_START_M, TRK_STEP_M, TRK_SWEEP_RATE, 31337u);
	synth.clutter_amplitude = 8000.0f;
	synth.clutter_range_m   = 0.42f;
	synth.noise_sigma       = 10.0f;

	/*
	 * 70 m/s crosses about one 0.12 m range bin during the 1.6 ms frame. That
	 * is a coarse measurement, but the Doppler aliases are 49.6 m/s apart, so
	 * it only has to be right to within ~25 m/s to pick the correct one.
	 */
	synth_target_t target = {
	    .range_m = 0.90f, .speed_mps = 70.0f, .amplitude = 6000.0f, .fwhm_m = 0.14f};

	synth_frame(&synth, &target, 1u, g_frame);

	float       confidence = 0.0f;
	const float receding   = rd_range_walk_speed(&rd, g_frame, 4u, &confidence);

	CHECK(receding > 25.0f);
	CHECK(receding < 115.0f);
	CHECK(confidence > 0.0f);
	CHECK(confidence <= 1.0f);

	/* The alias picked from that coarse figure has to be the right one. */
	CHECK_NEAR(rd_dealias(70.0f - (2.0f * 24.776f), 24.776f, receding), 70.0, 0.01);

	/* Sign must follow direction. */
	target.speed_mps = -70.0f;
	target.range_m   = 1.50f;
	synth_init(&synth, TRK_POINTS, TRK_SPF, TRK_START_M, TRK_STEP_M, TRK_SWEEP_RATE, 31337u);
	synth.clutter_amplitude = 8000.0f;
	synth.clutter_range_m   = 0.42f;
	synth.noise_sigma       = 10.0f;
	synth_frame(&synth, &target, 1u, g_frame);

	const float approaching = rd_range_walk_speed(&rd, g_frame, 4u, NULL);
	CHECK(approaching < -25.0f);
	CHECK(approaching > -115.0f);

	/* Degenerate segment counts must be refused, not guessed at. */
	CHECK_NEAR(rd_range_walk_speed(&rd, g_frame, 1u, NULL), 0.0, 1e-9);
	CHECK_NEAR(rd_range_walk_speed(&rd, g_frame, RD_MAX_WALK_SEGMENTS + 1u, NULL), 0.0, 1e-9);
}

void test_tracker(void)
{
	rd_t rd;
	CHECK(rd_init(&rd, TRK_POINTS, TRK_SPF, TRK_SWEEP_RATE, TRK_START_M, TRK_STEP_M));

	const float max_speed = psd_max_speed(&rd.psd);
	const float dt_s      = (float)TRK_SPF / TRK_SWEEP_RATE;

	tracker_cfg_t cfg;
	tracker_default_config(&cfg);

	tracker_t tracker;
	tracker_init(&tracker, &cfg);

	synth_t synth;
	synth_init(&synth, TRK_POINTS, TRK_SPF, TRK_START_M, TRK_STEP_M, TRK_SWEEP_RATE, 20240u);
	synth.clutter_range_m   = 0.42f;
	synth.clutter_amplitude = 8000.0f;
	synth.noise_sigma       = 40.0f;

	/* A slow, unaliased target closing on the sensor. */
	synth_target_t target = {
	    .range_m = 1.60f, .speed_mps = -6.0f, .amplitude = 5000.0f, .fwhm_m = 0.14f};

	uint32_t t_ms = 0u;

	for (uint16_t frame = 0; frame < 12u; frame++)
	{
		synth_frame(&synth, &target, 1u, g_frame);
		rd_process(&rd, g_frame, g_map);

		rd_det_t       dets[8];
		const uint16_t found = rd_detect(&rd, g_map, 12.0f, 2.0f, 2u, dets, 8u);

		float       confidence = 0.0f;
		const float coarse     = rd_range_walk_speed(&rd, g_frame, 4u, &confidence);

		tracker_update(&tracker, dets, found, dt_s, t_ms, max_speed, coarse, confidence > 0.3f);

		target.range_m += target.speed_mps * dt_s;
		t_ms += (uint32_t)(dt_s * 1000.0f);
	}

	const track_t *confirmed[TRACKER_MAX_TRACKS];
	uint16_t       num = tracker_confirmed(&tracker, confirmed, TRACKER_MAX_TRACKS);

	CHECK(num >= 1u);
	if (num >= 1u)
	{
		CHECK_NEAR(confirmed[0]->speed_mps, -6.0, 2.0);
		CHECK_NEAR(confirmed[0]->range_m, target.range_m, 0.2);
		CHECK(confirmed[0]->hits >= 8u);
		CHECK(confirmed[0]->id != 0u);
	}

	/*
	 * Now a target well past the folding limit. The tracker has to unwrap it:
	 * the raw Doppler reading for 60 m/s is 60 - 49.55 = 10.45 m/s.
	 */
	tracker_init(&tracker, &cfg);
	synth_init(&synth, TRK_POINTS, TRK_SPF, TRK_START_M, TRK_STEP_M, TRK_SWEEP_RATE, 909u);
	synth.clutter_range_m   = 0.42f;
	synth.clutter_amplitude = 8000.0f;
	synth.noise_sigma       = 20.0f;

	synth_target_t fast = {
	    .range_m = 0.60f, .speed_mps = 60.0f, .amplitude = 6000.0f, .fwhm_m = 0.14f};

	t_ms = 0u;

	for (uint16_t frame = 0; frame < 10u; frame++)
	{
		synth_frame(&synth, &fast, 1u, g_frame);
		rd_process(&rd, g_frame, g_map);

		rd_det_t       dets[8];
		const uint16_t found = rd_detect(&rd, g_map, 12.0f, 2.0f, 2u, dets, 8u);

		float       confidence = 0.0f;
		const float coarse     = rd_range_walk_speed(&rd, g_frame, 4u, &confidence);

		tracker_update(&tracker, dets, found, dt_s, t_ms, max_speed, coarse, confidence > 0.3f);

		fast.range_m += fast.speed_mps * dt_s;
		t_ms += (uint32_t)(dt_s * 1000.0f);
	}

	num = tracker_confirmed(&tracker, confirmed, TRACKER_MAX_TRACKS);
	CHECK(num >= 1u);
	if (num >= 1u)
	{
		/* Unwrapped, not the 10.45 m/s the spectrum literally showed. */
		CHECK_NEAR(confirmed[0]->speed_mps, 60.0, 6.0);
		CHECK(confirmed[0]->speed_mps > 40.0f);
	}
}
