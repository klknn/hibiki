#pragma once

#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

struct DelayDspState {
  int write_pos = 0;
  float hp_state_l = 0.0f;
  float hp_state_r = 0.0f;
  float lp_state_l = 0.0f;
  float lp_state_r = 0.0f;
  float input_rms = 0.0f;
  float output_rms = 0.0f;

  void reset();
};

float d_delay_norm_to_time_ms(double norm);
float d_delay_norm_to_hp_freq(double norm);
float d_delay_norm_to_lp_freq(double norm);
float d_delay_norm_to_division_beats(double norm);
const char* d_delay_norm_to_division_label(double norm);

void d_delay_process(DelayDspState& state, float* buf_l, float* buf_r,
                     int buf_size, const float* in_l, const float* in_r,
                     float* out_l, float* out_r, int num_samples,
                     double sample_rate, double tempo, double time_l_norm,
                     double time_r_norm, double feedback_norm, double mix_norm,
                     double hp_norm, double lp_norm, double ping_pong_norm,
                     bool sync, bool enabled);

// Stereo delay with ping-pong mode, feedback filters, and tempo-sync option.
class BuiltinDelay : public IPlugin {
 public:
  static constexpr int kTotalParams = 10;
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
    PARAM_SYNC = 8,
    PARAM_SYNC_DIV = 9,
  };

  BuiltinDelay();

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

  // Parameter mapping
  static float normToTimeMs(double norm);
  static float normToHpFreq(double norm);
  static float normToLpFreq(double norm);

  // Returns delay time in beats for synchronized mode.
  static float normToDivisionBeats(double norm);
  // Labels for display ("1/4", "1/8 T", etc.)
  static const char* normToDivisionLabel(double norm);

  // Metering (read from UI thread)
  float getInputDb() const;
  float getOutputDb() const;

 private:
  static constexpr int kMaxDelaySamples = 384000;  // 2s @ 192kHz

  double params_[kTotalParams] = {};
  double sample_rate_ = 44100.0;
  bool enabled_ = true;

  // Delay buffers
  std::vector<float> buffer_l_;
  std::vector<float> buffer_r_;

  DelayDspState dsp_state_{};

  void reset();
};

}  // namespace hibiki
