// Small smoothing filters (header-only).
#pragma once

#include <stdint.h>

namespace powercore {

// Boxcar average over the last N samples. Until N samples have arrived it
// averages the samples seen so far. The sum is recomputed on every call, so
// there is no accumulated rounding drift; N is expected to be small.
template <uint8_t N>
class MovingAverage {
 public:
  MovingAverage() : index_(0), count_(0) {}

  void reset() {
    index_ = 0;
    count_ = 0;
  }

  float add(float sample) {
    buffer_[index_] = sample;
    index_ = static_cast<uint8_t>((index_ + 1u) % N);
    if (count_ < N) ++count_;
    return value();
  }

  float value() const {
    if (count_ == 0) return 0.0f;
    float sum = 0.0f;
    for (uint8_t i = 0; i < count_; ++i) sum += buffer_[i];
    return sum / static_cast<float>(count_);
  }

  uint8_t count() const { return count_; }
  bool full() const { return count_ == N; }

 private:
  float buffer_[N];
  uint8_t index_;
  uint8_t count_;
};

// Exponential moving average: y += alpha * (x - y). The first sample seeds the
// output so there is no start-up ramp from zero.
class Ema {
 public:
  explicit Ema(float alpha) : alpha_(clampAlpha(alpha)), value_(0.0f), seeded_(false) {}

  void reset() {
    value_ = 0.0f;
    seeded_ = false;
  }

  void setAlpha(float alpha) { alpha_ = clampAlpha(alpha); }
  float alpha() const { return alpha_; }

  float add(float sample) {
    if (!seeded_) {
      value_ = sample;
      seeded_ = true;
    } else {
      value_ += alpha_ * (sample - value_);
    }
    return value_;
  }

  float value() const { return value_; }
  bool seeded() const { return seeded_; }

  // alpha for a first-order low-pass with time constant `tau` sampled every
  // `dt` (same unit): alpha = dt / (tau + dt).
  static float alphaFromTimeConstant(float dt, float tau) {
    if (dt <= 0.0f) return 0.0f;
    if (tau <= 0.0f) return 1.0f;
    return dt / (tau + dt);
  }

 private:
  static float clampAlpha(float a) { return a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a); }

  float alpha_;
  float value_;
  bool seeded_;
};

}  // namespace powercore
