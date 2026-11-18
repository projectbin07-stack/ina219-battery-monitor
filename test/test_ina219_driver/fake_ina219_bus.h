// A register-level model of an INA219 behind the driver's I2CBus interface.
//
// It implements the parts of the device the driver relies on: the register
// pointer, big-endian 16-bit registers, the reset bit, the read-only result
// registers, the unwritable calibration bit 0, and CNVR clearing when the
// power register is read. Every transaction is logged so tests can assert on
// the exact bus traffic.
#pragma once

#include <stdint.h>

#include <vector>

#include <ina219/driver.h>

class FakeIna219Bus final : public ina219drv::I2CBus {
 public:
  struct RegisterWrite {
    uint8_t reg;
    uint16_t value;
  };

  uint8_t device_address = 0x40;
  uint16_t regs[6] = {0x399F, 0, 0, 0, 0, 0};
  uint8_t pointer = 0;

  // Number of further transactions that succeed; -1 = never fail.
  int transactions_until_failure = -1;
  // When set, writes to the configuration register are acknowledged but ignored.
  bool config_stuck = false;

  int transactions = 0;
  int resets = 0;
  std::vector<RegisterWrite> writes;        // 3-byte register writes
  std::vector<uint8_t> reads;               // register read by each read()
  std::vector<std::vector<uint8_t>> raw_writes;  // every write(), byte for byte

  void clearLog() {
    transactions = 0;
    writes.clear();
    reads.clear();
    raw_writes.clear();
  }

  bool write(uint8_t address, const uint8_t* data, uint8_t length) override {
    if (!beginTransaction(address)) return false;
    raw_writes.emplace_back(data, data + length);
    if (length != 1 && length != 3) return false;
    if (data[0] > 5) return false;
    pointer = data[0];
    if (length == 3) {
      const uint16_t value = static_cast<uint16_t>((data[1] << 8) | data[2]);
      writes.push_back({pointer, value});
      if (pointer == 0) {
        if (value & 0x8000u) {
          ++resets;
          regs[0] = 0x399F;
          regs[5] = 0;
        } else if (!config_stuck) {
          regs[0] = value;
        }
      } else if (pointer == 5) {
        regs[5] = static_cast<uint16_t>(value & 0xFFFEu);
      }
      // Registers 1..4 are read-only: the write is acknowledged and ignored.
    }
    return true;
  }

  bool read(uint8_t address, uint8_t* data, uint8_t length) override {
    if (!beginTransaction(address)) return false;
    if (length != 2) return false;
    reads.push_back(pointer);
    data[0] = static_cast<uint8_t>(regs[pointer] >> 8);
    data[1] = static_cast<uint8_t>(regs[pointer] & 0xFFu);
    if (pointer == 3) regs[2] = static_cast<uint16_t>(regs[2] & ~0x0002u);  // CNVR clears
    return true;
  }

 private:
  bool beginTransaction(uint8_t address) {
    ++transactions;
    if (address != device_address) return false;  // nobody acknowledges
    if (transactions_until_failure == 0) return false;
    if (transactions_until_failure > 0) --transactions_until_failure;
    return true;
  }
};
