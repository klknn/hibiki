#include "engine/instruments/multi_point_envelope.hpp"

#include <algorithm>
#include <cmath>

namespace hibiki {

float MultiPointEnvelope::normToTime(float norm, float max_s) {
  return 0.001f * std::pow(max_s / 0.001f, norm);
}

float MultiPointEnvelope::interpolate(float t, float tension) {
  // t in [0..1], tension in [-1..+1].
  // tension > 0: ease-in (slow start, fast end) — t^(1+tension*3)
  // tension < 0: ease-out (fast start, slow end) — 1-(1-t)^(1+|tension|*3)
  // tension == 0: linear
  t = std::clamp(t, 0.0f, 1.0f);
  if (std::abs(tension) < 0.01f) return t;
  float exponent = 1.0f + std::abs(tension) * 3.0f;
  if (tension > 0) {
    return std::pow(t, exponent);
  } else {
    return 1.0f - std::pow(1.0f - t, exponent);
  }
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

  // Create 4 points: origin, peak, sustain, release-end.
  // Sustain marker at index 2 (the sustain point).
  points_ = {
      {0.0f, 0.0f, 0.0f},         // 0: origin
      {attack_s, 1.0f, 0.0f},     // 1: attack peak
      {decay_s, sustain, 0.0f},    // 2: sustain level (hold here)
      {release_s, 0.0f, 0.0f},    // 3: release end
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
    // Jump to the release phase (segment after sustain point).
    stage_ = Stage::RELEASING;
    current_segment_ = sustain_index_;
    segment_time_ = 0.0f;
    // Override the start value to current value for smooth transition.
    // We'll handle this in process() by using value_ as the start.
  } else {
    // No release section defined — go idle.
    value_ = 0.0f;
    stage_ = Stage::IDLE;
  }
}

float MultiPointEnvelope::process(float sample_rate) {
  if (points_.size() < 2 || stage_ == Stage::IDLE) {
    value_ = 0.0f;
    return value_;
  }

  if (stage_ == Stage::SUSTAIN) {
    // Hold at sustain point value.
    return value_;
  }

  float dt = 1.0f / sample_rate;

  if (stage_ == Stage::PLAYING) {
    // Playing through points [0..sustain_index].
    if (current_segment_ >= (int)points_.size() - 1) {
      // Reached end of all points.
      value_ = points_.back().value;
      stage_ = Stage::IDLE;
      return value_;
    }

    // Check if we hit the sustain point.
    if (sustain_index_ >= 0 && current_segment_ >= sustain_index_) {
      value_ = points_[sustain_index_].value;
      stage_ = Stage::SUSTAIN;
      return value_;
    }

    const Point& p0 = points_[current_segment_];
    const Point& p1 = points_[current_segment_ + 1];
    float seg_duration = std::max(0.001f, p1.time);

    segment_time_ += dt;
    float t = std::clamp(segment_time_ / seg_duration, 0.0f, 1.0f);
    float curved_t = interpolate(t, p0.tension);
    value_ = p0.value + (p1.value - p0.value) * curved_t;

    if (segment_time_ >= seg_duration) {
      current_segment_++;
      segment_time_ = 0.0f;
      // Check sustain again after advancing.
      if (sustain_index_ >= 0 && current_segment_ >= sustain_index_) {
        value_ = points_[sustain_index_].value;
        stage_ = Stage::SUSTAIN;
      }
    }
    return value_;
  }

  if (stage_ == Stage::RELEASING) {
    // Playing through points [sustain_index..end], but the first segment
    // starts from the current value (for smooth release from any level).
    if (current_segment_ >= (int)points_.size() - 1) {
      value_ = points_.back().value;
      stage_ = Stage::IDLE;
      return value_;
    }

    const Point& p0 = points_[current_segment_];
    const Point& p1 = points_[current_segment_ + 1];
    float seg_duration = std::max(0.001f, p1.time);

    segment_time_ += dt;
    float t = std::clamp(segment_time_ / seg_duration, 0.0f, 1.0f);
    float curved_t = interpolate(t, p0.tension);

    // For the first release segment, start from current value_ (set at
    // noteOff) instead of p0.value, for a smooth transition.
    float start_val =
        (current_segment_ == sustain_index_) ? value_ : p0.value;
    // After the first segment, we recalculate start_val from p0.
    // But on the very first call, value_ is still the sustain value.
    if (segment_time_ <= dt * 2.0f && current_segment_ == sustain_index_) {
      start_val = value_;  // use held value
    }
    value_ = start_val + (p1.value - start_val) * curved_t;

    if (segment_time_ >= seg_duration) {
      current_segment_++;
      segment_time_ = 0.0f;
      if (current_segment_ >= (int)points_.size() - 1) {
        value_ = points_.back().value;
        stage_ = Stage::IDLE;
      }
    }
    return value_;
  }

  return value_;
}

}  // namespace hibiki
