// State-of-charge estimation: open-circuit-voltage lookup blended with
// coulomb counting.
//
// The voltage curve is APPROXIMATE. Real cells differ by chemistry variant,
// age, temperature and load; replace the table with one measured for your
// cell if you need better than a rough indication.
#pragma once

#include <stdint.h>

namespace powercore {

struct SocPoint {
  uint16_t cell_mV;
  uint8_t percent;
};

// Approximate resting open-circuit voltage of a single Li-ion / LiPo cell
// (4.20 V full, 3.27 V empty), 11 points, ascending voltage.
extern const SocPoint kLiIon1SCurve[];
extern const uint8_t kLiIon1SCurveSize;

// Linear interpolation in a curve sorted by ascending voltage. Clamps to the
// first/last point outside the table. Returns percent (0..100).
float socFromVoltage(const SocPoint* curve, uint8_t size, float cell_mV);

struct SocConfig {
  const SocPoint* curve;   // ascending voltage
  uint8_t curve_size;
  uint8_t series_cells;    // pack voltage is divided by this
  float capacity_mAh;      // usable capacity
  float rest_current_mA;   // |I| at or below this counts as "resting"
  float rest_min_s;        // continuous rest needed before the voltage is trusted
  float rest_tau_s;        // time constant of the pull towards the voltage estimate
};

// Defaults for one Li-ion cell: rest threshold 20 mA, minimum rest 60 s,
// time constant 600 s.
//
// The blend is defined in time, not per sample, so the result does not depend
// on the sample rate: after `rest_min_s` of continuous rest, the gap between
// the estimate and the voltage curve shrinks by 63 % every `rest_tau_s`
// seconds whether the firmware samples every 20 ms or every 500 ms. The 60 s /
// 600 s values are a judgement call (terminal voltage recovers over minutes
// after a load step), not values fitted to a measured cell.
SocConfig liIon1SConfig(float capacity_mAh);

class SocEstimator {
 public:
  explicit SocEstimator(const SocConfig& config);

  // Forget the estimate; the next update() seeds it from the voltage curve.
  void reset();

  // Set the estimate directly from a pack voltage.
  void seedFromVoltage(float pack_mV);

  // Advance the estimate.
  //   pack_mV         terminal voltage of the whole pack
  //   current_mA      positive = discharge
  //   discharged_mAh  charge drawn since the previous call (negative = charged)
  //   dt_ms           time since the previous call (0 if unknown or a gap)
  //
  // Coulomb counting moves the estimate by discharged/capacity. Once the
  // current has stayed at or below the rest threshold for `rest_min_s`, the
  // terminal voltage is close to the open-circuit voltage, so the estimate is
  // additionally pulled towards the curve value with the fraction
  // dt / (rest_tau_s + dt) per call, i.e. a first-order filter with time
  // constant rest_tau_s. Under load the voltage is ignored because the drop
  // across the cell's internal resistance would read as lost charge.
  // Returns percent, clamped to 0..100.
  float update(float pack_mV, float current_mA, float discharged_mAh, uint32_t dt_ms);

  // Restart the rest timer, e.g. after a sample whose current is unknown
  // (overflow) and so cannot be assumed to be at rest.
  void breakRest() { rest_ms_ = 0; }

  // Milliseconds of continuous rest seen so far (saturates).
  uint32_t restTime_ms() const { return rest_ms_; }

  float percent() const { return percent_; }
  bool seeded() const { return seeded_; }

 private:
  float voltageEstimate(float pack_mV) const;

  SocConfig config_;
  float percent_;
  uint32_t rest_ms_;
  bool seeded_;
};

}  // namespace powercore
