#include "powercore/command.h"

namespace powercore {

namespace {

bool isSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool isDigit(char c) {
  return c >= '0' && c <= '9';
}

char toUpper(char c) {
  return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

void skipSpaces(const char*& p) {
  while (isSpace(*p)) ++p;
}

// Match an upper-case keyword that must be followed by whitespace or the end.
bool matchKeyword(const char*& p, const char* keyword) {
  const char* q = p;
  while (*keyword != '\0') {
    if (toUpper(*q) != *keyword) return false;
    ++q;
    ++keyword;
  }
  if (*q != '\0' && !isSpace(*q)) return false;
  p = q;
  return true;
}

bool parseUint(const char*& p, uint32_t& out) {
  const char* q = p;
  uint32_t value = 0;
  uint8_t digits = 0;
  while (isDigit(*q)) {
    if (++digits > 9) return false;
    value = value * 10u + static_cast<uint32_t>(*q - '0');
    ++q;
  }
  if (digits == 0) return false;
  out = value;
  p = q;
  return true;
}

bool atEnd(const char* p) {
  skipSpaces(p);
  return *p == '\0';
}

}  // namespace

bool parseMilli(const char*& p, uint32_t& out) {
  const char* q = p;
  uint32_t whole = 0;
  uint8_t whole_digits = 0;
  while (isDigit(*q)) {
    if (whole > 100000UL) return false;
    whole = whole * 10u + static_cast<uint32_t>(*q - '0');
    ++whole_digits;
    ++q;
  }
  if (whole > 1000000UL) return false;

  uint32_t fraction = 0;
  uint8_t fraction_digits = 0;
  if (*q == '.') {
    ++q;
    while (isDigit(*q)) {
      if (fraction_digits >= 3) return false;
      fraction = fraction * 10u + static_cast<uint32_t>(*q - '0');
      ++fraction_digits;
      ++q;
    }
  }
  if (whole_digits == 0 && fraction_digits == 0) return false;
  for (uint8_t i = fraction_digits; i < 3; ++i) fraction *= 10u;

  out = whole * 1000UL + fraction;
  p = q;
  return true;
}

Command parseCommand(const char* line) {
  Command c;
  c.type = CommandType::Invalid;
  c.rate_ms = 0;
  c.shunt_uohm = 0;
  c.max_mA = 0;
  if (line == nullptr) return c;

  const char* p = line;
  skipSpaces(p);
  if (*p == '\0') {
    c.type = CommandType::None;
    return c;
  }

  if (matchKeyword(p, "RESET")) {
    if (atEnd(p)) c.type = CommandType::Reset;
    return c;
  }

  if (matchKeyword(p, "RATE")) {
    skipSpaces(p);
    uint32_t rate = 0;
    if (parseUint(p, rate) && atEnd(p) && rate >= kMinRateMs && rate <= kMaxRateMs) {
      c.type = CommandType::Rate;
      c.rate_ms = rate;
    }
    return c;
  }

  if (matchKeyword(p, "CAL")) {
    skipSpaces(p);
    uint32_t shunt_uohm = 0;  // milliohms * 1000
    if (!parseMilli(p, shunt_uohm) || !isSpace(*p)) return c;
    skipSpaces(p);
    uint32_t max_mA = 0;  // amps * 1000
    if (!parseMilli(p, max_mA) || !atEnd(p)) return c;
    if (shunt_uohm == 0 || max_mA == 0) return c;
    c.type = CommandType::Cal;
    c.shunt_uohm = shunt_uohm;
    c.max_mA = max_mA;
    return c;
  }

  return c;
}

}  // namespace powercore
