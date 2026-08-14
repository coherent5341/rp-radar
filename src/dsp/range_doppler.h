/*
 * Range-Doppler map, detections, and Doppler de-aliasing.
 *
 * Transforming a sparse IQ frame down the sweep axis for every range point
 * gives a distance-versus-speed image. Local maxima in that image are candidate
 * targets: during a golf swing the club head appears as a large, fast return
 * that walks in from the far bins, and the ball as a much smaller return that
 * leaves at high speed after impact.
 *
 * De-aliasing
 * -----------
 * A frame samples Doppler at the sweep rate, so speeds beyond
 * sweep_rate * lambda / 4 fold back into the measured interval. A wide-range
 * configuration cannot reach the sweep rate needed for a 70 m/s ball, so the
 * folded estimate has to be unwrapped with independent information.
 *
 * The independent information is range walk. A fast target crosses several
 * range bins during the ~2 ms a frame takes, and the slope of that motion is a
 * direct, unambiguous speed measurement. It is coarse — a few m/s at best — but
 * that is plenty to pick which Doppler alias is the true one, because the
 * aliases are 2 * max_speed apart. Coarse estimate chooses the alias, precise
 * Doppler estimate supplies the value.
 */
#ifndef RP_RADAR_RANGE_DOPPLER_H
#define RP_RADAR_RANGE_DOPPLER_H

#include "peaks.h"
#include "psd.h"

/** Range points a map may span. Sized for the swing tracker's 24-point config. */
#define RD_MAX_POINTS 32u

/** Blocks the range-walk fit may split a frame into. */
#define RD_MAX_WALK_SEGMENTS 16u

typedef struct
{
	/**
	 * Interpolated across the range axis, so this is not restricted to the
	 * range gate grid. The profile envelope is close to Gaussian and a Gaussian
	 * is a parabola in the log domain, so fitting the neighbouring gates
	 * recovers well under a gate of position when the return is strong.
	 */
	float    range_m;
	float    speed_mps; /**< Folded speed, straight from the Doppler axis. */
	float    power;
	float    snr;
	uint16_t point; /**< Range bin the peak was found in. */
	float    bin;   /**< Fractional Doppler bin. */
} rd_det_t;

typedef struct
{
	psd_t    psd;
	uint16_t num_points;
	uint16_t spf;
	float    start_m;
	float    step_m;
	/* Scratch for median/centroid work, kept out of the caller's stack. */
	float scratch[FFT_MAX_SIZE];
} rd_t;

/**
 * @param num_points Range points per sweep, at most RD_MAX_POINTS.
 * @param spf        Sweeps per frame; also the Doppler transform length, so it
 *                   must be a power of two.
 * @param start_m    Range of the first point, in metres.
 * @param step_m     Spacing between range points, in metres.
 */
bool rd_init(rd_t *rd, uint16_t num_points, uint16_t spf, float sweep_rate_hz,
             float start_m, float step_m);

/**
 * Build the map. @p map receives num_points * spf floats, point-major, each row
 * fftshifted so index 0 is the most negative speed.
 */
void rd_process(rd_t *rd, const iq16_t *frame, float *map);

/**
 * Extract targets from a map produced by rd_process.
 *
 * @param threshold_rel Peak height required, relative to the median of that
 *        range bin's spectrum.
 * @param min_speed_mps Speeds below this magnitude are masked out, removing the
 *        static scene without disturbing anything that is actually moving.
 * @param guard Doppler bins of non-maximum suppression.
 * @param range_guard Range gates of non-maximum suppression. A target wider
 *        than one gate — which any target is, since the profile envelope is
 *        sampled without gaps — otherwise appears as a detection in each gate
 *        it touches. Those duplicates share a speed but differ in range, so the
 *        tracker treats them as separate objects and a single club head can
 *        occupy every track slot. Suppression applies only when a candidate is
 *        close in *both* range and Doppler, so two genuinely different targets
 *        at the same range but different speeds are still reported separately.
 *        1 suits the shipped configuration; 0 disables it.
 * @return Number of detections written, sorted by descending power.
 */
uint16_t rd_detect(rd_t *rd, const float *map, float threshold_rel, float min_speed_mps,
                   uint16_t guard, uint16_t range_guard, rd_det_t *out, uint16_t max_out);

/**
 * Coarse, alias-free speed from range walk within a single frame.
 *
 * Splits the frame into @p num_segments blocks of sweeps, takes the
 * amplitude-weighted range centroid of each, and least-squares fits a line to
 * the centroid against time. Returns the slope in m/s, positive for a receding
 * target, matching the Doppler sign convention.
 *
 * Only meaningful when one target dominates the frame; with two comparable
 * returns the centroid sits between them. @p out_confidence, if not NULL,
 * receives 0..1 based on how concentrated the energy is, so callers can ignore
 * the estimate when the scene is ambiguous.
 */
float rd_range_walk_speed(rd_t *rd, const iq16_t *frame, uint16_t num_segments,
                          float *out_confidence);

/**
 * Unfold @p folded_speed using @p coarse_speed as the guide.
 *
 * Picks the integer number of aliases k minimising the distance between
 * folded + k * 2 * max_speed and the coarse estimate.
 */
float rd_dealias(float folded_speed, float max_speed, float coarse_speed);

#endif /* RP_RADAR_RANGE_DOPPLER_H */
