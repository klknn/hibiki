#pragma once

#include <string>
#include <vector>

#include "engine/instruments/adsr.hpp"
#include "engine/instruments/biquad_filter.hpp"
#include "engine/plugin/iplugin.hpp"

namespace hibiki {

// 3-oscillator synthesizer (FL Studio 3xOSC-style).
// Each oscillator: waveform (sin/saw/square/tri), coarse/fine tune, vol, pan.
// Global: gain ADSR, filter (LP/HP/BP) with ADSR modulation.
// 8-voice polyphony with oldest-note stealing.
class Builtin3xOsc : public IPlugin {
 public:
  static constexpr int kMaxVoices = 8;
  static constexpr int kNumOsc = 3;
  static constexpr int kParamsPerOsc = 5;
  static constexpr int kOscParams = kNumOsc * kParamsPerOsc;
  static constexpr int kTotalParams = kOscParams + 14;
  static constexpr const char* kPath = "builtin://3xosc";
  static constexpr const char* kName = "3xOsc";

  enum Waveform { SINE = 0, SAW, SQUARE, TRIANGLE };

  enum ParamId {
    P_WAVEFORM = 0,
    P_COARSE = 1,
    P_FINE = 2,
    P_VOL = 3,
    P_PAN = 4,
    P_GAIN_A = 15,
    P_GAIN_D = 16,
    P_GAIN_S = 17,
    P_GAIN_R = 18,
    P_FILT_TYPE = 19,
    P_FILT_CUT = 20,
    P_FILT_RES = 21,
    P_FILT_A = 22,
    P_FILT_D = 23,
    P_FILT_S = 24,
    P_FILT_R = 25,
    P_FILT_DEPTH = 26,
    P_VOLUME = 27,
    P_ENABLE = 28,
  };

  Builtin3xOsc();

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

 private:
  struct Voice {
    bool active = false;
    int note = -1;
    float velocity = 0;
    float base_freq = 0;
    double phase[kNumOsc] = {};
    Adsr gain_env;
    Adsr filter_env;
    BiquadFilter filterL, filterR;
    uint64_t age = 0;
  };

  static Waveform normToWaveform(float norm);
  static float generateOsc(Waveform wf, double phase, float freq, double sr);

  void noteOn(int pitch, float velocity);
  void noteOff(int pitch);
  double getDefaultValue(int id) const;
  void reset();

  double sample_rate_ = 44100.0;
  bool enabled_ = true;
  float params_[kTotalParams] = {};
  Voice voices_[kMaxVoices];
  uint64_t voice_counter_ = 0;
};

}  // namespace hibiki
