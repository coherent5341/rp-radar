/*
 * Welch power spectral density across sweeps, one spectrum per range point.
 *
 * This is the workhorse of both demos. A sparse IQ frame is a matrix of
 * [sweeps_per_frame][num_points] complex samples; transforming down the sweep
 * axis turns it into a Doppler spectrum per range point, and Doppler maps to
 * radial speed by a single multiplication (see radar_math.h).
 *
 * Splitting the frame into segments and averaging their periodograms (Welch's
 * method, as Acconeer's own speed detector does) trades frequency resolution
 * for variance: a single 200-sweep periodogram is far noisier than the mean of
 * four 50-sweep ones, and for a golf shot we care much more about a stable peak
 * than about resolving two speeds 0.2 m/s apart.
 */
#ifndef RP_RADAR_PSD_H
#define RP_RADAR_PSD_H

#include "fft.h"
#include "radar_math.h"

/**
 * Sign mapping from Doppler frequency to radial speed.
 *
 * Positive speed means "moving away from the sensor" (range increasing), which
 * matches Acconeer's Exploration Tool: it plots speed as
 * frequency * PERCEIVED_WAVELENGTH and labels positive values as receding.
 *
 * Verify once on your own board by walking a hand slowly away from it; if the
 * reported sign is inverted, build with -DRADAR_SPEED_SIGN=-1.0f.
 */
#ifndef RADAR_SPEED_SIGN
#define RADAR_SPEED_SIGN 1.0f
#endif

typedef struct
{
	fft_t    fft;
	uint16_t seg_len;      /**< Sweeps per segment; the transform length. */
	uint16_t num_segments; /**< Periodograms averaged per spectrum. */
	uint16_t sweeps_used;  /**< seg_len * num_segments. */
	float    sample_rate_hz;
	bool     remove_mean;
	float    speed_sign;

	float win[FFT_MAX_SIZE];
	float scale; /**< Density normalisation, 1 / (fs * sum(w^2)). */

	cplx_t scratch[FFT_MAX_SIZE];
} psd_t;

/**
 * Configure a PSD estimator.
 *
 * @param sweeps_per_frame Sweeps in each frame delivered by the sensor.
 * @param num_segments     Periodograms to average. The transform length becomes
 *                         sweeps_per_frame / num_segments and must be a power
 *                         of two of at least 2.
 * @param sweep_rate_hz    Configured sweep rate; sets the speed axis.
 * @param remove_mean      Subtract each segment's complex mean before
 *                         windowing. This suppresses the static return (direct
 *                         leakage, the mat, the tee) that would otherwise put a
 *                         large peak at zero Doppler and leak across the band.
 *
 * @return false if the geometry is unusable, leaving @p psd untouched.
 */
bool psd_init(psd_t *psd, uint16_t sweeps_per_frame, uint16_t num_segments,
              float sweep_rate_hz, bool remove_mean);

/**
 * Estimate the spectrum of every range point in @p frame.
 *
 * @param frame Sweep-major sparse IQ, sweeps_per_frame * num_points samples.
 * @param out   Receives num_points * seg_len floats, point-major. Each spectrum
 *              is fftshifted: index 0 is -sample_rate/2, index seg_len/2 is DC.
 */
void psd_process(psd_t *psd, const iq16_t *frame, uint16_t num_points, float *out);

/** Speed in m/s for a (possibly fractional) fftshifted bin index. */
float psd_bin_to_speed(const psd_t *psd, float shifted_bin);

/** Fftshifted bin index for a speed in m/s. */
float psd_speed_to_bin(const psd_t *psd, float speed_mps);

/** Largest unambiguous speed, i.e. the value at the edges of the spectrum. */
float psd_max_speed(const psd_t *psd);

/** Width of one bin in m/s: the raw speed resolution before interpolation. */
float psd_speed_resolution(const psd_t *psd);

#endif /* RP_RADAR_PSD_H */
