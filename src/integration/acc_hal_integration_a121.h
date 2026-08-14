/*
 * Board-control side of the A121 integration.
 *
 * RSS itself only ever calls through the acc_hal_a121_t struct it is handed by
 * acc_rss_hal_register(). Everything in this header is called by *application*
 * code instead — powering the sensor, toggling ENABLE, waiting for the
 * data-ready interrupt. Acconeer's example applications use exactly these
 * names and signatures, so their examples drop onto this port unmodified.
 */
#ifndef ACC_HAL_INTEGRATION_A121_H_
#define ACC_HAL_INTEGRATION_A121_H_

#include <stdbool.h>
#include <stdint.h>

#include "acc_definitions_common.h"
#include "acc_hal_definitions_a121.h"

/**
 * Turn the sensor supply on.
 *
 * A no-op unless TRADAR_PIN_SUPPLY is defined: on a stock T-RADAR wired to
 * 3V3, the rail is always up.
 */
void acc_hal_integration_sensor_supply_on(acc_sensor_id_t sensor_id);

/** Turn the sensor supply off. */
void acc_hal_integration_sensor_supply_off(acc_sensor_id_t sensor_id);

/**
 * Raise ENABLE and let the sensor settle.
 *
 * The T-RADAR pulls ENABLE down through 10k, so the sensor stays off until the
 * host asserts this. Returns once the sensor is ready to be addressed.
 */
void acc_hal_integration_sensor_enable(acc_sensor_id_t sensor_id);

/** Drop ENABLE. The sensor loses its state and must be recalibrated. */
void acc_hal_integration_sensor_disable(acc_sensor_id_t sensor_id);

/**
 * Block until the sensor's INTERRUPT line goes high, or the timeout expires.
 *
 * @return true if the interrupt was seen, false on timeout. A timeout is worth
 *         surfacing rather than retrying blindly: it usually means the sensor
 *         is not powered, ENABLE is not connected, or SPI is misrouted.
 */
bool acc_hal_integration_wait_for_sensor_interrupt(acc_sensor_id_t sensor_id,
                                                   uint32_t        timeout_ms);

/** Number of sensors this board supports. Always 1 for the T-RADAR. */
uint16_t acc_hal_integration_sensor_count(void);

/**
 * The HAL implementation to hand to acc_rss_hal_register().
 * Safe to call before any other function here.
 */
const acc_hal_a121_t *acc_hal_rss_integration_get_implementation(void);

#endif /* ACC_HAL_INTEGRATION_A121_H_ */
