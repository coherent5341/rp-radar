/*
 * Golf shot detector: turns a stream of Doppler spectra into one measurement
 * per shot.
 *
 * The signature of a struck ball is a step. Before impact the fastest thing in
 * the beam is the club head, accelerating through the downswing. At impact the
 * ball takes off at roughly 1.3-1.5x the club-head speed, so the fastest
 * component in the spectrum jumps discontinuously in a single frame. For a few
 * tens of milliseconds afterwards both are in the beam at once: the ball as the
 * fast peak, the club as the slower, stronger one.
 *
 * So the detector records a short window around the trigger, finds the largest
 * frame-to-frame jump in peak speed, and reads club speed off the samples
 * before it and ball speed off the samples after it.
 *
 * The measurement is radial. Speed is under-read by cos(theta) where theta is
 * the angle between the ball's path and the line to the sensor, so placing the
 * unit down the target line matters more than anything in this file. See
 * docs/04-running-the-demos.md.
 */
#ifndef RP_RADAR_SHOT_H
#define RP_RADAR_SHOT_H

#include "peaks.h"
#include "psd.h"

/**
 * Frames retained per capture window. At the ball_speed demo's frame rate
 * (~350 Hz) this covers well over the default 250 ms window.
 */
#define SHOT_MAX_SAMPLES 192u

typedef enum
{
	SHOT_STATE_IDLE = 0,
	SHOT_STATE_CAPTURE,
	SHOT_STATE_COOLDOWN
} shot_state_t;

typedef struct
{
	/** Peak speed magnitude that starts a capture, m/s. */
	float trigger_speed_mps;
	/** Peaks below this SNR are ignored entirely. */
	float min_snr;
	/** How long to record after the trigger, ms. */
	uint32_t capture_ms;
	/** Quiet period enforced after a shot, ms; stops the follow-through
	 *  re-triggering the detector. */
	uint32_t cooldown_ms;
	/** Window after impact in which the ball's peak speed is sought, ms. */
	uint32_t ball_window_ms;
	/** Frame-to-frame speed ratio that counts as an impact. */
	float impact_jump_ratio;
} shot_cfg_t;

typedef struct
{
	uint32_t t_ms;
	float    fast_speed_mps; /**< Largest speed magnitude this frame (signed). */
	float    fast_snr;
	float    slow_speed_mps; /**< Second peak, if any; 0 when absent. */
	float    slow_snr;
} shot_sample_t;

typedef struct
{
	bool     valid;
	uint32_t id;
	float    club_speed_mps;
	float    ball_speed_mps;
	/** ball / club. ~1.5 is the practical ceiling for a legal driver. */
	float    smash_factor;
	uint32_t t_impact_ms;
	uint32_t duration_ms;
	uint16_t num_samples;
	/** 0..1. Low values mean the window was sparse or the numbers implausible. */
	float confidence;
	/** True when smash_factor sits outside the physically sensible range. */
	bool implausible;
} shot_result_t;

typedef struct
{
	shot_cfg_t    cfg;
	shot_state_t  state;
	uint32_t      t_state_ms;
	uint32_t      next_id;
	uint16_t      num_samples;
	shot_sample_t samples[SHOT_MAX_SAMPLES];
} shot_t;

/** Defaults tuned for a driver or mid-iron at 0.4-0.8 m. */
void shot_default_config(shot_cfg_t *cfg);

void shot_init(shot_t *shot, const shot_cfg_t *cfg);

/**
 * Feed one frame's peaks.
 *
 * @param peaks Peaks for the range point of interest, sorted by descending
 *        power, as returned by peak_find.
 * @param psd The estimator the peaks came from; supplies the bin-to-speed map.
 * @param out Written only when the return value is true.
 *
 * @return true when a shot completed on this frame.
 */
bool shot_push(shot_t *shot, uint32_t t_ms, const peak_t *peaks, uint16_t num_peaks,
               const psd_t *psd, shot_result_t *out);

/** Current state, for status output. */
shot_state_t shot_state(const shot_t *shot);

#endif /* RP_RADAR_SHOT_H */
