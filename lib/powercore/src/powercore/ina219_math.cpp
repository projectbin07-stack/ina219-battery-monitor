#include "powercore/ina219_math.h"

namespace powercore {
namespace ina219 {

namespace {

// Cal = 0.04096 / (LSB[A] * R[ohm]) = 4.096e13 / (LSB[nA] * R[micro-ohm]).
const uint64_t kCalNumerator = 40960000000000ULL;
// Same constant for an LSB in microamps: 4.096e10 = 2^12 * 1e7, exact in a float.
const float kCalNumeratorMicroAmp = 4.096e10f;

const uint32_t kMaxShunt_uohm = 100000000UL;    // 100 ohm
const uint32_t kMaxExpected_mA = 1000000UL;     // 1 kA
const uint32_t kMaxShuntVoltage_uV = 320000UL;  // PGA /8 full scale

const int32_t kInt32Max = 2147483647L;
const int32_t kInt32Min = -2147483647L - 1L;

int32_t roundToInt32(float x) {
  if (x >= 2147483520.0f) return kInt32Max;
  if (x <= -2147483520.0f) return kInt32Min;
  return static_cast<int32_t>(x >= 0.0f ? x + 0.5f : x - 0.5f);
}

PgaGain gainForShuntVoltage(uint32_t shunt_uV) {
  if (shunt_uV <= 40000UL) return PgaGain::Range40mV;
  if (shunt_uV <= 80000UL) return PgaGain::Range80mV;
  if (shunt_uV <= 160000UL) return PgaGain::Range160mV;
  return PgaGain::Range320mV;
}

Calibration invalidCalibration(CalStatus status) {
  Calibration c;
  c.status = status;
  c.cal_register = 0;
  c.gain = PgaGain::Range320mV;
  c.shunt_uohm = 0;
  c.nominal_lsb_nA = 0;
  c.current_lsb_uA = 0.0f;
  c.power_lsb_uW = 0.0f;
  c.max_current_mA = 0.0f;
  return c;
}

}  // namespace

uint16_t buildConfig(BusRange range, PgaGain gain, AdcMode bus_adc, AdcMode shunt_adc,
                     OperatingMode mode) {
  uint16_t value = 0;
  value |= static_cast<uint16_t>((static_cast<uint16_t>(range) & 0x1u) << 13);
  value |= static_cast<uint16_t>((static_cast<uint16_t>(gain) & 0x3u) << 11);
  value |= static_cast<uint16_t>((static_cast<uint16_t>(bus_adc) & 0xFu) << 7);
  value |= static_cast<uint16_t>((static_cast<uint16_t>(shunt_adc) & 0xFu) << 3);
  value |= static_cast<uint16_t>(static_cast<uint16_t>(mode) & 0x7u);
  return value;
}

uint32_t conversionTime_us(AdcMode mode) {
  const uint8_t bits = static_cast<uint8_t>(mode);
  if (bits & 0x8u) {
    // Averaging modes: 2^n samples of the 532 us 12-bit conversion.
    return 532UL << (bits & 0x7u);
  }
  switch (bits & 0x3u) {
    case 0:
      return 84UL;
    case 1:
      return 148UL;
    case 2:
      return 276UL;
    default:
      return 532UL;
  }
}

uint32_t shuntFullScale_uV(PgaGain gain) {
  return 40000UL << static_cast<uint8_t>(gain);
}

uint32_t roundUpTo125(uint32_t value) {
  if (value <= 1UL) return 1UL;
  uint32_t decade = 1UL;
  while (decade <= 100000000UL) {
    if (value <= decade) return decade;
    if (value <= 2UL * decade) return 2UL * decade;
    if (value <= 5UL * decade) return 5UL * decade;
    decade *= 10UL;
  }
  // Above 5e8 the series runs out of 32-bit range after one more step.
  return value <= 1000000000UL ? 1000000000UL : value;
}

Calibration calibrationFromLsb(uint32_t shunt_uohm, uint32_t current_lsb_nA) {
  if (shunt_uohm == 0 || current_lsb_nA == 0) {
    return invalidCalibration(CalStatus::InvalidInput);
  }
  const uint64_t denominator = static_cast<uint64_t>(current_lsb_nA) * shunt_uohm;
  const uint64_t cal = kCalNumerator / denominator;  // truncation as in the datasheet
  if (cal > 0xFFFFULL || cal < 2ULL) {
    return invalidCalibration(CalStatus::CalibrationOutOfRange);
  }
  Calibration c;
  c.status = CalStatus::Ok;
  // Bit 0 of the calibration register is not writable and always reads 0.
  c.cal_register = static_cast<uint16_t>(cal & 0xFFFEULL);
  c.gain = PgaGain::Range320mV;
  c.shunt_uohm = shunt_uohm;
  c.nominal_lsb_nA = current_lsb_nA;
  // The device works with the truncated register value, so derive the LSB that
  // is actually in effect rather than the one that was asked for.
  c.current_lsb_uA = kCalNumeratorMicroAmp /
                     (static_cast<float>(c.cal_register) * static_cast<float>(shunt_uohm));
  c.power_lsb_uW = 20.0f * c.current_lsb_uA;
  c.max_current_mA = 32767.0f * c.current_lsb_uA / 1000.0f;
  return c;
}

Calibration computeCalibration(uint32_t shunt_uohm, uint32_t max_expected_mA) {
  if (shunt_uohm == 0 || max_expected_mA == 0 || shunt_uohm > kMaxShunt_uohm ||
      max_expected_mA > kMaxExpected_mA) {
    return invalidCalibration(CalStatus::InvalidInput);
  }
  // micro-ohm * mA = nV; / 1000 -> uV.
  const uint64_t shunt_uV = static_cast<uint64_t>(shunt_uohm) * max_expected_mA / 1000ULL;
  if (shunt_uV > kMaxShuntVoltage_uV) {
    return invalidCalibration(CalStatus::ShuntVoltageTooHigh);
  }
  // Minimum LSB = Max_Expected_I / 32767, in nA, rounded up.
  const uint64_t min_lsb_nA =
      (static_cast<uint64_t>(max_expected_mA) * 1000000ULL + 32766ULL) / 32767ULL;
  // Smallest LSB for which trunc(Cal) still fits in 16 bits.
  const uint64_t fit_lsb_nA = kCalNumerator / (65536ULL * shunt_uohm) + 1ULL;
  const uint64_t lsb_nA = min_lsb_nA > fit_lsb_nA ? min_lsb_nA : fit_lsb_nA;

  Calibration c = calibrationFromLsb(shunt_uohm, roundUpTo125(static_cast<uint32_t>(lsb_nA)));
  if (c.status == CalStatus::Ok) {
    c.gain = gainForShuntVoltage(static_cast<uint32_t>(shunt_uV));
  }
  return c;
}

int32_t busVoltage_mV(uint16_t raw) {
  return static_cast<int32_t>(raw >> 3) * 4;
}

bool conversionReady(uint16_t raw_bus) {
  return (raw_bus & kBusFlagConversionReady) != 0;
}

bool mathOverflow(uint16_t raw_bus) {
  return (raw_bus & kBusFlagOverflow) != 0;
}

int32_t shuntVoltage_uV(uint16_t raw) {
  return static_cast<int32_t>(static_cast<int16_t>(raw)) * 10;
}

int32_t current_uA(uint16_t raw, const Calibration& cal) {
  return roundToInt32(static_cast<float>(static_cast<int16_t>(raw)) * cal.current_lsb_uA);
}

int32_t power_uW(uint16_t raw, const Calibration& cal) {
  return roundToInt32(static_cast<float>(raw) * cal.power_lsb_uW);
}

}  // namespace ina219
}  // namespace powercore
