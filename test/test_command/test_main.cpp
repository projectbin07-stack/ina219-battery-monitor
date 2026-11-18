#include <unity.h>

#include <powercore/command.h>

using namespace powercore;

void setUp() {}
void tearDown() {}

#define ASSERT_TYPE(expected, line) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(parseCommand(line).type))

static void test_empty_lines() {
  ASSERT_TYPE(CommandType::None, "");
  ASSERT_TYPE(CommandType::None, "   ");
  ASSERT_TYPE(CommandType::None, "\r\n");
  ASSERT_TYPE(CommandType::Invalid, nullptr);
}

static void test_reset() {
  ASSERT_TYPE(CommandType::Reset, "RESET");
  ASSERT_TYPE(CommandType::Reset, "reset");
  ASSERT_TYPE(CommandType::Reset, "  Reset \r\n");
  ASSERT_TYPE(CommandType::Invalid, "RESET now");
  ASSERT_TYPE(CommandType::Invalid, "RESETS");
  ASSERT_TYPE(CommandType::Invalid, "RESE");
}

static void test_rate() {
  Command c = parseCommand("RATE 100");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::Rate), static_cast<int>(c.type));
  TEST_ASSERT_EQUAL_UINT32(100, c.rate_ms);

  c = parseCommand("rate\t250\r\n");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::Rate), static_cast<int>(c.type));
  TEST_ASSERT_EQUAL_UINT32(250, c.rate_ms);

  TEST_ASSERT_EQUAL_UINT32(20, parseCommand("RATE 20").rate_ms);
  ASSERT_TYPE(CommandType::Rate, "RATE 20");
  TEST_ASSERT_EQUAL_UINT32(60000, parseCommand("RATE 60000").rate_ms);
  ASSERT_TYPE(CommandType::Rate, "RATE 60000");
}

static void test_rate_rejects() {
  ASSERT_TYPE(CommandType::Invalid, "RATE");
  ASSERT_TYPE(CommandType::Invalid, "RATE 19");     // below kMinRateMs
  ASSERT_TYPE(CommandType::Invalid, "RATE 60001");  // above kMaxRateMs
  ASSERT_TYPE(CommandType::Invalid, "RATE 0");
  ASSERT_TYPE(CommandType::Invalid, "RATE -5");
  ASSERT_TYPE(CommandType::Invalid, "RATE 1x");
  ASSERT_TYPE(CommandType::Invalid, "RATE 100 200");
  ASSERT_TYPE(CommandType::Invalid, "RATE 10.5");
  ASSERT_TYPE(CommandType::Invalid, "RATE100");
  ASSERT_TYPE(CommandType::Invalid, "RATE 99999999999999");
  // A rejected command carries no stale value.
  TEST_ASSERT_EQUAL_UINT32(0, parseCommand("RATE 19").rate_ms);
}

static void test_cal() {
  // 100 mOhm, 3.2 A
  Command c = parseCommand("CAL 100 3.2");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::Cal), static_cast<int>(c.type));
  TEST_ASSERT_EQUAL_UINT32(100000, c.shunt_uohm);
  TEST_ASSERT_EQUAL_UINT32(3200, c.max_mA);

  // 2.5 mOhm, 10 A
  c = parseCommand("cal 2.5 10");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::Cal), static_cast<int>(c.type));
  TEST_ASSERT_EQUAL_UINT32(2500, c.shunt_uohm);
  TEST_ASSERT_EQUAL_UINT32(10000, c.max_mA);

  // 0.5 mOhm, 0.25 A, extra whitespace
  c = parseCommand("  Cal   0.5   .25  \r\n");
  TEST_ASSERT_EQUAL_INT(static_cast<int>(CommandType::Cal), static_cast<int>(c.type));
  TEST_ASSERT_EQUAL_UINT32(500, c.shunt_uohm);
  TEST_ASSERT_EQUAL_UINT32(250, c.max_mA);
}

static void test_cal_rejects() {
  ASSERT_TYPE(CommandType::Invalid, "CAL");
  ASSERT_TYPE(CommandType::Invalid, "CAL 100");
  ASSERT_TYPE(CommandType::Invalid, "CAL 100 3.2 7");
  ASSERT_TYPE(CommandType::Invalid, "CAL 0 1");
  ASSERT_TYPE(CommandType::Invalid, "CAL 100 0");
  ASSERT_TYPE(CommandType::Invalid, "CAL 100 3.2345");  // more than three decimals
  ASSERT_TYPE(CommandType::Invalid, "CAL 100,3.2");
  ASSERT_TYPE(CommandType::Invalid, "CAL -100 3.2");
  ASSERT_TYPE(CommandType::Invalid, "CAL 100 3.2A");
  ASSERT_TYPE(CommandType::Invalid, "CALX 100 3.2");
  ASSERT_TYPE(CommandType::Invalid, "CAL 1.2.3 1");
  TEST_ASSERT_EQUAL_UINT32(0, parseCommand("CAL 100").shunt_uohm);
}

static void test_unknown_keywords() {
  ASSERT_TYPE(CommandType::Invalid, "HELLO");
  ASSERT_TYPE(CommandType::Invalid, "$BM1,0,0,0,0,0,0,0,0,0*78");
  ASSERT_TYPE(CommandType::Invalid, "R");
}

static bool milli(const char* text, uint32_t& out) {
  const char* p = text;
  return parseMilli(p, out);
}

static void test_parse_milli() {
  uint32_t v = 0;
  TEST_ASSERT_TRUE(milli("3.2", v));
  TEST_ASSERT_EQUAL_UINT32(3200, v);
  TEST_ASSERT_TRUE(milli("100", v));
  TEST_ASSERT_EQUAL_UINT32(100000, v);
  TEST_ASSERT_TRUE(milli(".5", v));
  TEST_ASSERT_EQUAL_UINT32(500, v);
  TEST_ASSERT_TRUE(milli("0.001", v));
  TEST_ASSERT_EQUAL_UINT32(1, v);
  TEST_ASSERT_TRUE(milli("12.34", v));
  TEST_ASSERT_EQUAL_UINT32(12340, v);
  TEST_ASSERT_TRUE(milli("7.", v));
  TEST_ASSERT_EQUAL_UINT32(7000, v);
  TEST_ASSERT_TRUE(milli("1000000", v));
  TEST_ASSERT_EQUAL_UINT32(1000000000UL, v);

  v = 77;
  TEST_ASSERT_FALSE(milli("1000001", v));
  TEST_ASSERT_FALSE(milli("99999999999", v));
  TEST_ASSERT_FALSE(milli(".", v));
  TEST_ASSERT_FALSE(milli("", v));
  TEST_ASSERT_FALSE(milli("abc", v));
  TEST_ASSERT_FALSE(milli("1.2345", v));
  TEST_ASSERT_EQUAL_UINT32(77, v);  // untouched on failure
}

static void test_parse_milli_advances_pointer() {
  const char* text = "2.5 10";
  const char* p = text;
  uint32_t v = 0;
  TEST_ASSERT_TRUE(parseMilli(p, v));
  TEST_ASSERT_EQUAL_UINT32(2500, v);
  TEST_ASSERT_EQUAL_PTR(text + 3, p);

  const char* bad = "x1";
  p = bad;
  TEST_ASSERT_FALSE(parseMilli(p, v));
  TEST_ASSERT_EQUAL_PTR(bad, p);  // not advanced on failure
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_empty_lines);
  RUN_TEST(test_reset);
  RUN_TEST(test_rate);
  RUN_TEST(test_rate_rejects);
  RUN_TEST(test_cal);
  RUN_TEST(test_cal_rejects);
  RUN_TEST(test_unknown_keywords);
  RUN_TEST(test_parse_milli);
  RUN_TEST(test_parse_milli_advances_pointer);
  return UNITY_END();
}
