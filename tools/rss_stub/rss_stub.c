/*
 * Link-only stand-in for the Acconeer RSS library.
 *
 * Enabled with -DRP_RADAR_RSS_STUB=ON. It defines every RSS symbol this project
 * references so the firmware compiles and links without the licensed library,
 * which is useful for checking a toolchain setup or for CI. It does not talk to
 * a sensor: acc_sensor_create returns NULL and the applications exit with a
 * clear message.
 *
 * What this proves: this project's own code compiles, and the Pico SDK links.
 * What it does not prove: that your compiler's float ABI matches the real
 * library's. This stub is built with your flags, so it always agrees with them.
 * See cmake/toolchain_rp2350_hardfp.cmake.
 *
 * The Acconeer headers are still required to build this file — only the
 * compiled library is replaced.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "acc_config.h"
#include "acc_definitions_a121.h"
#include "acc_definitions_common.h"
#include "acc_hal_definitions_a121.h"
#include "acc_processing.h"
#include "acc_rss_a121.h"
#include "acc_sensor.h"
#include "acc_version.h"

struct acc_config
{
	acc_config_profile_t    profile;
	acc_config_prf_t        prf;
	acc_config_idle_state_t inter_sweep_idle_state;
	acc_config_idle_state_t inter_frame_idle_state;
	int32_t                 start_point;
	uint16_t                num_points;
	uint16_t                step_length;
	uint16_t                hwaas;
	uint8_t                 receiver_gain;
	uint16_t                sweeps_per_frame;
	float                   sweep_rate;
	float                   frame_rate;
	bool                    phase_enhancement;
	bool                    iq_imbalance_compensation;
	bool                    continuous_sweep_mode;
	bool                    double_buffering;
	bool                    enable_tx;
	bool                    enable_loopback;
};

struct acc_processing_handle
{
	uint16_t num_points;
	uint16_t sweeps_per_frame;
};

static void stub_note(const char *what)
{
	printf("[E/rss-stub] %s: this firmware was built with RP_RADAR_RSS_STUB=ON\n", what);
	printf("[E/rss-stub] and contains no radar software. Rebuild against the real\n");
	printf("[E/rss-stub] Acconeer A121 Cortex-M33 SDK; see docs/02-rss-library.md.\n");
}

const char *acc_version_get(void)
{
	return "stub (no RSS linked)";
}

uint32_t acc_version_get_hex(void)
{
	return 0u;
}

bool acc_rss_hal_register(const acc_hal_a121_t *hal)
{
	return hal != NULL;
}

void acc_rss_set_log_level(acc_log_level_t level)
{
	(void)level;
}

bool acc_rss_get_buffer_size(const acc_config_t *config, uint32_t *buffer_size)
{
	if (config == NULL || buffer_size == NULL)
	{
		return false;
	}

	*buffer_size =
	    ((uint32_t)config->num_points * config->sweeps_per_frame * sizeof(acc_int16_complex_t)) +
	    1024u;

	return true;
}

acc_config_t *acc_config_create(void)
{
	acc_config_t *config = calloc(1u, sizeof(*config));

	if (config != NULL)
	{
		config->profile          = ACC_CONFIG_PROFILE_3;
		config->prf              = ACC_CONFIG_PRF_15_6_MHZ;
		config->num_points       = 1u;
		config->step_length      = 24u;
		config->hwaas            = 8u;
		config->receiver_gain    = 16u;
		config->sweeps_per_frame = 16u;
		config->enable_tx        = true;
	}

	return config;
}

void acc_config_destroy(acc_config_t *config)
{
	free(config);
}

void acc_config_log(const acc_config_t *config)
{
	(void)config;
}

#define STUB_SETTER(name, type, field)                                                    \
	void acc_config_##name##_set(acc_config_t *config, type value)                        \
	{                                                                                     \
		if (config != NULL)                                                               \
		{                                                                                 \
			config->field = value;                                                        \
		}                                                                                 \
	}                                                                                     \
	type acc_config_##name##_get(const acc_config_t *config)                              \
	{                                                                                     \
		return (config != NULL) ? config->field : (type)0;                                \
	}

STUB_SETTER(profile, acc_config_profile_t, profile)
STUB_SETTER(prf, acc_config_prf_t, prf)
STUB_SETTER(start_point, int32_t, start_point)
STUB_SETTER(num_points, uint16_t, num_points)
STUB_SETTER(step_length, uint16_t, step_length)
STUB_SETTER(hwaas, uint16_t, hwaas)
STUB_SETTER(receiver_gain, uint8_t, receiver_gain)
STUB_SETTER(sweeps_per_frame, uint16_t, sweeps_per_frame)
STUB_SETTER(sweep_rate, float, sweep_rate)
STUB_SETTER(frame_rate, float, frame_rate)
STUB_SETTER(phase_enhancement, bool, phase_enhancement)
STUB_SETTER(iq_imbalance_compensation, bool, iq_imbalance_compensation)
STUB_SETTER(continuous_sweep_mode, bool, continuous_sweep_mode)
STUB_SETTER(double_buffering, bool, double_buffering)
STUB_SETTER(enable_tx, bool, enable_tx)
STUB_SETTER(enable_loopback, bool, enable_loopback)
STUB_SETTER(inter_sweep_idle_state, acc_config_idle_state_t, inter_sweep_idle_state)
STUB_SETTER(inter_frame_idle_state, acc_config_idle_state_t, inter_frame_idle_state)

acc_processing_t *acc_processing_create(const acc_config_t         *config,
                                        acc_processing_metadata_t *processing_metadata)
{
	if (config == NULL)
	{
		return NULL;
	}

	acc_processing_t *handle = calloc(1u, sizeof(*handle));

	if (handle == NULL)
	{
		return NULL;
	}

	handle->num_points       = config->num_points;
	handle->sweeps_per_frame = config->sweeps_per_frame;

	if (processing_metadata != NULL)
	{
		memset(processing_metadata, 0, sizeof(*processing_metadata));
		processing_metadata->sweep_data_length = config->num_points;
		processing_metadata->frame_data_length =
		    (uint16_t)(config->num_points * config->sweeps_per_frame);
		processing_metadata->subsweep_data_length[0] = config->num_points;
		processing_metadata->max_sweep_rate          = config->sweep_rate;
	}

	return handle;
}

void acc_processing_execute(acc_processing_t *handle, void *buffer, acc_processing_result_t *result)
{
	(void)handle;

	if (result != NULL)
	{
		memset(result, 0, sizeof(*result));
		result->frame = (acc_int16_complex_t *)buffer;
	}
}

void acc_processing_destroy(acc_processing_t *handle)
{
	free(handle);
}

float acc_processing_points_to_meter(int32_t points)
{
	return (float)points * 2.5e-3f;
}

int32_t acc_processing_meter_to_points(float length)
{
	return (int32_t)(length / 2.5e-3f);
}

void acc_processing_get_temperature_adjustment_factors(int16_t reference_temperature,
                                                       int16_t current_temperature,
                                                       acc_config_profile_t profile,
                                                       float *signal_adjust_factor,
                                                       float *deviation_adjust_factor)
{
	(void)reference_temperature;
	(void)current_temperature;
	(void)profile;

	if (signal_adjust_factor != NULL)
	{
		*signal_adjust_factor = 1.0f;
	}
	if (deviation_adjust_factor != NULL)
	{
		*deviation_adjust_factor = 1.0f;
	}
}

acc_sensor_t *acc_sensor_create(acc_sensor_id_t sensor_id)
{
	(void)sensor_id;

	stub_note("acc_sensor_create");

	return NULL;
}

void acc_sensor_destroy(acc_sensor_t *sensor)
{
	(void)sensor;
}

bool acc_sensor_calibrate(acc_sensor_t *sensor, bool *cal_complete, acc_cal_result_t *cal_result,
                          void *buffer, uint32_t buffer_size)
{
	(void)sensor;
	(void)cal_result;
	(void)buffer;
	(void)buffer_size;

	if (cal_complete != NULL)
	{
		*cal_complete = true;
	}

	return false;
}

bool acc_sensor_get_cal_info(const acc_cal_result_t *cal_result, acc_cal_info_t *cal_info)
{
	(void)cal_result;

	if (cal_info != NULL)
	{
		cal_info->temperature = 0;
	}

	return false;
}

bool acc_sensor_prepare(acc_sensor_t *sensor, const acc_config_t *config,
                        const acc_cal_result_t *cal_result, void *buffer, uint32_t buffer_size)
{
	(void)sensor;
	(void)config;
	(void)cal_result;
	(void)buffer;
	(void)buffer_size;

	return false;
}

bool acc_sensor_measure(acc_sensor_t *sensor)
{
	(void)sensor;

	return false;
}

bool acc_sensor_read(const acc_sensor_t *sensor, void *buffer, uint32_t buffer_size)
{
	(void)sensor;
	(void)buffer;
	(void)buffer_size;

	return false;
}

bool acc_sensor_connected(acc_sensor_id_t sensor_id)
{
	(void)sensor_id;

	return false;
}

void acc_sensor_status(const acc_sensor_t *sensor)
{
	(void)sensor;

	stub_note("acc_sensor_status");
}

bool acc_sensor_hibernate_on(acc_sensor_t *sensor)
{
	(void)sensor;

	return false;
}

bool acc_sensor_hibernate_off(const acc_sensor_t *sensor)
{
	(void)sensor;

	return false;
}

bool acc_sensor_validate_calibration(const acc_cal_result_t *cal_result)
{
	(void)cal_result;

	return false;
}
