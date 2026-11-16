#pragma once

#include <memory>
#include <string>
#include <vector>

#include "engine/plugin/iplugin.hpp"

namespace hibiki {

/// Dual-layer unison supersaw & punch-kick synthesizer (u-he Hive / Sylenth1
/// inspired). Features independent Part A & Part B 7-voice oscillators, dual
/// ZDF SVF filters & ADSRs, phase-locked sub + exponential pitch snap, and a
/// serial Distortion->Chorus->Delay->Reverb rack.
class BuiltinSylph : public IPlugin {
 public:
  static constexpr int kMaxVoices = 8;
  static constexpr int kTotalParams = 71;
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

    P_FILTA_TYPE = 8,
    P_FILTA_CUT = 9,
    P_FILTA_RES = 10,
    P_FILTA_DRIVE = 11,
    P_FILTA_ENV_AMT = 12,

    P_AMPA_A = 13,
    P_AMPA_D = 14,
    P_AMPA_S = 15,
    P_AMPA_R = 16,
    P_FILTA_A = 17,
    P_FILTA_D = 18,
    P_FILTA_S = 19,
    P_FILTA_R = 20,

    P_OSC2_WAVE = 21,
    P_OSC2_OCT = 22,
    P_OSC2_SEMI = 23,
    P_OSC2_UNISON = 24,
    P_OSC2_DETUNE = 25,
    P_OSC2_STEREO = 26,
    P_OSC2_RETRIG = 27,
    P_OSC2_VOL = 28,

    P_FILTB_TYPE = 29,
    P_FILTB_CUT = 30,
    P_FILTB_RES = 31,
    P_FILTB_DRIVE = 32,
    P_FILTB_ENV_AMT = 33,

    P_AMPB_A = 34,
    P_AMPB_D = 35,
    P_AMPB_S = 36,
    P_AMPB_R = 37,
    P_FILTB_A = 38,
    P_FILTB_D = 39,
    P_FILTB_S = 40,
    P_FILTB_R = 41,

    P_SUB_WAVE = 42,
    P_SUB_OCT = 43,
    P_SUB_VOL = 44,
    P_CLICK_NOISE = 45,
    P_PITCH_AMT = 46,
    P_PITCH_DECAY = 47,
    P_PITCH_CURVE = 48,
    P_LFO_RATE = 49,
    P_LFO_PITCH = 50,
    P_LFO_CUT = 51,
    P_PLAY_MODE = 52,
    P_GLIDE = 53,
    P_VOLUME = 54,

    P_DIST_ON = 55,
    P_DIST_DRIVE = 56,
    P_DIST_TONE = 57,
    P_DIST_MIX = 58,
    P_CHORUS_ON = 59,
    P_CHORUS_RATE = 60,
    P_CHORUS_DEPTH = 61,
    P_CHORUS_MIX = 62,
    P_DELAY_ON = 63,
    P_DELAY_TIME = 64,
    P_DELAY_FB = 65,
    P_DELAY_MIX = 66,
    P_REVERB_ON = 67,
    P_REVERB_SIZE = 68,
    P_REVERB_DAMP = 69,
    P_REVERB_MIX = 70,
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
