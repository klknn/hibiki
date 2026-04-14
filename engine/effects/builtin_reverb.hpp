#pragma once

#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

// Algorithmic reverb (Freeverb/Schroeder topology).
// 8 parallel comb filters + 4 series allpass filters per channel.
class BuiltinReverb : public IPlugin {
 public:
  static constexpr int kTotalParams = 8;
  static constexpr const char* kPath = "builtin://reverb";
  static constexpr const char* kName = "Reverb";

  enum ParamId {
    PARAM_ROOM_SIZE = 0,
    PARAM_DAMPING = 1,
    PARAM_MIX = 2,
    PARAM_PRE_DELAY = 3,
    PARAM_HP_FREQ = 4,
    PARAM_LP_FREQ = 5,
    PARAM_WIDTH = 6,
    PARAM_ENABLE = 7,
  };

  BuiltinReverb();

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
  static float normToPreDelayMs(double norm);

 private:
  static constexpr int kNumCombs = 8;
  static constexpr int kNumAllpasses = 4;
  static constexpr int kMaxPreDelay = 19200;  // 100ms @ 192kHz

  struct CombFilter {
    std::vector<float> buffer;
    int size = 0;
    int pos = 0;
    float feedback = 0;
    float damp1 = 0, damp2 = 0;
    float filter_store = 0;
  };

  struct AllpassFilter {
    std::vector<float> buffer;
    int size = 0;
    int pos = 0;
  };

  double params_[kTotalParams] = {};
  double sample_rate_ = 44100.0;
  bool enabled_ = true;

  CombFilter combs_l_[kNumCombs];
  CombFilter combs_r_[kNumCombs];
  AllpassFilter allpasses_l_[kNumAllpasses];
  AllpassFilter allpasses_r_[kNumAllpasses];

  // Pre-delay buffer
  std::vector<float> predelay_l_, predelay_r_;
  int predelay_pos_ = 0;

  // Output filters
  float hp_state_l_ = 0, hp_state_r_ = 0;
  float lp_state_l_ = 0, lp_state_r_ = 0;

  void reset();
  void initBuffers();
  void updateParams();

  static float processComb(CombFilter& c, float input);
  static float processAllpass(AllpassFilter& a, float input);
};

}  // namespace hibiki
