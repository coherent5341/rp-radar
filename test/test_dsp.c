#include <string.h>

#include "peaks.h"
#include "psd.h"
#include "radar_math.h"
#include "synth.h"
#include "test_util.h"

void test_radar_math(void)
{
	/* Wavelength and its halved, "perceived" form. */
	CHECK_NEAR(RADAR_WAVELENGTH_M, 4.9553e-3, 1e-6);
	CHECK_NEAR(RADAR_PERCEIVED_WAVELENGTH_M, 2.4776e-3, 1e-6);

	/* Sweep rate and max speed are inverses of each other. */
	const float max_speed = radar_max_speed_from_sweep_rate(20000.0f);
	CHECK_NEAR(max_speed, 24.776, 0.01);
	CHECK_NEAR(radar_sweep_rate_for_max_speed(max_speed, 1.0f), 20000.0, 1.0);

	/* Acconeer's 10% oversampling margin raises the required rate accordingly. */
	CHECK_NEAR(radar_sweep_rate_for_max_speed(80.0f, 1.1f), 71036.0, 50.0);

	CHECK_NEAR(radar_point_to_meter(200), 0.5, 1e-6);
	CHECK(radar_meter_to_point(0.5f) == 200);

	/* PRF 19.5 MHz is profile 1 only; everything else falls to 15.6 MHz here. */
	CHECK(radar_select_prf(400, RADAR_PROFILE_1) == RADAR_PRF_19_5_MHZ);
	CHECK(radar_select_prf(400, RADAR_PROFILE_3) == RADAR_PRF_15_6_MHZ);
	/* 2000 points is 5 m, past the 15.6 MHz limit of 5.1 m only just; 6 m is not. */
	CHECK(radar_select_prf(2400, RADAR_PROFILE_3) == RADAR_PRF_13_0_MHZ);

	CHECK(radar_step_length_valid(1));
	CHECK(radar_step_length_valid(24));
	CHECK(radar_step_length_valid(48));
	CHECK(radar_step_length_valid(120));
	CHECK(!radar_step_length_valid(5));
	CHECK(!radar_step_length_valid(0));

	/* Sweep duration model: 16 points of profile 3 at 15.6 MHz, HWAAS 1. */
	float duration;
	CHECK(radar_sweep_duration_s(RADAR_PRF_15_6_MHZ, RADAR_PROFILE_3, 16u, 1u, &duration));
	CHECK_NEAR(duration, 38.928e-6, 1e-8);

	/*
	 * The swing tracker's operating point: 16 points at 20 kHz must be
	 * reachable. Acconeer's conservative estimator rejects it, which is exactly
	 * why radar_hwaas_for_sweep_rate exists.
	 */
	CHECK(radar_hwaas_for_sweep_rate(RADAR_PRF_15_6_MHZ, RADAR_PROFILE_3, 16u, 20000.0f, 0.95f) >=
	      1u);
	CHECK(radar_max_hwaas(RADAR_PRF_15_6_MHZ, RADAR_PROFILE_3, 16u, 20000.0f) == 0u);

	/* The ball speed demo: 1 point of profile 5 at 71 kHz should afford averaging. */
	const uint16_t hwaas =
	    radar_hwaas_for_sweep_rate(RADAR_PRF_15_6_MHZ, RADAR_PROFILE_5, 1u, 71000.0f, 0.95f);
	CHECK(hwaas >= 5u);
	CHECK(hwaas <= RADAR_MAX_HWAAS);

	/* Asking for an impossible rate must fail rather than return nonsense. */
	CHECK(radar_hwaas_for_sweep_rate(RADAR_PRF_15_6_MHZ, RADAR_PROFILE_3, 16u, 500000.0f, 0.95f) ==
	      0u);

	/* PRF 19.5 MHz has no timing entry above profile 1. */
	float unused;
	CHECK(!radar_sample_duration_s(RADAR_PRF_19_5_MHZ, RADAR_PROFILE_3, &unused));
}

void test_fft(void)
{
	static const uint16_t sizes[] = {8u, 16u, 64u, 256u};

	for (unsigned s = 0; s < (sizeof(sizes) / sizeof(sizes[0])); s++)
	{
		const uint16_t n = sizes[s];
		fft_t          plan;

		CHECK(fft_init(&plan, n));

		cplx_t input[256];
		cplx_t work[256];
		synth_t rng;

		synth_init(&rng, 1u, 1u, 0.0f, 0.0f, 1.0f, 12345u);

		for (uint16_t i = 0; i < n; i++)
		{
			input[i].re = (float)(synth_uniform(&rng) - 0.5);
			input[i].im = (float)(synth_uniform(&rng) - 0.5);
			work[i]     = input[i];
		}

		fft_forward(&plan, work);

		/* Compare against a direct DFT. */
		double worst = 0.0;

		for (uint16_t k = 0; k < n; k++)
		{
			double re = 0.0;
			double im = 0.0;

			for (uint16_t t = 0; t < n; t++)
			{
				const double angle = -2.0 * 3.14159265358979323846 * (double)k * (double)t /
				                     (double)n;
				const double c = cos(angle);
				const double sN = sin(angle);

				re += ((double)input[t].re * c) - ((double)input[t].im * sN);
				im += ((double)input[t].re * sN) + ((double)input[t].im * c);
			}

			const double dr = re - (double)work[k].re;
			const double di = im - (double)work[k].im;
			const double err = sqrt((dr * dr) + (di * di));

			if (err > worst)
			{
				worst = err;
			}
		}

		CHECK(worst < 1e-3);
	}

	fft_t plan;
	CHECK(!fft_init(&plan, 12u));  /* not a power of two */
	CHECK(!fft_init(&plan, 1024u)); /* above FFT_MAX_SIZE */
}

void test_psd(void)
{
	enum
	{
		NUM_POINTS = 1u,
		SPF        = 128u,
		SEGMENTS   = 2u
	};
	const float sweep_rate = 71000.0f;

	psd_t psd;
	CHECK(psd_init(&psd, SPF, SEGMENTS, sweep_rate, true));
	CHECK(psd.seg_len == 64u);

	CHECK_NEAR(psd_max_speed(&psd), 87.95, 0.1);
	CHECK_NEAR(psd_speed_resolution(&psd), 2.749, 0.01);

	/* The bin/speed mapping must round-trip. */
	CHECK_NEAR(psd_bin_to_speed(&psd, psd_speed_to_bin(&psd, 42.0f)), 42.0, 1e-3);
	CHECK_NEAR(psd_bin_to_speed(&psd, (float)(psd.seg_len / 2u)), 0.0, 1e-6);

	synth_t synth;
	synth_init(&synth, NUM_POINTS, SPF, 0.8f, 0.12f, sweep_rate, 4242u);
	synth.clutter_range_m   = 0.8f;
	synth.clutter_amplitude = 6000.0f;
	synth.noise_sigma       = 60.0f;

	static iq16_t frame[SPF * NUM_POINTS];
	static float  spectrum[64];

	const synth_target_t target = {
	    .range_m = 0.8f, .speed_mps = 40.0f, .amplitude = 4000.0f, .fwhm_m = 0.32f};

	synth_frame(&synth, &target, 1u, frame);
	psd_process(&psd, frame, NUM_POINTS, spectrum);

	float          scratch[64];
	const float    median = peak_median(spectrum, 64u, scratch);
	peak_t         peaks[PEAKS_MAX];
	const uint16_t found =
	    peak_find(spectrum, 64u, median, 10.0f, 2u, 0u, 0u, peaks, PEAKS_MAX);

	CHECK(found >= 1u);

	if (found >= 1u)
	{
		const float speed = psd_bin_to_speed(&psd, peaks[0].bin);

		/* Interpolation should beat the raw bin width. */
		CHECK_NEAR(speed, 40.0, 1.5);
		CHECK(peaks[0].snr > 10.0f);
	}

	/*
	 * Mean removal is what keeps the 6000-count static return from dominating.
	 * Without it the strongest peak sits at zero speed instead.
	 */
	psd_t no_removal;
	CHECK(psd_init(&no_removal, SPF, SEGMENTS, sweep_rate, false));
	psd_process(&no_removal, frame, NUM_POINTS, spectrum);

	uint16_t argmax = 0u;
	for (uint16_t i = 1; i < 64u; i++)
	{
		if (spectrum[i] > spectrum[argmax])
		{
			argmax = i;
		}
	}
	CHECK_NEAR(psd_bin_to_speed(&no_removal, (float)argmax), 0.0, 3.0);

	psd_process(&psd, frame, NUM_POINTS, spectrum);
	argmax = 0u;
	for (uint16_t i = 1; i < 64u; i++)
	{
		if (spectrum[i] > spectrum[argmax])
		{
			argmax = i;
		}
	}
	CHECK_NEAR(psd_bin_to_speed(&psd, (float)argmax), 40.0, 3.0);
}

void test_peaks(void)
{
	float values[9] = {5.0f, 1.0f, 9.0f, 3.0f, 7.0f, 2.0f, 8.0f, 4.0f, 6.0f};
	float scratch[9];

	CHECK_NEAR(peak_median(values, 9u, scratch), 5.0, 1e-9);
	/* peak_median must not disturb the caller's data. */
	CHECK_NEAR(values[0], 5.0, 1e-9);
	CHECK_NEAR(values[2], 9.0, 1e-9);

	float two[2] = {3.0f, 7.0f};
	float scratch2[2];
	CHECK_NEAR(peak_median(two, 2u, scratch2), 3.0, 1e-9);

	/* A symmetric triangle peaks exactly at the centre sample. */
	const float symmetric[5] = {0.0f, 1.0f, 2.0f, 1.0f, 0.0f};
	CHECK_NEAR(peak_parabolic(symmetric, 2u, 5u), 2.0, 1e-6);

	/* Skewing the neighbours moves the interpolated peak towards the taller one. */
	const float skewed[5] = {0.0f, 1.0f, 2.0f, 1.5f, 0.0f};
	CHECK(peak_parabolic(skewed, 2u, 5u) > 2.0f);
	CHECK(peak_parabolic(skewed, 2u, 5u) < 2.5f);

	/* Edges have no fit available. */
	CHECK_NEAR(peak_parabolic(symmetric, 0u, 5u), 0.0, 1e-9);
	CHECK_NEAR(peak_parabolic(symmetric, 4u, 5u), 4.0, 1e-9);

	/*
	 * The log-domain variant agrees on a symmetric peak and, like the linear
	 * one, declines to extrapolate past half a bin.
	 */
	const float positive[5] = {1.0f, 4.0f, 16.0f, 4.0f, 1.0f};
	CHECK_NEAR(peak_parabolic_log(positive, 2u, 5u), 2.0, 1e-6);
	CHECK_NEAR(peak_parabolic_log(positive, 0u, 5u), 0.0, 1e-9);

	const float skewed_positive[5] = {1.0f, 4.0f, 16.0f, 8.0f, 1.0f};
	CHECK(peak_parabolic_log(skewed_positive, 2u, 5u) > 2.0f);
	CHECK(peak_parabolic_log(skewed_positive, 2u, 5u) <= 2.5f);

	/* A zero neighbour must be floored, not turned into -inf or NaN. */
	const float with_zero[3] = {0.0f, 9.0f, 1.0f};
	const float located      = peak_parabolic_log(with_zero, 1u, 3u);
	CHECK(located >= 0.5f);
	CHECK(located <= 1.5f);

	/* Two well-separated peaks, both above threshold, strongest reported first. */
	float spectrum[32];
	for (uint16_t i = 0; i < 32u; i++)
	{
		spectrum[i] = 1.0f;
	}
	spectrum[7]  = 4.0f;
	spectrum[8]  = 20.0f;
	spectrum[9]  = 4.0f;
	spectrum[22] = 3.0f;
	spectrum[23] = 40.0f;
	spectrum[24] = 3.0f;

	float          scratch32[32];
	const float    median = peak_median(spectrum, 32u, scratch32);
	peak_t         peaks[PEAKS_MAX];
	uint16_t       found = peak_find(spectrum, 32u, median, 5.0f, 2u, 0u, 0u, peaks, PEAKS_MAX);

	CHECK(found == 2u);
	if (found == 2u)
	{
		CHECK_NEAR(peaks[0].bin, 23.0, 0.2);
		CHECK_NEAR(peaks[1].bin, 8.0, 0.2);
		CHECK(peaks[0].power > peaks[1].power);
		CHECK_NEAR(peaks[0].snr, 40.0, 1e-6);
	}

	/* Masking bins 20..26 must remove the stronger peak from the result. */
	found = peak_find(spectrum, 32u, median, 5.0f, 2u, 20u, 27u, peaks, PEAKS_MAX);
	CHECK(found == 1u);
	if (found == 1u)
	{
		CHECK_NEAR(peaks[0].bin, 8.0, 0.2);
	}

	/* Raising the threshold above the weaker peak leaves only the stronger. */
	found = peak_find(spectrum, 32u, median, 30.0f, 2u, 0u, 0u, peaks, PEAKS_MAX);
	CHECK(found == 1u);

	/* A guard band wider than the separation collapses them to one. */
	found = peak_find(spectrum, 32u, median, 5.0f, 20u, 0u, 0u, peaks, PEAKS_MAX);
	CHECK(found == 1u);
	if (found == 1u)
	{
		CHECK_NEAR(peaks[0].bin, 23.0, 0.2);
	}
}

/*
 * How accurately the ball speed chain recovers a known speed.
 *
 * The raw bin is 2.75 m/s wide, but that is not the accuracy: interpolating the
 * peak recovers a small fraction of a bin. The truth is stepped by an amount
 * that does not divide the bin width, so every sub-bin position gets sampled
 * and any interpolation bias shows up rather than averaging away.
 *
 * Bounds are set well above what was measured (0.034 m/s RMS, 0.068 m/s worst)
 * so this catches a regression rather than failing on floating-point drift
 * between toolchains.
 */
void test_interpolation_accuracy(void)
{
	enum
	{
		SPF      = 128u,
		SEGMENTS = 2u,
		SEG_LEN  = 64u
	};
	const float sweep_rate = 71000.0f;
	const float range_m    = 0.80f;

	psd_t psd;
	CHECK(psd_init(&psd, SPF, SEGMENTS, sweep_rate, true));

	static iq16_t frame[SPF];
	static float  spectrum[SEG_LEN];
	static float  scratch[SEG_LEN];

	double   interpolated_sq = 0.0;
	double   raw_bin_sq      = 0.0;
	double   worst           = 0.0;
	uint16_t measured        = 0u;
	uint16_t attempted       = 0u;

	for (float truth = 30.0f; truth <= 85.0f; truth += 0.37f)
	{
		synth_t synth;
		synth_init(&synth, 1u, SPF, range_m, 0.30f, sweep_rate,
		           1000u + (uint64_t)(truth * 97.0f));
		synth.clutter_range_m   = range_m;
		synth.clutter_amplitude = 6000.0f;
		synth.noise_sigma       = 60.0f;

		const synth_target_t target = {
		    .range_m = range_m, .speed_mps = truth, .amplitude = 1500.0f, .fwhm_m = 0.32f};

		synth_frame(&synth, &target, 1u, frame);
		psd_process(&psd, frame, 1u, spectrum);

		attempted++;

		const float    median = peak_median(spectrum, SEG_LEN, scratch);
		peak_t         peaks[PEAKS_MAX];
		const uint16_t found =
		    peak_find(spectrum, SEG_LEN, median, 10.0f, 2u, 0u, 0u, peaks, PEAKS_MAX);

		if (found == 0u)
		{
			continue;
		}

		const float speed   = fabsf(psd_bin_to_speed(&psd, peaks[0].bin));
		const float nearest = fabsf(psd_bin_to_speed(&psd, (float)lrintf(peaks[0].bin)));

		/* Ignore the rare frame where the strongest peak is not the target. */
		if (fabsf(speed - truth) > 6.0f)
		{
			continue;
		}

		const double error = (double)speed - (double)truth;

		interpolated_sq += error * error;
		raw_bin_sq += ((double)nearest - (double)truth) * ((double)nearest - (double)truth);

		if (fabs(error) > worst)
		{
			worst = fabs(error);
		}

		measured++;
	}

	/* Nearly every trial should yield a usable estimate at this SNR. */
	CHECK(measured > (uint16_t)((attempted * 9u) / 10u));

	if (measured > 0u)
	{
		const double rms     = sqrt(interpolated_sq / (double)measured);
		const double raw_rms = sqrt(raw_bin_sq / (double)measured);

		CHECK(rms < 0.10);
		CHECK(worst < 0.20);

		/*
		 * The whole point of interpolating: it must be far better than simply
		 * taking the peak bin, which is where the 2.75 m/s figure comes from.
		 */
		CHECK(raw_rms > (rms * 5.0));
	}
}
