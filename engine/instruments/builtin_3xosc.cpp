#include "engine/instruments/builtin_3xosc.hpp"

#include <cmath>

#include "absl/base/optimization.h"

namespace hibiki {

Builtin3xOsc::Builtin3xOsc() { reset(); }

bool Builtin3xOsc::load(const std::string& /*path*/, int /*plugin_index*/,
                        double sample_rate) {
  sample_rate_ = sample_rate;
  reset();
  return true;
}

void Builtin3xOsc::process(float** /*inputs*/, float** outputs, int num_samples,
                           const HostProcessContext& context,
                           const std::vector<MidiNoteEvent>& events,
                           float** /*sidechain*/) {
  sample_rate_ = context.sampleRate;

  for (const auto& ev : events) {
    if (ev.isNoteOn && ev.velocity > 0) {
      noteOn(ev.pitch, ev.velocity);
    } else {
      noteOff(ev.pitch);
    }
  }

  float* outL = outputs[0];
  float* outR = outputs[1];

  for (int i = 0; i < num_samples; ++i) {
    outL[i] = 0;
    outR[i] = 0;
  }

  if (!enabled_) return;

  float master_vol = params_[P_VOLUME];
  float filt_cut_norm = params_[P_FILT_CUT];
  float filt_res_norm = params_[P_FILT_RES];
  float filt_depth = (params_[P_FILT_DEPTH] - 0.5f) * 2.0f;
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

      float mixL = 0, mixR = 0;
      for (int o = 0; o < kNumOsc; ++o) {
        int base = o * kParamsPerOsc;
        float osc_vol = params_[base + P_VOL];
        if (osc_vol < 0.001f) continue;

        float osc_pan = params_[base + P_PAN];
        int coarse = (int)((params_[base + P_COARSE] - 0.5f) * 48.0f);
        float fine = (params_[base + P_FINE] - 0.5f) * 200.0f;

        float freq =
            voice.base_freq * std::pow(2.0f, (coarse + fine / 100.0f) / 12.0f);
        float sample = generateOsc(normToWaveform(params_[base + P_WAVEFORM]),
                                   voice.phase[o], freq, sample_rate_);
        voice.phase[o] += freq / sample_rate_;
        if (voice.phase[o] >= 1.0) voice.phase[o] -= 1.0;

        float panL = std::cos(osc_pan * 1.5708f);
        float panR = std::sin(osc_pan * 1.5708f);
        mixL += sample * osc_vol * panL;
        mixR += sample * osc_vol * panR;
      }

      voice.filterL.setModulatedCutoff(base_cutoff, filt_depth, filt_val,
                                       filt_q, 0.0f, (float)sample_rate_);
      voice.filterR.setModulatedCutoff(base_cutoff, filt_depth, filt_val,
                                       filt_q, 0.0f, (float)sample_rate_);
      voice.filterL.setParams(filt_type,
                              voice.filterL.process(0) * 0 + base_cutoff,
                              filt_q, 0.0f, (float)sample_rate_);
      float filtL = voice.filterL.process(mixL);
      float filtR = voice.filterR.process(mixR);

      outL[i] += filtL * gain_val * voice.velocity * master_vol;
      outR[i] += filtR * gain_val * voice.velocity * master_vol;
    }
  }
}

int Builtin3xOsc::getParameterCount() const { return kTotalParams; }

bool Builtin3xOsc::getParameterInfo(int index, VstParamInfo& info) const {
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

void Builtin3xOsc::setParameterValue(uint32_t id, double value) {
  if (id >= kTotalParams) return;
  params_[id] = (float)value;
  if (id == P_ENABLE) enabled_ = value >= 0.5;
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

double Builtin3xOsc::getParameterValue(uint32_t id) const {
  return (id < kTotalParams) ? params_[id] : 0;
}

const std::string& Builtin3xOsc::getName() const {
  static const std::string n = kName;
  return n;
}

const std::string& Builtin3xOsc::getPath() const {
  static const std::string p = kPath;
  return p;
}

int Builtin3xOsc::getPluginIndex() const { return 0; }
bool Builtin3xOsc::isInstrument() const { return true; }

// --- Private ---

Builtin3xOsc::Waveform Builtin3xOsc::normToWaveform(float norm) {
  if (norm < 0.25f) return Waveform::SINE;
  if (norm < 0.5f) return Waveform::SAW;
  if (norm < 0.75f) return Waveform::SQUARE;
  return Waveform::TRIANGLE;
}

float Builtin3xOsc::generateOsc(Waveform wf, double phase, float /*freq*/,
                                double /*sr*/) {
  float p = (float)phase;
  switch (wf) {
    case Waveform::SINE:
      return std::sin(p * 6.28318530718f);
    case Waveform::SAW:
      return 2.0f * p - 1.0f;
    case Waveform::SQUARE:
      return p < 0.5f ? 1.0f : -1.0f;
    case Waveform::TRIANGLE:
      return p < 0.5f ? (4.0f * p - 1.0f) : (3.0f - 4.0f * p);
  }
  ABSL_UNREACHABLE();
}

void Builtin3xOsc::noteOn(int pitch, float velocity) {
  int target = -1;
  for (int i = 0; i < kMaxVoices; ++i) {
    if (!voices_[i].active && voices_[i].gain_env.isIdle()) {
      target = i;
      break;
    }
  }
  if (target < 0) {
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

void Builtin3xOsc::noteOff(int pitch) {
  for (int i = 0; i < kMaxVoices; ++i) {
    if (voices_[i].active && voices_[i].note == pitch) {
      voices_[i].gain_env.noteOff();
      voices_[i].filter_env.noteOff();
    }
  }
}

double Builtin3xOsc::getDefaultValue(int id) const {
  if (id == P_VOL) return 1.0;
  if (id == 5 + P_VOL) return 0.0;
  if (id == 10 + P_VOL) return 0.0;
  if (id % kParamsPerOsc == P_PAN && id < kOscParams) return 0.5;
  if (id % kParamsPerOsc == P_COARSE && id < kOscParams) return 0.5;
  if (id % kParamsPerOsc == P_FINE && id < kOscParams) return 0.5;
  if (id == P_GAIN_A) return 0.0;
  if (id == P_GAIN_D) return 0.2;
  if (id == P_GAIN_S) return 0.7;
  if (id == P_GAIN_R) return 0.3;
  if (id == P_FILT_CUT) return 1.0;
  if (id == P_FILT_RES) return 0.0;
  if (id == P_FILT_DEPTH) return 0.5;
  if (id == P_VOLUME) return 0.8;
  if (id == P_ENABLE) return 1.0;
  return 0;
}

void Builtin3xOsc::reset() {
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

}  // namespace hibiki
