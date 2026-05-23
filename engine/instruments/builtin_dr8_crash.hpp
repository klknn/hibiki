#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

/**
 * @brief A built-in TR-808 inspired Crash Cymbal Synthesizer.
 *
 * It uses a sum of 6 detuned square-wave oscillators mixed with high-pass
 * filtered white noise, processed through parallel bandpass filters and
 * modulated by an exponential decay envelope.
 */
class BuiltinDr8Crash : public IPlugin {
 public:
  static constexpr int kTotalParams = 4;
  static constexpr const char* kPath = "builtin://dr8_crash";
  static constexpr const char* kName = "DR8 Crash";

  enum ParamId {
    PARAM_DECAY = 0,  ///< Amplitude envelope decay time (0.2s to 3.0s)
    PARAM_TONE =
        1,  ///< Balance between metallic sound (0.0) and noise sizzle (1.0)
    PARAM_TENSION =
        2,             ///< Detune tension of the metal oscillators (0.0 to 1.0)
    PARAM_VOLUME = 3,  ///< Master output volume (0.0 to 1.0)
  };

  /**
   * @brief Constructs a new BuiltinDr8Crash instrument.
   */
  BuiltinDr8Crash();

  /**
   * @brief Destructor.
   */
  ~BuiltinDr8Crash() override;

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

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace hibiki
