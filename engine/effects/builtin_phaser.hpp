#pragma once

#include <array>
#include <atomic>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

// Multi-mode allpass-based phase effect (Phaser / Chorus / Flanger /
// Ring-Mod / Disperser) with Lissajous scope output.
// Path: builtin://phaser
class BuiltinPhaser : public IPlugin {
 public:
  static constexpr int kTotalParams = 12;
  static constexpr const char* kPath = "builtin://phaser";
  static constexpr const char* kName = "Phaser";

  // Maximum allpass stages per channel
  static constexpr int kMaxStages = 12;
  // Scope buffer size for Lissajous display
  static constexpr int kScopeSize = 256;
  // Max comb delay for chorus/flanger modes (samples at 44.1kHz)
  static constexpr int kMaxCombDelay = 4096;

  enum ParamId {
    PARAM_RATE = 0,
    PARAM_DEPTH = 1,
    PARAM_FEEDBACK = 2,
    PARAM_STAGES = 3,
    PARAM_MIX = 4,
    PARAM_STEREO = 5,     // LFO phase offset between L/R
    PARAM_MODE = 6,       // 0–4: phaser/chorus/flanger/ringmod/disperser
    PARAM_LFO_SHAPE = 7,  // sin/tri/saw/square
    PARAM_CENTER = 8,     // Center freq (allpass tuning)
    PARAM_SPREAD = 9,     // Allpass freq spread
    PARAM_SYNC = 10,      // Tempo sync LFO
    PARAM_ENABLE = 11,
  };

  enum Mode {
    MODE_PHASER = 0,
    MODE_CHORUS = 1,
    MODE_FLANGER = 2,
    MODE_RINGMOD = 3,
    MODE_DISPERSER = 4,
  };

  BuiltinPhaser();

  // IPlugin interface
  bool load(const std::string& path, int plugin_index = 0,
            double sample_rate = 44100.0) override;
  void showEditor() override {}
  void stopEditor() override {}
  void process(float** inputs, float** outputs, int num_samples,
               const HostProcessContext& context,
               const std::vector<MidiNoteEvent>& events,
               float** sidechain = nullptr) override;
  int getParameterCount() const override;
  bool getParameterInfo(int index, VstParamInfo& info) const override;
  void setParameterValue(uint32_t id, double value) override;
  double getParameterValue(uint32_t id) const override;
  const std::string& getName() const override;
  const std::string& getPath() const override;
  int getPluginIndex() const override;
  bool isInstrument() const override;

  // Metering
  float getInputDb() const;
  float getOutputDb() const;

  // Scope data for Lissajous display (call from notification thread)
  void getScopeData(float* left, float* right, int size) const;

  // Parameter mapping
  static float normToRate(double norm);  // Hz
  static int normToStages(double norm);
  static int normToMode(double norm);

 private:
  double params_[kTotalParams] = {};
  double sample_rate_ = 44100.0;
  bool enabled_ = true;

  // LFO state
  double lfo_phase_ = 0.0;

  // Allpass filter states (per channel, per stage)
  float ap_state_l_[kMaxStages] = {};
  float ap_state_r_[kMaxStages] = {};

  // Feedback state
  float feedback_l_ = 0.0f;
  float feedback_r_ = 0.0f;

  // Comb delay buffers for chorus/flanger modes
  std::vector<float> comb_buf_l_;
  std::vector<float> comb_buf_r_;
  int comb_write_ = 0;

  // Scope ring buffer
  mutable std::array<float, kScopeSize> scope_l_ = {};
  mutable std::array<float, kScopeSize> scope_r_ = {};
  int scope_write_ = 0;

  // Metering
  std::atomic<float> input_rms_{0.0f};
  std::atomic<float> output_rms_{0.0f};

  void reset();

  // LFO generator
  float lfo(double phase, int shape) const;

  // First-order allpass: y[n] = a * x[n] + x[n-1] - a * y[n-1]
  static float allpass(float input, float coeff, float& state);
};

}  // namespace hibiki
