#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

/**
 * @brief A built-in monophonic Acid Bass Synthesizer.
 *
 * Inspired by the classic TB-303, it features band-limited Saw/Square
 * oscillators, a resonant 4-pole diode-ladder style filter with envelope
 * modulation, automatic portamento (glide), accent handling, and a built-in
 * overdrive stage.
 */
class BuiltinAcidBass : public IPlugin {
 public:
  static constexpr int kTotalParams = 8;
  static constexpr const char* kPath = "builtin://acid_bass";
  static constexpr const char* kName = "Acid Bass";

  enum ParamId {
    PARAM_WAVEFORM = 0,   ///< Oscillator shape (0.0 = Saw, 1.0 = Square)
    PARAM_CUTOFF = 1,     ///< Filter cutoff frequency (0.0 to 1.0)
    PARAM_RESONANCE = 2,  ///< Filter resonance (0.0 to 1.0)
    PARAM_ENV_MOD = 3,    ///< Filter envelope modulation depth (0.0 to 1.0)
    PARAM_DECAY = 4,      ///< Filter envelope decay time (0.05s to 3.0s)
    PARAM_ACCENT = 5,     ///< Accent level (0.0 to 1.0)
    PARAM_OVERDRIVE = 6,  ///< Post-filter distortion (0.0 to 1.0)
    PARAM_VOLUME = 7,     ///< Master output volume (0.0 to 1.0)
  };

  /**
   * @brief Constructs a new BuiltinAcidBass instrument.
   */
  BuiltinAcidBass();

  /**
   * @brief Destructor.
   */
  ~BuiltinAcidBass() override;

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
  static float normToCutoffHz(double norm);
  static float normToDecayS(double norm);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace hibiki
