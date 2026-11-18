#include <unity.h>

#include <ina219/driver.h>
#include <powercore/ina219_math.h>

#include "fake_ina219_bus.h"

using ina219drv::Ina219;
using ina219drv::Reading;
using ina219drv::Settings;
using ina219drv::Status;
using powercore::ina219::AdcMode;
using powercore::ina219::BusRange;

void setUp() {}
void tearDown() {}

#define ASSERT_STATUS(expected, actual) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual))

// 0.1 ohm shunt, 3.2 A, 32 V, 12-bit: config 0x399F, calibration 4096,
// current LSB 100 uA, power LSB 2 mW.
static Settings defaultSettings() {
  Settings s;
  s.shunt_uohm = 100000UL;
  s.max_expected_mA = 3200UL;
  s.bus_range = BusRange::V32;
  s.bus_adc = AdcMode::Bits12;
  s.shunt_adc = AdcMode::Bits12;
  return s;
}

static void test_begin_programs_reset_config_calibration_in_order() {
  FakeIna219Bus bus;
  Ina219 ina(bus);
  TEST_ASSERT_FALSE(ina.configured());
  ASSERT_STATUS(Status::Ok, ina.begin(defaultSettings()));
  TEST_ASSERT_TRUE(ina.configured());

  TEST_ASSERT_EQUAL_UINT(3, bus.writes.size());
  TEST_ASSERT_EQUAL_UINT8(0x00, bus.writes[0].reg);
  TEST_ASSERT_EQUAL_HEX16(0x8000, bus.writes[0].value);
  TEST_ASSERT_EQUAL_UINT8(0x00, bus.writes[1].reg);
  TEST_ASSERT_EQUAL_HEX16(0x399F, bus.writes[1].value);
  TEST_ASSERT_EQUAL_UINT8(0x05, bus.writes[2].reg);
  TEST_ASSERT_EQUAL_HEX16(0x1000, bus.writes[2].value);
  TEST_ASSERT_EQUAL_INT(1, bus.resets);

  TEST_ASSERT_EQUAL_HEX16(0x399F, bus.regs[0]);
  TEST_ASSERT_EQUAL_UINT16(4096, bus.regs[5]);
  TEST_ASSERT_EQUAL_HEX16(0x399F, ina.configValue());
  TEST_ASSERT_EQUAL_UINT16(4096, ina.calibration().cal_register);
}

static void test_begin_uses_computed_gain_and_adc_modes() {
  FakeIna219Bus bus;
  Ina219 ina(bus);
  Settings s = defaultSettings();
  s.max_expected_mA = 1000UL;  // 100 mV drop -> 160 mV range (PG = 10)
  s.bus_range = BusRange::V16;
  s.bus_adc = AdcMode::Avg16;
  s.shunt_adc = AdcMode::Avg16;
  ASSERT_STATUS(Status::Ok, ina.begin(s));
  // BRNG 0 | PG 10 (0x1000) | BADC 1100 (0x600) | SADC 1100 (0x060) | MODE 111
  TEST_ASSERT_EQUAL_HEX16(0x1667, bus.regs[0]);
  TEST_ASSERT_EQUAL_UINT16(8192, bus.regs[5]);  // 50 uA LSB
}

static void test_wire_format_is_big_endian() {
  FakeIna219Bus bus;
  Ina219 ina(bus);
  ina.begin(defaultSettings());
  // Raw traffic: reset, config, pointer for the read-back, calibration.
  TEST_ASSERT_EQUAL_UINT(4, bus.raw_writes.size());
  const uint8_t reset[] = {0x00, 0x80, 0x00};
  const uint8_t config[] = {0x00, 0x39, 0x9F};
  const uint8_t pointer[] = {0x00};
  const uint8_t calibration[] = {0x05, 0x10, 0x00};
  TEST_ASSERT_EQUAL_UINT(3, bus.raw_writes[0].size());
  TEST_ASSERT_EQUAL_HEX8_ARRAY(reset, bus.raw_writes[0].data(), 3);
  TEST_ASSERT_EQUAL_UINT(3, bus.raw_writes[1].size());
  TEST_ASSERT_EQUAL_HEX8_ARRAY(config, bus.raw_writes[1].data(), 3);
  TEST_ASSERT_EQUAL_UINT(1, bus.raw_writes[2].size());
  TEST_ASSERT_EQUAL_HEX8_ARRAY(pointer, bus.raw_writes[2].data(), 1);
  TEST_ASSERT_EQUAL_UINT(3, bus.raw_writes[3].size());
  TEST_ASSERT_EQUAL_HEX8_ARRAY(calibration, bus.raw_writes[3].data(), 3);

  // Reading assembles MSB first too.
  bus.regs[1] = 0x12AB;
  uint16_t value = 0;
  ASSERT_STATUS(Status::Ok, ina.readRegister(0x01, value));
  TEST_ASSERT_EQUAL_HEX16(0x12AB, value);
}

static void test_begin_fails_when_device_is_absent() {
  FakeIna219Bus bus;
  Ina219 ina(bus, 0x41);  // the fake only answers on 0x40
  ASSERT_STATUS(Status::BusError, ina.begin(defaultSettings()));
  TEST_ASSERT_FALSE(ina.configured());
  TEST_ASSERT_EQUAL_INT(1, bus.transactions);  // gave up after the first NACK
  TEST_ASSERT_EQUAL_UINT8(0x41, ina.address());
}

static void test_begin_rejects_bad_calibration_without_bus_traffic() {
  FakeIna219Bus bus;
  Ina219 ina(bus);
  Settings s = defaultSettings();
  s.max_expected_mA = 5000UL;  // 0.5 V across 0.1 ohm: beyond the PGA
  ASSERT_STATUS(Status::BadCalibration, ina.begin(s));
  TEST_ASSERT_EQUAL_INT(0, bus.transactions);
  TEST_ASSERT_FALSE(ina.configured());
}

static void test_begin_detects_config_readback_mismatch() {
  FakeIna219Bus bus;
  bus.config_stuck = true;
  Ina219 ina(bus);
  Settings s = defaultSettings();
  s.bus_adc = AdcMode::Avg16;  // anything other than the power-on value
  ASSERT_STATUS(Status::VerifyFailed, ina.begin(s));
  TEST_ASSERT_FALSE(ina.configured());
  TEST_ASSERT_EQUAL_UINT16(0, bus.regs[5]);  // calibration was not written
}

static void test_begin_reports_failure_at_each_step() {
  // begin() is five transactions: reset, config, pointer, read, calibration.
  for (int ok = 0; ok < 5; ++ok) {
    FakeIna219Bus bus;
    bus.transactions_until_failure = ok;
    Ina219 ina(bus);
    ASSERT_STATUS(Status::BusError, ina.begin(defaultSettings()));
    TEST_ASSERT_FALSE(ina.configured());
    TEST_ASSERT_EQUAL_INT(ok + 1, bus.transactions);
  }
  FakeIna219Bus bus;
  bus.transactions_until_failure = 5;
  Ina219 ina(bus);
  ASSERT_STATUS(Status::Ok, ina.begin(defaultSettings()));
}

static void test_read_before_begin() {
  FakeIna219Bus bus;
  Ina219 ina(bus);
  Reading r;
  ASSERT_STATUS(Status::NotConfigured, ina.read(r));
  bool intact = false;
  ASSERT_STATUS(Status::NotConfigured, ina.calibrationIntact(intact));
  ASSERT_STATUS(Status::NotConfigured, ina.powerDown());
  TEST_ASSERT_EQUAL_INT(0, bus.transactions);
}

// 1S cell at 3.876 V supplying 100 mA through the 0.1 ohm shunt:
//   bus     = 3876 / 4 = 969 counts -> (969 << 3) | CNVR = 0x1E4A
//   shunt   = 10 mV / 10 uV = 1000 counts = 0x03E8
//   current = 100 mA / 100 uA = 1000 counts
//   power   = 387.6 mW / 2 mW = 193 counts (truncated) -> 386 mW
static void test_read_converts_all_registers() {
  FakeIna219Bus bus;
  Ina219 ina(bus);
  ina.begin(defaultSettings());
  bus.regs[2] = 0x1E4A;
  bus.regs[1] = 0x03E8;
  bus.regs[4] = 1000;
  bus.regs[3] = 193;
  bus.clearLog();

  Reading r;
  ASSERT_STATUS(Status::Ok, ina.read(r));
  TEST_ASSERT_EQUAL_INT32(3876, r.bus_mV);
  TEST_ASSERT_EQUAL_INT32(10000, r.shunt_uV);
  TEST_ASSERT_EQUAL_INT32(100000, r.current_uA);
  TEST_ASSERT_EQUAL_INT32(386000, r.power_uW);
  TEST_ASSERT_TRUE(r.ready);
  TEST_ASSERT_FALSE(r.overflow);

  // Bus first (it carries the flags), power last (reading it clears CNVR).
  TEST_ASSERT_EQUAL_UINT(4, bus.reads.size());
  TEST_ASSERT_EQUAL_UINT8(0x02, bus.reads[0]);
  TEST_ASSERT_EQUAL_UINT8(0x01, bus.reads[1]);
  TEST_ASSERT_EQUAL_UINT8(0x04, bus.reads[2]);
  TEST_ASSERT_EQUAL_UINT8(0x03, bus.reads[3]);
  TEST_ASSERT_EQUAL_UINT(0, bus.writes.size());  // a read never writes a register
}

static void test_ready_flag_clears_until_next_conversion() {
  FakeIna219Bus bus;
  Ina219 ina(bus);
  ina.begin(defaultSettings());
  bus.regs[2] = 0x1E4A;

  Reading r;
  ina.read(r);
  TEST_ASSERT_TRUE(r.ready);
  // No new conversion in the model: the second read sees CNVR cleared.
  ina.read(r);
  TEST_ASSERT_FALSE(r.ready);
  TEST_ASSERT_EQUAL_INT32(3876, r.bus_mV);
  // The device completes another conversion.
  bus.regs[2] |= 0x0002;
  ina.read(r);
  TEST_ASSERT_TRUE(r.ready);
}

static void test_overflow_flag_is_reported() {
  FakeIna219Bus bus;
  Ina219 ina(bus);
  ina.begin(defaultSettings());
  bus.regs[2] = static_cast<uint16_t>((969u << 3) | 0x0003u);  // CNVR + OVF
  Reading r;
  ASSERT_STATUS(Status::Ok, ina.read(r));
  TEST_ASSERT_TRUE(r.overflow);
  TEST_ASSERT_TRUE(r.ready);
  TEST_ASSERT_EQUAL_INT32(3876, r.bus_mV);  // the voltage itself is still valid
}

// Charging: the shunt and current registers go negative, power stays unsigned.
static void test_negative_current() {
  FakeIna219Bus bus;
  Ina219 ina(bus);
  ina.begin(defaultSettings());
  bus.regs[2] = 0x1E4A;
  bus.regs[1] = 0xFC18;  // -1000 counts = -10 mV
  bus.regs[4] = 0xFC18;  // -1000 counts = -100 mA
  bus.regs[3] = 193;
  Reading r;
  ASSERT_STATUS(Status::Ok, ina.read(r));
  TEST_ASSERT_EQUAL_INT32(-10000, r.shunt_uV);
  TEST_ASSERT_EQUAL_INT32(-100000, r.current_uA);
  TEST_ASSERT_EQUAL_INT32(386000, r.power_uW);
}

static void test_read_reports_bus_failure_at_each_step() {
  // read() is eight transactions: pointer + data for four registers.
  for (int ok = 0; ok < 8; ++ok) {
    FakeIna219Bus bus;
    Ina219 ina(bus);
    ina.begin(defaultSettings());
    bus.transactions_until_failure = ok;
    Reading r;
    ASSERT_STATUS(Status::BusError, ina.read(r));
  }
}

static void test_calibration_intact_detects_device_reset() {
  FakeIna219Bus bus;
  Ina219 ina(bus);
  ina.begin(defaultSettings());
  bool intact = false;
  ASSERT_STATUS(Status::Ok, ina.calibrationIntact(intact));
  TEST_ASSERT_TRUE(intact);

  // Brown-out: the device comes back with power-on register values.
  bus.regs[0] = 0x399F;
  bus.regs[5] = 0;
  ASSERT_STATUS(Status::Ok, ina.calibrationIntact(intact));
  TEST_ASSERT_FALSE(intact);

  // Running begin() again restores it.
  ASSERT_STATUS(Status::Ok, ina.begin(defaultSettings()));
  ASSERT_STATUS(Status::Ok, ina.calibrationIntact(intact));
  TEST_ASSERT_TRUE(intact);

  bus.transactions_until_failure = 0;
  ASSERT_STATUS(Status::BusError, ina.calibrationIntact(intact));
}

static void test_power_down_clears_only_mode_bits() {
  FakeIna219Bus bus;
  Ina219 ina(bus);
  ina.begin(defaultSettings());
  ASSERT_STATUS(Status::Ok, ina.powerDown());
  TEST_ASSERT_EQUAL_HEX16(0x3998, bus.regs[0]);
  TEST_ASSERT_EQUAL_UINT16(4096, bus.regs[5]);  // calibration untouched
}

static void test_failed_begin_unconfigures_a_working_driver() {
  FakeIna219Bus bus;
  Ina219 ina(bus);
  ina.begin(defaultSettings());
  TEST_ASSERT_TRUE(ina.configured());
  bus.transactions_until_failure = 0;
  ASSERT_STATUS(Status::BusError, ina.begin(defaultSettings()));
  TEST_ASSERT_FALSE(ina.configured());
  Reading r;
  ASSERT_STATUS(Status::NotConfigured, ina.read(r));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_begin_programs_reset_config_calibration_in_order);
  RUN_TEST(test_begin_uses_computed_gain_and_adc_modes);
  RUN_TEST(test_wire_format_is_big_endian);
  RUN_TEST(test_begin_fails_when_device_is_absent);
  RUN_TEST(test_begin_rejects_bad_calibration_without_bus_traffic);
  RUN_TEST(test_begin_detects_config_readback_mismatch);
  RUN_TEST(test_begin_reports_failure_at_each_step);
  RUN_TEST(test_read_before_begin);
  RUN_TEST(test_read_converts_all_registers);
  RUN_TEST(test_ready_flag_clears_until_next_conversion);
  RUN_TEST(test_overflow_flag_is_reported);
  RUN_TEST(test_negative_current);
  RUN_TEST(test_read_reports_bus_failure_at_each_step);
  RUN_TEST(test_calibration_intact_detects_device_reset);
  RUN_TEST(test_power_down_clears_only_mode_bits);
  RUN_TEST(test_failed_begin_unconfigures_a_working_driver);
  return UNITY_END();
}
