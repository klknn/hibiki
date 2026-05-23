#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

/**
 * @brief A built-in TR-808 inspired Cowbell Synthesizer.
 *
 * It uses two detuned square-wave oscillators passed through a bandpass filter
 * and modulated by an exponential decay envelope.
 */
class BuiltinDr8Cowbell : public IPlugin {
 public:
  static constexpr int kTotalParams = 4;
  static constexpr const char* kPath = "builtin://dr8_cowbell";
  static constexpr const char* kName = "DR8 Cowbell";

  enum ParamId {
    PARAM_PITCH = 0,  ///< Base pitch frequency (400 Hz to 700 Hz)
    PARAM_DECAY = 1,  ///< Amplitude envelope decay time (0.05s to 0.5s)
    PARAM_DETUNE =
        2,  ///< Detune amount between the two square waves (0.0 to 1.0)
    PARAM_VOLUME = 3,  ///< Master output volume (0.0 to 1.0)
  };

  /**
   * @brief Constructs a new BuiltinDr8Cowbell instrument.
   */
  BuiltinDr8Cowbell();

  /**
   * @brief Destructor.
   */
  ~BuiltinDr8Cowbell() override;

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
  static float normToDetuneRatio(double norm);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace hibiki
