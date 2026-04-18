#pragma once

#include <string>
#include <vector>

#include "engine/core/audio_file.hpp"
#include "engine/core/biquad_filter.hpp"
#include "engine/instruments/adsr.hpp"
#include "engine/plugin/iplugin.hpp"

namespace hibiki {

// Single-waveform sampler (Ableton Simpler-style).
// Loads a WAV sample, plays it back with pitch transform based on root note.
// 8-voice polyphony, gain ADSR, filter with ADSR modulation.
class BuiltinSampler : public IPlugin {
 public:
  static constexpr int kMaxVoices = 8;
  static constexpr int kTotalParams = 17;
  static constexpr const char* kPath = "builtin://sampler";
  static constexpr const char* kName = "Sampler";

  enum ParamId {
    P_SAMPLE_START = 0,
    P_SAMPLE_END = 1,
    P_ROOT_NOTE = 2,
    P_GAIN_A = 3,
    P_GAIN_D = 4,
    P_GAIN_S = 5,
    P_GAIN_R = 6,
    P_FILT_TYPE = 7,
    P_FILT_CUT = 8,
    P_FILT_RES = 9,
    P_FILT_A = 10,
    P_FILT_D = 11,
    P_FILT_S = 12,
    P_FILT_R = 13,
    P_FILT_DEPTH = 14,
    P_VOLUME = 15,
    P_ENABLE = 16,
  };

  BuiltinSampler();

  bool load(const std::string& path, int plugin_index = 0,
            double sample_rate = 44100.0) override;
  bool loadSample(const std::string& path);
  const std::vector<float>& getWaveformSummary() const;

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
    double position = 0;
    Adsr gain_env;
    Adsr filter_env;
    BiquadFilter filterL, filterR;
    uint64_t age = 0;
  };

  void noteOn(int pitch, float velocity);
  void noteOff(int pitch);
  double getDefaultValue(int id) const;
  void generateWaveformSummary();
  void reset();

  double sample_rate_ = 44100.0;
  double sample_rate_file_ = 44100.0;
  bool enabled_ = true;
  float params_[kTotalParams] = {};
  Voice voices_[kMaxVoices];
  uint64_t voice_counter_ = 0;

  std::vector<float> sample_data_;
  int sample_channels_ = 0;
  std::string sample_path_;
  std::vector<float> waveform_summary_;
};

}  // namespace hibiki
