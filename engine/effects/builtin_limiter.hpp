#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

struct LimiterDspState {
  float lookahead_l[960];
  float lookahead_r[960];
  int la_write;
  float envelope;
  float gain_reduction_db;
  void reset();
};

float d_limiter_norm_to_ceiling_db(double norm);
float d_limiter_norm_to_release_ms(double norm);
float d_limiter_norm_to_lookahead_ms(double norm);
float d_limiter_norm_to_gain_db(double norm);

void d_limiter_process(LimiterDspState& state, const float* in_l,
                       const float* in_r, float* out_l, float* out_r,
                       int num_samples, double sample_rate, double ceiling_norm,
                       double release_norm, double lookahead_norm,
                       double gain_norm, bool link_stereo, bool enabled);

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

  LimiterDspState dsp_state_{};

  void reset();
};

}  // namespace hibiki
