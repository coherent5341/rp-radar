/*
 * Demo 1 — swing tracker.
 *
 * Streams the position and radial speed of everything moving in front of the
 * sensor, at 625 frames per second, so a whole swing can be reconstructed: the
 * club head coming down through the range gates, the strike, the ball leaving,
 * and the follow-through.
 *
 * Each frame becomes a range-Doppler map, peaks in that map become detections,
 * and detections are associated into tracks that persist across frames. Tracks
 * are labelled club or ball from their amplitude and speed — the club head is a
 * far larger reflector than the ball, and after impact the ball is the faster
 * of the two.
 *
 * Speeds beyond +/-24.8 m/s fold in the Doppler axis at this sweep rate and are
 * unwrapped using range walk within the frame; see range_doppler.h. The ball
 * speed demo takes the other approach, trading range coverage for a sweep rate
 * high enough that nothing folds at all.
 *
 * Output is CSV on stdout, one line per record:
 *
 *   F,<t_ms>,<detections>,<coarse_mps>,<coarse_conf>
 *   T,<t_ms>,<id>,<class>,<range_m>,<speed_mps>,<amp_db>,<hits>
 *   S,<frames>,<delayed>,<saturated>,<recalibrations>
 *
 * tools/monitor.py renders it live; tools/plot_swing.py plots a capture.
 */
#include <inttypes.h>
#include <math.h>
#include <stdio.h>

#include "pico/stdlib.h"

#include "radar_session.h"
#include "range_doppler.h"
#include "tracker.h"

/* Peak height over the median of its range bin needed to call a detection. */
#define DETECTION_THRESHOLD 12.0f
/* Ignore anything slower than this, which is the static scene. */
#define MIN_SPEED_MPS 1.5f
/* Doppler bins of non-maximum suppression within a range bin. */
#define DOPPLER_GUARD 2u
#define MAX_DETECTIONS 8u
/* Range walk is only trusted when one target dominates the frame. */
#define COARSE_CONFIDENCE_MIN 0.35f
#define STATS_INTERVAL_FRAMES 5000u

/*
 * The map holds num_points * sweeps_per_frame floats. 2048 covers the default
 * 16 x 32 with room to widen the range window or lengthen frames; the check in
 * main() refuses to run rather than overrun it.
 */
#define MAP_MAX_ELEMENTS 2048u

static rd_t      g_rd;
static tracker_t g_tracker;
static float     g_map[MAP_MAX_ELEMENTS];

static const char *class_name(track_class_t cls)
{
	switch (cls)
	{
		case TRACK_CLASS_CLUB:
			return "CLUB";
		case TRACK_CLASS_BALL:
			return "BALL";
		default:
			return "----";
	}
}

/* Power to dB, floored so a silent bin cannot produce -inf in the CSV. */
static float to_db(float power)
{
	return (power > 1e-12f) ? (10.0f * log10f(power)) : -120.0f;
}

int main(void)
{
	stdio_init_all();
	sleep_ms(2000);

	printf("\n=== A121 swing tracker (LilyGO T-RADAR on RP2350) ===\n");

	radar_session_cfg_t cfg;
	radar_session_swing_tracker_config(&cfg);

	radar_session_t session;

	if (!radar_session_open(&session, &cfg))
	{
		while (true)
		{
			tight_loop_contents();
		}
	}

	radar_session_log_summary(&session);

	if (((uint32_t)cfg.num_points * cfg.sweeps_per_frame) >
	    (uint32_t)(sizeof(g_map) / sizeof(g_map[0])))
	{
		printf("[E/app] range-Doppler map does not fit in the static buffer\n");
		radar_session_close(&session);
		while (true)
		{
			tight_loop_contents();
		}
	}

	if (!rd_init(&g_rd, cfg.num_points, cfg.sweeps_per_frame, cfg.sweep_rate_hz,
	             session.start_m, session.step_m))
	{
		printf("[E/app] rd_init failed; sweeps_per_frame must be a power of two\n");
		radar_session_close(&session);
		while (true)
		{
			tight_loop_contents();
		}
	}

	tracker_cfg_t tracker_cfg;
	tracker_default_config(&tracker_cfg);

	/*
	 * The gate has to admit how far a target can travel between frames. At
	 * 1.6 ms per frame a 70 m/s ball moves 0.11 m, so a gate below that would
	 * break the track exactly when it matters most.
	 */
	tracker_cfg.gate_m = 0.30f;
	tracker_init(&g_tracker, &tracker_cfg);

	printf("# t_ms,record\n");
	printf("# F,t_ms,detections,coarse_mps,coarse_conf\n");
	printf("# T,t_ms,id,class,range_m,speed_mps,amp_db,hits\n");

	const float dt_s = session.frame_period_s;

	while (true)
	{
		const iq16_t *frame;

		if (!radar_session_next_frame(&session, &frame))
		{
			break;
		}

		const uint32_t t_ms = to_ms_since_boot(get_absolute_time());

		rd_process(&g_rd, frame, g_map);

		rd_det_t       detections[MAX_DETECTIONS];
		const uint16_t num_dets = rd_detect(&g_rd, g_map, DETECTION_THRESHOLD, MIN_SPEED_MPS,
		                                    DOPPLER_GUARD, detections, MAX_DETECTIONS);

		float coarse_confidence = 0.0f;
		float coarse_mps        = 0.0f;

		/*
		 * Range walk costs a pass over the frame, so only pay for it when
		 * something is actually moving fast enough to need unfolding.
		 */
		bool coarse_valid = false;

		if (num_dets > 0u && fabsf(detections[0].speed_mps) > (session.max_speed_mps * 0.5f))
		{
			coarse_mps   = rd_range_walk_speed(&g_rd, frame, 4u, &coarse_confidence);
			coarse_valid = coarse_confidence >= COARSE_CONFIDENCE_MIN;
		}

		tracker_update(&g_tracker, detections, num_dets, dt_s, t_ms, session.max_speed_mps,
		               coarse_mps, coarse_valid);

		const track_t *tracks[TRACKER_MAX_TRACKS];
		const uint16_t num_tracks = tracker_confirmed(&g_tracker, tracks, TRACKER_MAX_TRACKS);

		if (num_dets > 0u || num_tracks > 0u)
		{
			printf("F,%lu,%u,%.2f,%.2f\n", (unsigned long)t_ms, num_dets, (double)coarse_mps,
			       (double)coarse_confidence);

			for (uint16_t i = 0; i < num_tracks; i++)
			{
				printf("T,%lu,%u,%s,%.3f,%.2f,%.1f,%u\n", (unsigned long)t_ms, tracks[i]->id,
				       class_name(tracks[i]->cls), (double)tracks[i]->range_m,
				       (double)tracks[i]->speed_mps, (double)to_db(tracks[i]->amp),
				       tracks[i]->hits);
			}
		}

		if ((session.frames_read % STATS_INTERVAL_FRAMES) == 0u)
		{
			printf("S,%lu,%lu,%lu,%lu\n", (unsigned long)session.frames_read,
			       (unsigned long)session.frames_delayed,
			       (unsigned long)session.frames_saturated,
			       (unsigned long)session.recalibrations);
		}
	}

	radar_session_close(&session);

	printf("# stopped\n");

	while (true)
	{
		tight_loop_contents();
	}
}
