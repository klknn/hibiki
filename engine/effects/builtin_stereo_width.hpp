#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

/**
 * @brief A built-in Haas Stereo Enhancer effect.
 *
 * This effect delays one channel relative to the other to create spatial width
 * (the Haas effect), combined with a low-crossover filter to keep low-end
 * frequencies centered in mono and prevent phase cancellation, and a mid-side
 * width adjustment.
 */
class BuiltinStereoWidth : public IPlugin {
 public:
  static constexpr int kTotalParams = 5;
  static constexpr const char* kPath = "builtin://stereo_width";
  static constexpr const char* kName = "Stereo Width";

  enum ParamId {
    PARAM_DELAY = 0,      ///< Delay time (0 to 40 ms)
    PARAM_CHANNEL = 1,    ///< Delayed Channel (0.0 = Left, 1.0 = Right)
    PARAM_MONO_FREQ = 2,  ///< Mono crossover cutoff frequency (50 to 500 Hz)
    PARAM_WIDTH = 3,      ///< Mid/Side Width multiplier (0.0 to 2.0)
    PARAM_ENABLE = 4,     ///< Bypass enable flag (0.0 or 1.0)
  };

  /**
   * @brief Constructs a new BuiltinStereoWidth with default parameters.
   */
  BuiltinStereoWidth();

  /**
   * @brief Destructor.
   */
  ~BuiltinStereoWidth() override;

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

  // Parameter mappings
  static float normToDelayMs(double norm);
  static float normToMonoFreq(double norm);
  static float normToWidth(double norm);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace hibiki
