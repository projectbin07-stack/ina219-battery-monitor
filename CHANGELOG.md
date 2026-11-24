# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

## [0.2.0] - 2026-11-24

Nothing in this release was run on hardware; the fixes are covered by native
unit tests and the firmware builds for all three boards in CI.

### Fixed

- The largest measurable current reported in the `CAL` reply (`max_mA`) and in
  the README table was `32767 * Current_LSB` and ignored the PGA range the code
  selects. It is now `min(32767 * LSB, PGA full scale / shunt)`. Examples:
  0.1 ohm / 0.4 A reported 655.3 mA, true limit 400.0 mA; 0.075 ohm / 2 A
  reported 3277.5 mA, true limit 2133.3 mA; 0.1 ohm / 3.2 A reported 3276.7 mA,
  true limit 3200.0 mA. The reply format is unchanged.
- The state-of-charge voltage blend was 0.02 per sample, so its speed depended
  on the sample rate: 63 % convergence to the voltage curve took 1.0 s at
  `RATE 20` and 24.7 s at 500 ms, overriding coulomb counting on any resting
  node. It is now a first-order filter in time (`rest_tau_s` = 600 s) that
  starts only after `rest_min_s` = 60 s of continuous rest; 63 % convergence
  takes 660 s (including the rest delay) at 20, 100, 500 and 2000 ms alike.
- Samples with the INA219 OVF flag were skipped, but the next good sample then
  integrated across the whole overflow span using the pre-overflow current
  (100 mA, 2 A, 2 A, 100 mA at 500 ms gave 55 uAh; true 583 uAh), while the
  README said they were not integrated. The span is now a counted gap and the
  integrator is re-seeded (13.9 uAh for that sequence: the charge drawn during
  the overflow is missing rather than wrongly estimated).

### Changed

- `powercore` API: `SocConfig::rest_blend` is replaced by `rest_min_s` and
  `rest_tau_s`; `SocEstimator::update()` takes the interval `dt_ms`; new
  `SocEstimator::breakRest()`, `CoulombCounter::markGap()`,
  `CoulombCounter::lastInterval_ms()` and
  `ina219::maxMeasurableCurrent_mA()`. Hence the minor version bump.
- README: documents the OCV table's disagreement with solar-node-sizer's
  (up to 32 points); neither table is changed.

## [0.1.0] - 2026-11-18

First release. Built and unit-tested in CI; not yet validated on physical
hardware (see "Hardware validation status" in the README).

### Added

- `lib/powercore`: hardware-independent C++ library
  - INA219 register maths: configuration word, calibration from shunt
    resistance and maximum expected current, PGA range selection, raw register
    to bus voltage / shunt voltage / current / power conversions, CNVR and OVF
    flag decoding.
  - Coulomb counter with trapezoidal integration in 64-bit integers,
    `millis()` wraparound handling and gap detection.
  - State-of-charge estimator: voltage lookup curve with interpolation blended
    with coulomb counting; configurable curve, approximate 1S Li-ion table
    included.
  - Moving-average and exponential-moving-average filters.
  - Low-voltage cutoff state machine with hysteresis and debounce.
  - Telemetry frame encoder/decoder (`$BM1,...*HH`, CRC-8 polynomial 0x07).
  - Serial command parser (`RESET`, `RATE <ms>`, `CAL <shunt_mohm> <max_A>`).
- `lib/ina219`: INA219 driver on an abstract I2C bus interface.
- Arduino firmware for ESP32 DevKit and Arduino Nano (ATmega328P) with SSD1306
  OLED screen and serial telemetry; serial-only build for the Nano.
- `tools/logger.py`: host logger that validates frames, writes CSV and replays
  raw logs.
- Native unit tests (Unity) for all library code, including the driver against
  a fake I2C bus; pytest suite for the logger.
- CI: native tests, firmware builds for all board environments, Python lint,
  type check and tests.

[0.2.0]: https://github.com/projectbin07-stack/ina219-battery-monitor/releases/tag/v0.2.0
[0.1.0]: https://github.com/projectbin07-stack/ina219-battery-monitor/releases/tag/v0.1.0
