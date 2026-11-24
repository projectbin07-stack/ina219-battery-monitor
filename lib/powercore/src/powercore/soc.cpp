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
  c.rest_min_s = 60.0f;
  c.rest_tau_s = 600.0f;
  return c;
}

SocEstimator::SocEstimator(const SocConfig& config)
    : config_(config), percent_(0.0f), rest_ms_(0), seeded_(false) {}

void SocEstimator::reset() {
  percent_ = 0.0f;
  rest_ms_ = 0;
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

float SocEstimator::update(float pack_mV, float current_mA, float discharged_mAh,
                          uint32_t dt_ms) {
  if (!seeded_) {
    seedFromVoltage(pack_mV);
    rest_ms_ = 0;
    return percent_;
  }
  if (config_.capacity_mAh > 0.0f) {
    percent_ -= 100.0f * discharged_mAh / config_.capacity_mAh;
  }
  const float magnitude = current_mA < 0.0f ? -current_mA : current_mA;
  if (magnitude > config_.rest_current_mA) {
    rest_ms_ = 0;
  } else if (dt_ms > 0) {
    // Rest time before this interval; only the part of the interval beyond the
    // minimum rest contributes to the blend.
    const float min_ms = config_.rest_min_s > 0.0f ? config_.rest_min_s * 1000.0f : 0.0f;
    const float before_ms = static_cast<float>(rest_ms_);
    rest_ms_ = (0xFFFFFFFFUL - rest_ms_ < dt_ms) ? 0xFFFFFFFFUL : rest_ms_ + dt_ms;
    const float after_ms = static_cast<float>(rest_ms_);
    if (after_ms > min_ms) {
      const float eligible_ms = before_ms >= min_ms ? static_cast<float>(dt_ms) : after_ms - min_ms;
      float fraction = 1.0f;
      if (config_.rest_tau_s > 0.0f) {
        const float tau_ms = config_.rest_tau_s * 1000.0f;
        // Backward-Euler first-order step dt / (tau + dt): unconditionally
        // stable, and its time to 63 % is tau * (1 + dt / 2 tau) to first
        // order, i.e. within 0.05 % of tau for dt <= 500 ms and tau = 600 s.
        fraction = eligible_ms / (tau_ms + eligible_ms);
      }
      percent_ += fraction * (voltageEstimate(pack_mV) - percent_);
    }
  }
  if (percent_ < 0.0f) percent_ = 0.0f;
  if (percent_ > 100.0f) percent_ = 100.0f;
  return percent_;
}

}  // namespace powercore
