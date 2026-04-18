#include "engine/instruments/adsr.hpp"

#include <algorithm>
#include <cmath>

namespace hibiki {
namespace {
// Log-scale mapping: 0->0.001s, 1->max_s
float normToTime(float norm, float max_s) {
  return 0.001f * std::pow(max_s / 0.001f, norm);
}
}  // namespace

void Adsr::setParams(float attack_s, float decay_s, float sustain,
                     float release_s) {
  attack_ = std::max(0.001f, attack_s);
  decay_ = std::max(0.001f, decay_s);
  sustain_ = std::clamp(sustain, 0.0f, 1.0f);
  release_ = std::max(0.001f, release_s);
}

// Map normalized 0..1 params to real times
void Adsr::setNormalized(float a, float d, float s, float r) {
  setParams(normToTime(a, 5.0f), normToTime(d, 5.0f), s, normToTime(r, 10.0f));
}

void Adsr::noteOn() {
  stage_ = ENV_ATTACK;
  phase_ = 0.0f;
}

void Adsr::noteOff() {
  if (stage_ != ENV_IDLE) {
    release_start_ = value_;
    stage_ = ENV_RELEASE;
    phase_ = 0.0f;
  }
}

// Returns envelope value [0..1], call once per sample
float Adsr::process(float sample_rate) {
  float dt = 1.0f / sample_rate;
  switch (stage_) {
    case ENV_ATTACK:
      phase_ += dt;
      value_ = phase_ / attack_;
      if (value_ >= 1.0f) {
        value_ = 1.0f;
        stage_ = ENV_DECAY;
        phase_ = 0.0f;
      }
      break;
    case ENV_DECAY:
      phase_ += dt;
      value_ = 1.0f - (1.0f - sustain_) * (phase_ / decay_);
      if (phase_ >= decay_) {
        value_ = sustain_;
        stage_ = ENV_SUSTAIN;
      }
      break;
    case ENV_SUSTAIN:
      value_ = sustain_;
      break;
    case ENV_RELEASE:
      phase_ += dt;
      value_ = release_start_ * (1.0f - phase_ / release_);
      if (value_ <= 0.0f || phase_ >= release_) {
        value_ = 0.0f;
        stage_ = ENV_IDLE;
      }
      break;
    case ENV_IDLE:
    default:
      value_ = 0.0f;
      break;
  }
  return value_;
}

}  // namespace hibiki