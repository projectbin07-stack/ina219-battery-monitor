// Expected values are worked out by hand from the INA219 datasheet formulas:
//   Cal          = trunc(0.04096 / (Current_LSB * R_shunt))
//   Power_LSB    = 20 * Current_LSB
//   Bus voltage  = (register >> 3) * 4 mV
//   Shunt voltage= register * 10 uV (two's complement)
#include <unity.h>

#include <powercore/ina219_math.h>

using namespace powercore::ina219;

#define ASSERT_ENUM_EQ(expected, actual) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual))

void setUp() {}
void tearDown() {}

static void test_config_power_on_default() {
  // 32 V, PGA /8, 12-bit bus, 12-bit shunt, continuous = datasheet reset value.
  TEST_ASSERT_EQUAL_HEX16(0x399F, buildConfig(BusRange::V32, PgaGain::Range320mV, AdcMode::Bits12,
                                              AdcMode::Bits12,
                                              OperatingMode::ShuntBusContinuous));
}

static void test_config_field_positions() {
  // BRNG=0, PG=00, BADC=1100 (<<7 = 0x600), SADC=1100 (<<3 = 0x060), MODE=111.
  TEST_ASSERT_EQUAL_HEX16(0x0667, buildConfig(BusRange::V16, PgaGain::Range40mV, AdcMode::Avg16,
                                              AdcMode::Avg16,
                                              OperatingMode::ShuntBusContinuous));
  // Power-down only clears the three mode bits of the default.
  TEST_ASSERT_EQUAL_HEX16(0x3998, buildConfig(BusRange::V32, PgaGain::Range320mV, AdcMode::Bits12,
                                              AdcMode::Bits12, OperatingMode::PowerDown));
  // PG=10 (<<11 = 0x1000), BADC=0000, SADC=1111 (0x078), MODE=101.
  TEST_ASSERT_EQUAL_HEX16(0x107D, buildConfig(BusRange::V16, PgaGain::Range160mV, AdcMode::Bits9,
                                              AdcMode::Avg128, OperatingMode::ShuntContinuous));
}

static void test_conversion_times() {
  TEST_ASSERT_EQUAL_UINT32(84, conversionTime_us(AdcMode::Bits9));
  TEST_ASSERT_EQUAL_UINT32(148, conversionTime_us(AdcMode::Bits10));
  TEST_ASSERT_EQUAL_UINT32(276, conversionTime_us(AdcMode::Bits11));
  TEST_ASSERT_EQUAL_UINT32(532, conversionTime_us(AdcMode::Bits12));
  TEST_ASSERT_EQUAL_UINT32(1064, conversionTime_us(AdcMode::Avg2));    // datasheet: 1.06 ms
  TEST_ASSERT_EQUAL_UINT32(8512, conversionTime_us(AdcMode::Avg16));   // datasheet: 8.51 ms
  TEST_ASSERT_EQUAL_UINT32(68096, conversionTime_us(AdcMode::Avg128)); // datasheet: 68.10 ms
}

static void test_shunt_full_scale() {
  TEST_ASSERT_EQUAL_UINT32(40000, shuntFullScale_uV(PgaGain::Range40mV));
  TEST_ASSERT_EQUAL_UINT32(80000, shuntFullScale_uV(PgaGain::Range80mV));
  TEST_ASSERT_EQUAL_UINT32(160000, shuntFullScale_uV(PgaGain::Range160mV));
  TEST_ASSERT_EQUAL_UINT32(320000, shuntFullScale_uV(PgaGain::Range320mV));
}

static void test_round_up_125() {
  TEST_ASSERT_EQUAL_UINT32(1, roundUpTo125(0));
  TEST_ASSERT_EQUAL_UINT32(1, roundUpTo125(1));
  TEST_ASSERT_EQUAL_UINT32(2, roundUpTo125(2));
  TEST_ASSERT_EQUAL_UINT32(5, roundUpTo125(3));
  TEST_ASSERT_EQUAL_UINT32(5, roundUpTo125(5));
  TEST_ASSERT_EQUAL_UINT32(10, roundUpTo125(6));
  TEST_ASSERT_EQUAL_UINT32(20000, roundUpTo125(18312));
  TEST_ASSERT_EQUAL_UINT32(50000, roundUpTo125(30519));
  TEST_ASSERT_EQUAL_UINT32(100000, roundUpTo125(97660));
  TEST_ASSERT_EQUAL_UINT32(100000, roundUpTo125(100000));
  TEST_ASSERT_EQUAL_UINT32(200000, roundUpTo125(100001));
  TEST_ASSERT_EQUAL_UINT32(1000000000UL, roundUpTo125(500000001UL));
  TEST_ASSERT_EQUAL_UINT32(3000000000UL, roundUpTo125(3000000000UL));
}

// Datasheet design example: R = 0.5 ohm, 0.6 A expected.
//   min LSB = 0.6 / 32767 = 18.31 uA -> 20 uA; Cal = 0.04096 / (20e-6 * 0.5) = 4096
//   power LSB = 400 uW; shunt drop 0.3 V -> 320 mV range.
static void test_calibration_datasheet_example() {
  const Calibration c = computeCalibration(500000UL, 600UL);
  ASSERT_ENUM_EQ(CalStatus::Ok, c.status);
  TEST_ASSERT_EQUAL_UINT32(20000, c.nominal_lsb_nA);
  TEST_ASSERT_EQUAL_UINT16(4096, c.cal_register);
  ASSERT_ENUM_EQ(PgaGain::Range320mV, c.gain);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 20.0f, c.current_lsb_uA);
  TEST_ASSERT_FLOAT_WITHIN(1e-2f, 400.0f, c.power_lsb_uW);
  TEST_ASSERT_FLOAT_WITHIN(1e-2f, 655.34f, c.max_current_mA);
}

// Common breakout: R = 0.1 ohm, 3.2 A.
//   min LSB = 3.2 / 32767 = 97.66 uA -> 100 uA; Cal = 0.04096 / (100e-6 * 0.1) = 4096
static void test_calibration_breakout_3A2() {
  const Calibration c = computeCalibration(100000UL, 3200UL);
  ASSERT_ENUM_EQ(CalStatus::Ok, c.status);
  TEST_ASSERT_EQUAL_UINT16(4096, c.cal_register);
  ASSERT_ENUM_EQ(PgaGain::Range320mV, c.gain);  // 0.32 V exactly
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 100.0f, c.current_lsb_uA);
  TEST_ASSERT_FLOAT_WITHIN(1e-2f, 2000.0f, c.power_lsb_uW);
  TEST_ASSERT_FLOAT_WITHIN(1e-2f, 3276.7f, c.max_current_mA);
}

// R = 0.1 ohm, 1 A: min LSB 30.52 uA -> 50 uA; Cal = 0.04096 / 5e-6 = 8192;
// shunt drop 100 mV -> 160 mV range.
static void test_calibration_1A_selects_160mV() {
  const Calibration c = computeCalibration(100000UL, 1000UL);
  ASSERT_ENUM_EQ(CalStatus::Ok, c.status);
  TEST_ASSERT_EQUAL_UINT16(8192, c.cal_register);
  ASSERT_ENUM_EQ(PgaGain::Range160mV, c.gain);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 50.0f, c.current_lsb_uA);
}

// R = 0.1 ohm, 0.4 A: min LSB 12.21 uA -> 20 uA; Cal = 0.04096 / 2e-6 = 20480;
// shunt drop 40 mV -> 40 mV range. 0.8 A -> 80 mV range.
static void test_calibration_small_ranges() {
  const Calibration a = computeCalibration(100000UL, 400UL);
  ASSERT_ENUM_EQ(CalStatus::Ok, a.status);
  TEST_ASSERT_EQUAL_UINT16(20480, a.cal_register);
  ASSERT_ENUM_EQ(PgaGain::Range40mV, a.gain);

  const Calibration b = computeCalibration(100000UL, 800UL);
  ASSERT_ENUM_EQ(CalStatus::Ok, b.status);
  ASSERT_ENUM_EQ(PgaGain::Range80mV, b.gain);
  // min LSB 24.41 uA -> 50 uA -> Cal 8192
  TEST_ASSERT_EQUAL_UINT16(8192, b.cal_register);
}

// R = 0.075 ohm, 2 A: LSB 61.04 uA -> 100 uA;
//   Cal = 0.04096 / (100e-6 * 0.075) = 5461.33 -> 5461 -> bit 0 cleared -> 5460
//   effective LSB = 0.04096 / (5460 * 0.075) = 100.0244 uA
static void test_calibration_truncation_gives_effective_lsb() {
  const Calibration c = computeCalibration(75000UL, 2000UL);
  ASSERT_ENUM_EQ(CalStatus::Ok, c.status);
  TEST_ASSERT_EQUAL_UINT16(5460, c.cal_register);
  TEST_ASSERT_EQUAL_UINT32(100000, c.nominal_lsb_nA);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 100.0244f, c.current_lsb_uA);
  TEST_ASSERT_FLOAT_WITHIN(2e-2f, 2000.488f, c.power_lsb_uW);
}

// R = 0.01 ohm, 0.1 A: the 3.05 uA minimum LSB would need Cal = 819200, which
// does not fit in 16 bits. Smallest LSB that fits: 0.04096 / (65536 * 0.01)
// = 62.5 uA -> 100 uA -> Cal = 40960.
static void test_calibration_raises_lsb_to_fit_register() {
  const Calibration c = computeCalibration(10000UL, 100UL);
  ASSERT_ENUM_EQ(CalStatus::Ok, c.status);
  TEST_ASSERT_EQUAL_UINT32(100000, c.nominal_lsb_nA);
  TEST_ASSERT_EQUAL_UINT16(40960, c.cal_register);
  ASSERT_ENUM_EQ(PgaGain::Range40mV, c.gain);
}

static void test_calibration_rejects_bad_input() {
  ASSERT_ENUM_EQ(CalStatus::InvalidInput, computeCalibration(0, 1000).status);
  ASSERT_ENUM_EQ(CalStatus::InvalidInput, computeCalibration(100000UL, 0).status);
  ASSERT_ENUM_EQ(CalStatus::InvalidInput, computeCalibration(200000000UL, 1).status);
  ASSERT_ENUM_EQ(CalStatus::InvalidInput, computeCalibration(1000, 2000000UL).status);
  // 0.1 ohm * 3.3 A = 330 mV > 320 mV
  ASSERT_ENUM_EQ(CalStatus::ShuntVoltageTooHigh, computeCalibration(100000UL, 3300UL).status);
  TEST_ASSERT_EQUAL_UINT16(0, computeCalibration(100000UL, 3300UL).cal_register);

  ASSERT_ENUM_EQ(CalStatus::InvalidInput, calibrationFromLsb(0, 1000).status);
  ASSERT_ENUM_EQ(CalStatus::InvalidInput, calibrationFromLsb(1000, 0).status);
  // 1 nA LSB on 0.1 ohm would need Cal = 4.096e8.
  ASSERT_ENUM_EQ(CalStatus::CalibrationOutOfRange, calibrationFromLsb(100000UL, 1).status);
  // 1 A LSB on 100 ohm gives Cal = 0.0004 -> 0.
  ASSERT_ENUM_EQ(CalStatus::CalibrationOutOfRange,
                 calibrationFromLsb(100000000UL, 1000000000UL).status);
}

static void test_bus_voltage() {
  // Datasheet example: 11.98 V reads as 0x5D98.
  TEST_ASSERT_EQUAL_INT32(11980, busVoltage_mV(0x5D98));
  // 1000 counts * 4 mV, flag bits must not leak into the value.
  TEST_ASSERT_EQUAL_INT32(4000, busVoltage_mV(0x1F40));
  TEST_ASSERT_EQUAL_INT32(4000, busVoltage_mV(0x1F43));
  TEST_ASSERT_EQUAL_INT32(0, busVoltage_mV(0x0007));
  // 32 V full scale = 8000 counts = 0xFA00.
  TEST_ASSERT_EQUAL_INT32(32000, busVoltage_mV(0xFA00));
}

static void test_bus_flags() {
  TEST_ASSERT_FALSE(conversionReady(0x1F40));
  TEST_ASSERT_FALSE(mathOverflow(0x1F40));
  TEST_ASSERT_TRUE(conversionReady(0x1F42));
  TEST_ASSERT_FALSE(mathOverflow(0x1F42));
  TEST_ASSERT_TRUE(mathOverflow(0x1F41));
  TEST_ASSERT_FALSE(conversionReady(0x1F41));
  TEST_ASSERT_TRUE(mathOverflow(0x1F43));
  TEST_ASSERT_TRUE(conversionReady(0x1F43));
  // Bit 2 is unused and is neither flag.
  TEST_ASSERT_FALSE(conversionReady(0x0004));
  TEST_ASSERT_FALSE(mathOverflow(0x0004));
}

static void test_shunt_voltage() {
  TEST_ASSERT_EQUAL_INT32(20000, shuntVoltage_uV(0x07D0));     // +2000 counts = 20 mV
  TEST_ASSERT_EQUAL_INT32(-20000, shuntVoltage_uV(0xF830));    // -2000 counts
  TEST_ASSERT_EQUAL_INT32(320000, shuntVoltage_uV(0x7D00));    // +320 mV full scale
  TEST_ASSERT_EQUAL_INT32(-320000, shuntVoltage_uV(0x8300));   // -320 mV (datasheet table)
  TEST_ASSERT_EQUAL_INT32(0, shuntVoltage_uV(0x0000));
  TEST_ASSERT_EQUAL_INT32(-10, shuntVoltage_uV(0xFFFF));
}

// Datasheet worked example: 2 mOhm shunt, 1 mA current LSB, 10 A load at 11.98 V.
//   Cal = 0.04096 / (1e-3 * 0.002) = 20480 (0x5000)
//   current register 0x2710 = 10000 -> 10.000 A
//   power register   0x1766 = 5990, power LSB 20 mW -> 119.8 W
static void test_current_and_power_datasheet_example() {
  const Calibration c = calibrationFromLsb(2000UL, 1000000UL);
  ASSERT_ENUM_EQ(CalStatus::Ok, c.status);
  TEST_ASSERT_EQUAL_HEX16(0x5000, c.cal_register);
  TEST_ASSERT_EQUAL_INT32(10000000L, current_uA(0x2710, c));
  TEST_ASSERT_EQUAL_INT32(119800000L, power_uW(0x1766, c));
}

static void test_current_sign_and_scaling() {
  const Calibration c = computeCalibration(100000UL, 3200UL);  // 100 uA per count
  TEST_ASSERT_EQUAL_INT32(100000L, current_uA(1000, c));
  TEST_ASSERT_EQUAL_INT32(-100000L, current_uA(0xFC18, c));  // -1000 counts
  TEST_ASSERT_EQUAL_INT32(0, current_uA(0, c));
  TEST_ASSERT_EQUAL_INT32(3276700L, current_uA(0x7FFF, c));
  TEST_ASSERT_EQUAL_INT32(-3276800L, current_uA(0x8000, c));
  // Power register is unsigned: 0xFFFF is 65535 counts * 2 mW, not -1.
  TEST_ASSERT_EQUAL_INT32(131070000L, power_uW(0xFFFF, c));
  TEST_ASSERT_EQUAL_INT32(386000L, power_uW(193, c));
}

static void test_power_saturates() {
  // 1 mOhm shunt, 10 mA LSB -> Cal 4096, power LSB 200 mW.
  const Calibration c = calibrationFromLsb(1000UL, 10000000UL);
  ASSERT_ENUM_EQ(CalStatus::Ok, c.status);
  TEST_ASSERT_EQUAL_UINT16(4096, c.cal_register);
  TEST_ASSERT_EQUAL_INT32(-327680000L, current_uA(0x8000, c));
  // 65535 * 200000 uW = 1.3e10 uW does not fit: clamp to INT32_MAX.
  TEST_ASSERT_EQUAL_INT32(2147483647L, power_uW(0xFFFF, c));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_config_power_on_default);
  RUN_TEST(test_config_field_positions);
  RUN_TEST(test_conversion_times);
  RUN_TEST(test_shunt_full_scale);
  RUN_TEST(test_round_up_125);
  RUN_TEST(test_calibration_datasheet_example);
  RUN_TEST(test_calibration_breakout_3A2);
  RUN_TEST(test_calibration_1A_selects_160mV);
  RUN_TEST(test_calibration_small_ranges);
  RUN_TEST(test_calibration_truncation_gives_effective_lsb);
  RUN_TEST(test_calibration_raises_lsb_to_fit_register);
  RUN_TEST(test_calibration_rejects_bad_input);
  RUN_TEST(test_bus_voltage);
  RUN_TEST(test_bus_flags);
  RUN_TEST(test_shunt_voltage);
  RUN_TEST(test_current_and_power_datasheet_example);
  RUN_TEST(test_current_sign_and_scaling);
  RUN_TEST(test_power_saturates);
  return UNITY_END();
}
