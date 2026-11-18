#include <unity.h>

#include <powercore/cutoff.h>

using powercore::CutoffConfig;
using powercore::CutoffState;
using powercore::LowVoltageCutoff;

void setUp() {}
void tearDown() {}

#define ASSERT_STATE(expected, actual) \
  TEST_ASSERT_EQUAL_INT(static_cast<int>(expected), static_cast<int>(actual))

// Trip below 3000 mV after 3 samples, release at >= 3300 mV after 2 samples.
static const CutoffConfig kConfig = {3000, 3300, 3, 2};

static void test_starts_normal() {
  LowVoltageCutoff cutoff(kConfig);
  ASSERT_STATE(CutoffState::Normal, cutoff.state());
  TEST_ASSERT_TRUE(cutoff.loadEnabled());
  TEST_ASSERT_TRUE(cutoff.configValid());
}

static void test_trips_after_consecutive_low_samples() {
  LowVoltageCutoff cutoff(kConfig);
  ASSERT_STATE(CutoffState::Normal, cutoff.update(3500));
  ASSERT_STATE(CutoffState::Normal, cutoff.update(2990));
  ASSERT_STATE(CutoffState::Normal, cutoff.update(2990));
  ASSERT_STATE(CutoffState::Tripped, cutoff.update(2990));
  TEST_ASSERT_FALSE(cutoff.loadEnabled());
}

static void test_short_dip_does_not_trip() {
  LowVoltageCutoff cutoff(kConfig);
  cutoff.update(2990);
  cutoff.update(2990);
  ASSERT_STATE(CutoffState::Normal, cutoff.update(3100));  // run broken
  ASSERT_STATE(CutoffState::Normal, cutoff.update(2990));
  ASSERT_STATE(CutoffState::Normal, cutoff.update(2990));
  ASSERT_STATE(CutoffState::Tripped, cutoff.update(2990));  // three in a row again
}

static void test_threshold_is_exclusive_for_trip() {
  LowVoltageCutoff cutoff(kConfig);
  for (int i = 0; i < 10; ++i) {
    ASSERT_STATE(CutoffState::Normal, cutoff.update(3000));  // not below
  }
  cutoff.update(2999);
  cutoff.update(2999);
  ASSERT_STATE(CutoffState::Tripped, cutoff.update(2999));
}

static void test_recovery_needs_voltage_above_the_band() {
  LowVoltageCutoff cutoff(kConfig);
  cutoff.update(2900);
  cutoff.update(2900);
  cutoff.update(2900);
  ASSERT_STATE(CutoffState::Tripped, cutoff.state());

  // Back above the cutoff but inside the hysteresis band: stays tripped.
  ASSERT_STATE(CutoffState::Tripped, cutoff.update(3200));
  ASSERT_STATE(CutoffState::Tripped, cutoff.update(3299));
  // One good sample, then a relapse, then two good samples.
  ASSERT_STATE(CutoffState::Tripped, cutoff.update(3300));
  ASSERT_STATE(CutoffState::Tripped, cutoff.update(3299));
  ASSERT_STATE(CutoffState::Tripped, cutoff.update(3300));  // inclusive threshold
  ASSERT_STATE(CutoffState::Normal, cutoff.update(3300));
  TEST_ASSERT_TRUE(cutoff.loadEnabled());
}

// The usual failure without hysteresis: the load switches off, the unloaded
// battery rebounds a little, the load switches on again, and so on. A voltage
// bouncing inside the band must leave either state unchanged.
static void test_no_chatter_inside_band() {
  LowVoltageCutoff tripped(kConfig);
  tripped.update(2900);
  tripped.update(2900);
  tripped.update(2900);
  LowVoltageCutoff normal(kConfig);
  for (int i = 0; i < 40; ++i) {
    const int32_t mv = (i % 2 == 0) ? 3050 : 3250;
    ASSERT_STATE(CutoffState::Tripped, tripped.update(mv));
    ASSERT_STATE(CutoffState::Normal, normal.update(mv));
  }
}

static void test_full_cycle_twice() {
  LowVoltageCutoff cutoff(kConfig);
  for (int cycle = 0; cycle < 2; ++cycle) {
    cutoff.update(2800);
    cutoff.update(2800);
    ASSERT_STATE(CutoffState::Tripped, cutoff.update(2800));
    cutoff.update(3400);
    ASSERT_STATE(CutoffState::Normal, cutoff.update(3400));
  }
}

static void test_counter_does_not_carry_across_states() {
  LowVoltageCutoff cutoff(kConfig);
  cutoff.update(2900);
  cutoff.update(2900);
  cutoff.update(2900);  // tripped, counter cleared
  // One sample above recovery must not be enough even though three low
  // samples were just counted.
  ASSERT_STATE(CutoffState::Tripped, cutoff.update(3400));
  ASSERT_STATE(CutoffState::Normal, cutoff.update(3400));
  // And after release, two low samples are not enough to trip again.
  cutoff.update(2900);
  ASSERT_STATE(CutoffState::Normal, cutoff.update(2900));
  ASSERT_STATE(CutoffState::Tripped, cutoff.update(2900));
}

static void test_single_sample_debounce_and_zero_is_one() {
  const CutoffConfig immediate = {3000, 3300, 1, 1};
  LowVoltageCutoff a(immediate);
  ASSERT_STATE(CutoffState::Tripped, a.update(2999));
  ASSERT_STATE(CutoffState::Normal, a.update(3300));

  const CutoffConfig zero = {3000, 3300, 0, 0};
  LowVoltageCutoff b(zero);
  ASSERT_STATE(CutoffState::Tripped, b.update(2999));
  ASSERT_STATE(CutoffState::Normal, b.update(3300));
}

static void test_reset_and_config_validity() {
  LowVoltageCutoff cutoff(kConfig);
  cutoff.update(2900);
  cutoff.update(2900);
  cutoff.update(2900);
  cutoff.reset();
  ASSERT_STATE(CutoffState::Normal, cutoff.state());
  // The debounce counter was cleared as well.
  cutoff.update(2900);
  ASSERT_STATE(CutoffState::Normal, cutoff.update(2900));

  const CutoffConfig no_band = {3000, 3000, 1, 1};
  TEST_ASSERT_FALSE(LowVoltageCutoff(no_band).configValid());
  const CutoffConfig inverted = {3300, 3000, 1, 1};
  TEST_ASSERT_FALSE(LowVoltageCutoff(inverted).configValid());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_starts_normal);
  RUN_TEST(test_trips_after_consecutive_low_samples);
  RUN_TEST(test_short_dip_does_not_trip);
  RUN_TEST(test_threshold_is_exclusive_for_trip);
  RUN_TEST(test_recovery_needs_voltage_above_the_band);
  RUN_TEST(test_no_chatter_inside_band);
  RUN_TEST(test_full_cycle_twice);
  RUN_TEST(test_counter_does_not_carry_across_states);
  RUN_TEST(test_single_sample_debounce_and_zero_is_one);
  RUN_TEST(test_reset_and_config_validity);
  return UNITY_END();
}
