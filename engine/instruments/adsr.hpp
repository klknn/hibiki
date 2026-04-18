#pragma once

namespace hibiki {

// ADSR envelope generator. One instance per voice.
// All times are in seconds. Call noteOn/noteOff per voice,
// then process() each sample to get the current envelope value (0..1).
class Adsr {
 public:
  enum Stage { ENV_IDLE, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

  void setParams(float attack_s, float decay_s, float sustain, float release_s);

  // Map normalized 0..1 params to real times
  void setNormalized(float a, float d, float s, float r);

  void noteOn();

  void noteOff();

  // Returns envelope value [0..1], call once per sample
  float process(float sample_rate);

  bool isIdle() const { return stage_ == ENV_IDLE; }
  float getValue() const { return value_; }
  Stage getStage() const { return stage_; }

 private:
  Stage stage_ = ENV_IDLE;
  float attack_ = 0.01f;
  float decay_ = 0.1f;
  float sustain_ = 0.7f;
  float release_ = 0.3f;
  float phase_ = 0.0f;
  float value_ = 0.0f;
  float release_start_ = 0.0f;
};

}  // namespace hibiki
