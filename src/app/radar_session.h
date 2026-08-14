/*
 * Sensor bring-up and the frame loop, shared by all three applications.
 *
 * Wraps the RSS sequence — register HAL, power up, configure, calibrate, reset,
 * prepare, then measure/wait/read/process per frame — behind an interface that
 * hands back plain sparse IQ frames. It also fills in the parts of the
 * configuration that can be derived rather than guessed: PRF from the range
 * end, and HWAAS from whatever time is left inside a sweep period.
 */
#ifndef RP_RADAR_SESSION_H
#define RP_RADAR_SESSION_H

#include "acc_config.h"
#include "acc_definitions_a121.h"
#include "acc_processing.h"
#include "acc_sensor.h"

#include "dsp_types.h"
#include "radar_math.h"

typedef struct
{
	radar_profile_t profile;
	int32_t         start_point;
	uint16_t        num_points;
	uint16_t        step_length;
	uint16_t        sweeps_per_frame;
	/** Sweep rate in Hz. Sets the maximum unambiguous speed. */
	float sweep_rate_hz;
	/** 0 selects the largest value that fits the sweep period. */
	uint16_t hwaas;
	/** Frame rate in Hz; 0 means as fast as the host reads frames out. */
	float frame_rate_hz;
	/**
	 * Identical timing across every sweep, including across frame boundaries,
	 * so nothing is missed in the gap between frames. Requires frame_rate_hz
	 * of 0 and a non-zero sweep rate.
	 */
	bool continuous_sweep_mode;
	/** Let the sensor sweep into one buffer while the host reads the other. */
	bool double_buffering;
	/** Receiver gain, 0..23. Higher sees more, and saturates sooner. */
	uint8_t receiver_gain;
	/** Set by radar_session_open from the range end and profile. */
	radar_prf_t prf;
} radar_session_cfg_t;

typedef struct
{
	acc_sensor_t     *sensor;
	acc_config_t     *config;
	acc_processing_t *processing;
	acc_cal_result_t  cal_result;

	acc_processing_metadata_t metadata;
	acc_processing_result_t   result;

	void    *buffer;
	uint32_t buffer_size;

	radar_session_cfg_t cfg;

	/* Derived, for the applications to report and use. */
	float    start_m;
	float    step_m;
	float    frame_period_s;
	float    max_speed_mps;
	uint32_t frames_read;
	uint32_t frames_delayed;
	uint32_t frames_saturated;
	uint32_t recalibrations;
} radar_session_t;

/** Configuration for the swing tracker: wide zone, moderate sweep rate. */
void radar_session_swing_tracker_config(radar_session_cfg_t *cfg);

/** Configuration for the ball speed demo: narrow zone, very high sweep rate. */
void radar_session_ball_speed_config(radar_session_cfg_t *cfg);

/**
 * Bring the sensor up and start measuring.
 *
 * Fills in cfg->prf and cfg->hwaas when they were left to be derived, and
 * writes the derived range and timing fields of @p session.
 *
 * @return false if any RSS step failed; a message explaining which is logged.
 */
bool radar_session_open(radar_session_t *session, const radar_session_cfg_t *cfg);

/**
 * Read the next frame, blocking until it is ready.
 *
 * @param frame Receives a pointer to sweeps_per_frame * num_points samples,
 *        sweep-major, valid until the next call.
 * @return false on a sensor timeout or read failure.
 */
bool radar_session_next_frame(radar_session_t *session, const iq16_t **frame);

/**
 * Redo the sensor calibration, which the sensor asks for after a large
 * temperature change. radar_session_next_frame does this automatically when the
 * frame is flagged; exposed here for applications that want to force it.
 */
bool radar_session_recalibrate(radar_session_t *session);

/** Print the applied configuration and what it implies for range and speed. */
void radar_session_log_summary(const radar_session_t *session);

void radar_session_close(radar_session_t *session);

#endif /* RP_RADAR_SESSION_H */
