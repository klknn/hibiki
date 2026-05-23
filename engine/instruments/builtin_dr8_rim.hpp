#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

/**
 * @brief A built-in TR-808 inspired Rimshot Synthesizer.
 *
 * It uses three bandpass-filtered oscillators (sines) tuned at fundamental f,
 * 2.6 * f, and 4.6 * f, modulated by extremely short exponential decay
 * envelopes.
 */
class BuiltinDr8Rim : public IPlugin {
 public:
  static constexpr int kTotalParams = 3;
  static constexpr const char* kPath = "builtin://dr8_rim";
  static constexpr const char* kName = "DR8 Rimshot";

  enum ParamId {
    PARAM_PITCH = 0,   ///< Base fundamental frequency (200 Hz to 500 Hz)
    PARAM_DECAY = 1,   ///< Envelope decay time (0.01s to 0.1s)
    PARAM_VOLUME = 2,  ///< Master output volume (0.0 to 1.0)
  };

  /**
   * @brief Constructs a new BuiltinDr8Rim instrument.
   */
  BuiltinDr8Rim();

  /**
   * @brief Destructor.
   */
  ~BuiltinDr8Rim() override;

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
  static float normToPitchHz(double norm);
  static float normToDecayS(double norm);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace hibiki
