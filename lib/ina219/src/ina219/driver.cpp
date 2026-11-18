#include "ina219/driver.h"

namespace ina219drv {

namespace reg = powercore::ina219;

Ina219::Ina219(I2CBus& bus, uint8_t address)
    : bus_(bus),
      calibration_(reg::calibrationFromLsb(0, 0)),  // an "invalid" placeholder
      config_(reg::kConfigPowerOn),
      address_(address),
      configured_(false) {}

Status Ina219::writeRegister(uint8_t reg_address, uint16_t value) {
  // Register pointer followed by the value, most significant byte first.
  const uint8_t frame[3] = {reg_address, static_cast<uint8_t>(value >> 8),
                            static_cast<uint8_t>(value & 0xFFu)};
  return bus_.write(address_, frame, 3) ? Status::Ok : Status::BusError;
}

Status Ina219::readRegister(uint8_t reg_address, uint16_t& value) {
  if (!bus_.write(address_, &reg_address, 1)) return Status::BusError;
  uint8_t data[2] = {0, 0};
  if (!bus_.read(address_, data, 2)) return Status::BusError;
  value = static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);
  return Status::Ok;
}

Status Ina219::begin(const Settings& settings) {
  configured_ = false;

  const reg::Calibration cal =
      reg::computeCalibration(settings.shunt_uohm, settings.max_expected_mA);
  if (cal.status != reg::CalStatus::Ok) return Status::BadCalibration;

  const uint16_t config =
      reg::buildConfig(settings.bus_range, cal.gain, settings.bus_adc, settings.shunt_adc,
                       reg::OperatingMode::ShuntBusContinuous);

  Status status = writeRegister(reg::kRegConfig, reg::kConfigReset);
  if (status != Status::Ok) return status;
  status = writeRegister(reg::kRegConfig, config);
  if (status != Status::Ok) return status;

  uint16_t readback = 0;
  status = readRegister(reg::kRegConfig, readback);
  if (status != Status::Ok) return status;
  if (readback != config) return Status::VerifyFailed;

  status = writeRegister(reg::kRegCalibration, cal.cal_register);
  if (status != Status::Ok) return status;

  calibration_ = cal;
  config_ = config;
  configured_ = true;
  return Status::Ok;
}

Status Ina219::read(Reading& out) {
  if (!configured_) return Status::NotConfigured;

  uint16_t bus = 0;
  uint16_t shunt = 0;
  uint16_t current = 0;
  uint16_t power = 0;
  Status status = readRegister(reg::kRegBusVoltage, bus);
  if (status != Status::Ok) return status;
  status = readRegister(reg::kRegShuntVoltage, shunt);
  if (status != Status::Ok) return status;
  status = readRegister(reg::kRegCurrent, current);
  if (status != Status::Ok) return status;
  status = readRegister(reg::kRegPower, power);
  if (status != Status::Ok) return status;

  out.bus_mV = reg::busVoltage_mV(bus);
  out.shunt_uV = reg::shuntVoltage_uV(shunt);
  out.current_uA = reg::current_uA(current, calibration_);
  out.power_uW = reg::power_uW(power, calibration_);
  out.ready = reg::conversionReady(bus);
  out.overflow = reg::mathOverflow(bus);
  return Status::Ok;
}

Status Ina219::calibrationIntact(bool& intact) {
  if (!configured_) return Status::NotConfigured;
  uint16_t value = 0;
  const Status status = readRegister(reg::kRegCalibration, value);
  if (status != Status::Ok) return status;
  intact = (value == calibration_.cal_register);
  return Status::Ok;
}

Status Ina219::powerDown() {
  if (!configured_) return Status::NotConfigured;
  const uint16_t value = static_cast<uint16_t>(config_ & ~0x0007u);
  return writeRegister(reg::kRegConfig, value);
}

}  // namespace ina219drv
