#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

/**
 * @brief A built-in TR-808 inspired Snare Drum Synthesizer.
 *
 * It uses a dual-component sound generator:
 * - A resonant skin body (two detuned sine oscillators tuned to fundamental and
 * a 1.6x harmonic).
 * - Snare wires (a white noise generator passed through a resonant high-pass
 * filter).
 */
class BuiltinDr8Snare : public IPlugin {
 public:
  static constexpr int kTotalParams = 7;
  static constexpr const char* kPath = "builtin://dr8_snare";
  static constexpr const char* kName = "DR8 Snare";

  enum ParamId {
    PARAM_PITCH = 0,        ///< Fundamental skin pitch (100 Hz to 250 Hz)
    PARAM_DECAY = 1,        ///< Skin tone decay time (0.05s to 0.5s)
    PARAM_NOISE_LEVEL = 2,  ///< Snare wire noise volume (0.0 to 1.0)
    PARAM_NOISE_DECAY = 3,  ///< Snare wire noise decay time (0.05s to 1.0s)
    PARAM_NOISE_HPF =
        4,  ///< Highpass filter cutoff frequency (800 Hz to 8000 Hz)
    PARAM_TONE_NOISE_MIX =
        5,  ///< Mix balance between body tone (0.0) and wire noise (1.0)
    PARAM_VOLUME = 6,  ///< Master output volume (0.0 to 1.0)
  };

  /**
   * @brief Constructs a new BuiltinDr8Snare instrument.
   */
  BuiltinDr8Snare();

  /**
   * @brief Destructor.
   */
  ~BuiltinDr8Snare() override;

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
  static float normToNoiseDecayS(double norm);
  static float normToNoiseHpfHz(double norm);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace hibiki
