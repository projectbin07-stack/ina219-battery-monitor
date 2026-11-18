// Low-voltage cutoff state machine with hysteresis and debounce.
#pragma once

#include <stdint.h>

namespace powercore {

enum class CutoffState : uint8_t { Normal = 0, Tripped = 1 };

struct CutoffConfig {
  int32_t cutoff_mV;        // trip when the voltage is below this ...
  int32_t recover_mV;       // ... and release when it is at or above this
  uint8_t trip_samples;     // consecutive samples needed to trip (>= 1)
  uint8_t recover_samples;  // consecutive samples needed to release (>= 1)
};

class LowVoltageCutoff {
 public:
  explicit LowVoltageCutoff(const CutoffConfig& config);

  // True if recover_mV > cutoff_mV, i.e. there is a real hysteresis band.
  bool configValid() const { return config_.recover_mV > config_.cutoff_mV; }

  // Back to Normal with the debounce counter cleared.
  void reset();

  // Feed one voltage sample and return the new state.
  //   Normal  -> Tripped after `trip_samples` consecutive samples < cutoff_mV
  //   Tripped -> Normal  after `recover_samples` consecutive samples >= recover_mV
  // Any sample that breaks the run clears the counter, so a voltage sitting
  // inside the band between the two thresholds never changes the state.
  CutoffState update(int32_t voltage_mV);

  CutoffState state() const { return state_; }
  bool loadEnabled() const { return state_ == CutoffState::Normal; }

 private:
  CutoffConfig config_;
  CutoffState state_;
  uint8_t run_;
};

}  // namespace powercore
