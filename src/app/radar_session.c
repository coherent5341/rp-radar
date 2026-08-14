#include "radar_session.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "acc_definitions_common.h"
#include "acc_hal_integration_a121.h"
#include "acc_integration.h"
#include "acc_rss_a121.h"
#include "acc_version.h"
#include "t_radar_pins.h"

/*
 * The DSP layer keeps its own copy of these types so it can be built and tested
 * without the RSS headers. If either ever drifts, catch it here rather than in
 * a field somewhere.
 */
_Static_assert(sizeof(iq16_t) == sizeof(acc_int16_complex_t),
               "iq16_t must match acc_int16_complex_t");
_Static_assert(offsetof(iq16_t, real) == offsetof(acc_int16_complex_t, real),
               "iq16_t real offset must match acc_int16_complex_t");
_Static_assert(offsetof(iq16_t, imag) == offsetof(acc_int16_complex_t, imag),
               "iq16_t imag offset must match acc_int16_complex_t");

_Static_assert((int)RADAR_PRF_19_5_MHZ == (int)ACC_CONFIG_PRF_19_5_MHZ, "PRF enum drift");
_Static_assert((int)RADAR_PRF_5_2_MHZ == (int)ACC_CONFIG_PRF_5_2_MHZ, "PRF enum drift");
_Static_assert((int)RADAR_PROFILE_1 == (int)ACC_CONFIG_PROFILE_1, "profile enum drift");
_Static_assert((int)RADAR_PROFILE_5 == (int)ACC_CONFIG_PROFILE_5, "profile enum drift");

#define SENSOR_ID ((acc_sensor_id_t)TRADAR_SENSOR_ID)

void radar_session_swing_tracker_config(radar_session_cfg_t *cfg)
{
	memset(cfg, 0, sizeof(*cfg));

	/*
	 * A 0.30 - 2.10 m window sampled every 0.12 m. Profile 3's envelope is
	 * 0.14 m wide, so 0.12 m spacing samples it without gaps, and starting at
	 * 0.30 m keeps the range clear of the profile's direct leakage.
	 */
	cfg->profile          = RADAR_PROFILE_3;
	cfg->start_point      = 120;
	cfg->num_points       = 16u;
	cfg->step_length      = 48u;
	cfg->sweeps_per_frame = 32u;

	/*
	 * 20 kHz over 16 points is close to what the sensor can sustain. It gives
	 * 625 frames/s and unambiguous speed to +/-24.8 m/s; anything faster is
	 * unfolded using range walk (see range_doppler.h).
	 */
	cfg->sweep_rate_hz = 20000.0f;
	cfg->hwaas         = 0u;
	cfg->frame_rate_hz = 0.0f;

	cfg->continuous_sweep_mode = true;
	cfg->double_buffering      = true;
	cfg->receiver_gain         = 16u;
}

void radar_session_ball_speed_config(radar_session_cfg_t *cfg)
{
	memset(cfg, 0, sizeof(*cfg));

	/*
	 * One range point of profile 5. Profile 5 has both the widest envelope
	 * (0.32 m, so a 60 m/s ball stays in the beam for around 5 ms rather than
	 * 2) and the highest loop gain, which is what a ball's small return needs.
	 * Its direct leakage forces a start beyond 2 x 0.32 m, hence 0.65 m.
	 *
	 * Spending the whole sweep budget on one point rather than two buys about
	 * 8 dB from the extra HWAAS. See docs/03-radar-config-and-theory.md for
	 * the trade against dwell time.
	 */
	cfg->profile          = RADAR_PROFILE_5;
	cfg->start_point      = 260;
	cfg->num_points       = 1u;
	cfg->step_length      = 120u;
	cfg->sweeps_per_frame = 128u;

	/*
	 * 71 kHz puts the folding limit at 88 m/s, comfortably above any golf ball
	 * speed, so this demo never has to unfold anything.
	 */
	cfg->sweep_rate_hz = 71000.0f;
	cfg->hwaas         = 0u;
	cfg->frame_rate_hz = 0.0f;

	cfg->continuous_sweep_mode = true;
	cfg->double_buffering      = true;
	cfg->receiver_gain         = 16u;
}

static bool apply_config(radar_session_t *session)
{
	radar_session_cfg_t *cfg    = &session->cfg;
	acc_config_t        *config = session->config;

	if (!radar_step_length_valid(cfg->step_length))
	{
		printf("[E/session] step_length %u must divide or be a multiple of %u\n",
		       cfg->step_length, RADAR_SPARSE_IQ_PPC);
		return false;
	}

	const uint32_t elements = (uint32_t)cfg->num_points * cfg->sweeps_per_frame;

	if (elements > RADAR_MAX_FRAME_ELEMENTS)
	{
		printf("[E/session] frame of %lu elements exceeds the sensor buffer (%u)\n",
		       (unsigned long)elements, RADAR_MAX_FRAME_ELEMENTS);
		return false;
	}

	const int32_t end_point =
	    cfg->start_point + ((int32_t)(cfg->num_points - 1u) * (int32_t)cfg->step_length);

	cfg->prf = radar_select_prf(end_point, cfg->profile);

	if (cfg->hwaas == 0u)
	{
		cfg->hwaas = radar_hwaas_for_sweep_rate(cfg->prf, cfg->profile, cfg->num_points,
		                                        cfg->sweep_rate_hz, 0.95f);

		if (cfg->hwaas == 0u)
		{
			printf("[E/session] %.0f Hz is unreachable with %u points of profile %d;"
			       " lower the sweep rate or the point count\n",
			       (double)cfg->sweep_rate_hz, cfg->num_points, (int)cfg->profile);
			return false;
		}
	}

	acc_config_profile_set(config, (acc_config_profile_t)cfg->profile);
	acc_config_prf_set(config, (acc_config_prf_t)cfg->prf);
	acc_config_start_point_set(config, cfg->start_point);
	acc_config_num_points_set(config, cfg->num_points);
	acc_config_step_length_set(config, cfg->step_length);
	acc_config_hwaas_set(config, cfg->hwaas);
	acc_config_receiver_gain_set(config, cfg->receiver_gain);
	acc_config_sweeps_per_frame_set(config, cfg->sweeps_per_frame);
	acc_config_sweep_rate_set(config, cfg->sweep_rate_hz);
	acc_config_frame_rate_set(config, cfg->frame_rate_hz);
	acc_config_phase_enhancement_set(config, false);

	/*
	 * Continuous sweep mode requires an unlimited frame rate and the two idle
	 * states to agree. READY is the shallowest, which is what a rate this high
	 * needs; deeper states cannot wake between sweeps in time.
	 */
	acc_config_inter_sweep_idle_state_set(config, ACC_CONFIG_IDLE_STATE_READY);
	acc_config_inter_frame_idle_state_set(config, ACC_CONFIG_IDLE_STATE_READY);
	acc_config_continuous_sweep_mode_set(config, cfg->continuous_sweep_mode);
	acc_config_double_buffering_set(config, cfg->double_buffering);

	return true;
}

static bool calibrate(radar_session_t *session)
{
	bool complete = false;
	bool ok       = true;

	do
	{
		ok = acc_sensor_calibrate(session->sensor, &complete, &session->cal_result,
		                          session->buffer, session->buffer_size);

		if (ok && !complete)
		{
			ok = acc_hal_integration_wait_for_sensor_interrupt(SENSOR_ID,
			                                                   TRADAR_SENSOR_TIMEOUT_MS);
		}
	} while (ok && !complete);

	if (!ok)
	{
		printf("[E/session] calibration failed\n");
		acc_sensor_status(session->sensor);
		return false;
	}

	/*
	 * Acconeer's reference applications power-cycle the sensor after
	 * calibrating, before preparing it for measurement.
	 */
	acc_hal_integration_sensor_disable(SENSOR_ID);
	acc_hal_integration_sensor_enable(SENSOR_ID);

	return true;
}

bool radar_session_open(radar_session_t *session, const radar_session_cfg_t *cfg)
{
	memset(session, 0, sizeof(*session));
	session->cfg = *cfg;

	if (!acc_rss_hal_register(acc_hal_rss_integration_get_implementation()))
	{
		printf("[E/session] acc_rss_hal_register failed\n");
		return false;
	}

	printf("[I/session] RSS %s\n", acc_version_get());

	acc_hal_integration_sensor_supply_on(SENSOR_ID);
	acc_hal_integration_sensor_enable(SENSOR_ID);

	session->config = acc_config_create();
	if (session->config == NULL)
	{
		printf("[E/session] acc_config_create failed\n");
		goto fail;
	}

	if (!apply_config(session))
	{
		goto fail;
	}

	session->processing = acc_processing_create(session->config, &session->metadata);
	if (session->processing == NULL)
	{
		printf("[E/session] acc_processing_create failed; the configuration was rejected\n");
		goto fail;
	}

	if (!acc_rss_get_buffer_size(session->config, &session->buffer_size))
	{
		printf("[E/session] acc_rss_get_buffer_size failed\n");
		goto fail;
	}

	session->buffer = malloc(session->buffer_size);
	if (session->buffer == NULL)
	{
		printf("[E/session] out of memory for a %lu byte sensor buffer\n",
		       (unsigned long)session->buffer_size);
		goto fail;
	}

	session->sensor = acc_sensor_create(SENSOR_ID);
	if (session->sensor == NULL)
	{
		printf("[E/session] acc_sensor_create failed; check power, ENABLE and SPI wiring\n");
		goto fail;
	}

	if (!calibrate(session))
	{
		goto fail;
	}

	if (!acc_sensor_prepare(session->sensor, session->config, &session->cal_result,
	                        session->buffer, session->buffer_size))
	{
		printf("[E/session] acc_sensor_prepare failed\n");
		acc_sensor_status(session->sensor);
		goto fail;
	}

	session->start_m = acc_processing_points_to_meter(session->cfg.start_point);
	session->step_m  = acc_processing_points_to_meter((int32_t)session->cfg.step_length);
	session->frame_period_s =
	    (float)session->cfg.sweeps_per_frame / session->cfg.sweep_rate_hz;
	session->max_speed_mps = radar_max_speed_from_sweep_rate(session->cfg.sweep_rate_hz);

	const uint32_t expected =
	    (uint32_t)session->cfg.num_points * session->cfg.sweeps_per_frame;

	if (session->metadata.frame_data_length != expected)
	{
		printf("[W/session] frame is %u samples, expected %lu\n",
		       session->metadata.frame_data_length, (unsigned long)expected);
	}

	/*
	 * The sensor reports what it can actually sustain for this configuration.
	 * Asking for more than that is the usual cause of a speed axis that reads
	 * consistently wrong, so say so loudly.
	 */
	if (session->metadata.max_sweep_rate > 0.0f &&
	    session->cfg.sweep_rate_hz > session->metadata.max_sweep_rate)
	{
		printf("[W/session] asked for %.0f Hz but the sensor tops out at %.0f Hz here;"
		       " speeds will be wrong. Reduce points, HWAAS or sweep rate.\n",
		       (double)session->cfg.sweep_rate_hz, (double)session->metadata.max_sweep_rate);
	}

	return true;

fail:
	radar_session_close(session);
	return false;
}

bool radar_session_recalibrate(radar_session_t *session)
{
	printf("[I/session] recalibrating\n");

	if (!calibrate(session))
	{
		return false;
	}

	if (!acc_sensor_prepare(session->sensor, session->config, &session->cal_result,
	                        session->buffer, session->buffer_size))
	{
		printf("[E/session] prepare after recalibration failed\n");
		return false;
	}

	session->recalibrations++;

	return true;
}

bool radar_session_next_frame(radar_session_t *session, const iq16_t **frame)
{
	if (!acc_sensor_measure(session->sensor))
	{
		printf("[E/session] acc_sensor_measure failed\n");
		return false;
	}

	if (!acc_hal_integration_wait_for_sensor_interrupt(SENSOR_ID, TRADAR_SENSOR_TIMEOUT_MS))
	{
		printf("[E/session] timed out waiting for the sensor interrupt\n");
		acc_sensor_status(session->sensor);
		return false;
	}

	if (!acc_sensor_read(session->sensor, session->buffer, session->buffer_size))
	{
		printf("[E/session] acc_sensor_read failed\n");
		acc_sensor_status(session->sensor);
		return false;
	}

	acc_processing_execute(session->processing, session->buffer, &session->result);

	session->frames_read++;

	if (session->result.frame_delayed)
	{
		session->frames_delayed++;
	}

	if (session->result.data_saturated)
	{
		session->frames_saturated++;
	}

	if (session->result.calibration_needed)
	{
		if (!radar_session_recalibrate(session))
		{
			return false;
		}
	}

	*frame = (const iq16_t *)session->result.frame;

	return true;
}

static float prf_mhz(radar_prf_t prf)
{
	static const float mhz[RADAR_PRF_COUNT] = {19.5f, 15.6f, 13.0f, 8.7f, 6.5f, 5.2f};

	return (prf < RADAR_PRF_COUNT) ? mhz[prf] : 0.0f;
}

void radar_session_log_summary(const radar_session_t *session)
{
	const radar_session_cfg_t *cfg = &session->cfg;

	const float end_m =
	    session->start_m + ((float)(cfg->num_points - 1u) * session->step_m);

	printf("\n");
	printf("  profile           %d\n", (int)cfg->profile);
	printf("  PRF               %.1f MHz\n", (double)prf_mhz(cfg->prf));
	printf("  range             %.3f - %.3f m in %u points of %.3f m\n", (double)session->start_m,
	       (double)end_m, cfg->num_points, (double)session->step_m);
	printf("  HWAAS             %u\n", cfg->hwaas);
	printf("  sweeps per frame  %u\n", cfg->sweeps_per_frame);
	printf("  sweep rate        %.0f Hz (sensor maximum %.0f Hz)\n", (double)cfg->sweep_rate_hz,
	       (double)session->metadata.max_sweep_rate);
	printf("  frame rate        %.1f Hz (period %.3f ms)\n",
	       (double)(1.0f / session->frame_period_s), (double)(session->frame_period_s * 1000.0f));
	printf("  unambiguous speed +/- %.1f m/s\n", (double)session->max_speed_mps);
	printf("  continuous sweep  %s, double buffering %s\n", cfg->continuous_sweep_mode ? "on" : "off",
	       cfg->double_buffering ? "on" : "off");
	printf("  sensor buffer     %lu bytes\n", (unsigned long)session->buffer_size);
	printf("\n");
}

void radar_session_close(radar_session_t *session)
{
	if (session->sensor != NULL)
	{
		acc_sensor_destroy(session->sensor);
		session->sensor = NULL;
	}

	if (session->processing != NULL)
	{
		acc_processing_destroy(session->processing);
		session->processing = NULL;
	}

	if (session->config != NULL)
	{
		acc_config_destroy(session->config);
		session->config = NULL;
	}

	if (session->buffer != NULL)
	{
		free(session->buffer);
		session->buffer = NULL;
	}

	acc_hal_integration_sensor_disable(SENSOR_ID);
	acc_hal_integration_sensor_supply_off(SENSOR_ID);
}
