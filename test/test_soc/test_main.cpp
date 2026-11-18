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
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 50.0f, soc.update(3840.0f, 500.0f, 123.0f));
  TEST_ASSERT_TRUE(soc.seeded());
}

static void test_coulomb_counting_under_load_ignores_voltage() {
  SocEstimator soc(liIon1SConfig(2000.0f));
  soc.seedFromVoltage(3840.0f);  // 50 %
  // 200 mAh out of 2000 mAh is 10 %. The sagging 3.5 V terminal voltage would
  // read as about 5 % on the curve, but 500 mA is far above the rest threshold.
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 40.0f, soc.update(3500.0f, 500.0f, 200.0f));
  // Charging 100 mAh brings back 5 %.
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 45.0f, soc.update(4100.0f, -500.0f, -100.0f));
}

static void test_rest_blend_pulls_towards_voltage() {
  SocConfig config = liIon1SConfig(2000.0f);
  config.rest_blend = 0.1f;
  SocEstimator soc(config);
  soc.seedFromVoltage(3840.0f);  // 50 %
  // Resting at 3730 mV (20 % on the curve): 50 + 0.1 * (20 - 50) = 47
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 47.0f, soc.update(3730.0f, 0.0f, 0.0f));
  // Again: 47 + 0.1 * (20 - 47) = 44.3
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 44.3f, soc.update(3730.0f, 0.0f, 0.0f));
  // Converges on the curve value and does not overshoot.
  float value = 0.0f;
  for (int i = 0; i < 200; ++i) value = soc.update(3730.0f, 0.0f, 0.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-2f, 20.0f, value);
}

static void test_rest_threshold_boundary() {
  SocConfig config = liIon1SConfig(1000.0f);
  config.rest_blend = 0.5f;
  config.rest_current_mA = 20.0f;
  SocEstimator soc(config);

  soc.seedFromVoltage(3840.0f);  // 50 %
  // Exactly at the threshold counts as resting (both signs): 50 + 0.5 * 50 = 75
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 75.0f, soc.update(4200.0f, 20.0f, 0.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 87.5f, soc.update(4200.0f, -20.0f, 0.0f));
  // Just above it does not.
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 87.5f, soc.update(4200.0f, 20.5f, 0.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 87.5f, soc.update(4200.0f, -20.5f, 0.0f));
}

static void test_combined_coulomb_and_blend() {
  SocConfig config = liIon1SConfig(1000.0f);
  config.rest_blend = 0.2f;
  SocEstimator soc(config);
  soc.seedFromVoltage(3840.0f);  // 50 %
  // 10 mAh of 1000 mAh = 1 % -> 49, then 49 + 0.2 * (50 - 49) = 49.2
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 49.2f, soc.update(3840.0f, 10.0f, 10.0f));
}

static void test_clamps_to_0_and_100() {
  SocEstimator soc(liIon1SConfig(1000.0f));
  soc.seedFromVoltage(3840.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, soc.update(3000.0f, 1000.0f, 5000.0f));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 100.0f, soc.update(4200.0f, -1000.0f, -5000.0f));
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
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 50.0f, soc.update(3840.0f, 500.0f, 100.0f));
}

static void test_reset_reseeds() {
  SocEstimator soc(liIon1SConfig(2000.0f));
  soc.update(4200.0f, 0.0f, 0.0f);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 100.0f, soc.percent());
  soc.reset();
  TEST_ASSERT_FALSE(soc.seeded());
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, soc.percent());
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 20.0f, soc.update(3730.0f, 300.0f, 0.0f));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_curve_is_well_formed);
  RUN_TEST(test_lookup_at_table_points);
  RUN_TEST(test_lookup_interpolates);
  RUN_TEST(test_lookup_clamps);
  RUN_TEST(test_custom_curve);
  RUN_TEST(test_first_update_seeds_from_voltage);
  RUN_TEST(test_coulomb_counting_under_load_ignores_voltage);
  RUN_TEST(test_rest_blend_pulls_towards_voltage);
  RUN_TEST(test_rest_threshold_boundary);
  RUN_TEST(test_combined_coulomb_and_blend);
  RUN_TEST(test_clamps_to_0_and_100);
  RUN_TEST(test_series_cells_divide_pack_voltage);
  RUN_TEST(test_zero_capacity_disables_coulomb_term);
  RUN_TEST(test_reset_reseeds);
  return UNITY_END();
}
