# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/).

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

[0.1.0]: https://github.com/projectbin07-stack/ina219-battery-monitor/releases/tag/v0.1.0
