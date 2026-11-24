#include "powercore/coulomb_counter.h"

namespace powercore {

namespace {

// 1 uAh = 3 600 000 uA*ms; the accumulators hold twice the area.
const int64_t kTwiceMsPerHour = 7200000LL;
const float kTwiceMicroMsPerMilliHour = 7.2e9f;

int32_t saturate32(int64_t v) {
  if (v > 2147483647LL) return 2147483647L;
  if (v < -2147483647LL - 1LL) return -2147483647L - 1L;
  return static_cast<int32_t>(v);
}

}  // namespace

CoulombCounter::CoulombCounter(uint32_t max_gap_ms)
    : charge2_(0),
      energy2_(0),
      last_delta2_(0),
      max_gap_ms_(max_gap_ms),
      last_interval_ms_(0),
      prev_ms_(0),
      prev_current_uA_(0),
      prev_power_uW_(0),
      gap_count_(0),
      seeded_(false) {}

void CoulombCounter::reset() {
  charge2_ = 0;
  energy2_ = 0;
  last_delta2_ = 0;
  last_interval_ms_ = 0;
  gap_count_ = 0;
  seeded_ = false;
}

void CoulombCounter::markGap() {
  last_delta2_ = 0;
  last_interval_ms_ = 0;
  // Count each unusable span once, however many unusable samples it has.
  if (seeded_ && gap_count_ != 0xFFFFu) ++gap_count_;
  seeded_ = false;
}

bool CoulombCounter::update(uint32_t now_ms, int32_t current_uA, int32_t power_uW) {
  bool integrated = false;
  last_delta2_ = 0;
  last_interval_ms_ = 0;
  if (seeded_) {
    const uint32_t dt = now_ms - prev_ms_;  // modulo 2^32: wraparound-safe
    if (dt > max_gap_ms_) {
      if (gap_count_ != 0xFFFFu) ++gap_count_;
    } else {
      const int64_t dt64 = static_cast<int64_t>(dt);
      last_delta2_ = (static_cast<int64_t>(prev_current_uA_) + current_uA) * dt64;
      charge2_ += last_delta2_;
      energy2_ += (static_cast<int64_t>(prev_power_uW_) + power_uW) * dt64;
      last_interval_ms_ = dt;
      integrated = true;
    }
  }
  prev_ms_ = now_ms;
  prev_current_uA_ = current_uA;
  prev_power_uW_ = power_uW;
  seeded_ = true;
  return integrated;
}

int32_t CoulombCounter::charge_uAh() const {
  return saturate32(charge2_ / kTwiceMsPerHour);
}

int32_t CoulombCounter::energy_uWh() const {
  return saturate32(energy2_ / kTwiceMsPerHour);
}

float CoulombCounter::charge_mAh() const {
  return static_cast<float>(charge2_) / kTwiceMicroMsPerMilliHour;
}

float CoulombCounter::energy_mWh() const {
  return static_cast<float>(energy2_) / kTwiceMicroMsPerMilliHour;
}

float CoulombCounter::lastDelta_mAh() const {
  return static_cast<float>(last_delta2_) / kTwiceMicroMsPerMilliHour;
}

}  // namespace powercore
