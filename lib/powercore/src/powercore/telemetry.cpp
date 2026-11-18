#include "powercore/telemetry.h"

namespace powercore {

namespace {

bool putChar(char*& p, const char* end, char c) {
  if (p >= end) return false;
  *p++ = c;
  return true;
}

bool putU32(char*& p, const char* end, uint32_t v) {
  char digits[10];
  uint8_t n = 0;
  do {
    digits[n++] = static_cast<char>('0' + (v % 10u));
    v /= 10u;
  } while (v != 0);
  while (n != 0) {
    if (!putChar(p, end, digits[--n])) return false;
  }
  return true;
}

bool putI32(char*& p, const char* end, int32_t v) {
  uint32_t magnitude = static_cast<uint32_t>(v);
  if (v < 0) {
    if (!putChar(p, end, '-')) return false;
    magnitude = 0u - magnitude;  // well defined for INT32_MIN too
  }
  return putU32(p, end, magnitude);
}

char hexDigit(uint8_t nibble) {
  return static_cast<char>(nibble < 10 ? '0' + nibble : 'A' + (nibble - 10));
}

int8_t hexValue(char c) {
  if (c >= '0' && c <= '9') return static_cast<int8_t>(c - '0');
  if (c >= 'A' && c <= 'F') return static_cast<int8_t>(c - 'A' + 10);
  if (c >= 'a' && c <= 'f') return static_cast<int8_t>(c - 'a' + 10);
  return -1;
}

// Parse a decimal integer in [p, end) that must be terminated by `terminator`
// (or by `end` itself when `last` is set). Advances p past the terminator.
DecodeStatus parseField(const char*& p, const char* end, bool last, int64_t low, int64_t high,
                        int64_t& value) {
  bool negative = false;
  if (p < end && *p == '-') {
    negative = true;
    ++p;
  }
  int64_t magnitude = 0;
  uint8_t digits = 0;
  while (p < end && *p >= '0' && *p <= '9') {
    if (++digits > 10) return DecodeStatus::BadNumber;
    magnitude = magnitude * 10 + (*p - '0');
    ++p;
  }
  if (digits == 0) {
    // Nothing numeric here: either the frame ran out of fields or it is junk.
    return p >= end ? DecodeStatus::BadFieldCount : DecodeStatus::BadNumber;
  }
  if (last) {
    if (p != end) return *p == ',' ? DecodeStatus::BadFieldCount : DecodeStatus::BadNumber;
  } else {
    if (p >= end) return DecodeStatus::BadFieldCount;
    if (*p != ',') return DecodeStatus::BadNumber;
    ++p;
  }
  value = negative ? -magnitude : magnitude;
  if (value < low || value > high) return DecodeStatus::BadNumber;
  return DecodeStatus::Ok;
}

const int64_t kU32Max = 4294967295LL;
const int64_t kI32Max = 2147483647LL;
const int64_t kI32Min = -2147483647LL - 1LL;

}  // namespace

uint8_t crc8(const uint8_t* data, size_t length, uint8_t crc) {
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80u) ? static_cast<uint8_t>((crc << 1) ^ 0x07u)
                          : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}

size_t encodeFrame(const TelemetryFrame& f, char* out, size_t capacity) {
  if (out == nullptr || capacity == 0) return 0;
  char* p = out;
  const char* end = out + capacity - 1;  // keep one byte for the NUL

  const bool ok = putChar(p, end, '$') && putChar(p, end, 'B') && putChar(p, end, 'M') &&
                  putChar(p, end, '1') && putChar(p, end, ',') && putU32(p, end, f.seq) &&
                  putChar(p, end, ',') && putU32(p, end, f.t_ms) && putChar(p, end, ',') &&
                  putI32(p, end, f.bus_mV) && putChar(p, end, ',') &&
                  putI32(p, end, f.current_uA) && putChar(p, end, ',') &&
                  putI32(p, end, f.power_uW) && putChar(p, end, ',') &&
                  putI32(p, end, f.charge_uAh) && putChar(p, end, ',') &&
                  putI32(p, end, f.energy_uWh) && putChar(p, end, ',') &&
                  putU32(p, end, f.soc_permille) && putChar(p, end, ',') &&
                  putU32(p, end, f.flags);
  if (!ok || end - p < 3) {
    out[0] = '\0';
    return 0;
  }
  const uint8_t crc =
      crc8(reinterpret_cast<const uint8_t*>(out + 1), static_cast<size_t>(p - (out + 1)));
  *p++ = '*';
  *p++ = hexDigit(static_cast<uint8_t>(crc >> 4));
  *p++ = hexDigit(static_cast<uint8_t>(crc & 0x0Fu));
  *p = '\0';
  return static_cast<size_t>(p - out);
}

DecodeStatus decodeFrame(const char* line, TelemetryFrame& out) {
  if (line == nullptr || line[0] != '$') return DecodeStatus::NoStart;
  const char* body = line + 1;
  const char* star = body;
  while (*star != '\0' && *star != '*') ++star;
  if (*star != '*') return DecodeStatus::NoChecksum;

  const int8_t hi = hexValue(star[1]);
  if (hi < 0) return DecodeStatus::NoChecksum;
  const int8_t lo = hexValue(star[2]);
  if (lo < 0) return DecodeStatus::NoChecksum;
  const char* tail = star + 3;
  if (*tail == '\r') ++tail;
  if (*tail == '\n') ++tail;
  if (*tail != '\0') return DecodeStatus::NoChecksum;

  const uint8_t expected = static_cast<uint8_t>((hi << 4) | lo);
  const uint8_t actual =
      crc8(reinterpret_cast<const uint8_t*>(body), static_cast<size_t>(star - body));
  if (expected != actual) return DecodeStatus::BadChecksum;

  if (star - body < 3 || body[0] != 'B' || body[1] != 'M' || body[2] != '1') {
    return DecodeStatus::BadTag;
  }
  if (star - body == 3) return DecodeStatus::BadFieldCount;
  if (body[3] != ',') return DecodeStatus::BadTag;

  const char* p = body + 4;
  int64_t v[9];
  const int64_t low[9] = {0, 0, kI32Min, kI32Min, kI32Min, kI32Min, kI32Min, 0, 0};
  const int64_t high[9] = {kU32Max, kU32Max, kI32Max, kI32Max, kI32Max, kI32Max, kI32Max, 1000, 255};
  for (uint8_t i = 0; i < 9; ++i) {
    const DecodeStatus status = parseField(p, star, i == 8, low[i], high[i], v[i]);
    if (status != DecodeStatus::Ok) return status;
  }

  out.seq = static_cast<uint32_t>(v[0]);
  out.t_ms = static_cast<uint32_t>(v[1]);
  out.bus_mV = static_cast<int32_t>(v[2]);
  out.current_uA = static_cast<int32_t>(v[3]);
  out.power_uW = static_cast<int32_t>(v[4]);
  out.charge_uAh = static_cast<int32_t>(v[5]);
  out.energy_uWh = static_cast<int32_t>(v[6]);
  out.soc_permille = static_cast<uint16_t>(v[7]);
  out.flags = static_cast<uint8_t>(v[8]);
  return DecodeStatus::Ok;
}

}  // namespace powercore
