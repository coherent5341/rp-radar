/*
 * Demo 2 — ball hit speed.
 *
 * Reports club-head speed, ball speed and smash factor for each shot.
 *
 * The configuration is the opposite trade to the swing tracker: a single range
 * point, and the entire sweep budget spent on rate rather than coverage. At
 * 71 kHz the Doppler axis reaches +/-88 m/s, so a golf ball never folds and no
 * unwrapping is needed — the measurement is a direct frequency reading.
 *
 * Per frame: Welch PSD across the 128 sweeps, peaks above the median, and the
 * two fastest handed to the shot detector. It watches for the discontinuity in
 * peak speed that impact produces, then reads club speed from before it and
 * ball speed from just after. See shot.h for the reasoning.
 *
 * Output on stdout:
 *
 *   V,<t_ms>,<speed_mps>,<snr>          live peak, while something is moving
 *   SHOT,<id>,<club>,<ball>,<smash>,<t_impact_ms>,<confidence>,<samples>,<flags>
 *
 * plus a human-readable block per shot.
 */
#include <inttypes.h>
#include <math.h>
#include <stdio.h>

#include "pico/stdlib.h"

#include "peaks.h"
#include "psd.h"
#include "radar_session.h"
#include "shot.h"

/* Averaged periodograms per frame. Two keeps 2.7 m/s resolution at 128 sweeps. */
#define PSD_SEGMENTS 2u
/* Peak height over the median needed to be considered at all. */
#define PEAK_THRESHOLD 10.0f
#define DOPPLER_GUARD 2u
/* Only echo live speeds above this, or the console fills with noise. */
#define LIVE_PRINT_MIN_MPS 5.0f
#define MPS_TO_MPH 2.2369363f

static psd_t  g_psd;
static shot_t g_shot;
static float  g_spectrum[FFT_MAX_SIZE];
static float  g_scratch[FFT_MAX_SIZE];

int main(void)
{
	stdio_init_all();
	sleep_ms(2000);

	printf("\n=== A121 golf ball speed (LilyGO T-RADAR on RP2350) ===\n");

	radar_session_cfg_t cfg;
	radar_session_ball_speed_config(&cfg);

	radar_session_t session;

	if (!radar_session_open(&session, &cfg))
	{
		while (true)
		{
			tight_loop_contents();
		}
	}

	radar_session_log_summary(&session);

	if (!psd_init(&g_psd, cfg.sweeps_per_frame, PSD_SEGMENTS, cfg.sweep_rate_hz, true))
	{
		printf("[E/app] psd_init failed; sweeps_per_frame / %u must be a power of two\n",
		       PSD_SEGMENTS);
		radar_session_close(&session);
		while (true)
		{
			tight_loop_contents();
		}
	}

	/*
	 * A ball that folds would be reported at some unrelated speed, silently.
	 * Refuse to run rather than produce numbers that look plausible.
	 */
	if (psd_max_speed(&g_psd) < 80.0f)
	{
		printf("[E/app] unambiguous speed is only %.1f m/s; raise the sweep rate\n",
		       (double)psd_max_speed(&g_psd));
		radar_session_close(&session);
		while (true)
		{
			tight_loop_contents();
		}
	}

	shot_cfg_t shot_cfg;
	shot_default_config(&shot_cfg);

	shot_init(&g_shot, &shot_cfg);

	printf("Speed resolution %.2f m/s, unambiguous to +/-%.1f m/s.\n",
	       (double)psd_speed_resolution(&g_psd), (double)psd_max_speed(&g_psd));
	printf("Ready. Place the sensor down the target line, about %.2f m from the ball.\n\n",
	       (double)session.start_m);

	while (true)
	{
		const iq16_t *frame;

		if (!radar_session_next_frame(&session, &frame))
		{
			break;
		}

		const uint32_t t_ms = to_ms_since_boot(get_absolute_time());

		psd_process(&g_psd, frame, cfg.num_points, g_spectrum);

		/*
		 * With more than one range point, keep whichever spectrum holds the
		 * fastest credible peak; the ball may be in either gate.
		 */
		uint16_t best_point = 0u;
		float    best_speed = 0.0f;

		peak_t   peaks[PEAKS_MAX];
		uint16_t num_peaks = 0u;

		for (uint16_t point = 0; point < cfg.num_points; point++)
		{
			const float *spectrum = &g_spectrum[(uint32_t)point * g_psd.seg_len];
			const float  median   = peak_median(spectrum, g_psd.seg_len, g_scratch);

			peak_t         candidates[PEAKS_MAX];
			const uint16_t found = peak_find(spectrum, g_psd.seg_len, median, PEAK_THRESHOLD,
			                                 DOPPLER_GUARD, 0u, 0u, candidates, PEAKS_MAX);

			for (uint16_t i = 0; i < found; i++)
			{
				const float speed = fabsf(psd_bin_to_speed(&g_psd, candidates[i].bin));

				if (speed > best_speed)
				{
					best_speed = speed;
					best_point = point;
				}
			}

			if (point == best_point)
			{
				num_peaks = found;
				for (uint16_t i = 0; i < found; i++)
				{
					peaks[i] = candidates[i];
				}
			}
		}

		if (best_speed >= LIVE_PRINT_MIN_MPS && num_peaks > 0u)
		{
			printf("V,%lu,%.2f,%.1f\n", (unsigned long)t_ms,
			       (double)psd_bin_to_speed(&g_psd, peaks[0].bin), (double)peaks[0].snr);
		}

		shot_result_t shot;

		if (shot_push(&g_shot, t_ms, peaks, num_peaks, &g_psd, &shot))
		{
			printf("SHOT,%lu,%.2f,%.2f,%.3f,%lu,%.2f,%u,%s%s\n", (unsigned long)shot.id,
			       (double)shot.club_speed_mps, (double)shot.ball_speed_mps,
			       (double)shot.smash_factor, (unsigned long)shot.t_impact_ms,
			       (double)shot.confidence, shot.num_samples, shot.valid ? "ok" : "invalid",
			       shot.implausible ? "|implausible" : "");

			printf("\n  shot %lu\n", (unsigned long)shot.id);
			printf("    club head   %5.1f m/s  (%5.1f mph)\n", (double)shot.club_speed_mps,
			       (double)(shot.club_speed_mps * MPS_TO_MPH));
			printf("    ball        %5.1f m/s  (%5.1f mph)\n", (double)shot.ball_speed_mps,
			       (double)(shot.ball_speed_mps * MPS_TO_MPH));
			printf("    smash       %5.2f\n", (double)shot.smash_factor);
			printf("    confidence  %5.2f over %u frames\n", (double)shot.confidence,
			       shot.num_samples);

			if (shot.implausible)
			{
				printf("    note: smash factor is outside 0.8-1.6. Usually this means the\n");
				printf("          club or the ball was missed, or the sensor is off the\n");
				printf("          target line so the radial speeds are foreshortened.\n");
			}

			printf("\n");
		}
	}

	radar_session_close(&session);

	printf("# stopped\n");

	while (true)
	{
		tight_loop_contents();
	}
}
