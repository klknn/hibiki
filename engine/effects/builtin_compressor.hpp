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
  static constexpr int kTotalParams = 7;
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
  };

  BuiltinCompressor();

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

  // For UI metering
  float getGainReductionDb() const;
  float getInputDb() const;
  float getOutputDb() const;

  // For UI transfer curve rendering
  float computeOutputDb(float input_db) const;

  // Parameter mapping (public for tests)
  static float normToThreshold(double norm);
  static float normToRatio(double norm);
  static float normToAttack(double norm);
  static float normToRelease(double norm);

 private:
  double params_[kTotalParams] = {};
  double sample_rate_ = 44100.0;
  bool enabled_ = true;
  float envelope_db_ = 0.0f;
  float gain_reduction_db_ = 0.0f;
  std::atomic<float> input_db_{-200.0f};
  std::atomic<float> output_db_{-200.0f};

  void reset();
  static float computeGainReduction(float input_db, float threshold,
                                    float ratio, float knee_db);
};

}  // namespace hibiki
