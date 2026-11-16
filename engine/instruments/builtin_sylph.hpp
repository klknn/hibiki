#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

/// Dual-oscillator unison supersaw & punch-kick synthesizer (u-he Hive /
/// Sylenth1 inspired). Supports up to 7 unison voices per oscillator with phase
/// retrigger, mono sub, exponential pitch snap envelope, ZDF SVF filter +
/// drive, LFO modulation, and built-in chorus/delay/reverb.
class BuiltinSylph : public IPlugin {
 public:
  static constexpr int kMaxVoices = 8;
  static constexpr int kTotalParams = 46;
  static constexpr int kNumPresets = 6;
  static constexpr const char* kPath = "builtin://sylph";
  static constexpr const char* kName = "Sylph";

  enum ParamId {
    P_OSC1_WAVE = 0,
    P_OSC1_OCT = 1,
    P_OSC1_SEMI = 2,
    P_OSC1_UNISON = 3,
    P_OSC1_DETUNE = 4,
    P_OSC1_STEREO = 5,
    P_OSC1_RETRIG = 6,
    P_OSC1_VOL = 7,

    P_OSC2_WAVE = 8,
    P_OSC2_OCT = 9,
    P_OSC2_SEMI = 10,
    P_OSC2_UNISON = 11,
    P_OSC2_DETUNE = 12,
    P_OSC2_STEREO = 13,
    P_OSC2_RETRIG = 14,
    P_OSC2_VOL = 15,

    P_SUB_WAVE = 16,
    P_SUB_OCT = 17,
    P_SUB_VOL = 18,
    P_CLICK_NOISE = 19,
    P_PITCH_AMT = 20,
    P_PITCH_DECAY = 21,
    P_PITCH_CURVE = 22,

    P_AMP_A = 23,
    P_AMP_D = 24,
    P_AMP_S = 25,
    P_AMP_R = 26,
    P_PLAY_MODE = 27,

    P_FILT_TYPE = 28,
    P_FILT_CUT = 29,
    P_FILT_RES = 30,
    P_FILT_ENV_AMT = 31,
    P_FILT_A = 32,
    P_FILT_D = 33,
    P_FILT_S = 34,
    P_FILT_R = 35,

    P_DRIVE = 36,
    P_LFO_RATE = 37,
    P_LFO_PITCH = 38,
    P_LFO_CUT = 39,
    P_CHORUS_MIX = 40,
    P_DELAY_MIX = 41,
    P_DELAY_TIME = 42,
    P_REVERB_MIX = 43,
    P_GLIDE = 44,
    P_VOLUME = 45,
  };

  BuiltinSylph();
  ~BuiltinSylph() override;

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

  /// Applies one of the built-in factory presets (0..kNumPresets-1).
  void applyPreset(int preset_index);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace hibiki
