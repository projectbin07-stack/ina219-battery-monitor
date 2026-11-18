// The sample lines here are the same ones used by tools/tests/test_logger.py,
// so the firmware encoder and the Python decoder are checked against a common
// set of fixtures.
#include <string.h>

#include <string>

#include <unity.h>

#include <powercore/telemetry.h>

using namespace powercore;

void setUp() {}
void tearDown() {}

#define ASSERT_STATUS(expected, actual) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual))

static uint8_t crcOf(const char* text) {
  return crc8(reinterpret_cast<const uint8_t*>(text), strlen(text));
}

// Build "$<body>*HH" with a correct checksum, to test the parser stages that
// come after the CRC check.
static std::string sealed(const std::string& body) {
  static const char kHex[] = "0123456789ABCDEF";
  const uint8_t crc = crcOf(body.c_str());
  std::string line = "$" + body + "*";
  line += kHex[crc >> 4];
  line += kHex[crc & 0x0F];
  return line;
}

static TelemetryFrame typicalFrame() {
  TelemetryFrame f;
  f.seq = 42;
  f.t_ms = 21000;
  f.bus_mV = 3876;
  f.current_uA = 100000;
  f.power_uW = 386000;
  f.charge_uAh = 583;
  f.energy_uWh = 2261;
  f.soc_permille = 512;
  f.flags = 0;
  return f;
}

// CRC-8, polynomial 0x07, init 0x00 (the "CRC-8/SMBUS" parameters).
static void test_crc8_vectors() {
  TEST_ASSERT_EQUAL_HEX8(0x00, crc8(nullptr, 0));
  TEST_ASSERT_EQUAL_HEX8(0xF4, crcOf("123456789"));  // standard check value
  const uint8_t one = 0x01;
  TEST_ASSERT_EQUAL_HEX8(0x07, crc8(&one, 1));  // the polynomial itself
  // 0x80 worked by hand, register after each of the eight shifts:
  //   07 0E 1C 38 70 E0 C7 89
  const uint8_t top = 0x80;
  TEST_ASSERT_EQUAL_HEX8(0x89, crc8(&top, 1));
  const uint8_t zero = 0x00;
  TEST_ASSERT_EQUAL_HEX8(0x00, crc8(&zero, 1));
  const uint8_t ff = 0xFF;
  TEST_ASSERT_EQUAL_HEX8(0xF3, crc8(&ff, 1));
  TEST_ASSERT_EQUAL_HEX8(0xC0, crcOf("A"));
  TEST_ASSERT_EQUAL_HEX8(0x75, crcOf("BM1"));
}

static void test_crc8_can_be_chained() {
  const uint8_t first = crc8(reinterpret_cast<const uint8_t*>("1234"), 4);
  TEST_ASSERT_EQUAL_HEX8(0xF4, crc8(reinterpret_cast<const uint8_t*>("56789"), 5, first));
}

static void test_encode_typical_frame() {
  char buffer[kFrameBufferSize];
  const size_t n = encodeFrame(typicalFrame(), buffer, sizeof(buffer));
  TEST_ASSERT_EQUAL_STRING("$BM1,42,21000,3876,100000,386000,583,2261,512,0*D1", buffer);
  TEST_ASSERT_EQUAL_UINT(50, n);
}

static void test_encode_zero_frame() {
  TelemetryFrame f;
  memset(&f, 0, sizeof(f));
  char buffer[kFrameBufferSize];
  TEST_ASSERT_EQUAL_UINT(25, encodeFrame(f, buffer, sizeof(buffer)));
  TEST_ASSERT_EQUAL_STRING("$BM1,0,0,0,0,0,0,0,0,0*78", buffer);
}

static void test_encode_negative_values() {
  TelemetryFrame f = typicalFrame();
  f.seq = 43;
  f.t_ms = 21500;
  f.bus_mV = 3872;
  f.current_uA = -250300;
  f.power_uW = -969000;
  f.charge_uAh = 548;
  f.energy_uWh = 2127;
  f.soc_permille = 513;
  char buffer[kFrameBufferSize];
  encodeFrame(f, buffer, sizeof(buffer));
  TEST_ASSERT_EQUAL_STRING("$BM1,43,21500,3872,-250300,-969000,548,2127,513,0*3F", buffer);
}

static void test_encode_extreme_values_fit_the_buffer() {
  TelemetryFrame f;
  f.seq = 4294967295UL;
  f.t_ms = 4294967295UL;
  f.bus_mV = 2147483647L;
  f.current_uA = -2147483647L - 1L;
  f.power_uW = -2147483647L - 1L;
  f.charge_uAh = 2147483647L;
  f.energy_uWh = -2147483647L - 1L;
  f.soc_permille = 1000;
  f.flags = 255;
  char buffer[kFrameBufferSize];
  const size_t n = encodeFrame(f, buffer, sizeof(buffer));
  TEST_ASSERT_EQUAL_STRING(
      "$BM1,4294967295,4294967295,2147483647,-2147483648,-2147483648,2147483647,-2147483648,"
      "1000,255*6F",
      buffer);
  TEST_ASSERT_EQUAL_UINT(96, n);
  TEST_ASSERT_TRUE(n < kFrameBufferSize);
}

static void test_encode_respects_capacity() {
  const TelemetryFrame f = typicalFrame();  // encodes to 50 characters
  char buffer[64];

  memset(buffer, 'x', sizeof(buffer));
  TEST_ASSERT_EQUAL_UINT(50, encodeFrame(f, buffer, 51));  // exact fit incl. NUL
  TEST_ASSERT_EQUAL_CHAR('\0', buffer[50]);
  TEST_ASSERT_EQUAL_CHAR('x', buffer[51]);  // nothing written past the end

  memset(buffer, 'x', sizeof(buffer));
  TEST_ASSERT_EQUAL_UINT(0, encodeFrame(f, buffer, 50));  // one byte short
  TEST_ASSERT_EQUAL_CHAR('\0', buffer[0]);
  TEST_ASSERT_EQUAL_CHAR('x', buffer[50]);

  TEST_ASSERT_EQUAL_UINT(0, encodeFrame(f, buffer, 10));
  TEST_ASSERT_EQUAL_UINT(0, encodeFrame(f, buffer, 0));
  TEST_ASSERT_EQUAL_UINT(0, encodeFrame(f, nullptr, 64));
}

static void test_decode_typical_frame() {
  TelemetryFrame f;
  ASSERT_STATUS(DecodeStatus::Ok,
                decodeFrame("$BM1,44,22000,2990,1200000,3588000,715,2625,12,8*69", f));
  TEST_ASSERT_EQUAL_UINT32(44, f.seq);
  TEST_ASSERT_EQUAL_UINT32(22000, f.t_ms);
  TEST_ASSERT_EQUAL_INT32(2990, f.bus_mV);
  TEST_ASSERT_EQUAL_INT32(1200000, f.current_uA);
  TEST_ASSERT_EQUAL_INT32(3588000, f.power_uW);
  TEST_ASSERT_EQUAL_INT32(715, f.charge_uAh);
  TEST_ASSERT_EQUAL_INT32(2625, f.energy_uWh);
  TEST_ASSERT_EQUAL_UINT16(12, f.soc_permille);
  TEST_ASSERT_EQUAL_HEX8(kFlagCutoff, f.flags);
}

static void test_decode_accepts_line_endings_and_lowercase_hex() {
  TelemetryFrame f;
  ASSERT_STATUS(DecodeStatus::Ok, decodeFrame("$BM1,45,22500,0,0,0,715,2625,12,4*4E\r\n", f));
  TEST_ASSERT_EQUAL_HEX8(kFlagSensorError, f.flags);
  ASSERT_STATUS(DecodeStatus::Ok, decodeFrame("$BM1,45,22500,0,0,0,715,2625,12,4*4E\n", f));
  ASSERT_STATUS(DecodeStatus::Ok, decodeFrame("$BM1,45,22500,0,0,0,715,2625,12,4*4e", f));
}

static void test_round_trip() {
  TelemetryFrame in;
  in.seq = 4294967295UL;
  in.t_ms = 4294967295UL;
  in.bus_mV = 2147483647L;
  in.current_uA = -2147483647L - 1L;
  in.power_uW = -2147483647L - 1L;
  in.charge_uAh = 2147483647L;
  in.energy_uWh = -2147483647L - 1L;
  in.soc_permille = 1000;
  in.flags = 255;
  char buffer[kFrameBufferSize];
  TEST_ASSERT_TRUE(encodeFrame(in, buffer, sizeof(buffer)) > 0);
  TelemetryFrame out;
  memset(&out, 0, sizeof(out));
  ASSERT_STATUS(DecodeStatus::Ok, decodeFrame(buffer, out));
  TEST_ASSERT_EQUAL_UINT32(in.seq, out.seq);
  TEST_ASSERT_EQUAL_UINT32(in.t_ms, out.t_ms);
  TEST_ASSERT_EQUAL_INT32(in.bus_mV, out.bus_mV);
  TEST_ASSERT_EQUAL_INT32(in.current_uA, out.current_uA);
  TEST_ASSERT_EQUAL_INT32(in.power_uW, out.power_uW);
  TEST_ASSERT_EQUAL_INT32(in.charge_uAh, out.charge_uAh);
  TEST_ASSERT_EQUAL_INT32(in.energy_uWh, out.energy_uWh);
  TEST_ASSERT_EQUAL_UINT16(in.soc_permille, out.soc_permille);
  TEST_ASSERT_EQUAL_UINT8(in.flags, out.flags);
}

static void test_decode_rejects_framing_errors() {
  TelemetryFrame f = typicalFrame();
  ASSERT_STATUS(DecodeStatus::NoStart, decodeFrame(nullptr, f));
  ASSERT_STATUS(DecodeStatus::NoStart, decodeFrame("", f));
  ASSERT_STATUS(DecodeStatus::NoStart, decodeFrame("#OK RESET", f));
  ASSERT_STATUS(DecodeStatus::NoStart,
                decodeFrame("BM1,42,21000,3876,100000,386000,583,2261,512,0*D1", f));
  ASSERT_STATUS(DecodeStatus::NoChecksum,
                decodeFrame("$BM1,42,21000,3876,100000,386000,583,2261,512,0", f));
  ASSERT_STATUS(DecodeStatus::NoChecksum,
                decodeFrame("$BM1,42,21000,3876,100000,386000,583,2261,512,0*D", f));
  ASSERT_STATUS(DecodeStatus::NoChecksum,
                decodeFrame("$BM1,42,21000,3876,100000,386000,583,2261,512,0*G1", f));
  ASSERT_STATUS(DecodeStatus::NoChecksum,
                decodeFrame("$BM1,42,21000,3876,100000,386000,583,2261,512,0*D1junk", f));
  // A failed decode leaves the output untouched.
  TEST_ASSERT_EQUAL_UINT32(42, f.seq);
  TEST_ASSERT_EQUAL_INT32(3876, f.bus_mV);
}

static void test_decode_detects_corruption() {
  TelemetryFrame f;
  // Wrong checksum digits.
  ASSERT_STATUS(DecodeStatus::BadChecksum,
                decodeFrame("$BM1,42,21000,3876,100000,386000,583,2261,512,0*D2", f));
  // One payload digit changed (3876 -> 3877).
  ASSERT_STATUS(DecodeStatus::BadChecksum,
                decodeFrame("$BM1,42,21000,3877,100000,386000,583,2261,512,0*D1", f));
  // Two characters swapped.
  ASSERT_STATUS(DecodeStatus::BadChecksum,
                decodeFrame("$BM1,42,21000,3786,100000,386000,583,2261,512,0*D1", f));
  // Truncated in the middle but still ending in the old trailer.
  ASSERT_STATUS(DecodeStatus::BadChecksum, decodeFrame("$BM1,42,21000,3876*D1", f));
}

// Every single-bit error in the payload must be caught (CRC-8 guarantees it).
static void test_every_single_bit_flip_is_detected() {
  const char* good = "$BM1,42,21000,3876,100000,386000,583,2261,512,0*D1";
  const size_t star = strlen(good) - 3;
  TelemetryFrame f;
  for (size_t i = 1; i < star; ++i) {
    for (int bit = 0; bit < 7; ++bit) {
      std::string bad(good);
      bad[i] = static_cast<char>(bad[i] ^ (1 << bit));
      if (bad[i] == '\0') continue;  // would just terminate the string early
      TEST_ASSERT_TRUE(decodeFrame(bad.c_str(), f) != DecodeStatus::Ok);
    }
  }
}

static void test_decode_rejects_bad_structure_with_valid_crc() {
  TelemetryFrame f;
  ASSERT_STATUS(DecodeStatus::BadTag, decodeFrame(sealed("BM2,0,0,0,0,0,0,0,0,0").c_str(), f));
  ASSERT_STATUS(DecodeStatus::BadTag, decodeFrame(sealed("BM10,0,0,0,0,0,0,0,0").c_str(), f));
  ASSERT_STATUS(DecodeStatus::BadTag, decodeFrame(sealed("").c_str(), f));
  ASSERT_STATUS(DecodeStatus::BadFieldCount, decodeFrame(sealed("BM1").c_str(), f));
  ASSERT_STATUS(DecodeStatus::BadFieldCount, decodeFrame(sealed("BM1,1,2,3").c_str(), f));
  ASSERT_STATUS(DecodeStatus::BadFieldCount, decodeFrame(sealed("BM1,1,2,3,").c_str(), f));
  ASSERT_STATUS(DecodeStatus::BadFieldCount,
                decodeFrame(sealed("BM1,0,0,0,0,0,0,0,0").c_str(), f));  // 8 fields
  ASSERT_STATUS(DecodeStatus::BadFieldCount,
                decodeFrame(sealed("BM1,0,0,0,0,0,0,0,0,0,0").c_str(), f));  // 10 fields
  // Sanity check of the helper itself.
  ASSERT_STATUS(DecodeStatus::Ok, decodeFrame(sealed("BM1,0,0,0,0,0,0,0,0,0").c_str(), f));
}

static void test_decode_rejects_bad_numbers_with_valid_crc() {
  TelemetryFrame f;
  ASSERT_STATUS(DecodeStatus::BadNumber, decodeFrame(sealed("BM1,x,0,0,0,0,0,0,0,0").c_str(), f));
  ASSERT_STATUS(DecodeStatus::BadNumber, decodeFrame(sealed("BM1,1,,0,0,0,0,0,0,0").c_str(), f));
  ASSERT_STATUS(DecodeStatus::BadNumber,
                decodeFrame(sealed("BM1,0,0,3.8,0,0,0,0,0,0").c_str(), f));
  ASSERT_STATUS(DecodeStatus::BadNumber, decodeFrame(sealed("BM1,0,0,-,0,0,0,0,0,0").c_str(), f));
  ASSERT_STATUS(DecodeStatus::BadNumber, decodeFrame(sealed("BM1,0,0,0,0,0,0,0,0,0 ").c_str(), f));
  // Range checks: seq is unsigned, int32 fields, soc <= 1000, flags <= 255.
  ASSERT_STATUS(DecodeStatus::BadNumber, decodeFrame(sealed("BM1,-1,0,0,0,0,0,0,0,0").c_str(), f));
  ASSERT_STATUS(DecodeStatus::BadNumber,
                decodeFrame(sealed("BM1,4294967296,0,0,0,0,0,0,0,0").c_str(), f));
  ASSERT_STATUS(DecodeStatus::BadNumber,
                decodeFrame(sealed("BM1,0,0,2147483648,0,0,0,0,0,0").c_str(), f));
  ASSERT_STATUS(DecodeStatus::BadNumber,
                decodeFrame(sealed("BM1,0,0,0,-2147483649,0,0,0,0,0").c_str(), f));
  ASSERT_STATUS(DecodeStatus::BadNumber,
                decodeFrame(sealed("BM1,0,0,0,0,0,0,0,1001,0").c_str(), f));
  ASSERT_STATUS(DecodeStatus::BadNumber,
                decodeFrame(sealed("BM1,0,0,0,0,0,0,0,0,256").c_str(), f));
  ASSERT_STATUS(DecodeStatus::BadNumber,
                decodeFrame(sealed("BM1,12345678901,0,0,0,0,0,0,0,0").c_str(), f));  // 11 digits
  // Boundary values that must be accepted.
  ASSERT_STATUS(DecodeStatus::Ok,
                decodeFrame(sealed("BM1,4294967295,0,0,-2147483648,0,0,0,1000,255").c_str(), f));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_crc8_vectors);
  RUN_TEST(test_crc8_can_be_chained);
  RUN_TEST(test_encode_typical_frame);
  RUN_TEST(test_encode_zero_frame);
  RUN_TEST(test_encode_negative_values);
  RUN_TEST(test_encode_extreme_values_fit_the_buffer);
  RUN_TEST(test_encode_respects_capacity);
  RUN_TEST(test_decode_typical_frame);
  RUN_TEST(test_decode_accepts_line_endings_and_lowercase_hex);
  RUN_TEST(test_round_trip);
  RUN_TEST(test_decode_rejects_framing_errors);
  RUN_TEST(test_decode_detects_corruption);
  RUN_TEST(test_every_single_bit_flip_is_detected);
  RUN_TEST(test_decode_rejects_bad_structure_with_valid_crc);
  RUN_TEST(test_decode_rejects_bad_numbers_with_valid_crc);
  return UNITY_END();
}
