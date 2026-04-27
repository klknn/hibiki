#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace hibiki {

// Multi-point envelope generator with per-segment tension curves and
// sustain/release markers. Replaces the fixed ADSR envelope.
// One instance per voice per envelope target.
class MultiPointEnvelope {
 public:
  struct Point {
    float time;     // seconds from envelope start (or from noteOff for release)
    float value;    // 0..1
    float tension;  // -1..+1: curve shape to NEXT point
                    // 0 = linear, >0 = ease-in, <0 = ease-out
  };

  // Set envelope from a list of points with sustain marker.
  // sustain_index: index of the point where envelope holds until noteOff.
  //   Points [0..sustain_index] play on noteOn.
  //   Points [sustain_index+1..end] play on noteOff (release phase).
  //   If sustain_index < 0, no sustain hold — envelope plays straight through.
  void setPoints(const std::vector<Point>& points, int sustain_index = -1);

  // Backward-compatible: create a 5-point envelope matching ADSR behavior.
  // Maps normalized 0..1 values to real times (same as Adsr::setNormalized).
  void setFromADSR(float a_norm, float d_norm, float s, float r_norm);

  // Set from real ADSR times in seconds.
  void setFromADSRTimes(float attack_s, float decay_s, float sustain,
                        float release_s);

  void noteOn();
  void noteOff();

  // Returns envelope value [0..1], call once per sample.
  float process(float sample_rate);

  bool isIdle() const { return stage_ == Stage::IDLE; }
  float getValue() const { return value_; }

 private:
  enum class Stage { IDLE, PLAYING, SUSTAIN, RELEASING };

  // Interpolate between two points using tension curve.
  static float interpolate(float t, float tension);

  // Log-scale mapping: 0->0.001s, 1->max_s (same as Adsr).
  static float normToTime(float norm, float max_s);

  std::vector<Point> points_;
  int sustain_index_ = -1;

  Stage stage_ = Stage::IDLE;
  int current_segment_ = 0;    // index of start point of current segment
  float segment_time_ = 0.0f;  // time elapsed in current segment
  float value_ = 0.0f;
};

}  // namespace hibiki
