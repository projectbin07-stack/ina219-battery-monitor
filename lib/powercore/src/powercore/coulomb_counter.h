// Charge (mAh) and energy (mWh) integration with the trapezoidal rule.
//
// Accumulators are 64-bit integers so that long runs do not lose precision the
// way a 32-bit float sum would (on AVR `double` is also 32 bits wide).
#pragma once

#include <stdint.h>

namespace powercore {

class CoulombCounter {
 public:
  // Two samples further apart than `max_gap_ms` are not integrated across.
  explicit CoulombCounter(uint32_t max_gap_ms = 5000UL);

  // Clear the accumulators and forget the previous sample.
  void reset();

  void setMaxGap(uint32_t max_gap_ms) { max_gap_ms_ = max_gap_ms; }
  uint32_t maxGap() const { return max_gap_ms_; }

  // Feed one sample. `now_ms` is a free-running millisecond counter such as
  // Arduino millis(); the unsigned subtraction makes the 2^32 ms (~49.7 day)
  // wraparound transparent. Positive current/power = discharge.
  //
  // Returns true if the interval since the previous sample was integrated.
  // The first sample after reset() only seeds the integrator. An interval
  // longer than the maximum gap (which is also what a clock that stepped
  // backwards looks like) is skipped, counted in gapCount(), and the
  // integrator is re-seeded from the new sample.
  bool update(uint32_t now_ms, int32_t current_uA, int32_t power_uW);

  // Net charge in microamp-milliseconds and energy in microwatt-milliseconds.
  int64_t charge_uAms() const { return charge2_ / 2; }
  int64_t energy_uWms() const { return energy2_ / 2; }

  // Convenience conversions. The integer versions saturate at the int32 range.
  int32_t charge_uAh() const;
  int32_t energy_uWh() const;
  float charge_mAh() const;
  float energy_mWh() const;

  // Charge added by the most recent update() call, in mAh (0 if it was skipped).
  float lastDelta_mAh() const;

  uint16_t gapCount() const { return gap_count_; }
  bool seeded() const { return seeded_; }

 private:
  // Sums of (a + b) * dt, i.e. twice the trapezoid area, so no bit is lost to
  // the division by two.
  int64_t charge2_;
  int64_t energy2_;
  int64_t last_delta2_;
  uint32_t max_gap_ms_;
  uint32_t prev_ms_;
  int32_t prev_current_uA_;
  int32_t prev_power_uW_;
  uint16_t gap_count_;
  bool seeded_;
};

}  // namespace powercore
