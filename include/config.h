// Build-time settings for the battery monitor firmware.
//
// Every value can be overridden from platformio.ini, e.g.
//   build_flags = -D BM_SHUNT_UOHM=50000UL -D BM_BATTERY_CAPACITY_MAH=3000
// Shunt, maximum current and sample period can also be changed at run time
// with the CAL and RATE serial commands (not persisted across resets).
#pragma once

#define BM_FIRMWARE_VERSION "0.1.0"

// ---- Serial ---------------------------------------------------------------
#ifndef BM_SERIAL_BAUD
#define BM_SERIAL_BAUD 115200UL
#endif

// ---- I2C ------------------------------------------------------------------
// ESP32 only; the Nano's I2C pins are fixed (A4 = SDA, A5 = SCL).
#ifndef BM_I2C_SDA_PIN
#define BM_I2C_SDA_PIN 21
#endif
#ifndef BM_I2C_SCL_PIN
#define BM_I2C_SCL_PIN 22
#endif

// ---- INA219 ---------------------------------------------------------------
#ifndef BM_INA219_ADDRESS
#define BM_INA219_ADDRESS 0x40  // A0 = A1 = GND
#endif
// Shunt resistance in micro-ohms. Common breakout boards carry 0.1 ohm (R100).
#ifndef BM_SHUNT_UOHM
#define BM_SHUNT_UOHM 100000UL
#endif
// Largest current you expect, in mA. Sets the current LSB and the PGA range.
#ifndef BM_MAX_CURRENT_MA
#define BM_MAX_CURRENT_MA 3200UL
#endif
// 1 = 32 V bus range, 0 = 16 V bus range.
#ifndef BM_BUS_RANGE_32V
#define BM_BUS_RANGE_32V 0
#endif
// ADC mode for both channels (powercore::ina219::AdcMode). Avg16 takes
// 8.51 ms per channel, about 17 ms for a shunt + bus pair.
#ifndef BM_ADC_MODE
#define BM_ADC_MODE powercore::ina219::AdcMode::Avg16
#endif
// Set to 1 if the shunt is wired so that discharge reads negative.
#ifndef BM_INVERT_CURRENT
#define BM_INVERT_CURRENT 0
#endif

// ---- Sampling -------------------------------------------------------------
#ifndef BM_SAMPLE_PERIOD_MS
#define BM_SAMPLE_PERIOD_MS 500UL
#endif

// ---- Battery --------------------------------------------------------------
#ifndef BM_BATTERY_CAPACITY_MAH
#define BM_BATTERY_CAPACITY_MAH 2000.0f
#endif
// Low-voltage cutoff with hysteresis (pack voltage, mV).
#ifndef BM_CUTOFF_MV
#define BM_CUTOFF_MV 3000L
#endif
#ifndef BM_CUTOFF_RECOVER_MV
#define BM_CUTOFF_RECOVER_MV 3300L
#endif
// Consecutive samples needed to trip / release the cutoff.
#ifndef BM_CUTOFF_SAMPLES
#define BM_CUTOFF_SAMPLES 4
#endif
// GPIO driven HIGH while the load may run and LOW when the cutoff has tripped
// (for a load switch / MOSFET driver). -1 disables the output.
#ifndef BM_LOAD_ENABLE_PIN
#define BM_LOAD_ENABLE_PIN -1
#endif

// ---- OLED -----------------------------------------------------------------
#ifndef BM_ENABLE_OLED
#define BM_ENABLE_OLED 1
#endif
#ifndef BM_OLED_ADDRESS
#define BM_OLED_ADDRESS 0x3C
#endif
#ifndef BM_OLED_WIDTH
#define BM_OLED_WIDTH 128
#endif
#ifndef BM_OLED_HEIGHT
#define BM_OLED_HEIGHT 64
#endif
#ifndef BM_DISPLAY_PERIOD_MS
#define BM_DISPLAY_PERIOD_MS 500UL
#endif
// Smoothing of the displayed voltage/current only (telemetry is unfiltered).
#ifndef BM_DISPLAY_EMA_ALPHA
#define BM_DISPLAY_EMA_ALPHA 0.3f
#endif

// ---- Command input --------------------------------------------------------
#ifndef BM_COMMAND_BUFFER
#define BM_COMMAND_BUFFER 32
#endif
