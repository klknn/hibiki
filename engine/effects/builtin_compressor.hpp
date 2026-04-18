#pragma once

#include <atomic>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

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
  float envelope_db_ = 0.0f;
  float gain_reduction_db_ = 0.0f;
  std::atomic<float> input_db_{-200.0f};
  std::atomic<float> output_db_{-200.0f};
  std::atomic<float> sidechain_db_{-200.0f};

  // RMS detection ring buffer (~3ms window at 44.1kHz ≈ 128 samples)
  static constexpr int kRmsWindowSize = 128;
  float rms_buf_L_[kRmsWindowSize] = {};
  float rms_buf_R_[kRmsWindowSize] = {};
  float rms_sum_L_ = 0.0f;
  float rms_sum_R_ = 0.0f;
  int rms_index_ = 0;
  int64_t last_time_samples_ = -1;  // For transport discontinuity detection

  void reset();
  static float computeGainReduction(float input_db, float threshold,
                                    float ratio, float knee_db,
                                    float up_threshold, float up_ratio);
};

}  // namespace hibiki
