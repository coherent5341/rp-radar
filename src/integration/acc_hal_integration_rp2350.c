/*
 * A121 hardware abstraction layer for the RP2350.
 *
 * This is the whole of the platform port. Everything above it — RSS, the
 * service API, Acconeer's example applications — is unchanged from the vendor
 * SDK; the ESP32 build LilyGO points at differs from this build only in the
 * contents of this file and its sibling acc_integration_rp2350.c.
 *
 * Three things are worth knowing about:
 *
 * SPI transfers are full duplex and in place. RSS hands over one buffer that is
 * both the command and the response. DMA moves it without occupying the CPU,
 * which matters because the swing tracker reads 2 kB every 1.6 ms and the CPU
 * has processing to do in that window.
 *
 * Chip select is driven as a plain GPIO. The RP2350's SPI block can deassert
 * its hardware CSn between FIFO frames, and RSS requires it held low across a
 * whole transfer.
 *
 * The data-ready wait is level-triggered rather than edge-triggered. A rising
 * edge can occur between RSS starting a measurement and the host arming the
 * interrupt; a level-sensitive source latches the condition instead of losing
 * it, so the "already high" case needs no special handling. The handler
 * disables the source immediately, since a level interrupt would otherwise
 * re-fire continuously while the line stays high.
 */
#include "acc_hal_integration_a121.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"
#include "pico/time.h"

#include "acc_integration.h"
#include "t_radar_pins.h"

/* Set to 1 to poll the interrupt line instead of using a GPIO interrupt. */
#ifndef TRADAR_POLL_INTERRUPT
#define TRADAR_POLL_INTERRUPT 0
#endif

/* Set to 0 to use the blocking SPI helpers instead of DMA. */
#ifndef TRADAR_USE_DMA
#define TRADAR_USE_DMA 1
#endif

/** Lowest level that reaches the console. Raise for quieter output. */
#ifndef TRADAR_LOG_LEVEL
#define TRADAR_LOG_LEVEL ACC_LOG_LEVEL_INFO
#endif

static bool s_initialised;

#if TRADAR_USE_DMA
static int s_dma_tx = -1;
static int s_dma_rx = -1;
#endif

#if !TRADAR_POLL_INTERRUPT
static volatile bool s_interrupt_seen;

static void interrupt_handler(uint gpio, uint32_t events)
{
	(void)events;

	if (gpio == TRADAR_PIN_INTERRUPT)
	{
		/* Level-sensitive: disarm before returning or this re-enters forever. */
		gpio_set_irq_enabled(TRADAR_PIN_INTERRUPT, GPIO_IRQ_LEVEL_HIGH, false);
		s_interrupt_seen = true;
	}
}
#endif

static void hal_init(void)
{
	if (s_initialised)
	{
		return;
	}

	spi_init(TRADAR_SPI_INSTANCE, TRADAR_SPI_BAUDRATE_HZ);

	/*
	 * SPI mode 0: sample on the rising edge, MSB first, 8-bit frames. This is
	 * what the A121 expects.
	 */
	spi_set_format(TRADAR_SPI_INSTANCE, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

	gpio_set_function(TRADAR_PIN_MISO, GPIO_FUNC_SPI);
	gpio_set_function(TRADAR_PIN_SCK, GPIO_FUNC_SPI);
	gpio_set_function(TRADAR_PIN_MOSI, GPIO_FUNC_SPI);

	gpio_init(TRADAR_PIN_CS);
	gpio_set_dir(TRADAR_PIN_CS, GPIO_OUT);
	gpio_put(TRADAR_PIN_CS, 1);

	gpio_init(TRADAR_PIN_ENABLE);
	gpio_set_dir(TRADAR_PIN_ENABLE, GPIO_OUT);
	gpio_put(TRADAR_PIN_ENABLE, 0);

	gpio_init(TRADAR_PIN_INTERRUPT);
	gpio_set_dir(TRADAR_PIN_INTERRUPT, GPIO_IN);
	/*
	 * The A121 drives this line actively; the pull-down only defines the level
	 * while the sensor is disabled or the wire is off, which stops a floating
	 * input from looking like a permanent data-ready.
	 */
	gpio_pull_down(TRADAR_PIN_INTERRUPT);

#ifdef TRADAR_PIN_SUPPLY
	gpio_init(TRADAR_PIN_SUPPLY);
	gpio_set_dir(TRADAR_PIN_SUPPLY, GPIO_OUT);
	gpio_put(TRADAR_PIN_SUPPLY, 0);
#endif

#if !TRADAR_POLL_INTERRUPT
	gpio_set_irq_enabled_with_callback(TRADAR_PIN_INTERRUPT, GPIO_IRQ_LEVEL_HIGH, false,
	                                   &interrupt_handler);
#endif

#if TRADAR_USE_DMA
	s_dma_tx = dma_claim_unused_channel(false);
	s_dma_rx = dma_claim_unused_channel(false);

	if (s_dma_tx < 0 || s_dma_rx < 0)
	{
		/* Fall back to blocking transfers rather than failing to start. */
		if (s_dma_tx >= 0)
		{
			dma_channel_unclaim((uint)s_dma_tx);
			s_dma_tx = -1;
		}
		if (s_dma_rx >= 0)
		{
			dma_channel_unclaim((uint)s_dma_rx);
			s_dma_rx = -1;
		}
	}
#endif

	s_initialised = true;
}

#if TRADAR_USE_DMA
static void spi_transfer_dma(uint8_t *buffer, size_t size)
{
	dma_channel_config tx = dma_channel_get_default_config((uint)s_dma_tx);
	dma_channel_config rx = dma_channel_get_default_config((uint)s_dma_rx);

	channel_config_set_transfer_data_size(&tx, DMA_SIZE_8);
	channel_config_set_dreq(&tx, spi_get_dreq(TRADAR_SPI_INSTANCE, true));
	channel_config_set_read_increment(&tx, true);
	channel_config_set_write_increment(&tx, false);

	channel_config_set_transfer_data_size(&rx, DMA_SIZE_8);
	channel_config_set_dreq(&rx, spi_get_dreq(TRADAR_SPI_INSTANCE, false));
	channel_config_set_read_increment(&rx, false);
	channel_config_set_write_increment(&rx, true);

	/*
	 * Receive is configured first and both are started together, so the RX
	 * channel is already draining the FIFO when the first byte arrives.
	 * In-place is safe: a byte is only received after it has been sent.
	 */
	dma_channel_configure((uint)s_dma_rx, &rx, buffer, &spi_get_hw(TRADAR_SPI_INSTANCE)->dr,
	                      size, false);
	dma_channel_configure((uint)s_dma_tx, &tx, &spi_get_hw(TRADAR_SPI_INSTANCE)->dr, buffer,
	                      size, false);

	dma_start_channel_mask((1u << (uint)s_dma_tx) | (1u << (uint)s_dma_rx));
	dma_channel_wait_for_finish_blocking((uint)s_dma_rx);
}
#endif

static void spi_transfer(acc_sensor_id_t sensor_id, uint8_t *buffer, size_t buffer_size)
{
	(void)sensor_id;

	if (buffer_size == 0u)
	{
		return;
	}

	gpio_put(TRADAR_PIN_CS, 0);

#if TRADAR_USE_DMA
	if (s_dma_tx >= 0 && s_dma_rx >= 0)
	{
		spi_transfer_dma(buffer, buffer_size);
	}
	else
#endif
	{
		spi_write_read_blocking(TRADAR_SPI_INSTANCE, buffer, buffer, buffer_size);
	}

	gpio_put(TRADAR_PIN_CS, 1);
}

static void hal_log(acc_log_level_t level, const char *module, const char *format, ...)
{
	static const char *const names[] = {"E", "W", "I", "V", "D"};

	if (level > TRADAR_LOG_LEVEL)
	{
		return;
	}

	const char *tag = (level <= ACC_LOG_LEVEL_DEBUG) ? names[level] : "?";

	printf("[%s/%s] ", tag, module);

	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);

	printf("\n");
}

void acc_hal_integration_sensor_supply_on(acc_sensor_id_t sensor_id)
{
	(void)sensor_id;
	hal_init();

#ifdef TRADAR_PIN_SUPPLY
	gpio_put(TRADAR_PIN_SUPPLY, 1);
	/* Let the board's 1.8 V regulator come up before ENABLE is touched. */
	acc_integration_sleep_ms(5u);
#endif
}

void acc_hal_integration_sensor_supply_off(acc_sensor_id_t sensor_id)
{
	(void)sensor_id;
	hal_init();

	gpio_put(TRADAR_PIN_ENABLE, 0);

#ifdef TRADAR_PIN_SUPPLY
	gpio_put(TRADAR_PIN_SUPPLY, 0);
	acc_integration_sleep_ms(5u);
#endif
}

void acc_hal_integration_sensor_enable(acc_sensor_id_t sensor_id)
{
	(void)sensor_id;
	hal_init();

	/* CS must be idle high before the sensor starts listening. */
	gpio_put(TRADAR_PIN_CS, 1);
	gpio_put(TRADAR_PIN_ENABLE, 1);

	acc_integration_sleep_ms(TRADAR_ENABLE_SETTLE_MS);
}

void acc_hal_integration_sensor_disable(acc_sensor_id_t sensor_id)
{
	(void)sensor_id;
	hal_init();

	gpio_put(TRADAR_PIN_ENABLE, 0);

	acc_integration_sleep_ms(TRADAR_ENABLE_SETTLE_MS);
}

bool acc_hal_integration_wait_for_sensor_interrupt(acc_sensor_id_t sensor_id, uint32_t timeout_ms)
{
	(void)sensor_id;

	const absolute_time_t deadline = make_timeout_time_ms(timeout_ms);

#if TRADAR_POLL_INTERRUPT
	while (!gpio_get(TRADAR_PIN_INTERRUPT))
	{
		if (time_reached(deadline))
		{
			return gpio_get(TRADAR_PIN_INTERRUPT);
		}
		tight_loop_contents();
	}

	return true;
#else
	s_interrupt_seen = false;
	gpio_set_irq_enabled(TRADAR_PIN_INTERRUPT, GPIO_IRQ_LEVEL_HIGH, true);

	while (!s_interrupt_seen)
	{
		/*
		 * Sleeps until the GPIO interrupt sets the event register or the
		 * deadline passes, so a stalled sensor cannot hang the application.
		 */
		if (best_effort_wfe_or_timeout(deadline))
		{
			gpio_set_irq_enabled(TRADAR_PIN_INTERRUPT, GPIO_IRQ_LEVEL_HIGH, false);

			/* One last look, in case it arrived as the deadline expired. */
			return s_interrupt_seen || gpio_get(TRADAR_PIN_INTERRUPT);
		}
	}

	return true;
#endif
}

uint16_t acc_hal_integration_sensor_count(void)
{
	return 1u;
}

const acc_hal_a121_t *acc_hal_rss_integration_get_implementation(void)
{
	static const acc_hal_a121_t hal = {
	    .max_spi_transfer_size = TRADAR_MAX_SPI_TRANSFER_SIZE,
	    .mem_alloc             = malloc,
	    .mem_free              = free,
	    .transfer              = spi_transfer,
	    .log                   = hal_log,
	    /*
	     * transfer16 is left unset. It exists for platforms where 16-bit SPI
	     * frames cost less than 8-bit ones; on the RP2350 the DMA moves bytes
	     * either way, so the byte-wise path is equally fast and avoids any
	     * question about halfword ordering on the wire.
	     */
	    .optimization = {.transfer16 = NULL},
	};

	hal_init();

	return &hal;
}
