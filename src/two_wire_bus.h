// Adapter from the driver's abstract I2C interface to Arduino's TwoWire.
#pragma once

#include <Arduino.h>
#include <Wire.h>

#include <ina219/driver.h>

class TwoWireBus final : public ina219drv::I2CBus {
 public:
  explicit TwoWireBus(TwoWire& wire) : wire_(wire) {}

  bool write(uint8_t address, const uint8_t* data, uint8_t length) override {
    wire_.beginTransmission(address);
    const size_t queued = wire_.write(data, static_cast<size_t>(length));
    const uint8_t result = wire_.endTransmission();
    return queued == length && result == 0;
  }

  bool read(uint8_t address, uint8_t* data, uint8_t length) override {
    const uint8_t received = wire_.requestFrom(address, length);
    if (received != length) {
      while (wire_.available() > 0) (void)wire_.read();
      return false;
    }
    for (uint8_t i = 0; i < length; ++i) {
      data[i] = static_cast<uint8_t>(wire_.read());
    }
    return true;
  }

 private:
  TwoWire& wire_;
};
