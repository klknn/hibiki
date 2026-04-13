#include "engine/instruments/builtin_sampler.hpp"

#include <cmath>

namespace hibiki {

BuiltinSampler::BuiltinSampler() { reset(); }

bool BuiltinSampler::load(const std::string& /*path*/, int /*plugin_index*/,
                            double sample_rate) {
  sample_rate_ = sample_rate;
  reset();
  return true;
}

bool BuiltinSampler::loadSample(const std::string& path) {
  std::vector<float> data;
  int channels = 0;
  double dur = 0;
  if (!LoadWav(path, data, channels, dur).ok()) return false;

  sample_data_ = std::move(data);
  sample_channels_ = channels;
  sample_rate_file_ = (channels > 0 && dur > 0)
                          ? (double)sample_data_.size() / channels / dur
                          : 44100.0;
  sample_path_ = path;
  generateWaveformSummary();
  return true;
}

const std::vector<float>& BuiltinSampler::getWaveformSummary() const {
  return waveform_summary_;
}

void BuiltinSampler::process(float** /*inputs*/, float** outputs,
                              int num_samples,
                              const HostProcessContext& context,
                              const std::vector<MidiNoteEvent>& events) {
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

  if (!enabled_ || sample_data_.empty()) return;

  float master_vol = params_[P_VOLUME];
  int root_note = (int)(params_[P_ROOT_NOTE] * 127.0f);
  float sample_start = params_[P_SAMPLE_START];
  float sample_end = std::max(sample_start + 0.001f, params_[P_SAMPLE_END]);
  int total_frames = (int)sample_data_.size() / std::max(1, sample_channels_);
  int start_frame = (int)(sample_start * total_frames);
  int end_frame = (int)(sample_end * total_frames);
  if (end_frame <= start_frame) end_frame = start_frame + 1;

  float filt_cut_norm = params_[P_FILT_CUT];
  float filt_res_norm = params_[P_FILT_RES];
  float filt_depth = (params_[P_FILT_DEPTH] - 0.5f) * 2.0f;
  auto filt_type = BiquadFilter::normToType(params_[P_FILT_TYPE]);
  float base_cutoff = BiquadFilter::normToCutoff(filt_cut_norm);
  float filt_q = BiquadFilter::normToQ(filt_res_norm);

  for (int v = 0; v < kMaxVoices; ++v) {
    auto& voice = voices_[v];
    if (!voice.active && voice.gain_env.isIdle()) continue;

    float pitch_ratio = std::pow(2.0f, (voice.note - root_note) / 12.0f) *
                        (float)(sample_rate_file_ / sample_rate_);

    for (int i = 0; i < num_samples; ++i) {
      float gain_val = voice.gain_env.process((float)sample_rate_);
      float filt_val = voice.filter_env.process((float)sample_rate_);

      if (voice.gain_env.isIdle()) {
        voice.active = false;
        break;
      }

      double pos = voice.position + start_frame;
      int idx0 = (int)pos;
      float frac = (float)(pos - idx0);

      float sL = 0, sR = 0;
      if (idx0 >= start_frame && idx0 < end_frame) {
        if (sample_channels_ == 1) {
          float s0 = sample_data_[idx0];
          float s1 = (idx0 + 1 < end_frame) ? sample_data_[idx0 + 1] : s0;
          sL = sR = s0 + frac * (s1 - s0);
        } else {
          float l0 = sample_data_[idx0 * 2];
          float r0 = sample_data_[idx0 * 2 + 1];
          float l1 =
              (idx0 + 1 < end_frame) ? sample_data_[(idx0 + 1) * 2] : l0;
          float r1 =
              (idx0 + 1 < end_frame) ? sample_data_[(idx0 + 1) * 2 + 1] : r0;
          sL = l0 + frac * (l1 - l0);
          sR = r0 + frac * (r1 - r0);
        }
      }

      voice.position += pitch_ratio;
      if (voice.position + start_frame >= end_frame) {
        voice.gain_env.noteOff();
        voice.filter_env.noteOff();
      }

      voice.filterL.setParams(filt_type, base_cutoff, filt_q,
                              (float)sample_rate_);
      voice.filterR.setParams(filt_type, base_cutoff, filt_q,
                              (float)sample_rate_);
      voice.filterL.setModulatedCutoff(base_cutoff, filt_depth, filt_val,
                                       filt_q, (float)sample_rate_);
      voice.filterR.setModulatedCutoff(base_cutoff, filt_depth, filt_val,
                                       filt_q, (float)sample_rate_);
      sL = voice.filterL.process(sL);
      sR = voice.filterR.process(sR);

      outL[i] += sL * gain_val * voice.velocity * master_vol;
      outR[i] += sR * gain_val * voice.velocity * master_vol;
    }
  }
}

int BuiltinSampler::getParameterCount() const { return kTotalParams; }

bool BuiltinSampler::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  info.id = index;
  info.defaultValue = getDefaultValue(index);
  static const char* names[] = {
      "Sample Start",   "Sample End",       "Root Note",     "Gain Attack",
      "Gain Decay",     "Gain Sustain",     "Gain Release",  "Filter Type",
      "Filter Cutoff",  "Filter Resonance", "Filter Attack", "Filter Decay",
      "Filter Sustain", "Filter Release",   "Filter Depth",  "Volume",
      "Enable"};
  info.name = names[index];
  return true;
}

void BuiltinSampler::setParameterValue(uint32_t id, double value) {
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

double BuiltinSampler::getParameterValue(uint32_t id) const {
  return (id < kTotalParams) ? params_[id] : 0;
}

const std::string& BuiltinSampler::getName() const {
  static const std::string n = kName;
  return n;
}

const std::string& BuiltinSampler::getPath() const {
  static const std::string p = kPath;
  return p;
}

int BuiltinSampler::getPluginIndex() const { return 0; }
bool BuiltinSampler::isInstrument() const { return true; }

// --- Private ---

void BuiltinSampler::noteOn(int pitch, float velocity) {
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
  v.position = 0;
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

void BuiltinSampler::noteOff(int pitch) {
  for (int i = 0; i < kMaxVoices; ++i) {
    if (voices_[i].active && voices_[i].note == pitch) {
      voices_[i].gain_env.noteOff();
      voices_[i].filter_env.noteOff();
    }
  }
}

double BuiltinSampler::getDefaultValue(int id) const {
  if (id == P_SAMPLE_END) return 1.0;
  if (id == P_ROOT_NOTE) return 60.0 / 127.0;
  if (id == P_GAIN_A) return 0.0;
  if (id == P_GAIN_D) return 0.2;
  if (id == P_GAIN_S) return 1.0;
  if (id == P_GAIN_R) return 0.3;
  if (id == P_FILT_CUT) return 1.0;
  if (id == P_FILT_DEPTH) return 0.5;
  if (id == P_VOLUME) return 0.8;
  if (id == P_ENABLE) return 1.0;
  return 0;
}

void BuiltinSampler::generateWaveformSummary() {
  waveform_summary_.clear();
  if (sample_data_.empty()) return;
  int total = (int)sample_data_.size() / std::max(1, sample_channels_);
  int num_points = 128;
  waveform_summary_.resize(num_points);
  for (int i = 0; i < num_points; ++i) {
    int from = i * total / num_points;
    int to = (i + 1) * total / num_points;
    float peak = 0;
    for (int j = from; j < to && j < total; ++j) {
      float v = std::abs(sample_data_[j * sample_channels_]);
      if (v > peak) peak = v;
    }
    waveform_summary_[i] = peak;
  }
}

void BuiltinSampler::reset() {
  for (int i = 0; i < kTotalParams; ++i) {
    params_[i] = (float)getDefaultValue(i);
  }
  enabled_ = true;
  for (auto& v : voices_) {
    v.active = false;
    v.note = -1;
    v.position = 0;
    v.filterL.reset();
    v.filterR.reset();
  }
  voice_counter_ = 0;
}

}  // namespace hibiki
