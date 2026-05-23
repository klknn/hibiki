#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

/**
 * @brief A built-in TR-808 inspired Hihat Synthesizer.
 *
 * Emulates the classic analog hi-hat circuit by summing 6 detuned square wave
 * oscillators, passed through a resonant bandpass filter and a highpass filter
 * cascade.
 */
class BuiltinDr8Hat : public IPlugin {
 public:
  static constexpr int kTotalParams = 5;
  static constexpr const char* kPath = "builtin://dr8_hat";
  static constexpr const char* kName = "DR8 Hat";

  enum ParamId {
    PARAM_DECAY = 0,     ///< Amplitude decay time (0.02s to 0.8s)
    PARAM_HPF_FREQ = 1,  ///< Highpass filter cutoff (3 kHz to 12 kHz)
    PARAM_BPF_FREQ = 2,  ///< Bandpass filter center frequency (6 kHz to 15 kHz)
    PARAM_TENSION = 3,   ///< Detune tension of the oscillators (0.0 to 1.0)
    PARAM_VOLUME = 4,    ///< Master output volume (0.0 to 1.0)
  };

  /**
   * @brief Constructs a new BuiltinDr8Hat instrument.
   */
  BuiltinDr8Hat();

  /**
   * @brief Destructor.
   */
  ~BuiltinDr8Hat() override;

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
  static float normToHpfHz(double norm);
  static float normToBpfHz(double norm);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace hibiki
