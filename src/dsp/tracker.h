/*
 * Multi-target tracker over range-Doppler detections.
 *
 * Detections are per-frame and noisy; a track carries identity across frames so
 * the output is "the same object, here is where it went" rather than a cloud of
 * unrelated points. That continuity is also what makes de-aliasing reliable: an
 * established track's own speed is a far better guide for unwrapping the next
 * Doppler measurement than any single-frame estimate, because a club head
 * cannot change speed by 2 * max_speed between two frames 2 ms apart.
 */
#ifndef RP_RADAR_TRACKER_H
#define RP_RADAR_TRACKER_H

#include "range_doppler.h"

#define TRACKER_MAX_TRACKS 4u

typedef enum
{
	TRACK_CLASS_UNKNOWN = 0,
	TRACK_CLASS_CLUB,
	TRACK_CLASS_BALL
} track_class_t;

typedef struct
{
	bool          active;
	uint16_t      id;
	float         range_m;   /**< Filtered range. */
	float         speed_mps; /**< Filtered, de-aliased radial speed. */
	float         amp;       /**< Smoothed detection power. */
	float         peak_speed_mps;
	uint16_t      hits;
	uint16_t      misses;
	uint32_t      t_first_ms;
	uint32_t      t_last_ms;
	track_class_t cls;
} track_t;

typedef struct
{
	/** Association gate: a detection farther than this never joins a track. */
	float gate_m;
	/** Range filter gain, 0..1. Higher follows the measurement more closely. */
	float alpha;
	/** Speed filter gain, 0..1, applied to the de-aliased Doppler measurement. */
	float beta;
	/** Smoothing for the amplitude estimate used by classification. */
	float amp_alpha;
	/** Consecutive frames without a detection before a track is dropped. */
	uint16_t max_misses;
	/** Frames a track must be seen in before it is reported. */
	uint16_t min_hits;
	/** A track slower than this is never called a ball. */
	float ball_min_speed_mps;
	/**
	 * A ball's return is weaker than the club head's. A track is only called a
	 * ball if its amplitude is below this fraction of the strongest track's.
	 */
	float ball_max_amp_ratio;
} tracker_cfg_t;

typedef struct
{
	tracker_cfg_t cfg;
	track_t       tracks[TRACKER_MAX_TRACKS];
	uint16_t      next_id;
} tracker_t;

/** Fill @p cfg with values that suit the swing tracker configuration. */
void tracker_default_config(tracker_cfg_t *cfg);

void tracker_init(tracker_t *tracker, const tracker_cfg_t *cfg);

/**
 * Advance all tracks by @p dt_s and fold in this frame's detections.
 *
 * @param max_speed_mps Doppler folding period is 2 * this. Pass 0 to skip
 *        de-aliasing entirely.
 * @param coarse_speed_mps Frame-level range-walk estimate, used to unwrap
 *        detections that do not match an existing track.
 * @param coarse_valid Whether @p coarse_speed_mps is trustworthy this frame.
 */
void tracker_update(tracker_t *tracker, const rd_det_t *dets, uint16_t num_dets,
                    float dt_s, uint32_t t_ms, float max_speed_mps,
                    float coarse_speed_mps, bool coarse_valid);

/**
 * Collect confirmed tracks, strongest first.
 * @return Number of pointers written to @p out.
 */
uint16_t tracker_confirmed(const tracker_t *tracker, const track_t **out, uint16_t max_out);

#endif /* RP_RADAR_TRACKER_H */
