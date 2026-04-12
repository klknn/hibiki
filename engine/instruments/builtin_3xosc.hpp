#pragma once

#include <cmath>
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
  static constexpr int kParamsPerOsc = 5;  // waveform, coarse, fine, vol, pan
  static constexpr int kOscParams = kNumOsc * kParamsPerOsc;  // 15
  static constexpr int kTotalParams = kOscParams + 14;        // 29
  static constexpr const char* kPath = "builtin://3xosc";
  static constexpr const char* kName = "3xOsc";

  enum Waveform { SINE = 0, SAW, SQUARE, TRIANGLE };

  // Parameter IDs
  // Osc 1: 0-4, Osc 2: 5-9, Osc 3: 10-14
  // Per-osc: +0=waveform, +1=coarse, +2=fine, +3=volume, +4=pan
  // Global: 15=gA, 16=gD, 17=gS, 18=gR, 19=fType, 20=fCut, 21=fRes,
  //         22=fA, 23=fD, 24=fS, 25=fR, 26=fDepth, 27=volume, 28=enable
  enum ParamId {
    // Oscillator params (offsets within each group of 5)
    P_WAVEFORM = 0,
    P_COARSE = 1,
    P_FINE = 2,
    P_VOL = 3,
    P_PAN = 4,
    // Global params
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

  Builtin3xOsc() { reset(); }

  bool load(const std::string& /*path*/, int /*plugin_index*/ = 0,
            double sample_rate = 44100.0) override {
    sample_rate_ = sample_rate;
    reset();
    return true;
  }

  void showEditor() override {}
  void stopEditor() override {}

  void process(float** /*inputs*/, float** outputs, int num_samples,
               const HostProcessContext& context,
               const std::vector<MidiNoteEvent>& events) override {
    sample_rate_ = context.sampleRate;

    // Handle MIDI events
    for (const auto& ev : events) {
      if (ev.isNoteOn && ev.velocity > 0) {
        noteOn(ev.pitch, ev.velocity);
      } else {
        noteOff(ev.pitch);
      }
    }

    float* outL = outputs[0];
    float* outR = outputs[1];

    // Zero output
    for (int i = 0; i < num_samples; ++i) {
      outL[i] = 0;
      outR[i] = 0;
    }

    if (!enabled_) return;

    float master_vol = params_[P_VOLUME];
    float filt_cut_norm = params_[P_FILT_CUT];
    float filt_res_norm = params_[P_FILT_RES];
    float filt_depth = (params_[P_FILT_DEPTH] - 0.5f) * 2.0f;  // -1..1
    auto filt_type = BiquadFilter::normToType(params_[P_FILT_TYPE]);
    float base_cutoff = BiquadFilter::normToCutoff(filt_cut_norm);
    float filt_q = BiquadFilter::normToQ(filt_res_norm);

    for (int v = 0; v < kMaxVoices; ++v) {
      auto& voice = voices_[v];
      if (!voice.active && voice.gain_env.isIdle()) continue;

      for (int i = 0; i < num_samples; ++i) {
        float gain_val = voice.gain_env.process((float)sample_rate_);
        float filt_val = voice.filter_env.process((float)sample_rate_);

        if (voice.gain_env.isIdle()) {
          voice.active = false;
          break;
        }

        // Mix oscillators
        float mixL = 0, mixR = 0;
        for (int o = 0; o < kNumOsc; ++o) {
          int base = o * kParamsPerOsc;
          float osc_vol = params_[base + P_VOL];
          if (osc_vol < 0.001f) continue;

          float osc_pan = params_[base + P_PAN];  // 0=L, 0.5=center, 1=R
          int coarse = (int)((params_[base + P_COARSE] - 0.5f) *
                             48.0f);  // ±24 semitones
          float fine = (params_[base + P_FINE] - 0.5f) * 200.0f;  // ±100 cents

          float freq = voice.base_freq *
                       std::pow(2.0f, (coarse + fine / 100.0f) / 12.0f);
          float sample = generateOsc(normToWaveform(params_[base + P_WAVEFORM]),
                                     voice.phase[o], freq, sample_rate_);
          voice.phase[o] += freq / sample_rate_;
          if (voice.phase[o] >= 1.0) voice.phase[o] -= 1.0;

          float panL = std::cos(osc_pan * 1.5708f);  // pi/2
          float panR = std::sin(osc_pan * 1.5708f);
          mixL += sample * osc_vol * panL;
          mixR += sample * osc_vol * panR;
        }

        // Apply filter with envelope modulation
        voice.filterL.setModulatedCutoff(base_cutoff, filt_depth, filt_val,
                                         filt_q, (float)sample_rate_);
        voice.filterR.setModulatedCutoff(base_cutoff, filt_depth, filt_val,
                                         filt_q, (float)sample_rate_);
        voice.filterL.setParams(filt_type,
                                voice.filterL.process(0) * 0 + base_cutoff,
                                filt_q, (float)sample_rate_);
        // Simplified: just use setModulatedCutoff then process
        float filtL = voice.filterL.process(mixL);
        float filtR = voice.filterR.process(mixR);

        outL[i] += filtL * gain_val * voice.velocity * master_vol;
        outR[i] += filtR * gain_val * voice.velocity * master_vol;
      }
    }
  }

  int getParameterCount() const override { return kTotalParams; }

  bool getParameterInfo(int index, VstParamInfo& info) const override {
    if (index < 0 || index >= kTotalParams) return false;
    info.id = index;
    info.defaultValue = getDefaultValue(index);
    static const char* osc_names[] = {"Waveform", "Coarse", "Fine", "Volume",
                                      "Pan"};
    if (index < kOscParams) {
      int osc = index / kParamsPerOsc + 1;
      int param = index % kParamsPerOsc;
      info.name = "Osc" + std::to_string(osc) + " " + osc_names[param];
    } else {
      static const char* global_names[] = {
          "Gain Attack",  "Gain Decay",     "Gain Sustain",     "Gain Release",
          "Filter Type",  "Filter Cutoff",  "Filter Resonance", "Filter Attack",
          "Filter Decay", "Filter Sustain", "Filter Release",   "Filter Depth",
          "Volume",       "Enable"};
      info.name = global_names[index - kOscParams];
    }
    return true;
  }

  void setParameterValue(uint32_t id, double value) override {
    if (id >= kTotalParams) return;
    params_[id] = (float)value;
    if (id == P_ENABLE) enabled_ = value >= 0.5;
    // Update ADSR params on all voices
    if (id >= P_GAIN_A && id <= P_GAIN_R) {
      for (auto& v : voices_) {
        v.gain_env.setNormalized(params_[P_GAIN_A], params_[P_GAIN_D],
                                 params_[P_GAIN_S], params_[P_GAIN_R]);
      }
    }
    if (id >= P_FILT_A && id <= P_FILT_R) {
      for (auto& v : voices_) {
        v.filter_env.setNormalized(params_[P_FILT_A], params_[P_FILT_D],
                                   params_[P_FILT_S], params_[P_FILT_R]);
      }
    }
  }

  double getParameterValue(uint32_t id) const override {
    return (id < kTotalParams) ? params_[id] : 0;
  }

  const std::string& getName() const override {
    static const std::string n = kName;
    return n;
  }
  const std::string& getPath() const override {
    static const std::string p = kPath;
    return p;
  }
  int getPluginIndex() const override { return 0; }
  bool isInstrument() const override { return true; }

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

  static Waveform normToWaveform(float norm) {
    if (norm < 0.25f) return SINE;
    if (norm < 0.5f) return SAW;
    if (norm < 0.75f) return SQUARE;
    return TRIANGLE;
  }

  static float generateOsc(Waveform wf, double phase, float /*freq*/,
                           double /*sr*/) {
    float p = (float)phase;
    switch (wf) {
      case SINE:
        return std::sin(p * 6.28318530718f);
      case SAW:
        return 2.0f * p - 1.0f;
      case SQUARE:
        return p < 0.5f ? 1.0f : -1.0f;
      case TRIANGLE:
        return p < 0.5f ? (4.0f * p - 1.0f) : (3.0f - 4.0f * p);
      default:
        return 0;
    }
  }

  void noteOn(int pitch, float velocity) {
    // Find free voice or steal oldest
    int target = -1;
    for (int i = 0; i < kMaxVoices; ++i) {
      if (!voices_[i].active && voices_[i].gain_env.isIdle()) {
        target = i;
        break;
      }
    }
    if (target < 0) {
      // Steal oldest
      uint64_t min_age = UINT64_MAX;
      for (int i = 0; i < kMaxVoices; ++i) {
        if (voices_[i].age < min_age) {
          min_age = voices_[i].age;
          target = i;
        }
      }
    }
    if (target < 0) target = 0;

    auto& v = voices_[target];
    v.active = true;
    v.note = pitch;
    v.velocity = velocity;
    v.base_freq = 440.0f * std::pow(2.0f, (pitch - 69) / 12.0f);
    for (int o = 0; o < kNumOsc; ++o) v.phase[o] = 0;
    v.age = ++voice_counter_;
    v.filterL.reset();
    v.filterR.reset();

    v.gain_env.setNormalized(params_[P_GAIN_A], params_[P_GAIN_D],
                             params_[P_GAIN_S], params_[P_GAIN_R]);
    v.filter_env.setNormalized(params_[P_FILT_A], params_[P_FILT_D],
                               params_[P_FILT_S], params_[P_FILT_R]);
    v.gain_env.noteOn();
    v.filter_env.noteOn();
  }

  void noteOff(int pitch) {
    for (int i = 0; i < kMaxVoices; ++i) {
      if (voices_[i].active && voices_[i].note == pitch) {
        voices_[i].gain_env.noteOff();
        voices_[i].filter_env.noteOff();
        // Don't set active=false yet — let release envelope finish
      }
    }
  }

  double getDefaultValue(int id) const {
    // Osc1 volume = 1.0 (others 0), pans center, sine waveform
    if (id == P_VOL) return 1.0;       // Osc1 volume
    if (id == 5 + P_VOL) return 0.0;   // Osc2 volume off
    if (id == 10 + P_VOL) return 0.0;  // Osc3 volume off
    if (id % kParamsPerOsc == P_PAN && id < kOscParams) return 0.5;  // center
    if (id % kParamsPerOsc == P_COARSE && id < kOscParams)
      return 0.5;  // no detune
    if (id % kParamsPerOsc == P_FINE && id < kOscParams)
      return 0.5;  // no detune
    if (id == P_GAIN_A) return 0.0;
    if (id == P_GAIN_D) return 0.2;
    if (id == P_GAIN_S) return 0.7;
    if (id == P_GAIN_R) return 0.3;
    if (id == P_FILT_CUT) return 1.0;  // fully open
    if (id == P_FILT_RES) return 0.0;
    if (id == P_FILT_DEPTH) return 0.5;  // no mod
    if (id == P_VOLUME) return 0.8;
    if (id == P_ENABLE) return 1.0;
    return 0;
  }

  void reset() {
    // Set defaults
    for (int i = 0; i < kTotalParams; ++i) {
      params_[i] = (float)getDefaultValue(i);
    }
    enabled_ = true;
    for (auto& v : voices_) {
      v.active = false;
      v.note = -1;
      for (int o = 0; o < kNumOsc; ++o) v.phase[o] = 0;
      v.filterL.reset();
      v.filterR.reset();
    }
    voice_counter_ = 0;
  }

  double sample_rate_ = 44100.0;
  bool enabled_ = true;
  float params_[kTotalParams] = {};
  Voice voices_[kMaxVoices];
  uint64_t voice_counter_ = 0;
  std::string name_ = kName;
  std::string path_ = kPath;
};

}  // namespace hibiki
