#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

struct CompressorDspState {
  float envelope_db;
  float gain_reduction_db;
  float input_db;
  float output_db;
  float sidechain_db;
  float rms_buf_l[128];
  float rms_buf_r[128];
  float rms_sum_l;
  float rms_sum_r;
  int rms_index;
  void reset();
};

float d_compressor_norm_to_threshold(double norm);
float d_compressor_norm_to_ratio(double norm);
float d_compressor_norm_to_attack(double norm);
float d_compressor_norm_to_release(double norm);
float d_compressor_norm_to_up_threshold(double norm);
float d_compressor_compute_gain_reduction(float input_db, float threshold,
                                          float ratio, float knee_db,
                                          float up_threshold, float up_ratio);
void d_compressor_process(CompressorDspState& state, const float* in_l,
                          const float* in_r, float* out_l, float* out_r,
                          int num_samples, double sample_rate,
                          double threshold_norm, double ratio_norm,
                          double attack_norm, double release_norm,
                          double knee_norm, double makeup_norm,
                          double up_threshold_norm, double up_ratio_norm,
                          bool use_rms, bool enabled, const float* sc_l,
                          const float* sc_r);

// Stereo compressor with hard/soft knee, implemented as an IPlugin.
// Features: adjustable threshold, ratio, attack, release, knee width,
// makeup gain. Provides gain reduction metering for UI.
class BuiltinCompressor : public IPlugin {
 public:
  static constexpr int kTotalParams = 10;
  static constexpr const char* kPath = "builtin://compressor";
  static constexpr const char* kName = "Compressor";

  enum ParamId {
    PARAM_THRESHOLD = 0,
    PARAM_RATIO = 1,
    PARAM_ATTACK = 2,
    PARAM_RELEASE = 3,
    PARAM_KNEE = 4,
    PARAM_MAKEUP = 5,
    PARAM_ENABLE = 6,
    PARAM_UP_THRESHOLD = 7,
    PARAM_UP_RATIO = 8,
    PARAM_RMS_MODE = 9,
  };

  BuiltinCompressor();

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
  float getInputDb() const;
  float getOutputDb() const;
  float getSidechainDb() const;

  // For UI transfer curve rendering
  float computeOutputDb(float input_db) const;

  // Parameter mapping (public for tests)
  static float normToThreshold(double norm);
  static float normToRatio(double norm);
  static float normToAttack(double norm);
  static float normToRelease(double norm);
  static float normToUpThreshold(double norm);

 private:
  double params_[kTotalParams] = {};
  double sample_rate_ = 44100.0;
  bool enabled_ = true;

  CompressorDspState dsp_state_{};

  void reset();
};

}  // namespace hibiki
