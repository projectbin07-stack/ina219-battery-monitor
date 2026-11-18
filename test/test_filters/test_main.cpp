#include <unity.h>

#include <powercore/filters.h>

using powercore::Ema;
using powercore::MovingAverage;

void setUp() {}
void tearDown() {}

static void test_moving_average_fills_then_slides() {
  MovingAverage<4> avg;
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, avg.value());
  TEST_ASSERT_EQUAL_UINT8(0, avg.count());
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, avg.add(1.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.5f, avg.add(2.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 2.0f, avg.add(3.0f));
  TEST_ASSERT_FALSE(avg.full());
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 2.5f, avg.add(4.0f));
  TEST_ASSERT_TRUE(avg.full());
  // Window is now {2, 3, 4, 5}, then {3, 4, 5, 6}.
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 3.5f, avg.add(5.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 4.5f, avg.add(6.0f));
  TEST_ASSERT_EQUAL_UINT8(4, avg.count());
}

static void test_moving_average_forgets_old_spike() {
  MovingAverage<3> avg;
  avg.add(900.0f);
  avg.add(0.0f);
  avg.add(0.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 300.0f, avg.value());
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, avg.add(0.0f));  // spike has left the window
}

static void test_moving_average_reset_and_size_one() {
  MovingAverage<4> avg;
  avg.add(10.0f);
  avg.add(20.0f);
  avg.reset();
  TEST_ASSERT_EQUAL_UINT8(0, avg.count());
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 7.0f, avg.add(7.0f));

  MovingAverage<1> passthrough;
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 3.0f, passthrough.add(3.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -8.0f, passthrough.add(-8.0f));
}

static void test_ema_first_sample_seeds() {
  Ema ema(0.5f);
  TEST_ASSERT_FALSE(ema.seeded());
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 10.0f, ema.add(10.0f));
  TEST_ASSERT_TRUE(ema.seeded());
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 15.0f, ema.add(20.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 17.5f, ema.add(20.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 17.5f, ema.value());
}

// Step from 0 to 1 with alpha = 0.25: 1 - 0.75^n = 0.25, 0.4375, 0.578125
static void test_ema_step_response() {
  Ema ema(0.25f);
  ema.add(0.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.25f, ema.add(1.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.4375f, ema.add(1.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.578125f, ema.add(1.0f));
}

static void test_ema_alpha_limits() {
  Ema too_big(2.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, too_big.alpha());
  too_big.add(1.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 5.0f, too_big.add(5.0f));  // alpha 1 = no filtering

  Ema negative(-1.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, negative.alpha());
  negative.add(1.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, negative.add(5.0f));  // alpha 0 = frozen

  negative.setAlpha(0.5f);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 3.0f, negative.add(5.0f));
}

static void test_ema_reset() {
  Ema ema(0.1f);
  ema.add(100.0f);
  ema.reset();
  TEST_ASSERT_FALSE(ema.seeded());
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -4.0f, ema.add(-4.0f));
}

static void test_alpha_from_time_constant() {
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.25f, Ema::alphaFromTimeConstant(1.0f, 3.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.5f, Ema::alphaFromTimeConstant(500.0f, 500.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, Ema::alphaFromTimeConstant(0.0f, 3.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, Ema::alphaFromTimeConstant(1.0f, 0.0f));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_moving_average_fills_then_slides);
  RUN_TEST(test_moving_average_forgets_old_spike);
  RUN_TEST(test_moving_average_reset_and_size_one);
  RUN_TEST(test_ema_first_sample_seeds);
  RUN_TEST(test_ema_step_response);
  RUN_TEST(test_ema_alpha_limits);
  RUN_TEST(test_ema_reset);
  RUN_TEST(test_alpha_from_time_constant);
  return UNITY_END();
}
