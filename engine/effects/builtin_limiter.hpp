#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

// Brickwall lookahead limiter for mastering.
// Uses a lookahead buffer to achieve transparent limiting.
class BuiltinLimiter : public IPlugin {
 public:
  static constexpr int kTotalParams = 6;
  static constexpr const char* kPath = "builtin://limiter";
  static constexpr const char* kName = "Limiter";

  enum ParamId {
    PARAM_CEILING = 0,
    PARAM_RELEASE = 1,
    PARAM_LOOKAHEAD = 2,
    PARAM_GAIN = 3,
    PARAM_LINK_STEREO = 4,
    PARAM_ENABLE = 5,
  };

  BuiltinLimiter();

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

  // For UI metering
  float getGainReductionDb() const;

  // Parameter mapping
  static float normToCeilingDb(double norm);
  static float normToReleaseMs(double norm);
  static float normToLookaheadMs(double norm);
  static float normToGainDb(double norm);

 private:
  static constexpr int kMaxLookahead = 960;  // 5ms @ 192kHz

  double params_[kTotalParams] = {};
  double sample_rate_ = 44100.0;
  bool enabled_ = true;

  // Lookahead delay line
  std::vector<float> lookahead_l_, lookahead_r_;
  int la_write_ = 0;

  // Gain reduction envelope
  float envelope_ = 1.0f;  // linear gain
  std::atomic<float> gain_reduction_db_{0.0f};

  void reset();
};

}  // namespace hibiki
