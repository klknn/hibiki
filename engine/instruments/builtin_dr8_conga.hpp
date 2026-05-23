#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

/**
 * @brief A built-in TR-808 inspired Conga Synthesizer.
 *
 * It uses a pitch-swept sine oscillator combined with a fast initial
 * noise/attack click passed through a lowpass filter to produce a warm conga
 * drum sound.
 */
class BuiltinDr8Conga : public IPlugin {
 public:
  static constexpr int kTotalParams = 5;
  static constexpr const char* kPath = "builtin://dr8_conga";
  static constexpr const char* kName = "DR8 Conga";

  enum ParamId {
    PARAM_PITCH = 0,  ///< Base pitch (150 Hz to 350 Hz)
    PARAM_DECAY = 1,  ///< Amplitude envelope decay time (0.05s to 0.8s)
    PARAM_PITCH_ENV_DECAY = 2,  ///< Pitch sweep decay time (0.01s to 0.15s)
    PARAM_PITCH_ENV_DEPTH = 3,  ///< Pitch sweep depth (0 Hz to 120 Hz)
    PARAM_VOLUME = 4,           ///< Master output volume (0.0 to 1.0)
  };

  /**
   * @brief Constructs a new BuiltinDr8Conga instrument.
   */
  BuiltinDr8Conga();

  /**
   * @brief Destructor.
   */
  ~BuiltinDr8Conga() override;

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
  static float normToPitchEnvDecayS(double norm);
  static float normToPitchEnvDepthHz(double norm);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace hibiki
