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
  float rest_blend;        // 0..1 pull towards the voltage estimate per resting update
};

// Defaults for one Li-ion cell: rest threshold 20 mA, blend 0.02 per update.
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
  //
  // Coulomb counting moves the estimate by discharged/capacity. While the
  // current is at or below the rest threshold the terminal voltage is close to
  // the open-circuit voltage, so the estimate is additionally pulled towards
  // the curve value by `rest_blend`. Under load the voltage is ignored because
  // the drop across the cell's internal resistance would read as lost charge.
  // Returns percent, clamped to 0..100.
  float update(float pack_mV, float current_mA, float discharged_mAh);

  float percent() const { return percent_; }
  bool seeded() const { return seeded_; }

 private:
  float voltageEstimate(float pack_mV) const;

  SocConfig config_;
  float percent_;
  bool seeded_;
};

}  // namespace powercore
