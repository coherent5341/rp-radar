/*
 * Peak extraction from a Doppler spectrum.
 *
 * The detection statistic is the peak's height relative to the *median* of the
 * spectrum rather than its mean. The median is barely moved by a handful of
 * large bins, so a strong club-head return does not raise the threshold that
 * the much weaker ball return has to clear — which is exactly the situation a
 * few milliseconds after impact, when both are in the beam at once.
 */
#ifndef RP_RADAR_PEAKS_H
#define RP_RADAR_PEAKS_H

#include <stdbool.h>
#include <stdint.h>

/** Upper bound on peaks reported per spectrum. */
#define PEAKS_MAX 8u

typedef struct
{
	float bin;   /**< Fractional, fftshifted bin index of the peak. */
	float power; /**< Interpolated peak height, linear power. */
	float snr;   /**< power / median of the spectrum. */
} peak_t;

/**
 * Median of @p n values. @p scratch must have room for @p n floats and is
 * clobbered. For even @p n this returns the lower of the two central values,
 * which is all the threshold needs and avoids a second selection pass.
 */
float peak_median(const float *x, uint16_t n, float *scratch);

/**
 * Sub-bin peak location by fitting a parabola through (i-1, i, i+1).
 * Returns @p i unchanged at the array edges, where no fit is possible.
 */
float peak_parabolic(const float *y, uint16_t i, uint16_t n);

/**
 * Find local maxima that exceed @p median * @p threshold_rel.
 *
 * @param exclude_lo,exclude_hi Half-open bin range to ignore, used to mask the
 *        zero-Doppler region where static clutter lives. Pass an empty range
 *        (lo >= hi) to disable.
 * @param guard Minimum separation, in bins, between reported peaks. Peaks
 *        within this distance of an already-accepted, stronger peak are
 *        suppressed, so one broad return is not reported several times.
 * @param out Receives peaks sorted by descending power.
 *
 * @return Number of peaks written, at most @p max_out.
 */
uint16_t peak_find(const float *spectrum, uint16_t n, float median, float threshold_rel,
                   uint16_t guard, uint16_t exclude_lo, uint16_t exclude_hi,
                   peak_t *out, uint16_t max_out);

#endif /* RP_RADAR_PEAKS_H */
