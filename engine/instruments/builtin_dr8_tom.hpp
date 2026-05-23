#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

/**
 * @brief A built-in TR-808 inspired Tom Synthesizer.
 *
 * It uses a pitch-swept sine oscillator passed through a biquad lowpass filter
 * to produce a clean woody tom sound, combined with a fast initial attack noise
 * click.
 */
class BuiltinDr8Tom : public IPlugin {
 public:
  static constexpr int kTotalParams = 6;
  static constexpr const char* kPath = "builtin://dr8_tom";
  static constexpr const char* kName = "DR8 Tom";

  enum ParamId {
    PARAM_PITCH = 0,  ///< Base pitch (70 Hz to 200 Hz)
    PARAM_DECAY = 1,  ///< Amplitude envelope decay time (0.1s to 1.5s)
    PARAM_PITCH_ENV_DECAY = 2,  ///< Pitch sweep decay time (0.02s to 0.3s)
    PARAM_PITCH_ENV_DEPTH = 3,  ///< Pitch sweep depth (0 Hz to 100 Hz)
    PARAM_NOISE_ATTACK = 4,     ///< Initial attack click level (0.0 to 1.0)
    PARAM_VOLUME = 5,           ///< Master output volume (0.0 to 1.0)
  };

  /**
   * @brief Constructs a new BuiltinDr8Tom instrument.
   */
  BuiltinDr8Tom();

  /**
   * @brief Destructor.
   */
  ~BuiltinDr8Tom() override;

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
