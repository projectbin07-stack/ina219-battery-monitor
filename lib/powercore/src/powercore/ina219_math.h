// INA219 register maths, following the TI INA219 datasheet (SBOS448).
//
// Everything here is plain integer/float arithmetic with no I/O, so it runs
// unchanged on an AVR, an ESP32 and a desktop host (where it is unit-tested).
// Only C headers are used because avr-gcc ships without the C++ standard
// library.
#pragma once

#include <stdint.h>

namespace powercore {
namespace ina219 {

// Register addresses (datasheet table 2).
const uint8_t kRegConfig = 0x00;
const uint8_t kRegShuntVoltage = 0x01;
const uint8_t kRegBusVoltage = 0x02;
const uint8_t kRegPower = 0x03;
const uint8_t kRegCurrent = 0x04;
const uint8_t kRegCalibration = 0x05;

const uint16_t kConfigReset = 0x8000;    // RST bit
const uint16_t kConfigPowerOn = 0x399F;  // register value after reset

const uint16_t kBusFlagOverflow = 0x0001;         // OVF, bus voltage register bit 0
const uint16_t kBusFlagConversionReady = 0x0002;  // CNVR, bus voltage register bit 1

enum class BusRange : uint8_t { V16 = 0, V32 = 1 };

// PGA setting = full-scale shunt voltage range.
enum class PgaGain : uint8_t {
  Range40mV = 0,   // gain /1
  Range80mV = 1,   // gain /2
  Range160mV = 2,  // gain /4
  Range320mV = 3,  // gain /8
};

// BADC / SADC field values (datasheet table 5).
enum class AdcMode : uint8_t {
  Bits9 = 0x0,
  Bits10 = 0x1,
  Bits11 = 0x2,
  Bits12 = 0x3,
  Avg2 = 0x9,
  Avg4 = 0xA,
  Avg8 = 0xB,
  Avg16 = 0xC,
  Avg32 = 0xD,
  Avg64 = 0xE,
  Avg128 = 0xF,
};

enum class OperatingMode : uint8_t {
  PowerDown = 0,
  ShuntTriggered = 1,
  BusTriggered = 2,
  ShuntBusTriggered = 3,
  AdcOff = 4,
  ShuntContinuous = 5,
  BusContinuous = 6,
  ShuntBusContinuous = 7,
};

// Assemble the configuration register value.
uint16_t buildConfig(BusRange range, PgaGain gain, AdcMode bus_adc, AdcMode shunt_adc,
                     OperatingMode mode);

// Conversion time of one ADC channel in microseconds (datasheet table 5):
// 84 / 148 / 276 / 532 us for 9..12 bit, 532 us * N for N-sample averaging.
uint32_t conversionTime_us(AdcMode mode);

// Full-scale shunt voltage of a PGA setting, in microvolts.
uint32_t shuntFullScale_uV(PgaGain gain);

enum class CalStatus : uint8_t {
  Ok = 0,
  InvalidInput,           // zero or absurdly large shunt / current
  ShuntVoltageTooHigh,    // shunt * max current exceeds the 320 mV PGA limit
  CalibrationOutOfRange,  // result does not fit the 16-bit calibration register
};

struct Calibration {
  CalStatus status;
  uint16_t cal_register;    // value for register 0x05 (bit 0 is always 0)
  PgaGain gain;             // smallest PGA range that covers shunt * max current
  uint32_t shunt_uohm;
  uint32_t nominal_lsb_nA;  // current LSB that was asked for before truncation
  float current_lsb_uA;     // effective current LSB after truncating the register
  float power_lsb_uW;       // always 20 * current LSB
  // Largest current the configuration can measure:
  //   min(32767 * current LSB, PGA full-scale shunt voltage / shunt).
  // The first term is the current register's range; the second is where the
  // shunt ADC clips. With a PGA range chosen close to shunt * max current the
  // second term is usually the smaller one (0.1 ohm, 400 mA: 400.0 mA, not
  // the 655.3 mA the register alone could hold).
  float max_current_mA;
};

// max_current_mA for a calibration's shunt, current LSB and PGA gain.
float maxMeasurableCurrent_mA(uint32_t shunt_uohm, float current_lsb_uA, PgaGain gain);

// Round up to the next value of the 1-2-5 series (1, 2, 5, 10, 20, 50, ...).
uint32_t roundUpTo125(uint32_t value);

// Calibration for an explicitly chosen current LSB (datasheet equation 1):
//   Cal = trunc(0.04096 / (Current_LSB * R_shunt))
// The PGA gain is left at Range320mV; use computeCalibration() to have it
// chosen from the expected current.
Calibration calibrationFromLsb(uint32_t shunt_uohm, uint32_t current_lsb_nA);

// Choose a current LSB and PGA range from the shunt resistance and the largest
// current the application expects. The LSB is Max_Expected_I / 32767 (datasheet
// equation 2) rounded up to the 1-2-5 series, and is raised further if needed
// so that the result fits the 16-bit calibration register.
Calibration computeCalibration(uint32_t shunt_uohm, uint32_t max_expected_mA);

// Bus voltage register: bits 15..3 hold the value, LSB = 4 mV.
int32_t busVoltage_mV(uint16_t raw);
bool conversionReady(uint16_t raw_bus);
bool mathOverflow(uint16_t raw_bus);

// Shunt voltage register: two's complement, LSB = 10 uV.
int32_t shuntVoltage_uV(uint16_t raw);

// Current register: two's complement, LSB = Calibration::current_lsb_uA.
int32_t current_uA(uint16_t raw, const Calibration& cal);

// Power register: unsigned, LSB = Calibration::power_lsb_uW. Saturates at INT32_MAX.
int32_t power_uW(uint16_t raw, const Calibration& cal);

}  // namespace ina219
}  // namespace powercore
