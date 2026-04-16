#pragma once

#include <cmath>

namespace hibiki {

// Biquad filter with LP/HP/BP modes.
// Uses Robert Bristow-Johnson's Audio EQ Cookbook formulas.
// One instance per voice per channel.
class BiquadFilter {
 public:
  enum Type { LOWPASS = 0, HIGHPASS, BANDPASS };

  void setParams(Type type, float cutoff_hz, float q, float sample_rate) {
    if (cutoff_hz == cutoff_ && q == q_ && type == type_ && sample_rate == sr_)
      return;
    type_ = type;
    cutoff_ = cutoff_hz;
    q_ = q;
    sr_ = sample_rate;
    recalc();
  }

  // Modulate cutoff by envelope: base_hz * 2^(depth * env_value * 4)
  // depth in [-1, 1], env_value in [0, 1]
  void setModulatedCutoff(float base_hz, float depth, float env_value, float q,
                          float sample_rate) {
    float mod_oct = depth * env_value * 4.0f;  // ±4 octaves
    float mod_hz = base_hz * std::pow(2.0f, mod_oct);
    mod_hz = std::max(20.0f, std::min(20000.0f, mod_hz));
    setParams(type_, mod_hz, q, sample_rate);
  }

  float process(float x) {
    float y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
    x2_ = x1_;
    x1_ = x;
    y2_ = y1_;
    y1_ = y;
    return y;
  }

  void reset() { x1_ = x2_ = y1_ = y2_ = 0.0f; }

  // Map normalized 0..1 to cutoff Hz (log: 20Hz..20kHz)
  static float normToCutoff(float norm) {
    return 20.0f * std::pow(1000.0f, norm);
  }

  // Map normalized 0..1 to Q (0.5..20)
  static float normToQ(float norm) { return 0.5f + norm * 19.5f; }

  // Map normalized 0..1 to filter type
  static Type normToType(float norm) {
    if (norm < 0.33f) return LOWPASS;
    if (norm < 0.67f) return HIGHPASS;
    return BANDPASS;
  }

 private:
  void recalc() {
    float w0 = 2.0f * 3.14159265f * cutoff_ / sr_;
    float cw = std::cos(w0);
    float sw = std::sin(w0);
    float alpha = sw / (2.0f * q_);

    float a0;
    switch (type_) {
      case LOWPASS:
        b0_ = (1.0f - cw) / 2.0f;
        b1_ = 1.0f - cw;
        b2_ = (1.0f - cw) / 2.0f;
        a0 = 1.0f + alpha;
        a1_ = -2.0f * cw;
        a2_ = 1.0f - alpha;
        break;
      case HIGHPASS:
        b0_ = (1.0f + cw) / 2.0f;
        b1_ = -(1.0f + cw);
        b2_ = (1.0f + cw) / 2.0f;
        a0 = 1.0f + alpha;
        a1_ = -2.0f * cw;
        a2_ = 1.0f - alpha;
        break;
      case BANDPASS:
      default:
        b0_ = alpha;
        b1_ = 0.0f;
        b2_ = -alpha;
        a0 = 1.0f + alpha;
        a1_ = -2.0f * cw;
        a2_ = 1.0f - alpha;
        break;
    }
    // Normalize
    b0_ /= a0;
    b1_ /= a0;
    b2_ /= a0;
    a1_ /= a0;
    a2_ /= a0;
  }

  Type type_ = LOWPASS;
  float cutoff_ = 20000.0f;
  float q_ = 0.707f;
  float sr_ = 44100.0f;
  float b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0;
  float x1_ = 0, x2_ = 0, y1_ = 0, y2_ = 0;
};

}  // namespace hibiki
