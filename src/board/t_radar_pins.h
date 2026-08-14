/*
 * LilyGO T-RADAR (A121 breakout) wiring to an RP2350.
 *
 * The T-RADAR V1.0 board carries no MCU of its own: it is an A121, a 24 MHz
 * crystal, an RT9013-18 regulator producing the sensor's 1.8 V core rail, and
 * an 8-pin header. Every A121 GPIO and the CTRL pin are tied to ground on the
 * board, so the entire interface is SPI plus ENABLE and INTERRUPT.
 *
 * Header P1 (and the 8-way HC-1.0 connector CN1), from the board schematic:
 *
 *     P1.1  ENABLE      (10k pull-down on the board, so drive it high)
 *     P1.2  INTERRUPT   (sensor output, active high)
 *     P1.3  SPI_MOSI
 *     P1.4  SPI_MISO
 *     P1.5  SPI_CLK
 *     P1.6  SPI_CS
 *     P1.7  GND
 *     P1.8  VIN
 *
 * VIN feeds both the LDO and the sensor's VIO domain. The board ships with
 * R1 = 0R and R2 unpopulated, which is the 3.3 V I/O option: supply 3.3 V on
 * VIN and the SPI lines are 3.3 V logic, directly compatible with the RP2350.
 * (The alternative build, R1 unpopulated and R2 = 0R, bypasses the LDO for a
 * 1.8 V supply and would need level shifting. Check your board before wiring.)
 *
 * Default RP2350 assignment, chosen so all six signals plus a ground land on
 * one contiguous run of the Pico 2 header:
 *
 *     GP16  physical 21   SPI0 RX    <- MISO
 *     GP17  physical 22   GPIO       -> CS
 *     GP18  physical 24   SPI0 SCK   -> CLK
 *     GP19  physical 25   SPI0 TX    -> MOSI
 *     GP20  physical 26   GPIO       -> ENABLE
 *     GP21  physical 27   GPIO       <- INTERRUPT
 *           physical 23   GND
 *           physical 36   3V3(OUT)   -> VIN
 *
 * Chip select is a plain GPIO rather than the SPI block's hardware CSn: the
 * RP2350 SPI peripheral deasserts CSn between frames in some modes, and RSS
 * needs it held low for the whole of each transfer.
 */
#ifndef RP_RADAR_T_RADAR_PINS_H
#define RP_RADAR_T_RADAR_PINS_H

#ifndef TRADAR_SPI_INSTANCE
#define TRADAR_SPI_INSTANCE spi0
#endif

#ifndef TRADAR_PIN_MISO
#define TRADAR_PIN_MISO 16u
#endif

#ifndef TRADAR_PIN_CS
#define TRADAR_PIN_CS 17u
#endif

#ifndef TRADAR_PIN_SCK
#define TRADAR_PIN_SCK 18u
#endif

#ifndef TRADAR_PIN_MOSI
#define TRADAR_PIN_MOSI 19u
#endif

#ifndef TRADAR_PIN_ENABLE
#define TRADAR_PIN_ENABLE 20u
#endif

#ifndef TRADAR_PIN_INTERRUPT
#define TRADAR_PIN_INTERRUPT 21u
#endif

/*
 * Optional GPIO driving a load switch on VIN. Leave undefined when VIN is wired
 * straight to 3V3, which is the usual case; supply on/off then does nothing.
 */
/* #define TRADAR_PIN_SUPPLY 22u */

/**
 * SPI clock. The A121 accepts up to 50 MHz, but this runs over flying leads to
 * a breakout, so the default is deliberately modest. The swing tracker moves
 * 2 kB per frame at 625 Hz, which needs roughly 6 Mbit/s of payload; 24 MHz
 * leaves ample headroom. Drop to 8 MHz if bring-up is unreliable, and keep the
 * ground return short before raising it.
 */
#ifndef TRADAR_SPI_BAUDRATE_HZ
#define TRADAR_SPI_BAUDRATE_HZ (24u * 1000u * 1000u)
#endif

/** Only one sensor on this board. */
#ifndef TRADAR_SENSOR_ID
#define TRADAR_SENSOR_ID 1u
#endif

/**
 * Settling time after ENABLE changes. Acconeer's reference ports allow 2 ms
 * between raising ENABLE and talking to the sensor.
 */
#ifndef TRADAR_ENABLE_SETTLE_MS
#define TRADAR_ENABLE_SETTLE_MS 2u
#endif

/** How long to wait for the data-ready interrupt before giving up. */
#ifndef TRADAR_SENSOR_TIMEOUT_MS
#define TRADAR_SENSOR_TIMEOUT_MS 1000u
#endif

/**
 * Largest single SPI transaction RSS will ask for. It splits longer transfers
 * into several calls, each a complete transaction with its own CS pulse, so a
 * larger value means less per-transaction overhead.
 */
#ifndef TRADAR_MAX_SPI_TRANSFER_SIZE
#define TRADAR_MAX_SPI_TRANSFER_SIZE 4096u
#endif

#endif /* RP_RADAR_T_RADAR_PINS_H */
