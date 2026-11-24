#include <unity.h>

#include <powercore/soc.h>

using namespace powercore;

void setUp() {}
void tearDown() {}

static float lookup(float mv) {
  return socFromVoltage(kLiIon1SCurve, kLiIon1SCurveSize, mv);
}

static void test_curve_is_well_formed() {
  TEST_ASSERT_EQUAL_UINT8(11, kLiIon1SCurveSize);
  TEST_ASSERT_EQUAL_UINT8(0, kLiIon1SCurve[0].percent);
  TEST_ASSERT_EQUAL_UINT8(100, kLiIon1SCurve[kLiIon1SCurveSize - 1].percent);
  for (uint8_t i = 1; i < kLiIon1SCurveSize; ++i) {
    TEST_ASSERT_TRUE(kLiIon1SCurve[i].cell_mV > kLiIon1SCurve[i - 1].cell_mV);
    TEST_ASSERT_TRUE(kLiIon1SCurve[i].percent > kLiIon1SCurve[i - 1].percent);
  }
}

static void test_lookup_at_table_points() {
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, lookup(3270.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 10.0f, lookup(3690.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 50.0f, lookup(3840.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 100.0f, lookup(4200.0f));
}

static void test_lookup_interpolates() {
  // Halfway between 3770 mV (30 %) and 3800 mV (40 %).
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 35.0f, lookup(3785.0f));
  // 3950 mV (70 %) .. 4020 mV (80 %): 70 + 10 * 30 / 70 = 74.2857
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 74.2857f, lookup(3980.0f));
  // Steep bottom segment 3270 (0 %) .. 3690 (10 %): 10 * 210 / 420 = 5
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 5.0f, lookup(3480.0f));
  // 4110 (90 %) .. 4200 (100 %): 90 + 10 * 45 / 90 = 95
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 95.0f, lookup(4155.0f));
}

static void test_lookup_clamps() {
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, lookup(2500.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, lookup(-1.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 100.0f, lookup(4350.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, socFromVoltage(nullptr, 0, 3800.0f));
}

static void test_custom_curve() {
  // A made-up three-point LiFePO4-like table to show the curve is pluggable.
  static const SocPoint curve[] = {{2800, 0}, {3200, 20}, {3400, 100}};
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 10.0f, socFromVoltage(curve, 3, 3000.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 60.0f, socFromVoltage(curve, 3, 3300.0f));
  // A single-point table always returns that point.
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 20.0f, socFromVoltage(curve + 1, 1, 5000.0f));
}

static void test_first_update_seeds_from_voltage() {
  SocEstimator soc(liIon1SConfig(2000.0f));
  TEST_ASSERT_FALSE(soc.seeded());
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, soc.percent());
  // The charge delta of the seeding call is ignored.
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 50.0f, soc.update(3840.0f, 500.0f, 123.0f, 500));
  TEST_ASSERT_TRUE(soc.seeded());
}

static void test_default_config_values() {
  const SocConfig c = liIon1SConfig(2000.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 20.0f, c.rest_current_mA);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 60.0f, c.rest_min_s);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 600.0f, c.rest_tau_s);
}

static void test_coulomb_counting_under_load_ignores_voltage() {
  SocEstimator soc(liIon1SConfig(2000.0f));
  soc.seedFromVoltage(3840.0f);  // 50 %
  // 200 mAh out of 2000 mAh is 10 %. The sagging 3.5 V terminal voltage would
  // read as about 5 % on the curve, but 500 mA is far above the rest threshold.
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 40.0f, soc.update(3500.0f, 500.0f, 200.0f, 1000));
  // Charging 100 mAh brings back 5 %.
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 45.0f, soc.update(4100.0f, -500.0f, -100.0f, 1000));
}

// No minimum rest; the blend fraction for a 1000 ms step is 1000 / (tau + 1000).
static SocConfig blendConfig(float capacity_mAh, float tau_s) {
  SocConfig config = liIon1SConfig(capacity_mAh);
  config.rest_min_s = 0.0f;
  config.rest_tau_s = tau_s;
  return config;
}

static void test_rest_blend_pulls_towards_voltage() {
  SocEstimator soc(blendConfig(2000.0f, 9.0f));  // fraction 0.1 per 1 s step
  soc.seedFromVoltage(3840.0f);                   // 50 %
  // Resting at 3730 mV (20 % on the curve): 50 + 0.1 * (20 - 50) = 47
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 47.0f, soc.update(3730.0f, 0.0f, 0.0f, 1000));
  // Again: 47 + 0.1 * (20 - 47) = 44.3
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 44.3f, soc.update(3730.0f, 0.0f, 0.0f, 1000));
  // Converges on the curve value and does not overshoot.
  float value = 0.0f;
  for (int i = 0; i < 200; ++i) value = soc.update(3730.0f, 0.0f, 0.0f, 1000);
  TEST_ASSERT_FLOAT_WITHIN(1e-2f, 20.0f, value);
}

static void test_rest_threshold_boundary() {
  SocConfig config = blendConfig(1000.0f, 1.0f);  // fraction 0.5 per 1 s step
  config.rest_current_mA = 20.0f;
  SocEstimator soc(config);

  soc.seedFromVoltage(3840.0f);  // 50 %
  // Exactly at the threshold counts as resting (both signs): 50 + 0.5 * 50 = 75
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 75.0f, soc.update(4200.0f, 20.0f, 0.0f, 1000));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 87.5f, soc.update(4200.0f, -20.0f, 0.0f, 1000));
  // Just above it does not.
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 87.5f, soc.update(4200.0f, 20.5f, 0.0f, 1000));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 87.5f, soc.update(4200.0f, -20.5f, 0.0f, 1000));
}

static void test_combined_coulomb_and_blend() {
  SocEstimator soc(blendConfig(1000.0f, 4.0f));  // fraction 0.2 per 1 s step
  soc.seedFromVoltage(3840.0f);                   // 50 %
  // 10 mAh of 1000 mAh = 1 % -> 49, then 49 + 0.2 * (50 - 49) = 49.2
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 49.2f, soc.update(3840.0f, 10.0f, 10.0f, 1000));
}

static void test_zero_dt_does_not_blend() {
  SocEstimator soc(blendConfig(1000.0f, 1.0f));
  soc.seedFromVoltage(3840.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 50.0f, soc.update(4200.0f, 0.0f, 0.0f, 0));
  TEST_ASSERT_EQUAL_UINT32(0, soc.restTime_ms());
}

// Rest at 3730 mV (20 % on the curve) after seeding at 50 %, sampling every
// `period_ms`. Returns the rest time in seconds at which the estimate has moved
// 63.2 % (1 - 1/e) of the way to the curve.
static float timeTo63(uint32_t period_ms) {
  SocEstimator soc(liIon1SConfig(2000.0f));
  soc.seedFromVoltage(3840.0f);
  const float target = 50.0f - 30.0f * 0.63212f;
  uint32_t t_ms = 0;
  while (t_ms < 3600000UL) {
    t_ms += period_ms;
    if (soc.update(3730.0f, 0.5f, 0.0f, period_ms) <= target) break;
  }
  return static_cast<float>(t_ms) / 1000.0f;
}

static void test_time_constant_independent_of_sample_rate() {
  // Default: 60 s minimum rest + 600 s time constant = 660 s. With the old
  // per-sample blend of 0.02 this took 1.0 s at 20 ms and 24.7 s at 500 ms.
  const uint32_t periods[] = {20, 100, 500, 2000};
  for (uint32_t period : periods) {
    const float t = timeTo63(period);
    TEST_ASSERT_FLOAT_WITHIN(0.01f * 660.0f, 660.0f, t);
  }
}

static void test_no_blend_before_minimum_rest() {
  SocEstimator soc(liIon1SConfig(2000.0f));
  soc.seedFromVoltage(3840.0f);  // 50 %
  // 59.98 s of rest at 20 ms: the voltage must not have moved the estimate.
  float value = 0.0f;
  for (int i = 0; i < 2999; ++i) value = soc.update(3730.0f, 0.0f, 0.0f, 20);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 50.0f, value);
  TEST_ASSERT_EQUAL_UINT32(59980, soc.restTime_ms());
  // About one second past the minimum moves it by roughly 30 * 1 / 600 = 0.05 %.
  for (int i = 0; i < 51; ++i) value = soc.update(3730.0f, 0.0f, 0.0f, 20);
  TEST_ASSERT_FLOAT_WITHIN(0.005f, 50.0f - 0.05f, value);
}

static void test_load_restarts_rest_timer() {
  SocEstimator soc(liIon1SConfig(2000.0f));
  soc.seedFromVoltage(3840.0f);
  for (int i = 0; i < 100; ++i) soc.update(3730.0f, 0.0f, 0.0f, 500);  // 50 s
  TEST_ASSERT_EQUAL_UINT32(50000, soc.restTime_ms());
  soc.update(3700.0f, 150.0f, 0.0f, 500);  // radio burst
  TEST_ASSERT_EQUAL_UINT32(0, soc.restTime_ms());
  for (int i = 0; i < 100; ++i) soc.update(3730.0f, 0.0f, 0.0f, 500);
  TEST_ASSERT_EQUAL_UINT32(50000, soc.restTime_ms());
  soc.breakRest();
  TEST_ASSERT_EQUAL_UINT32(0, soc.restTime_ms());
  // 100 s of rest in total, but never 60 s in one stretch: no blend at all.
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 50.0f, soc.percent());
}

static void test_clamps_to_0_and_100() {
  SocEstimator soc(liIon1SConfig(1000.0f));
  soc.seedFromVoltage(3840.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, soc.update(3000.0f, 1000.0f, 5000.0f, 500));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 100.0f, soc.update(4200.0f, -1000.0f, -5000.0f, 500));
}

static void test_series_cells_divide_pack_voltage() {
  SocConfig config = liIon1SConfig(2000.0f);
  config.series_cells = 2;
  SocEstimator soc(config);
  soc.seedFromVoltage(7680.0f);  // 2 * 3840 mV
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 50.0f, soc.percent());

  config.series_cells = 0;  // treated as 1
  SocEstimator single(config);
  single.seedFromVoltage(3840.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 50.0f, single.percent());
}

static void test_zero_capacity_disables_coulomb_term() {
  SocEstimator soc(liIon1SConfig(0.0f));
  soc.seedFromVoltage(3840.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 50.0f, soc.update(3840.0f, 500.0f, 100.0f, 500));
}

static void test_reset_reseeds() {
  SocEstimator soc(liIon1SConfig(2000.0f));
  soc.update(4200.0f, 0.0f, 0.0f, 500);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 100.0f, soc.percent());
  soc.reset();
  TEST_ASSERT_FALSE(soc.seeded());
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, soc.percent());
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 20.0f, soc.update(3730.0f, 300.0f, 0.0f, 500));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_curve_is_well_formed);
  RUN_TEST(test_lookup_at_table_points);
  RUN_TEST(test_lookup_interpolates);
  RUN_TEST(test_lookup_clamps);
  RUN_TEST(test_custom_curve);
  RUN_TEST(test_first_update_seeds_from_voltage);
  RUN_TEST(test_default_config_values);
  RUN_TEST(test_coulomb_counting_under_load_ignores_voltage);
  RUN_TEST(test_rest_blend_pulls_towards_voltage);
  RUN_TEST(test_rest_threshold_boundary);
  RUN_TEST(test_combined_coulomb_and_blend);
  RUN_TEST(test_zero_dt_does_not_blend);
  RUN_TEST(test_time_constant_independent_of_sample_rate);
  RUN_TEST(test_no_blend_before_minimum_rest);
  RUN_TEST(test_load_restarts_rest_timer);
  RUN_TEST(test_clamps_to_0_and_100);
  RUN_TEST(test_series_cells_divide_pack_voltage);
  RUN_TEST(test_zero_capacity_disables_coulomb_term);
  RUN_TEST(test_reset_reseeds);
  return UNITY_END();
}
