/*
 * Shared plain-C types for the DSP layer.
 *
 * Nothing in src/dsp/ includes an Acconeer header. That keeps the whole signal
 * chain compilable and unit-testable on the host (see test/), where no RSS
 * library exists. The application layer is responsible for handing frames over,
 * which is a free cast because iq16_t is layout-compatible with
 * acc_int16_complex_t (see radar_session.c for the static assertions).
 */
#ifndef RP_RADAR_DSP_TYPES_H
#define RP_RADAR_DSP_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Layout-compatible mirror of acc_int16_complex_t. */
typedef struct
{
	int16_t real;
	int16_t imag;
} iq16_t;

/** Single-precision complex, used inside the FFT. */
typedef struct
{
	float re;
	float im;
} cplx_t;

/*
 * Sparse IQ frames are laid out sweep-major: the frame is sweeps_per_frame
 * consecutive sweeps, each of num_points complex samples. This helper is the
 * only place that encodes that ordering.
 */
static inline const iq16_t *iq_at(const iq16_t *frame, uint16_t num_points,
                                  uint16_t sweep, uint16_t point)
{
	return &frame[((uint32_t)sweep * num_points) + point];
}

#endif /* RP_RADAR_DSP_TYPES_H */
