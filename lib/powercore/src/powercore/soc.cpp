#include "powercore/soc.h"

namespace powercore {

const SocPoint kLiIon1SCurve[] = {
    {3270, 0},  {3690, 10}, {3730, 20}, {3770, 30}, {3800, 40}, {3840, 50},
    {3870, 60}, {3950, 70}, {4020, 80}, {4110, 90}, {4200, 100},
};
const uint8_t kLiIon1SCurveSize = sizeof(kLiIon1SCurve) / sizeof(kLiIon1SCurve[0]);

float socFromVoltage(const SocPoint* curve, uint8_t size, float cell_mV) {
  if (curve == nullptr || size == 0) return 0.0f;
  if (cell_mV <= static_cast<float>(curve[0].cell_mV)) {
    return static_cast<float>(curve[0].percent);
  }
  for (uint8_t i = 1; i < size; ++i) {
    const float hi_mV = static_cast<float>(curve[i].cell_mV);
    if (cell_mV <= hi_mV) {
      const float lo_mV = static_cast<float>(curve[i - 1].cell_mV);
      const float lo_pct = static_cast<float>(curve[i - 1].percent);
      const float hi_pct = static_cast<float>(curve[i].percent);
      if (hi_mV <= lo_mV) return hi_pct;  // malformed table: avoid dividing by zero
      return lo_pct + (hi_pct - lo_pct) * (cell_mV - lo_mV) / (hi_mV - lo_mV);
    }
  }
  return static_cast<float>(curve[size - 1].percent);
}

SocConfig liIon1SConfig(float capacity_mAh) {
  SocConfig c;
  c.curve = kLiIon1SCurve;
  c.curve_size = kLiIon1SCurveSize;
  c.series_cells = 1;
  c.capacity_mAh = capacity_mAh;
  c.rest_current_mA = 20.0f;
  c.rest_blend = 0.02f;
  return c;
}

SocEstimator::SocEstimator(const SocConfig& config)
    : config_(config), percent_(0.0f), seeded_(false) {}

void SocEstimator::reset() {
  percent_ = 0.0f;
  seeded_ = false;
}

float SocEstimator::voltageEstimate(float pack_mV) const {
  const uint8_t cells = config_.series_cells == 0 ? 1 : config_.series_cells;
  return socFromVoltage(config_.curve, config_.curve_size, pack_mV / static_cast<float>(cells));
}

void SocEstimator::seedFromVoltage(float pack_mV) {
  percent_ = voltageEstimate(pack_mV);
  seeded_ = true;
}

float SocEstimator::update(float pack_mV, float current_mA, float discharged_mAh) {
  if (!seeded_) {
    seedFromVoltage(pack_mV);
    return percent_;
  }
  if (config_.capacity_mAh > 0.0f) {
    percent_ -= 100.0f * discharged_mAh / config_.capacity_mAh;
  }
  const float magnitude = current_mA < 0.0f ? -current_mA : current_mA;
  if (magnitude <= config_.rest_current_mA) {
    percent_ += config_.rest_blend * (voltageEstimate(pack_mV) - percent_);
  }
  if (percent_ < 0.0f) percent_ = 0.0f;
  if (percent_ > 100.0f) percent_ = 100.0f;
  return percent_;
}

}  // namespace powercore
