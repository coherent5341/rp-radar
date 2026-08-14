/*
 * Minimal in-place radix-2 complex FFT.
 *
 * Written from scratch so that the repository carries no third-party DSP
 * dependency: everything here is portable C99 and builds identically for the
 * RP2350 and for the host test suite.
 */
#ifndef RP_RADAR_FFT_H
#define RP_RADAR_FFT_H

#include "dsp_types.h"

/*
 * Largest transform this build supports. Sizing driver:
 *   - ball_speed uses sweeps_per_frame/num_segments (200/4 = 50 -> 64)
 *   - swing_tracker transforms a whole frame of sweeps (32/64)
 * 512 leaves generous headroom at a cost of ~5 kB of tables per plan.
 */
#define FFT_MAX_SIZE 512u

typedef struct
{
	uint16_t n;
	uint8_t  log2n;
	/* Twiddles for k = 0 .. n/2-1, angle = -2*pi*k/n. */
	float tw_re[FFT_MAX_SIZE / 2];
	float tw_im[FFT_MAX_SIZE / 2];
} fft_t;

/**
 * Prepare a plan. @p n must be a power of two, 2 <= n <= FFT_MAX_SIZE.
 * Returns false if @p n is unusable, in which case the plan is left untouched.
 */
bool fft_init(fft_t *plan, uint16_t n);

/**
 * Forward DFT, in place, natural order in and out (the bit-reversal permutation
 * is applied internally). No scaling is applied; a length-n transform of a unit
 * amplitude tone yields a peak of magnitude n/2 per sideband.
 */
void fft_forward(const fft_t *plan, cplx_t *x);

/** True if @p n is a power of two within the supported range. */
bool fft_size_supported(uint16_t n);

#endif /* RP_RADAR_FFT_H */
