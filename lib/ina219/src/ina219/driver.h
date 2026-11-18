// Thin INA219 driver.
//
// The driver talks to the device through the abstract I2CBus below, so the
// same code runs against Arduino's TwoWire on a board and against a fake bus
// in the host unit tests. All register maths lives in powercore/ina219_math.
#pragma once

#include <stdint.h>

#include <powercore/ina219_math.h>

namespace ina219drv {

// Minimal I2C master interface. Both calls are complete transactions
// (START ... STOP) and return true only if every byte was acknowledged.
class I2CBus {
 public:
  virtual bool write(uint8_t address, const uint8_t* data, uint8_t length) = 0;
  virtual bool read(uint8_t address, uint8_t* data, uint8_t length) = 0;

 protected:
  ~I2CBus() {}
};

enum class Status : uint8_t {
  Ok = 0,
  BusError,        // an I2C transaction was not acknowledged
  BadCalibration,  // shunt / max current combination is not usable
  VerifyFailed,    // configuration register did not read back as written
  NotConfigured,   // begin() has not succeeded yet
};

struct Settings {
  uint32_t shunt_uohm;                   // shunt resistance in micro-ohms
  uint32_t max_expected_mA;              // largest current the application expects
  powercore::ina219::BusRange bus_range;
  powercore::ina219::AdcMode bus_adc;
  powercore::ina219::AdcMode shunt_adc;
};

struct Reading {
  int32_t bus_mV;
  int32_t shunt_uV;
  int32_t current_uA;
  int32_t power_uW;  // unsigned in the device; always >= 0
  bool ready;        // CNVR: a new conversion completed since the last read()
  bool overflow;     // OVF: current/power are out of range and not valid
};

class Ina219 {
 public:
  static const uint8_t kDefaultAddress = 0x40;

  explicit Ina219(I2CBus& bus, uint8_t address = kDefaultAddress);

  // Reset the device, then program configuration (continuous shunt + bus
  // conversion) and calibration. The calibration is computed first, so an
  // unusable shunt/current pair fails without touching the bus.
  Status begin(const Settings& settings);

  // Read bus voltage, shunt voltage, current and power. The power register is
  // read last because reading it clears the CNVR flag in the device.
  Status read(Reading& out);

  // The calibration register is volatile: a brown-out resets it to 0 and the
  // current/power registers then read 0. Sets `intact` to whether the device
  // still holds the value programmed by begin().
  Status calibrationIntact(bool& intact);

  // Put the ADC into power-down mode (configuration is otherwise kept).
  Status powerDown();

  Status readRegister(uint8_t reg, uint16_t& value);
  Status writeRegister(uint8_t reg, uint16_t value);

  bool configured() const { return configured_; }
  uint8_t address() const { return address_; }
  uint16_t configValue() const { return config_; }
  const powercore::ina219::Calibration& calibration() const { return calibration_; }

 private:
  I2CBus& bus_;
  powercore::ina219::Calibration calibration_;
  uint16_t config_;
  uint8_t address_;
  bool configured_;
};

}  // namespace ina219drv
