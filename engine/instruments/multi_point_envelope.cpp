#include "engine/instruments/multi_point_envelope.hpp"

#include <algorithm>

namespace hibiki {

struct EnvPointD {
  float time;
  float value;
  float tension;
};

extern float d_env_norm_to_time(float norm, float max_s);
extern float d_env_interpolate(float t, float tension);
extern float d_env_process(const EnvPointD* points, int num_points,
                           int sustain_index, int* stage, int* current_segment,
                           float* segment_time, float* value,
                           float sample_rate);

float MultiPointEnvelope::normToTime(float norm, float max_s) {
  return d_env_norm_to_time(norm, max_s);
}

float MultiPointEnvelope::interpolate(float t, float tension) {
  return d_env_interpolate(t, tension);
}

void MultiPointEnvelope::setPoints(const std::vector<Point>& points,
                                   int sustain_index) {
  points_ = points;
  sustain_index_ = sustain_index;
}

void MultiPointEnvelope::setFromADSR(float a_norm, float d_norm, float s,
                                     float r_norm) {
  setFromADSRTimes(normToTime(a_norm, 5.0f), normToTime(d_norm, 5.0f),
                   std::clamp(s, 0.0f, 1.0f), normToTime(r_norm, 10.0f));
}

void MultiPointEnvelope::setFromADSRTimes(float attack_s, float decay_s,
                                          float sustain, float release_s) {
  attack_s = std::max(0.001f, attack_s);
  decay_s = std::max(0.001f, decay_s);
  sustain = std::clamp(sustain, 0.0f, 1.0f);
  release_s = std::max(0.001f, release_s);

  points_ = {
      {0.0f, 0.0f, 0.0f},
      {attack_s, 1.0f, 0.0f},
      {decay_s, sustain, 0.0f},
      {release_s, 0.0f, 0.0f},
  };
  sustain_index_ = 2;
}

void MultiPointEnvelope::noteOn() {
  stage_ = Stage::PLAYING;
  current_segment_ = 0;
  segment_time_ = 0.0f;
  value_ = points_.empty() ? 0.0f : points_[0].value;
}

void MultiPointEnvelope::noteOff() {
  if (stage_ == Stage::IDLE) return;

  if (sustain_index_ >= 0 && sustain_index_ + 1 < (int)points_.size()) {
    stage_ = Stage::RELEASING;
    current_segment_ = sustain_index_;
    segment_time_ = 0.0f;
  } else {
    value_ = 0.0f;
    stage_ = Stage::IDLE;
  }
}

float MultiPointEnvelope::process(float sample_rate) {
  int stage_int = static_cast<int>(stage_);
  float res = d_env_process(reinterpret_cast<const EnvPointD*>(points_.data()),
                            static_cast<int>(points_.size()), sustain_index_,
                            &stage_int, &current_segment_, &segment_time_,
                            &value_, sample_rate);
  stage_ = static_cast<Stage>(stage_int);
  return res;
}

}  // namespace hibiki
