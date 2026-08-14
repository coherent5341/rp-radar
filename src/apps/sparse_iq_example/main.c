/*
 * Sparse IQ bring-up example — the RP2350 port of Acconeer's example_service.
 *
 * Run this first. It exercises the whole stack (HAL registration, power-up,
 * calibration, configuration, frame readout) and prints raw sparse IQ, so if
 * the wiring is wrong you find out here rather than inside a tracker.
 *
 * What good output looks like: amplitudes that are large and steady at the
 * range points where something solid sits, and that visibly change when you
 * move your hand through the beam. All zeros, or values that never change,
 * means SPI is reading nothing.
 */
#include <inttypes.h>
#include <math.h>
#include <stdio.h>

#include "pico/stdlib.h"

#include "radar_session.h"

#define FRAMES_TO_PRINT 20u
#define MAX_POINTS_PRINTED 8u

int main(void)
{
	stdio_init_all();

	/* Give a USB host time to attach before the first output. */
	sleep_ms(2000);

	printf("\n=== A121 sparse IQ example (LilyGO T-RADAR on RP2350) ===\n");

	radar_session_cfg_t cfg;

	/*
	 * A plain, undemanding configuration: a wide range window, a modest sweep
	 * rate, and heavy averaging. Nothing here is golf-specific; the point is a
	 * clean signal that is easy to interpret by eye.
	 */
	radar_session_swing_tracker_config(&cfg);
	cfg.sweeps_per_frame      = 16u;
	cfg.sweep_rate_hz         = 2000.0f;
	cfg.frame_rate_hz         = 10.0f;
	cfg.continuous_sweep_mode = false;
	cfg.double_buffering      = false;

	radar_session_t session;

	if (!radar_session_open(&session, &cfg))
	{
		printf("Sensor bring-up failed. Check:\n");
		printf("  - VIN on 3V3 and a solid ground between the boards\n");
		printf("  - ENABLE driven (it has a 10k pull-down on the T-RADAR)\n");
		printf("  - MISO/MOSI not swapped\n");
		while (true)
		{
			tight_loop_contents();
		}
	}

	radar_session_log_summary(&session);

	const uint16_t points =
	    (cfg.num_points < MAX_POINTS_PRINTED) ? cfg.num_points : MAX_POINTS_PRINTED;

	for (uint32_t frame_index = 0; frame_index < FRAMES_TO_PRINT; frame_index++)
	{
		const iq16_t *frame;

		if (!radar_session_next_frame(&session, &frame))
		{
			break;
		}

		printf("frame %2lu  temp %3d C%s%s\n", (unsigned long)frame_index,
		       session.result.temperature,
		       session.result.data_saturated ? "  SATURATED" : "",
		       session.result.frame_delayed ? "  DELAYED" : "");

		/* First sweep of the frame, as raw complex samples. */
		printf("  sweep 0:");
		for (uint16_t point = 0; point < points; point++)
		{
			const iq16_t *s = iq_at(frame, cfg.num_points, 0u, point);

			printf(" %6" PRIi16 "%+6" PRIi16 "i", s->real, s->imag);
		}
		printf("\n");

		/* Mean amplitude per range point, which is easier to read at a glance. */
		printf("  amplitude:");
		for (uint16_t point = 0; point < points; point++)
		{
			double sum = 0.0;

			for (uint16_t sweep = 0; sweep < cfg.sweeps_per_frame; sweep++)
			{
				const iq16_t *s = iq_at(frame, cfg.num_points, sweep, point);

				sum += sqrt(((double)s->real * s->real) + ((double)s->imag * s->imag));
			}

			printf(" %8.0f", sum / (double)cfg.sweeps_per_frame);
		}
		printf("\n");
	}

	printf("\n%lu frames read, %lu delayed, %lu saturated\n", (unsigned long)session.frames_read,
	       (unsigned long)session.frames_delayed, (unsigned long)session.frames_saturated);

	radar_session_close(&session);

	printf("done\n");

	while (true)
	{
		tight_loop_contents();
	}
}
