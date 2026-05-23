#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

/**
 * @brief A built-in TR-808 inspired Kick Drum Synthesizer.
 *
 * It uses a pitch-swept sine oscillator, an exponential amplitude envelope,
 * a high-frequency noise-click transient generator, and a tanh distortion stage
 * to create heavy, punchy kick drum sounds.
 */
class BuiltinDr8Kick : public IPlugin {
 public:
  static constexpr int kTotalParams = 7;
  static constexpr const char* kPath = "builtin://dr8_kick";
  static constexpr const char* kName = "DR8 Kick";

  enum ParamId {
    PARAM_PITCH = 0,  ///< Base frequency (40 Hz to 80 Hz)
    PARAM_DECAY = 1,  ///< Amplitude envelope decay time (0.05s to 1.0s)
    PARAM_PITCH_ENV_DECAY = 2,  ///< Pitch sweep decay time (0.01s to 0.15s)
    PARAM_PITCH_ENV_DEPTH = 3,  ///< Pitch sweep depth (0 Hz to 300 Hz)
    PARAM_CLICK_LEVEL = 4,      ///< Attack click transient level (0.0 to 1.0)
    PARAM_DISTORTION = 5,       ///< Soft-clipping overdrive drive (0.0 to 1.0)
    PARAM_VOLUME = 6,           ///< Master output volume (0.0 to 1.0)
  };

  /**
   * @brief Constructs a new BuiltinDr8Kick instrument.
   */
  BuiltinDr8Kick();

  /**
   * @brief Destructor.
   */
  ~BuiltinDr8Kick() override;

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
