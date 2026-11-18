#include "powercore/cutoff.h"

namespace powercore {

LowVoltageCutoff::LowVoltageCutoff(const CutoffConfig& config)
    : config_(config), state_(CutoffState::Normal), run_(0) {
  if (config_.trip_samples == 0) config_.trip_samples = 1;
  if (config_.recover_samples == 0) config_.recover_samples = 1;
}

void LowVoltageCutoff::reset() {
  state_ = CutoffState::Normal;
  run_ = 0;
}

CutoffState LowVoltageCutoff::update(int32_t voltage_mV) {
  if (state_ == CutoffState::Normal) {
    if (voltage_mV < config_.cutoff_mV) {
      if (++run_ >= config_.trip_samples) {
        state_ = CutoffState::Tripped;
        run_ = 0;
      }
    } else {
      run_ = 0;
    }
  } else {
    if (voltage_mV >= config_.recover_mV) {
      if (++run_ >= config_.recover_samples) {
        state_ = CutoffState::Normal;
        run_ = 0;
      }
    } else {
      run_ = 0;
    }
  }
  return state_;
}

}  // namespace powercore
