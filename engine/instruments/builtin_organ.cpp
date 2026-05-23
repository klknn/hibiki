#include "engine/instruments/builtin_organ.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace hibiki {

static const std::string kOrganName = "Drawbar Organ";
static const std::string kOrganPath = "builtin://organ";

// Organ drawbar harmonic ratios
static constexpr double kHarmonics[9] = {
    0.5,  // 16' (sub-octave)
    1.5,  // 5 1/3' (3rd harmonic of 16')
    1.0,  // 8' (fundamental)
    2.0,  // 4' (2nd harmonic)
    3.0,  // 2 2/3' (3rd harmonic)
    4.0,  // 2' (4th harmonic)
    5.0,  // 1 3/5' (5th harmonic)
    6.0,  // 1 1/3' (6th harmonic)
    8.0   // 1' (8th harmonic)
};

struct BuiltinOrgan::Impl {
  static constexpr int kMaxVoices = 16;

  struct Voice {
    bool active = false;
    int note = -1;
    float velocity = 0.0f;
    double base_freq = 0.0;
    double phase[9] = {};
    double amp_gain = 0.0;
    double percussion_env = 0.0;
    uint64_t age = 0;
  };

  double params[kTotalParams] = {};
  double sample_rate = 44100.0;

  Voice voices[kMaxVoices];
  uint64_t voice_counter = 0;

  // Leslie Rotary LFO state
  double rotary_phase = 0.0;

  // Rotary speaker delay buffer (circular buffer for mono sum)
  std::vector<float> rotary_buf;
  int write_idx = 0;

  void reset() {
    for (int i = 0; i < kMaxVoices; ++i) {
      voices[i] = Voice();
    }
    voice_counter = 0;
    rotary_phase = 0.0;
    std::fill(rotary_buf.begin(), rotary_buf.end(), 0.0f);
    write_idx = 0;
  }
};

BuiltinOrgan::BuiltinOrgan() : impl_(std::make_unique<Impl>()) {
  // Default Hammond-style registration: 88 8000 000 (drawbars 1, 2, 3 fully
  // out, others in)
  impl_->params[PARAM_DRAWBAR_1] = 1.0;
  impl_->params[PARAM_DRAWBAR_2] = 1.0;
  impl_->params[PARAM_DRAWBAR_3] = 1.0;
  impl_->params[PARAM_DRAWBAR_4] = 0.0;
  impl_->params[PARAM_DRAWBAR_5] = 0.0;
  impl_->params[PARAM_DRAWBAR_6] = 0.0;
  impl_->params[PARAM_DRAWBAR_7] = 0.0;
  impl_->params[PARAM_DRAWBAR_8] = 0.0;
  impl_->params[PARAM_DRAWBAR_9] = 0.0;

  impl_->params[PARAM_PERCUSSION_ENABLE] = 0.0;  // Disabled by default
  impl_->params[PARAM_PERCUSSION_DECAY] = 0.3;   // moderate decay
  impl_->params[PARAM_ROTARY_SPEED] = 0.2;       // slow Leslie speed
  impl_->params[PARAM_VOLUME] = 0.7;             // master volume

  impl_->rotary_buf.resize(2048, 0.0f);
  impl_->reset();
}

BuiltinOrgan::~BuiltinOrgan() = default;

bool BuiltinOrgan::load(const std::string& /*path*/, int /*plugin_index*/,
                        double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->reset();
  return true;
}

float BuiltinOrgan::normToPercussionDecayS(double norm) {
  // Exponential mapping: 0.05 seconds to 1.0 seconds
  return (float)(0.05 * std::pow(20.0, norm));
}

void BuiltinOrgan::process(float** /*inputs*/, float** outputs, int num_samples,
                           const HostProcessContext& context,
                           const std::vector<MidiNoteEvent>& events,
                           float** /*sidechain*/) {
  impl_->sample_rate = context.sampleRate;

  // Process MIDI events (polyphonic note management)
  for (const auto& ev : events) {
    if (ev.isNoteOn && ev.velocity > 0) {
      // Find matching active voice (to retrigger) or inactive voice
      int target_voice = -1;
      for (int i = 0; i < Impl::kMaxVoices; ++i) {
        if (impl_->voices[i].active && impl_->voices[i].note == ev.pitch) {
          target_voice = i;
          break;
        }
      }

      if (target_voice == -1) {
        // Find inactive voice
        for (int i = 0; i < Impl::kMaxVoices; ++i) {
          if (!impl_->voices[i].active && impl_->voices[i].amp_gain < 0.001) {
            target_voice = i;
            break;
          }
        }
      }

      if (target_voice == -1) {
        // Steal the oldest active voice
        uint64_t min_age = 0xFFFFFFFFFFFFFFFFULL;
        for (int i = 0; i < Impl::kMaxVoices; ++i) {
          if (impl_->voices[i].active && impl_->voices[i].age < min_age) {
            min_age = impl_->voices[i].age;
            target_voice = i;
          }
        }
      }

      if (target_voice != -1) {
        auto& voice = impl_->voices[target_voice];
        voice.active = true;
        voice.note = ev.pitch;
        voice.velocity = ev.velocity;
        voice.base_freq = 440.0 * std::pow(2.0, (ev.pitch - 69.0) / 12.0);
        voice.percussion_env = 1.0;
        voice.age = ++impl_->voice_counter;
        // Don't reset phases completely to let voices feel analog, but if
        // starting fresh, reset
        if (voice.amp_gain < 0.001) {
          std::fill(std::begin(voice.phase), std::end(voice.phase), 0.0);
        }
      }
    } else {
      // Note Off: deactivate note
      for (int i = 0; i < Impl::kMaxVoices; ++i) {
        if (impl_->voices[i].active && impl_->voices[i].note == ev.pitch) {
          impl_->voices[i].active = false;
        }
      }
    }
  }

  float* out_l = outputs[0];
  float* out_r = outputs[1];

  // Initialize output buffers to silence
  for (int i = 0; i < num_samples; ++i) {
    out_l[i] = 0.0f;
    out_r[i] = 0.0f;
  }

  // Drawbar gains
  double drawbars[9];
  for (int d = 0; d < 9; ++d) {
    drawbars[d] = impl_->params[d];
  }

  bool perc_enabled = impl_->params[PARAM_PERCUSSION_ENABLE] >= 0.5;
  double perc_decay_s =
      normToPercussionDecayS(impl_->params[PARAM_PERCUSSION_DECAY]);
  double perc_decay_coeff =
      std::exp(-1.0 / (perc_decay_s * impl_->sample_rate));

  double rotary_speed = impl_->params[PARAM_ROTARY_SPEED];
  // Leslie speed maps from slow (~1.2 Hz) to fast (~6.8 Hz)
  double rotary_rate_hz = 1.2 + rotary_speed * 5.6;
  double rotary_phase_inc = 2.0 * M_PI * rotary_rate_hz / impl_->sample_rate;

  // Gate envelope coefficients (very fast attack/release, ~3ms)
  double env_attack_coeff = 1.0 - std::exp(-1.0 / (0.003 * impl_->sample_rate));
  double env_release_coeff =
      1.0 - std::exp(-1.0 / (0.003 * impl_->sample_rate));

  int rotary_buf_size = (int)impl_->rotary_buf.size();

  for (int i = 0; i < num_samples; ++i) {
    double mono_sum = 0.0;

    // Synthesize active voices
    for (int v = 0; v < Impl::kMaxVoices; ++v) {
      auto& voice = impl_->voices[v];
      bool envelope_active = voice.amp_gain > 0.0001;

      if (!voice.active && !envelope_active) {
        continue;
      }

      // Update gate envelope
      double target_gain = voice.active ? 1.0 : 0.0;
      if (voice.active) {
        voice.amp_gain += (target_gain - voice.amp_gain) * env_attack_coeff;
      } else {
        voice.amp_gain += (target_gain - voice.amp_gain) * env_release_coeff;
      }

      double voice_sample = 0.0;

      // Sum harmonics
      for (int d = 0; d < 9; ++d) {
        double freq = voice.base_freq * kHarmonics[d];
        double dt = freq / impl_->sample_rate;

        // Phase accumulation
        voice.phase[d] += dt;
        if (voice.phase[d] >= 1.0) voice.phase[d] -= 1.0;

        double sample = std::sin(2.0 * M_PI * voice.phase[d]);
        double gain = drawbars[d];

        // Apply percussion click to the 2nd harmonic (4')
        if (d == 3 && perc_enabled) {
          gain += voice.percussion_env * 0.7;
        }

        voice_sample += sample * gain;
      }

      // Update percussion envelope
      voice.percussion_env *= perc_decay_coeff;

      mono_sum += voice_sample * voice.amp_gain * voice.velocity;
    }

    // Write organ mono sum into Leslie delay buffer
    impl_->rotary_buf[impl_->write_idx] = (float)mono_sum;

    // Increment Leslie phase
    impl_->rotary_phase += rotary_phase_inc;
    if (impl_->rotary_phase >= 2.0 * M_PI) {
      impl_->rotary_phase -= 2.0 * M_PI;
    }

    // Leslie Speaker Emulation: pitch modulation via dual-channel modulated
    // delays Delay offsets between 1.0ms and 2.5ms
    double delay_ms_l = 1.75 + 0.75 * std::sin(impl_->rotary_phase);
    double delay_ms_r =
        1.75 + 0.75 * std::cos(impl_->rotary_phase);  // 90 degrees offset

    double delay_samples_l = delay_ms_l * 0.001 * impl_->sample_rate;
    double delay_samples_r = delay_ms_r * 0.001 * impl_->sample_rate;

    // Interpolated read Left
    double read_pos_l = (double)impl_->write_idx - delay_samples_l;
    if (read_pos_l < 0.0) read_pos_l += rotary_buf_size;
    int idx_l1 = (int)std::floor(read_pos_l) % rotary_buf_size;
    int idx_l2 = (idx_l1 + 1) % rotary_buf_size;
    float frac_l = (float)(read_pos_l - std::floor(read_pos_l));
    float delayed_l = (1.0f - frac_l) * impl_->rotary_buf[idx_l1] +
                      frac_l * impl_->rotary_buf[idx_l2];

    // Interpolated read Right
    double read_pos_r = (double)impl_->write_idx - delay_samples_r;
    if (read_pos_r < 0.0) read_pos_r += rotary_buf_size;
    int idx_r1 = (int)std::floor(read_pos_r) % rotary_buf_size;
    int idx_r2 = (idx_r1 + 1) % rotary_buf_size;
    float frac_r = (float)(read_pos_r - std::floor(read_pos_r));
    float delayed_r = (1.0f - frac_r) * impl_->rotary_buf[idx_r1] +
                      frac_r * impl_->rotary_buf[idx_r2];

    // Leslie Speaker Emulation: amplitude modulation (tremolo)
    double amp_l = 0.75 + 0.25 * std::sin(impl_->rotary_phase);
    double amp_r = 0.75 + 0.25 * std::cos(impl_->rotary_phase);

    // Increment buffer index
    impl_->write_idx = (impl_->write_idx + 1) % rotary_buf_size;

    // Master volume scaling
    double vol = impl_->params[PARAM_VOLUME];

    out_l[i] = (float)(delayed_l * amp_l * vol);
    out_r[i] = (float)(delayed_r * amp_r * vol);
  }
}

int BuiltinOrgan::getParameterCount() const { return kTotalParams; }

bool BuiltinOrgan::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {"Drawbar 1 (16')",
                                "Drawbar 2 (5 1/3')",
                                "Drawbar 3 (8')",
                                "Drawbar 4 (4')",
                                "Drawbar 5 (2 2/3')",
                                "Drawbar 6 (2')",
                                "Drawbar 7 (1 3/5')",
                                "Drawbar 8 (1 1/3')",
                                "Drawbar 9 (1')",
                                "Percussion Enable",
                                "Percussion Decay",
                                "Rotary Speed",
                                "Volume"};
  static const double defaults[] = {
      1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,  // registration 88 8000 000
      0.0, 0.3, 0.2, 0.7};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinOrgan::setParameterValue(uint32_t id, double value) {
  if (id < kTotalParams) {
    impl_->params[id] = std::clamp(value, 0.0, 1.0);
  }
}

double BuiltinOrgan::getParameterValue(uint32_t id) const {
  if (id < kTotalParams) {
    return impl_->params[id];
  }
  return 0.0;
}

const std::string& BuiltinOrgan::getName() const { return kOrganName; }

const std::string& BuiltinOrgan::getPath() const { return kOrganPath; }

int BuiltinOrgan::getPluginIndex() const { return 0; }

bool BuiltinOrgan::isInstrument() const { return true; }

}  // namespace hibiki
