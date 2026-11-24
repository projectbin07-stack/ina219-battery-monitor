# ina219-battery-monitor

[![CI](https://github.com/projectbin07-stack/ina219-battery-monitor/actions/workflows/ci.yml/badge.svg)](https://github.com/projectbin07-stack/ina219-battery-monitor/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

PlatformIO firmware that turns an INA219 breakout, an SSD1306 OLED and an ESP32
or Arduino Nano into a battery / power telemetry node, plus a small Python
logger for the host.

> **Status:** the code builds for every supported board and its logic is
> unit-tested in CI, but this release has **not yet been run on physical
> hardware by its author**. See [Hardware validation status](#hardware-validation-status).

## Why it exists

Most INA219 sketches print a voltage and a current and stop there. Getting from
that to something you can leave running on a battery or a small solar setup
takes a number of fiddly pieces: choosing a calibration value for a shunt that
is not the stock 0.1 ohm, integrating charge without drifting or tripping over
the `millis()` rollover, deciding when the battery is "empty" without the load
chattering on and off, and getting the data off the device in a form that
survives a noisy serial link.

This project implements those pieces as a hardware-independent library with
host-side unit tests, and keeps the Arduino-specific part down to one file.

## Features

- **INA219 register maths** from the TI datasheet: configuration word,
  calibration value from shunt resistance and maximum expected current,
  automatic PGA range selection, conversion of every result register, and the
  conversion-ready (CNVR) and math-overflow (OVF) flags.
- **Coulomb counter**: mAh and mWh by trapezoidal integration in 64-bit
  integers; handles the 49.7-day `millis()` wraparound and skips gaps,
  including the span around samples with the OVF flag set.
- **State of charge**: voltage lookup curve with linear interpolation, blended
  with coulomb counting through a first-order filter defined in time (60 s
  minimum rest, 600 s time constant), so the result does not depend on the
  sample rate. An approximate 1S Li-ion table is included and the curve is
  replaceable.
- **Filters**: moving average and exponential moving average.
- **Low-voltage cutoff** state machine with hysteresis and debounce, with an
  optional GPIO output for a load switch.
- **Telemetry**: one CSV line per sample, integers only, protected by CRC-8,
  with an encoder (firmware) and decoders (C++ and Python).
- **Serial commands**: `RESET`, `RATE <ms>`, `CAL <shunt_mohm> <max_A>`.
- **OLED screen** (128x64 SSD1306): voltage, current, power, mAh and a
  state-of-charge bar.
- **Host logger** (`tools/logger.py`): validates frames, writes CSV, counts
  dropped frames, and replays recorded logs.
- **Driver testable without hardware**: the INA219 driver talks to an abstract
  I2C interface and is tested against a register-level fake of the chip.

## Supported hardware

| Part | Notes |
| --- | --- |
| ESP32 DevKit (`esp32dev`) | OLED + serial |
| Arduino Nano, ATmega328P (`nanoatmega328`) | OLED + serial; experimental, RAM is marginal, see [Nano memory](#nano-memory) |
| Arduino Nano, ATmega328P (`nanoatmega328_serial`) | serial telemetry only, no display code |
| INA219 breakout | any board with the I2C pins and the shunt terminals broken out; 0.1 ohm shunt assumed by default |
| SSD1306 OLED, 128x64, I2C | optional |

"Supported" here means *the firmware compiles for it in CI*, nothing more yet.

## Wiring

All three devices share one I2C bus.

| Signal | ESP32 DevKit | Arduino Nano | INA219 breakout | SSD1306 OLED |
| --- | --- | --- | --- | --- |
| Supply | 3V3 | 5V | VCC | VCC |
| Ground | GND | GND | GND | GND |
| I2C data | GPIO 21 | A4 | SDA | SDA |
| I2C clock | GPIO 22 | A5 | SCL | SCL |
| Load enable (optional) | any free GPIO | any free pin | - | - |

| Device | I2C address | How it is selected |
| --- | --- | --- |
| INA219 | `0x40` | A0 and A1 pads open / tied to GND (breakout default) |
| SSD1306 | `0x3C` | module default (some modules use `0x3D`) |

Power path (high-side sensing):

```
battery (+) ──── VIN+ [ INA219 shunt ] VIN− ──── load (+)
battery (−) ──────────────────────────────────── load (−) ──── MCU GND
```

- Current flowing from `VIN+` to `VIN−` is reported as **positive = discharge**.
  If your wiring is the other way round, build with `-D BM_INVERT_CURRENT=1`.
- The INA219 measures bus voltage at `VIN−` relative to its own `GND`, so the
  battery negative **must** share ground with the INA219 and the MCU.
- The INA219's inputs are rated for 0 to 26 V. Do not connect a higher voltage.
- On the ESP32, power the INA219 and the OLED from 3.3 V so the I2C lines idle
  at 3.3 V. Most breakouts carry their own pull-up resistors; if yours do not,
  add 4.7 kOhm pull-ups on SDA and SCL.
- ESP32 I2C pins can be changed with `BM_I2C_SDA_PIN` / `BM_I2C_SCL_PIN`. The
  Nano's are fixed.

## Shunt and calibration

The INA219 measures the voltage across a shunt resistor and multiplies it by a
calibration factor to produce current and power. From the datasheet:

```
Current_LSB = Max_Expected_Current / 32767        (smallest usable LSB)
Cal         = trunc(0.04096 / (Current_LSB * R_shunt))
Power_LSB   = 20 * Current_LSB
```

`computeCalibration(shunt, max_current)` does the following, in integer
arithmetic so that the result is the same on every platform:

1. Rejects the pair if `R_shunt * I_max` exceeds 320 mV, the largest shunt
   voltage the chip can measure.
2. Takes the minimum LSB from the formula above and rounds it **up** to the
   1-2-5 series (10, 20, 50, 100 uA ...), which gives round numbers per count.
3. Raises the LSB further if `Cal` would not fit the 16-bit register (this
   happens with very small shunts and small currents).
4. Truncates `Cal`, clears bit 0 (not writable in the chip) and then
   **recomputes the LSB that is actually in effect** from the truncated value,
   so the conversion does not carry the truncation error.
5. Picks the smallest PGA range (40 / 80 / 160 / 320 mV) that covers
   `R_shunt * I_max`, for the best shunt-voltage resolution.
6. Reports the largest measurable current as
   `min(32767 * Current_LSB, PGA_full_scale / R_shunt)`. The first term is the
   range of the current register; the second is where the shunt ADC clips. With
   the PGA chosen in step 5 the second term is usually the limit, and it can be
   as low as the `I_max` you asked for, so leave some margin when choosing it.

Worked examples (these are asserted in `test/test_ina219_math`):

| Shunt | Max current | Current LSB | Cal register | PGA range | Largest measurable current |
| --- | --- | --- | --- | --- | --- |
| 0.1 ohm | 3.2 A | 100 uA | 4096 | 320 mV | 3.200 A |
| 0.1 ohm | 1 A | 50 uA | 8192 | 160 mV | 1.600 A |
| 0.1 ohm | 0.4 A | 20 uA | 20480 | 40 mV | 0.400 A |
| 0.5 ohm | 0.6 A | 20 uA | 4096 | 320 mV | 0.640 A (datasheet example) |
| 0.075 ohm | 2 A | 100.024 uA (after truncation) | 5460 | 160 mV | 2.133 A |

Up to 0.1.0 this column, and the `max_mA` in the `CAL` reply, showed
`32767 * Current_LSB` only (3.277, 1.638, 0.655, 0.655 and 3.277 A for the
rows above), which overstated the range by up to 64 % because the chip's shunt
ADC clips first. Whether the INA219 also raises OVF when the shunt ADC clips
has not been checked on a device.

Defaults live in [`include/config.h`](include/config.h): `BM_SHUNT_UOHM`
(100 000 micro-ohm) and `BM_MAX_CURRENT_MA` (3200). They can be changed at run
time with the `CAL` command.

The calibration is only as good as your knowledge of the shunt. Breakout
shunts are typically 1 % parts, and the copper and solder in series with them
add to the resistance; if you need accurate current readings, measure a known
current and adjust the shunt value you give the firmware.

## Build and flash

Install [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html)
(`pip install platformio`), then:

```sh
git clone https://github.com/projectbin07-stack/ina219-battery-monitor.git
cd ina219-battery-monitor

# ESP32 DevKit
pio run -e esp32dev -t upload

# Arduino Nano with OLED
pio run -e nanoatmega328 -t upload

# Arduino Nano, serial only
pio run -e nanoatmega328_serial -t upload

# Watch the output (115200 baud)
pio device monitor
```

Add `--upload-port COM5` (or `/dev/ttyUSB0`) if PlatformIO does not find the
board. The `nanoatmega328` board definition is for the old bootloader (57600
baud); for a Nano with the new bootloader change `board` to `nanoatmega328new`
in `platformio.ini`.

The first build downloads the toolchains and the pinned display libraries
(Adafruit SSD1306 2.5.13, Adafruit GFX 1.11.11, Adafruit BusIO 1.16.2).

### Configuration

Everything is a `#define` in [`include/config.h`](include/config.h) and can be
overridden with `-D` flags in `platformio.ini`:

| Setting | Default | Meaning |
| --- | --- | --- |
| `BM_SHUNT_UOHM` | `100000` | shunt resistance in micro-ohms |
| `BM_MAX_CURRENT_MA` | `3200` | largest expected current, mA |
| `BM_BUS_RANGE_32V` | `0` | 0 = 16 V bus range, 1 = 32 V |
| `BM_ADC_MODE` | `Avg16` | INA219 ADC resolution / averaging (about 17 ms per sample pair) |
| `BM_INVERT_CURRENT` | `0` | flip the sign of the current |
| `BM_SAMPLE_PERIOD_MS` | `500` | sample and telemetry period |
| `BM_BATTERY_CAPACITY_MAH` | `2000` | used by the state-of-charge estimate |
| `BM_CUTOFF_MV` / `BM_CUTOFF_RECOVER_MV` | `3000` / `3300` | low-voltage cutoff thresholds |
| `BM_CUTOFF_SAMPLES` | `4` | consecutive samples needed to trip or release |
| `BM_LOAD_ENABLE_PIN` | `-1` | GPIO driven HIGH while the load may run; -1 = unused |
| `BM_INA219_ADDRESS` / `BM_OLED_ADDRESS` | `0x40` / `0x3C` | I2C addresses |
| `BM_ENABLE_OLED` | `1` | set per environment in `platformio.ini` |

### Nano memory

Sizes reported by PlatformIO in CI for v0.1.0:

| Environment | Flash | Static RAM |
| --- | --- | --- |
| `esp32dev` | 306 065 of 1 310 720 bytes (23.4 %) | 22 200 of 327 680 bytes (6.8 %) |
| `nanoatmega328` (OLED) | 25 538 of 30 720 bytes (83.1 %) | 802 of 2 048 bytes (39.2 %) |
| `nanoatmega328_serial` | 15 334 of 30 720 bytes (49.9 %) | 660 of 2 048 bytes (32.2 %) |

These are build-time numbers from the first green CI run of this code
([run 44262392865](https://github.com/projectbin07-stack/ina219-battery-monitor/actions/runs/44262392865)),
not measurements on a device.

The OLED build **fits the Nano's flash**, but RAM is marginal: the "static RAM"
column does not include the SSD1306 frame buffer, which the display library
allocates from the heap at start-up (128 x 64 / 8 = 1 024 bytes). That leaves
roughly 2 048 - 802 - 1 024 = 222 bytes for the stack and everything else,
and the sampling path alone puts a 104-byte frame buffer on the stack. Whether
that is enough has not been checked on a real board. If the allocation fails
the firmware prints `#WARN OLED init failed, continuing without display` and
carries on serial-only, but a stack overflow would not announce itself. Until
this has been tested on hardware, treat `nanoatmega328` as experimental and
prefer **`nanoatmega328_serial`** on the Nano (about 1 388 bytes free) or use
an ESP32 when you want the display.

## Quick start

1. Wire the parts as shown above and flash a board.
2. Open the serial monitor at 115200 baud. The firmware prints a banner and
   then one frame per sample. The lines below show the *format* (they are the
   fixture file `tools/tests/data/sample_session.log`, written by hand, not a
   capture from a device):

   ```
   #ina219-battery-monitor 0.1.0
   $BM1,0,512,4012,152300,611000,0,0,790,2*F2
   $BM1,1,1012,4010,151900,609000,21,84,790,0*2C
   $BM1,2,1512,4011,152100,610000,42,169,790,0*43
   ```

3. Log to CSV from the host:

   ```sh
   pip install -r tools/requirements.txt
   python tools/logger.py record --port COM5 --out session.csv --raw session.log
   ```

4. Decode a raw log again later. This is the real output of the command on the
   fixture file, which deliberately contains a truncated line, a corrupted
   line, a missing sequence number and a device restart:

   ```
   $ python tools/logger.py replay tools/tests/data/sample_session.log
   #ina219-battery-monitor 0.1.0
   rejected line (missing '*' checksum marker): '$BM1,5,3012,4009,15180'
   #OK RATE 500
   rejected line (checksum mismatch: got 4E, computed 4D): '$BM1,4,2512,4009,151800,608000,84,338,789,0*4E'
   #ina219-battery-monitor 0.1.0
   5 frames, 2 bad lines, 3 comments, 1 dropped, 1 restarts
        0.512s   4.012 V    152.300 mA    611.000 mW      0.000 mAh   79.0 %  not-ready
        1.012s   4.010 V    151.900 mA    609.000 mW      0.021 mAh   79.0 %  -
        1.512s   4.011 V    152.100 mA    610.000 mW      0.042 mAh   79.0 %  -
        2.512s   4.009 V    151.800 mA    608.000 mW      0.084 mAh   78.9 %  -
        0.498s   4.011 V      0.000 mA      0.000 mW      0.000 mAh   78.9 %  not-ready
   ```

   (Status lines go to stderr and the table to stdout, so they interleave
   differently when redirected.)

## Telemetry frame format

One frame per sample, terminated by CR LF:

```
$BM1,<seq>,<t_ms>,<bus_mV>,<current_uA>,<power_uW>,<charge_uAh>,<energy_uWh>,<soc_permille>,<flags>*<HH>
```

| # | Field | Type | Unit / meaning |
| --- | --- | --- | --- |
| - | `BM1` | tag | frame type and version |
| 1 | `seq` | uint32 | frame counter, +1 per frame, restarts at 0 on reset |
| 2 | `t_ms` | uint32 | device uptime in ms (wraps after about 49.7 days) |
| 3 | `bus_mV` | int32 | bus voltage, mV (4 mV resolution) |
| 4 | `current_uA` | int32 | current, uA; positive = discharge |
| 5 | `power_uW` | int32 | power, uW; sign follows the current |
| 6 | `charge_uAh` | int32 | net charge drawn since reset, uAh |
| 7 | `energy_uWh` | int32 | net energy drawn since reset, uWh |
| 8 | `soc_permille` | 0..1000 | state of charge in 0.1 % steps |
| 9 | `flags` | 0..255 | bit field, see below |
| - | `HH` | 2 hex digits | CRC-8 of every character between `$` and `*` |

All fields are decimal integers; negative values carry a leading `-`. There are
no floats on the wire, so the encoder output is byte-identical on AVR, ESP32
and the host. The longest possible frame is 99 characters.

**Checksum:** CRC-8, polynomial `0x07` (x^8 + x^2 + x + 1), initial value
`0x00`, MSB first, no reflection, no final XOR (the parameters often listed as
"CRC-8/SMBUS"). `CRC("123456789") = 0xF4`. Upper-case hex is emitted;
decoders accept either case.

**Flags:**

| Bit | Value | Name | Meaning |
| --- | --- | --- | --- |
| 0 | 1 | overflow | INA219 OVF was set: current and power are not valid. The span from the last good sample to the next good one is not integrated (counted as a gap), so `charge_uAh` and `energy_uWh` under-read by whatever was drawn during it |
| 1 | 2 | not-ready | INA219 CNVR was not set: no new conversion since the previous read |
| 2 | 4 | sensor-error | I2C transaction failed: measurement fields are zero |
| 3 | 8 | cutoff | low-voltage cutoff is tripped |

Lines that start with `#` are human-readable status messages (`#OK ...`,
`#ERR ...`, `#WARN ...`) and are not frames.

Example, decoded: `$BM1,42,21000,3876,100000,386000,583,2261,512,0*D1` is frame
42 at 21.000 s: 3.876 V, 100.000 mA, 386.000 mW, 0.583 mAh and 2.261 mWh drawn
so far, 51.2 % state of charge, no flags.

## Serial commands

Send a line terminated by CR, LF or both. Keywords are case-insensitive.

| Command | Effect | Reply |
| --- | --- | --- |
| `RESET` | clear the mAh / mWh counters, re-seed the state of charge from the voltage, release the cutoff | `#OK RESET` |
| `RATE <ms>` | sample period, 20 to 60000 ms | `#OK RATE <ms>` |
| `CAL <shunt_mohm> <max_A>` | shunt resistance in milliohms and maximum expected current in amps, up to three decimals each, e.g. `CAL 100 3.2` or `CAL 2.5 20` | `#OK CAL reg=<cal> lsb_uA=<lsb> max_mA=<range>`, where `<range>` is the largest measurable current, `min(32767 * LSB, PGA full scale / shunt)` |

Anything else gets `#ERR expected RESET | RATE <ms> | CAL <shunt_mohm> <max_A>`.
A `CAL` pair the chip cannot handle is rejected with `#ERR CAL status=<n>` and
the previous calibration stays in effect. Settings changed by command are held
in RAM only and revert to the `config.h` values after a reset.

## Host logger

```
$ python tools/logger.py --help
usage: logger.py [-h] {record,replay} ...

Log and replay ina219-battery-monitor telemetry frames.

positional arguments:
  {record,replay}
    record         read frames from a serial port and write CSV
    replay         decode a raw log recorded earlier

options:
  -h, --help       show this help message and exit
```

- `record --port P --out FILE.csv [--baud N] [--raw FILE.log] [--duration S] [--quiet]`
  reads the serial port until Ctrl+C (or `--duration`), writes one CSV row per
  valid frame with a UTC host timestamp, and optionally saves the raw lines.
- `replay FILE.log [--out FILE.csv] [--realtime] [--speed X] [--quiet]`
  re-decodes a raw log; with `--realtime` it paces the output by the device
  timestamps.

CSV columns: `host_time, seq, t_ms, bus_v, current_ma, power_mw, charge_mah,
energy_mwh, soc_pct, flags`. The summary line reports rejected lines, frames
missing according to the sequence counter, and device restarts.

`record` needs `pyserial`; `replay` and importing the module need only the
standard library (Python 3.10+).

## Architecture

```
lib/powercore/        hardware-independent, C headers only, unit-tested on the host
  ina219_math         register addresses, config word, calibration, raw -> units
  coulomb_counter     trapezoidal mAh / mWh integration, wraparound + gap handling
  soc                 voltage curve lookup blended with coulomb counting
  filters             MovingAverage<N>, Ema
  cutoff              low-voltage cutoff state machine
  telemetry           CRC-8, frame encoder and decoder
  command             RESET / RATE / CAL parser
lib/ina219/           driver: I2CBus interface + Ina219 class (uses powercore maths)
src/main.cpp          Arduino glue: sampling loop, OLED drawing, serial I/O
src/two_wire_bus.h    I2CBus adapter for Arduino TwoWire
include/config.h      build-time settings
test/                 native Unity tests, one folder per module
tools/logger.py       host logger; tools/tests/ its pytest suite
```

Data flow per sample: `Ina219::read()` → sign/overflow handling →
`CoulombCounter::update()` → `SocEstimator::update()` →
`LowVoltageCutoff::update()` → `encodeFrame()` → `Serial`. The OLED is redrawn
on its own timer from the most recent values (voltage and current pass through
an EMA for the display only; telemetry is unfiltered).

Design notes:

- **No floats on the wire, integers in the accumulators.** On AVR `double` is
  32 bits, so a float running sum would stop registering small increments
  after a while. Charge and energy are accumulated as 64-bit integers
  (uA·ms and uW·ms).
- **Wraparound** is handled by unsigned subtraction of timestamps. An interval
  longer than the configured maximum gap (four sample periods, at least 2 s) is
  not integrated; that also covers a clock that appears to step backwards.
  A sample with OVF set is also treated as a gap: up to 0.1.0 the next good
  sample bridged the whole overflow span with the last good current (100 mA,
  2 A, 2 A, 100 mA at 500 ms integrated 55 uAh where the true current gives
  583 uAh); now the span is skipped and counted (13.9 uAh, the first interval
  only), so the error is a visible gap rather than a hidden wrong value.
- **State of charge** uses the voltage curve only after the current has stayed
  at or below a rest threshold (20 mA) for a minimum rest time (60 s), because
  under load, and for a while after it, the voltage differs from the
  open-circuit voltage. While resting, the estimate moves towards the curve
  value as a first-order filter with time constant `rest_tau_s` (600 s): each
  sample applies the fraction `dt / (tau + dt)`, so 63 % of the gap is closed
  600 s after the minimum rest whether the firmware samples every 20 ms or
  every 2 s. Up to 0.1.0 the blend was a fixed 0.02 per sample, which closed
  63 % of the gap in 1.0 s at `RATE 20` and 24.7 s at the default 500 ms, so on
  a sleeping node at a fast rate the voltage curve overrode coulomb counting
  almost immediately. The 60 s / 600 s values are a judgement call, not fitted
  to a measured cell; change them in `liIon1SConfig()` (`soc.cpp`).
- **The calibration register is volatile.** The firmware checks it every 16
  samples and reprograms the chip if it has been lost (for example after a
  brown-out on the sensor's supply).

## Testing

```sh
pio test -e native          # C++ unit tests on the host (Unity)
pip install -r tools/requirements.txt -r tools/requirements-dev.txt
ruff check tools && ruff format --check tools && mypy && pytest
```

What the tests cover:

- INA219 maths against values worked out by hand from the datasheet formulas,
  including the datasheet's own examples (0x5D98 = 11.98 V, Cal 0x5000 with a
  2 mOhm shunt, current 0x2710 = 10 A, power 0x1766 = 119.8 W).
- Coulomb counter: exact 1 A x 1 h = 1000 mAh, linear ramps, sign changes,
  `millis()` wraparound, gaps, backwards time, saturation.
- State of charge: interpolation points, blending arithmetic, clamping, the
  same 63 % time (660 s including the minimum rest, within 1 %) at 20, 100,
  500 and 2000 ms sample periods, no blend before the minimum rest, and the
  rest timer restarting under load.
- Cutoff: trip / release sequences, no chatter inside the hysteresis band.
- Telemetry: CRC vectors, encoder output, decoder rejection of every
  single-bit corruption of a sample frame, range and structure errors.
- Driver: exact bus traffic of `begin()` and `read()` against a fake INA219,
  byte order, failure at every transaction, calibration-loss detection.
- Logger: the same frame fixtures as the firmware tests, plus a sample session
  file with bad lines, a dropped frame and a restart.

CI ([workflow](.github/workflows/ci.yml)) runs the native tests, builds the
three board environments, and runs ruff, mypy and pytest for the logger on
Linux and Windows with Python 3.10 and 3.13.

## Hardware validation status

Stated plainly:

- **Done:** every board environment compiles in CI; all library logic and the
  driver's bus traffic are unit-tested on a host machine against hand-derived
  values and a software model of the INA219.
- **Not done:** this release has **not been validated on physical hardware by
  its author**. It has not been flashed to an ESP32 or a Nano, has not talked
  to a real INA219 or SSD1306, and has not measured a real battery.
- Consequently there are **no accuracy figures**. Nothing in this repository
  is a measured result; the sample log in `tools/tests/data/` is hand-written
  to exercise the decoder.
- Things that can only be confirmed on a device and are therefore unverified:
  I2C timing with real parts, the OLED layout as it actually renders, free RAM
  on the Nano with the display enabled, the serial command handling end to end,
  and the behaviour of the load-enable output.

If you run it on hardware, an issue describing what you saw (working or not)
is very welcome and this section will be updated accordingly.

## Known limitations

- Not hardware-validated yet (above).
- The Li-ion voltage curve is approximate and generic, and it disagrees with
  the curve in the companion `solar-node-sizer` by up to 32 points (3.69 V:
  10 % here, 42 % there). Neither table is from a cited measurement; which is
  closer for a given cell is unknown. Real cells differ with
  chemistry variant, age, temperature and load. The state of charge is an
  indication, not a fuel gauge. The first estimate after power-up or `RESET`
  comes from the voltage at that moment and is off if the battery is under
  load then.
- Only single-curve chemistries are provided (1S Li-ion table). Other
  chemistries need their own table (`SocPoint` array) and series packs need
  `series_cells` set in code; neither is exposed as a build flag yet.
- Coulomb counting has no coulombic-efficiency or self-discharge model and its
  error accumulates between resting periods.
- Runtime settings (`RATE`, `CAL`) and the accumulated mAh / mWh are not saved
  across resets (no EEPROM / NVS use).
- The INA219's bus-voltage resolution is 4 mV and its current resolution
  depends on the shunt and calibration; offsets near zero current are not
  compensated.
- The low-voltage cutoff is a convenience output, not a battery protection
  circuit. Use a proper protection IC with lithium cells.
- The serial command channel has no authentication or checksum.
- The firmware samples by polling in `loop()`; redrawing the OLED over I2C
  takes tens of milliseconds, so very short `RATE` values will jitter on the
  OLED builds.
- On the Nano the OLED build leaves only about 222 bytes of RAM for the stack
  (see [Nano memory](#nano-memory)); it is unverified and may be unstable.
- One INA219 per node.

## Roadmap

- Validate on an ESP32 DevKit and a Nano with a real INA219 and OLED, and
  record what was measured and how.
- Persist settings and counters (EEPROM on AVR, NVS on ESP32).
- Selectable chemistry tables (LiFePO4, lead-acid) and series cell count from
  `config.h`.
- Shunt offset / gain trim command based on a reference measurement.
- Optional Wi-Fi / MQTT output on the ESP32.
- Plotting helper for the logger's CSV output.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Security reports: [SECURITY.md](SECURITY.md).

## License

[MIT](LICENSE) © 2026 Navaneeth KG
