#include "fft.h"

#include <math.h>
#include <stddef.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

bool fft_size_supported(uint16_t n)
{
	return n >= 2u && n <= FFT_MAX_SIZE && (n & (uint16_t)(n - 1u)) == 0u;
}

bool fft_init(fft_t *plan, uint16_t n)
{
	if (plan == NULL || !fft_size_supported(n))
	{
		return false;
	}

	uint8_t log2n = 0;
	while (((uint16_t)1u << log2n) < n)
	{
		log2n++;
	}

	plan->n     = n;
	plan->log2n = log2n;

	const double step = -2.0 * M_PI / (double)n;
	for (uint16_t k = 0; k < (uint16_t)(n / 2u); k++)
	{
		plan->tw_re[k] = (float)cos(step * (double)k);
		plan->tw_im[k] = (float)sin(step * (double)k);
	}

	return true;
}

/* In-place bit-reversal permutation using the standard reversed-counter trick. */
static void bit_reverse(cplx_t *x, uint16_t n)
{
	uint16_t j = 0;

	for (uint16_t i = 1; i < n; i++)
	{
		uint16_t bit = n >> 1;

		for (; (j & bit) != 0u; bit >>= 1)
		{
			j &= (uint16_t)~bit;
		}
		j |= bit;

		if (i < j)
		{
			const cplx_t tmp = x[i];
			x[i]             = x[j];
			x[j]             = tmp;
		}
	}
}

void fft_forward(const fft_t *plan, cplx_t *x)
{
	const uint16_t n = plan->n;

	bit_reverse(x, n);

	for (uint16_t len = 2; len <= n; len <<= 1)
	{
		const uint16_t half = (uint16_t)(len >> 1);
		/* Twiddle table is sized for n, so stride down to the current stage. */
		const uint16_t stride = (uint16_t)(n / len);

		for (uint16_t base = 0; base < n; base += len)
		{
			uint16_t tw = 0;

			for (uint16_t k = 0; k < half; k++, tw = (uint16_t)(tw + stride))
			{
				const float wr = plan->tw_re[tw];
				const float wi = plan->tw_im[tw];

				cplx_t *a = &x[base + k];
				cplx_t *b = &x[base + k + half];

				const float br = (b->re * wr) - (b->im * wi);
				const float bi = (b->re * wi) + (b->im * wr);

				b->re = a->re - br;
				b->im = a->im - bi;
				a->re = a->re + br;
				a->im = a->im + bi;
			}
		}
	}
}
