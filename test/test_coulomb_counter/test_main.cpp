// Units: charge in uA*ms, energy in uW*ms. 1 uAh = 3 600 000 uA*ms.
#include <unity.h>

#include <powercore/coulomb_counter.h>

using powercore::CoulombCounter;

void setUp() {}
void tearDown() {}

static void test_first_sample_only_seeds() {
  CoulombCounter c;
  TEST_ASSERT_FALSE(c.seeded());
  TEST_ASSERT_FALSE(c.update(1000, 500000, 1850000));
  TEST_ASSERT_TRUE(c.seeded());
  TEST_ASSERT_TRUE(c.charge_uAms() == 0);
  TEST_ASSERT_TRUE(c.energy_uWms() == 0);
  TEST_ASSERT_EQUAL_UINT16(0, c.gapCount());
}

// 1 A for one hour at 1 Hz must give exactly 1000 mAh; at 3.7 W, 3700 mWh.
static void test_constant_one_amp_for_one_hour() {
  CoulombCounter c;
  for (uint32_t s = 0; s <= 3600; ++s) {
    c.update(s * 1000UL, 1000000L, 3700000L);
  }
  TEST_ASSERT_TRUE(c.charge_uAms() == 3600000000000LL);
  TEST_ASSERT_EQUAL_INT32(1000000L, c.charge_uAh());
  TEST_ASSERT_EQUAL_INT32(3700000L, c.energy_uWh());
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 1000.0f, c.charge_mAh());
  TEST_ASSERT_FLOAT_WITHIN(0.05f, 3700.0f, c.energy_mWh());
}

// Two samples: 100 mA then 300 mA, 500 ms apart.
//   area = (100000 + 300000) / 2 * 500 = 1e8 uA*ms = 27.78 uAh = 0.027778 mAh
static void test_single_trapezoid() {
  CoulombCounter c;
  c.update(0, 100000L, 0);
  TEST_ASSERT_TRUE(c.update(500, 300000L, 0));
  TEST_ASSERT_TRUE(c.charge_uAms() == 100000000LL);
  TEST_ASSERT_EQUAL_INT32(27, c.charge_uAh());
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0277778f, c.charge_mAh());
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0277778f, c.lastDelta_mAh());
}

// The trapezoidal rule is exact for a linear ramp: 0 -> 2 A over 1 s in ten
// 100 ms steps has a mean of 1 A, so the area is 1e6 uA * 1000 ms = 1e9.
static void test_linear_ramp_is_exact() {
  CoulombCounter c;
  for (int k = 0; k <= 10; ++k) {
    c.update(static_cast<uint32_t>(k) * 100UL, static_cast<int32_t>(k) * 200000L, 0);
  }
  TEST_ASSERT_TRUE(c.charge_uAms() == 1000000000LL);
}

// Odd (a + b) * dt products must not lose the half unit over time:
//   (1 + 2) * 1 / 2 = 1.5, twice = 3 -> reported as 1 then 3.
static void test_half_units_are_kept() {
  CoulombCounter c;
  c.update(0, 1, 1);
  c.update(1, 2, 2);
  TEST_ASSERT_TRUE(c.charge_uAms() == 1);
  c.update(2, 1, 1);
  TEST_ASSERT_TRUE(c.charge_uAms() == 3);
  TEST_ASSERT_TRUE(c.energy_uWms() == 3);
}

// Charging (negative current) takes charge back out again.
static void test_negative_current_subtracts() {
  CoulombCounter c;
  c.update(0, 500000L, 2000000L);
  c.update(1000, 500000L, 2000000L);    // +5e8
  TEST_ASSERT_TRUE(c.charge_uAms() == 500000000LL);
  c.update(2000, -500000L, -2000000L);  // crosses zero: (+0.5 - 0.5) / 2 = 0
  TEST_ASSERT_TRUE(c.charge_uAms() == 500000000LL);
  TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, c.lastDelta_mAh());
  c.update(3000, -500000L, -2000000L);  // -5e8
  TEST_ASSERT_TRUE(c.charge_uAms() == 0);
  TEST_ASSERT_TRUE(c.energy_uWms() == 0);
  c.update(4000, -500000L, -2000000L);
  TEST_ASSERT_TRUE(c.charge_uAms() == -500000000LL);
  TEST_ASSERT_EQUAL_INT32(-138, c.charge_uAh());  // -138.9 truncates towards zero
}

// millis() wraps from 0xFFFFFFFF to 0. 0xFFFFFF06 is 250 ms before the wrap,
// so the next sample at t = 250 is 500 ms later.
static void test_millis_wraparound() {
  CoulombCounter c;
  c.update(0xFFFFFF06UL, 1000000L, 0);
  TEST_ASSERT_TRUE(c.update(250UL, 1000000L, 0));
  TEST_ASSERT_TRUE(c.charge_uAms() == 500000000LL);
  TEST_ASSERT_EQUAL_UINT16(0, c.gapCount());
  // And it keeps going normally afterwards.
  TEST_ASSERT_TRUE(c.update(750UL, 1000000L, 0));
  TEST_ASSERT_TRUE(c.charge_uAms() == 1000000000LL);
}

static void test_wraparound_exactly_at_boundary() {
  CoulombCounter c;
  c.update(0xFFFFFFFFUL, 2000000L, 0);
  TEST_ASSERT_TRUE(c.update(0UL, 2000000L, 0));  // 1 ms later
  TEST_ASSERT_TRUE(c.charge_uAms() == 2000000LL);
}

static void test_long_gap_is_skipped_and_reseeds() {
  CoulombCounter c(5000);
  c.update(0, 1000000L, 0);
  TEST_ASSERT_FALSE(c.update(10000, 1000000L, 0));  // 10 s > 5 s
  TEST_ASSERT_TRUE(c.charge_uAms() == 0);
  TEST_ASSERT_EQUAL_UINT16(1, c.gapCount());
  TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, c.lastDelta_mAh());
  // The skipped sample became the new starting point.
  TEST_ASSERT_TRUE(c.update(10500, 1000000L, 0));
  TEST_ASSERT_TRUE(c.charge_uAms() == 500000000LL);
  // An interval of exactly the limit is still integrated.
  TEST_ASSERT_TRUE(c.update(15500, 1000000L, 0));
  TEST_ASSERT_TRUE(c.charge_uAms() == 5500000000LL);
}

// A clock that steps backwards looks like a ~49 day interval and is a gap.
static void test_backwards_time_is_a_gap() {
  CoulombCounter c;
  c.update(5000, 1000000L, 0);
  TEST_ASSERT_FALSE(c.update(4000, 1000000L, 0));
  TEST_ASSERT_EQUAL_UINT16(1, c.gapCount());
  TEST_ASSERT_TRUE(c.charge_uAms() == 0);
  TEST_ASSERT_TRUE(c.update(4100, 1000000L, 0));
  TEST_ASSERT_TRUE(c.charge_uAms() == 100000000LL);
}

static void test_max_gap_is_adjustable() {
  CoulombCounter c;
  TEST_ASSERT_EQUAL_UINT32(5000, c.maxGap());
  c.setMaxGap(240000UL);
  c.update(0, 100000L, 0);
  TEST_ASSERT_TRUE(c.update(60000UL, 100000L, 0));  // one sample a minute
  // 100 mA for 60 s = 6e9 uA*ms = 1666.67 uAh
  TEST_ASSERT_EQUAL_INT32(1666, c.charge_uAh());
}

static void test_zero_interval_adds_nothing() {
  CoulombCounter c;
  c.update(100, 1000000L, 0);
  TEST_ASSERT_TRUE(c.update(100, 3000000L, 0));
  TEST_ASSERT_TRUE(c.charge_uAms() == 0);
}

static void test_reset() {
  CoulombCounter c(1000);
  c.update(0, 1000000L, 1000000L);
  c.update(500, 1000000L, 1000000L);
  c.update(5000, 1000000L, 1000000L);  // gap
  TEST_ASSERT_EQUAL_UINT16(1, c.gapCount());
  c.reset();
  TEST_ASSERT_FALSE(c.seeded());
  TEST_ASSERT_TRUE(c.charge_uAms() == 0);
  TEST_ASSERT_TRUE(c.energy_uWms() == 0);
  TEST_ASSERT_EQUAL_UINT16(0, c.gapCount());
  TEST_ASSERT_EQUAL_UINT32(1000, c.maxGap());  // configuration survives
  // The first sample after a reset seeds again, whatever its timestamp.
  TEST_ASSERT_FALSE(c.update(5100, 1000000L, 1000000L));
  TEST_ASSERT_TRUE(c.charge_uAms() == 0);
}

// 1000 A for 1000 hours is 1e12 uAh, far outside int32: the integer views
// clamp while the 64-bit accumulator and the float view stay correct.
static void test_integer_views_saturate() {
  CoulombCounter c(0xFFFFFFFFUL);
  c.update(0, 1000000000L, -1000000000L);
  TEST_ASSERT_TRUE(c.update(3600000000UL, 1000000000L, -1000000000L));
  TEST_ASSERT_TRUE(c.charge_uAms() == 3600000000000000000LL);
  TEST_ASSERT_EQUAL_INT32(2147483647L, c.charge_uAh());
  TEST_ASSERT_EQUAL_INT32(-2147483647L - 1L, c.energy_uWh());
  TEST_ASSERT_FLOAT_WITHIN(1e3f, 1e9f, c.charge_mAh());
  TEST_ASSERT_FLOAT_WITHIN(1e3f, -1e9f, c.energy_mWh());
}

static void test_overflow_span_is_a_gap_not_bridged() {
  // Audit repro: 100 mA, 2 A (OVF), 2 A (OVF), 100 mA at 500 ms. Before
  // markGap() existed the firmware skipped the OVF samples and the next good
  // sample bridged 0.5 s .. 2.0 s with 100 mA (55 uAh in total). Now only the
  // first, fully valid interval is integrated and the span is counted.
  CoulombCounter c(2000);
  TEST_ASSERT_FALSE(c.update(0, 100000, 0));
  TEST_ASSERT_EQUAL_UINT32(0, c.lastInterval_ms());
  TEST_ASSERT_TRUE(c.update(500, 100000, 0));
  TEST_ASSERT_EQUAL_UINT32(500, c.lastInterval_ms());
  c.markGap();  // t = 1000 ms, OVF
  TEST_ASSERT_EQUAL_UINT32(0, c.lastInterval_ms());
  TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.0f, c.lastDelta_mAh());
  c.markGap();  // t = 1500 ms, OVF: same span, counted once
  TEST_ASSERT_EQUAL_UINT16(1, c.gapCount());
  TEST_ASSERT_FALSE(c.seeded());
  TEST_ASSERT_FALSE(c.update(2000, 100000, 0));  // re-seeds only
  TEST_ASSERT_TRUE(c.charge_uAms() == 50000000LL);  // 100 mA * 0.5 s
  TEST_ASSERT_EQUAL_INT32(13, c.charge_uAh());
  TEST_ASSERT_TRUE(c.update(2500, 100000, 0));
  TEST_ASSERT_TRUE(c.charge_uAms() == 100000000LL);
  TEST_ASSERT_EQUAL_UINT16(1, c.gapCount());
}

static void test_mark_gap_before_seed_is_not_counted() {
  CoulombCounter c;
  c.markGap();
  TEST_ASSERT_EQUAL_UINT16(0, c.gapCount());
  TEST_ASSERT_FALSE(c.update(0, 1000, 0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_first_sample_only_seeds);
  RUN_TEST(test_constant_one_amp_for_one_hour);
  RUN_TEST(test_single_trapezoid);
  RUN_TEST(test_linear_ramp_is_exact);
  RUN_TEST(test_half_units_are_kept);
  RUN_TEST(test_negative_current_subtracts);
  RUN_TEST(test_millis_wraparound);
  RUN_TEST(test_wraparound_exactly_at_boundary);
  RUN_TEST(test_long_gap_is_skipped_and_reseeds);
  RUN_TEST(test_backwards_time_is_a_gap);
  RUN_TEST(test_max_gap_is_adjustable);
  RUN_TEST(test_zero_interval_adds_nothing);
  RUN_TEST(test_reset);
  RUN_TEST(test_integer_views_saturate);
  RUN_TEST(test_overflow_span_is_a_gap_not_bridged);
  RUN_TEST(test_mark_gap_before_seed_is_not_counted);
  return UNITY_END();
}
