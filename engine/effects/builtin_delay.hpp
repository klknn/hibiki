#pragma once

#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

// Stereo delay with ping-pong mode, feedback filters, and tempo-sync option.
class BuiltinDelay : public IPlugin {
 public:
  static constexpr int kTotalParams = 8;
  static constexpr const char* kPath = "builtin://delay";
  static constexpr const char* kName = "Delay";

  enum ParamId {
    PARAM_TIME_L = 0,
    PARAM_TIME_R = 1,
    PARAM_FEEDBACK = 2,
    PARAM_MIX = 3,
    PARAM_HP_FREQ = 4,
    PARAM_LP_FREQ = 5,
    PARAM_PING_PONG = 6,
    PARAM_ENABLE = 7,
  };

  BuiltinDelay();

  // IPlugin interface
  bool load(const std::string& path, int plugin_index = 0,
            double sample_rate = 44100.0) override;
  void showEditor() override {}
  void stopEditor() override {}
  void process(float** inputs, float** outputs, int num_samples,
               const HostProcessContext& context,
               const std::vector<MidiNoteEvent>& events) override;
  int getParameterCount() const override;
  bool getParameterInfo(int index, VstParamInfo& info) const override;
  void setParameterValue(uint32_t id, double value) override;
  double getParameterValue(uint32_t id) const override;
  const std::string& getName() const override;
  const std::string& getPath() const override;
  int getPluginIndex() const override;
  bool isInstrument() const override;

  // Parameter mapping
  static float normToTimeMs(double norm);
  static float normToHpFreq(double norm);
  static float normToLpFreq(double norm);

 private:
  static constexpr int kMaxDelaySamples = 384000;  // 2s @ 192kHz

  double params_[kTotalParams] = {};
  double sample_rate_ = 44100.0;
  bool enabled_ = true;

  // Delay buffers
  std::vector<float> buffer_l_;
  std::vector<float> buffer_r_;
  int write_pos_ = 0;

  // 1-pole feedback filters
  float hp_state_l_ = 0, hp_state_r_ = 0;
  float lp_state_l_ = 0, lp_state_r_ = 0;

  void reset();
};

}  // namespace hibiki
