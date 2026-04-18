#pragma once

#include <algorithm>
#include <cmath>

namespace hibiki {

// Biquad filter with LP/HP/BP modes.
// Uses Robert Bristow-Johnson's Audio EQ Cookbook formulas.
// One instance per voice per channel.
class BiquadFilter {
 public:
  enum Type {
    OFF = -1,
    LOWPASS = 0,
    HIGHPASS,
    BANDPASS,
    LOW_SHELF,
    HIGH_SHELF,
    BELL
  };

  void setParams(Type type, float cutoff_hz, float q, float gain_db,
                 float sample_rate);

  // Modulate cutoff by envelope: base_hz * 2^(depth * env_value * 4)
  // depth in [-1, 1], env_value in [0, 1]
  void setModulatedCutoff(float base_hz, float depth, float env_value, float q,
                          float gain_db, float sample_rate);

  float process(float x);

  void reset() { x1_ = x2_ = y1_ = y2_ = 0.0f; }

  // Returns the squared magnitude response at a given frequency, precalculated
  // as trig values
  float getMagnitudeSq(double cos_w, double cos_2w, double sin_w,
                       double sin_2w) const;

  // Map normalized 0..1 to cutoff Hz (log: 20Hz..20kHz)
  static float normToCutoff(float norm);

  // Map normalized 0..1 to Q (0.5..20)
  static float normToQ(float norm);

  // Map normalized 0..1 to filter type
  static Type normToType(float norm);
  static Type normToTypeV2(float norm);

 private:
  void recalc();

  Type type_ = LOWPASS;
  float cutoff_ = 20000.0f;
  float q_ = 0.707f;
  float gain_db_ = 0.0f;
  float sr_ = 44100.0f;
  float b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0;
  float x1_ = 0, x2_ = 0, y1_ = 0, y2_ = 0;
};

}  // namespace hibiki
