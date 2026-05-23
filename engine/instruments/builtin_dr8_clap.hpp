#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

/**
 * @brief A built-in TR-808 inspired Hand Clap Synthesizer.
 *
 * It uses a bandpass-filtered noise generator modulated by a specialized
 * multi-trigger amplitude envelope (3 rapid pre-claps followed by a main decay
 * tail).
 */
class BuiltinDr8Clap : public IPlugin {
 public:
  static constexpr int kTotalParams = 4;
  static constexpr const char* kPath = "builtin://dr8_clap";
  static constexpr const char* kName = "DR8 Clap";

  enum ParamId {
    PARAM_DECAY = 0,          ///< Main tail decay time (0.05s to 1.0s)
    PARAM_FILTER_CUTOFF = 1,  ///< Bandpass filter frequency (500 Hz to 3000 Hz)
    PARAM_SPREAD = 2,         ///< Micro-trigger spacing (5ms to 20ms)
    PARAM_VOLUME = 3,         ///< Master output volume (0.0 to 1.0)
  };

  /**
   * @brief Constructs a new BuiltinDr8Clap instrument.
   */
  BuiltinDr8Clap();

  /**
   * @brief Destructor.
   */
  ~BuiltinDr8Clap() override;

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

  // Parameter conversion helpers
  static float normToDecayS(double norm);
  static float normToCutoffHz(double norm);
  static float normToSpreadS(double norm);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace hibiki
