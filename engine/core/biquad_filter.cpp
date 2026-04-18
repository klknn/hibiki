#include "engine/core/biquad_filter.hpp"

namespace hibiki {

void BiquadFilter::setParams(Type type, float cutoff_hz, float q, float gain_db,
                             float sample_rate) {
  if (cutoff_hz == cutoff_ && q == q_ && type == type_ && sample_rate == sr_ &&
      gain_db == gain_db_)
    return;
  type_ = type;
  cutoff_ = cutoff_hz;
  q_ = q;
  gain_db_ = gain_db;
  sr_ = sample_rate;
  recalc();
}

void BiquadFilter::setModulatedCutoff(float base_hz, float depth,
                                      float env_value, float q, float gain_db,
                                      float sample_rate) {
  float mod_oct = depth * env_value * 4.0f;  // ±4 octaves
  float mod_hz = base_hz * std::pow(2.0f, mod_oct);
  mod_hz = std::max(20.0f, std::min(20000.0f, mod_hz));
  setParams(type_, mod_hz, q, gain_db, sample_rate);
}

float BiquadFilter::normToCutoff(float norm) {
  return 20.0f * std::pow(1000.0f, norm);
}

float BiquadFilter::normToQ(float norm) { return 0.5f + norm * 19.5f; }

BiquadFilter::Type BiquadFilter::normToType(float norm) {
  if (norm < 0.2f) return LOWPASS;
  if (norm < 0.4f) return HIGHPASS;
  if (norm < 0.6f) return BANDPASS;
  if (norm < 0.8f) return LOW_SHELF;
  return HIGH_SHELF;
  // Note: For BuiltinEq, it overrides type mapping entirely.
}

// For BuiltinEq. TODO: Unify with normToType.
BiquadFilter::Type BiquadFilter::normToTypeV2(float norm) {
  if (norm < 0.1f)
    return OFF;
  else if (norm < 0.3f)
    return LOWPASS;
  else if (norm < 0.5f)
    return HIGHPASS;
  else if (norm < 0.7f)
    return LOW_SHELF;
  else if (norm < 0.9f)
    return HIGH_SHELF;
  else
    return BELL;
}

void BiquadFilter::recalc() {
  float w0 = 2.0f * 3.14159265f * cutoff_ / sr_;
  float cw = std::cos(w0);
  float sw = std::sin(w0);
  float alpha = sw / (2.0f * q_);
  float A = std::pow(10.0f, gain_db_ / 40.0f);

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
      b0_ = alpha;
      b1_ = 0.0f;
      b2_ = -alpha;
      a0 = 1.0f + alpha;
      a1_ = -2.0f * cw;
      a2_ = 1.0f - alpha;
      break;
    case LOW_SHELF: {
      float sqA = std::sqrt(A);
      float two_sqA_alpha = 2.0f * sqA * alpha;
      b0_ = A * ((A + 1.0f) - (A - 1.0f) * cw + two_sqA_alpha);
      b1_ = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cw);
      b2_ = A * ((A + 1.0f) - (A - 1.0f) * cw - two_sqA_alpha);
      a0 = (A + 1.0f) + (A - 1.0f) * cw + two_sqA_alpha;
      a1_ = -2.0f * ((A - 1.0f) + (A + 1.0f) * cw);
      a2_ = (A + 1.0f) + (A - 1.0f) * cw - two_sqA_alpha;
      break;
    }
    case HIGH_SHELF: {
      float sqA = std::sqrt(A);
      float two_sqA_alpha = 2.0f * sqA * alpha;
      b0_ = A * ((A + 1.0f) + (A - 1.0f) * cw + two_sqA_alpha);
      b1_ = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cw);
      b2_ = A * ((A + 1.0f) + (A - 1.0f) * cw - two_sqA_alpha);
      a0 = (A + 1.0f) - (A - 1.0f) * cw + two_sqA_alpha;
      a1_ = 2.0f * ((A - 1.0f) - (A + 1.0f) * cw);
      a2_ = (A + 1.0f) - (A - 1.0f) * cw - two_sqA_alpha;
      break;
    }
    case BELL:
    default:
      b0_ = 1.0f + alpha * A;
      b1_ = -2.0f * cw;
      b2_ = 1.0f - alpha * A;
      a0 = 1.0f + alpha / A;
      a1_ = -2.0f * cw;
      a2_ = 1.0f - alpha / A;
      break;
  }
  // Normalize
  b0_ /= a0;
  b1_ /= a0;
  b2_ /= a0;
  a1_ /= a0;
  a2_ /= a0;
}

float BiquadFilter::process(float x) {
  if (type_ == OFF) return x;
  float y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
  x2_ = x1_;
  x1_ = x;
  y2_ = y1_;
  y1_ = y;
  return y;
}

float BiquadFilter::getMagnitudeSq(double cos_w, double cos_2w, double sin_w,
                                   double sin_2w) const {
  if (type_ == OFF) return 1.0f;
  double num_re = b0_ + b1_ * cos_w + b2_ * cos_2w;
  double num_im = -(b1_ * sin_w + b2_ * sin_2w);
  double den_re = 1.0 + a1_ * cos_w + a2_ * cos_2w;
  double den_im = -(a1_ * sin_w + a2_ * sin_2w);
  return (float)((num_re * num_re + num_im * num_im) /
                 (den_re * den_re + den_im * den_im));
}

}  // namespace hibiki
