# Wiring the T-RADAR to an RP2350

## What the board actually is

The LilyGO T-RADAR V1.0 carries no microcontroller. Reading its schematic, the
whole board is:

- **U1** — the A121 itself, in its 10x10 BGA
- **X1** — a 24 MHz crystal (CMI-111YLC-24M) on XIN/XOUT
- **U2** — an RT9013-18 LDO producing the 1.8 V rail the sensor's RX, TX and
  digital domains run from
- **P1** — an 8-pin 0.1" header, and **CN1**, an 8-way HC-1.0 connector carrying
  the same signals
- passives

Every A121 GPIO (F1, H1, B10, K5) and the CTRL pin (A9) is tied to ground on the
board, and RESET_IN (J1) is tied to VIN. So the interface is exactly six signals:
SPI, ENABLE and INTERRUPT.

## I/O voltage

VIN feeds both the LDO and the sensor's VIO domain, and two resistor options
select which supply the board expects:

| Build | R1 | R2 | VIN | SPI logic |
|---|---|---|---|---|
| **3.3 V** (as shipped) | 0R | not fitted | 3.3 V | 3.3 V |
| 1.8 V | not fitted | 0R | 1.8 V | 1.8 V |

In the 3.3 V build, R1 ties VIN to the LDO's enable pin so the regulator
generates the 1.8 V core rail, and VIO sits at VIN. That is directly compatible
with the RP2350 at 3.3 V — no level shifting.

**Check your board before wiring.** If it came configured for 1.8 V (R2 fitted,
R1 absent), driving 3.3 V logic at it will damage the sensor.

## Connections

Default pin assignment, chosen so all six signals plus a ground land on one
contiguous run of the Pico 2 header:

| T-RADAR P1 | Signal | RP2350 | Pico 2 pin | Function |
|---|---|---|---|---|
| 1 | ENABLE | GP20 | 26 | GPIO out |
| 2 | INTERRUPT | GP21 | 27 | GPIO in |
| 3 | SPI_MOSI | GP19 | 25 | SPI0 TX |
| 4 | SPI_MISO | GP16 | 21 | SPI0 RX |
| 5 | SPI_CLK | GP18 | 24 | SPI0 SCK |
| 6 | SPI_CS | GP17 | 22 | GPIO out |
| 7 | GND | GND | 23 | |
| 8 | VIN | 3V3(OUT) | 36 | |

To change any of these, either edit `src/board/t_radar_pins.h` or pass overrides
at configure time:

```sh
cmake -S . -B build -DACCONEER_RSS_DIR=... \
      -DRP_RADAR_DEFINES="TRADAR_PIN_ENABLE=14;TRADAR_PIN_INTERRUPT=15"
```

If you move the SPI pins, keep them on a valid SPI0 (or SPI1) function set for
the RP2350 — the datasheet's GPIO function table lists which pins can carry
which SPI signal. CS is a plain GPIO and can be any pin.

## Two details that matter

**ENABLE has a 10k pull-down on the board.** The sensor stays off until the host
drives the line high. If ENABLE is left unconnected, nothing works and the
symptom is `acc_sensor_create failed`.

**Chip select is driven as a GPIO, not the SPI block's hardware CSn.** The
RP2350's SPI peripheral can deassert its own CSn between FIFO frames, and RSS
needs CS held low for the whole of each transfer. This is why `TRADAR_PIN_CS` is
configured as `GPIO_FUNC_SIO`.

## Current

The A121 draws around 85 mA while actively measuring. Both demos run the sensor
continuously with the idle state set to READY, so budget for that continuously,
not as a duty-cycled average. The Pico 2's 3V3 regulator supplies it comfortably
when the board itself is USB powered.

## SPI clock

The default is 24 MHz. The A121 accepts up to 50 MHz, but this is running over
flying leads to a breakout, so start conservatively:

```sh
-DRP_RADAR_DEFINES="TRADAR_SPI_BAUDRATE_HZ=8000000"
```

The swing tracker moves 2 kB per frame at 625 Hz, about 10 Mbit/s of payload
including overhead, so 8 MHz is too slow for it and will show up as delayed
frames in the `S,` statistics line. 24 MHz leaves comfortable headroom. Before
raising it further, shorten the ground return — a long, thin ground wire is what
usually limits this, not the clock rate.

## Sanity check

Flash `sparse_iq_example.uf2` and open the USB serial port at any baud rate.
Expected: a configuration summary, then twenty frames of complex samples and
per-point amplitudes. Amplitudes should be large where something solid sits and
should visibly change when you wave a hand through the beam.

| Symptom | Usual cause |
|---|---|
| `acc_sensor_create failed` | ENABLE not connected or not driven; no ground between boards; MISO/MOSI swapped |
| `timed out waiting for the sensor interrupt` | INTERRUPT not connected, or wired to a pin that is not `TRADAR_PIN_INTERRUPT` |
| Amplitudes all zero | MISO not connected |
| Amplitudes present but never change | MISO shorted or reading a stuck bus; try a lower SPI clock |
| Values wander with no target | SPI clock too high for the wiring |
