// Parser for the firmware's serial command set.
//
//   RESET                       clear the mAh/mWh counters and re-seed the SoC
//   RATE <ms>                   sample period, kMinRateMs..kMaxRateMs
//   CAL <shunt_mohm> <max_A>    shunt resistance in milliohms and largest
//                               expected current in amps; both accept up to
//                               three decimals, e.g. "CAL 100 3.2"
//
// Keywords are case-insensitive. The parser only checks syntax and the rate
// range; whether a CAL pair is usable is decided by ina219::computeCalibration.
#pragma once

#include <stdint.h>

namespace powercore {

const uint32_t kMinRateMs = 20UL;
const uint32_t kMaxRateMs = 60000UL;

enum class CommandType : uint8_t {
  None = 0,  // empty line
  Reset,
  Rate,
  Cal,
  Invalid,   // unknown keyword, missing/extra argument or value out of range
};

struct Command {
  CommandType type;
  uint32_t rate_ms;     // Rate
  uint32_t shunt_uohm;  // Cal
  uint32_t max_mA;      // Cal
};

// Parse an unsigned decimal with up to three fraction digits and return it
// multiplied by 1000 ("3.2" -> 3200, "100" -> 100000, ".5" -> 500). The whole
// part may not exceed 1 000 000. On success `p` is advanced past the number.
bool parseMilli(const char*& p, uint32_t& out);

Command parseCommand(const char* line);

}  // namespace powercore
