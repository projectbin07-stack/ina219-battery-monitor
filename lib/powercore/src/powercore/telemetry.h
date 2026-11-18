// Line-oriented telemetry frame: CSV fields protected by a CRC-8.
//
//   $BM1,<seq>,<t_ms>,<bus_mV>,<current_uA>,<power_uW>,<charge_uAh>,<energy_uWh>,<soc_permille>,<flags>*<HH>
//
// All fields are decimal integers (no floats, so the output is identical on
// every platform). <HH> is the CRC-8 of every character between '$' and '*',
// as two upper-case hex digits. The encoder does not append a line terminator.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace powercore {

const uint8_t kFlagOverflow = 0x01;     // INA219 math overflow (OVF) on this sample
const uint8_t kFlagNotReady = 0x02;     // conversion-ready (CNVR) was not set
const uint8_t kFlagSensorError = 0x04;  // I2C failure; measurement fields are zero
const uint8_t kFlagCutoff = 0x08;       // low-voltage cutoff is tripped

struct TelemetryFrame {
  uint32_t seq;           // increments by one per frame
  uint32_t t_ms;          // device uptime, wraps at 2^32
  int32_t bus_mV;
  int32_t current_uA;     // positive = discharge
  int32_t power_uW;       // sign follows the current
  int32_t charge_uAh;     // net charge drawn since reset
  int32_t energy_uWh;     // net energy drawn since reset
  uint16_t soc_permille;  // 0..1000
  uint8_t flags;          // kFlag* bits
};

// Longest possible frame is 99 characters; this leaves room for the NUL.
const size_t kFrameBufferSize = 104;

// CRC-8, polynomial 0x07 (x^8 + x^2 + x + 1), MSB first, initial value `crc`,
// no reflection, no final XOR. crc8("123456789") == 0xF4.
uint8_t crc8(const uint8_t* data, size_t length, uint8_t crc = 0x00);

// Write the NUL-terminated frame into `out`. Returns the number of characters
// written (excluding the NUL), or 0 if `capacity` is too small.
size_t encodeFrame(const TelemetryFrame& frame, char* out, size_t capacity);

enum class DecodeStatus : uint8_t {
  Ok = 0,
  NoStart,        // line does not begin with '$'
  NoChecksum,     // no "*HH" trailer, or garbage after it
  BadChecksum,    // CRC mismatch
  BadTag,         // not a "BM1" frame
  BadFieldCount,  // too few or too many fields
  BadNumber,      // a field is not a decimal integer or is out of range
};

// Parse one NUL-terminated line (a trailing CR and/or LF is accepted).
// `out` is only written when the result is Ok.
DecodeStatus decodeFrame(const char* line, TelemetryFrame& out);

}  // namespace powercore
