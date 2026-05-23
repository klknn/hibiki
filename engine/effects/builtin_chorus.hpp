#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

/**
 * @brief A built-in stereo Chorus effect.
 *
 * It uses dual delay lines modulated by a multi-phase LFO to create
 * pitch modulation, detuning, and stereo widening.
 */
class BuiltinChorus : public IPlugin {
 public:
  static constexpr int kTotalParams = 6;
  static constexpr const char* kPath = "builtin://chorus";
  static constexpr const char* kName = "Chorus";

  enum ParamId {
    PARAM_RATE = 0,      ///< LFO Modulation Rate (0.1 to 10.0 Hz)
    PARAM_DEPTH = 1,     ///< LFO Modulation Depth (0.0 to 1.0)
    PARAM_DELAY = 2,     ///< Base Delay Offset (5 to 30 ms)
    PARAM_FEEDBACK = 3,  ///< Feedback amount (0.0 to 0.95)
    PARAM_WET_DRY = 4,   ///< Wet/Dry mix ratio (0.0 to 1.0)
    PARAM_ENABLE = 5,    ///< Bypass enable flag (0.0 or 1.0)
  };

  /**
   * @brief Constructs a new BuiltinChorus with default parameters.
   */
  BuiltinChorus();

  /**
   * @brief Destructor.
   */
  ~BuiltinChorus() override;

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
  static float normToRateHz(double norm);
  static float normToDepthMs(double norm);
  static float normToDelayMs(double norm);
  static float normToFeedback(double norm);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace hibiki
