#include "engine/instruments/builtin_acid_bass.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "engine/core/math.hpp"

namespace hibiki {

static const std::string kAcidBassName = "Acid Bass";
static const std::string kAcidBassPath = "builtin://acid_bass";

// Helper for PolyBLEP band-limited oscillator step correction
inline double polyblep(double t, double dt) {
  if (t < dt) {
    double x = t / dt;
    return x + x - x * x - 1.0;
  } else if (t > 1.0 - dt) {
    double x = (t - 1.0) / dt;
    return x + x + x * x + 1.0;
  }
  return 0.0;
}

struct BuiltinAcidBass::Impl {
  double params[kTotalParams] = {};
  double sample_rate = 44100.0;

  // Voice/MIDI note tracking
  std::vector<int> note_stack;
  bool note_active = false;
  int current_pitch = -1;
  double target_freq = 0.0;
  double current_freq = 0.0;

  // Oscillator state
  double osc_phase = 0.0;

  // Envelopes
  double env_val = 0.0;   // Filter env value (0.0 to 1.0)
  double amp_gain = 0.0;  // Amp envelope value (0.0 to 1.0)
  bool accent_active = false;

  // Filter state (4-pole cascade)
  double s1 = 0.0;
  double s2 = 0.0;
  double s3 = 0.0;
  double s4 = 0.0;

  void reset() {
    note_stack.clear();
    note_active = false;
    current_pitch = -1;
    target_freq = 0.0;
    current_freq = 0.0;
    osc_phase = 0.0;
    env_val = 0.0;
    amp_gain = 0.0;
    accent_active = false;
    s1 = s2 = s3 = s4 = 0.0;
  }
};

BuiltinAcidBass::BuiltinAcidBass() : impl_(std::make_unique<Impl>()) {
  impl_->params[PARAM_WAVEFORM] = 0.0;   // Sawtooth default
  impl_->params[PARAM_CUTOFF] = 0.3;     // moderate cutoff
  impl_->params[PARAM_RESONANCE] = 0.6;  // high resonance
  impl_->params[PARAM_ENV_MOD] = 0.5;    // env mod amount
  impl_->params[PARAM_DECAY] = 0.2;      // moderate decay
  impl_->params[PARAM_ACCENT] = 0.0;     // no accent
  impl_->params[PARAM_OVERDRIVE] = 0.1;  // light overdrive
  impl_->params[PARAM_VOLUME] = 0.7;     // output volume
  impl_->params[PARAM_TRANSPOSE] = 0.5;   // 0 octaves (middle)
  impl_->params[PARAM_SLIDE] = 0.0;       // Slide off
  impl_->params[PARAM_ACCENT_SWITCH] = 0.0; // Accent switch off

  impl_->reset();
}

BuiltinAcidBass::~BuiltinAcidBass() = default;

bool BuiltinAcidBass::load(const std::string& /*path*/, int /*plugin_index*/,
                           double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->reset();
  return true;
}

float BuiltinAcidBass::normToCutoffHz(double norm) {
  // 100 Hz to 3000 Hz exponential mapping
  return (float)(100.0 * std::pow(30.0, norm));
}

float BuiltinAcidBass::normToDecayS(double norm) {
  // 0.05 seconds to 3.0 seconds exponential mapping
  return (float)(0.05 * std::pow(60.0, norm));
}

void BuiltinAcidBass::process(float** /*inputs*/, float** outputs,
                              int num_samples,
                              const HostProcessContext& context,
                              const std::vector<MidiNoteEvent>& events,
                              float** /*sidechain*/) {
  impl_->sample_rate = context.sampleRate;

  double transpose_val = impl_->params[PARAM_TRANSPOSE];
  int transpose_octaves = (int)std::round(transpose_val * 4.0) - 2;
  int transpose_semitones = transpose_octaves * 12;

  // Process MIDI events
  for (const auto& ev : events) {
    int pitch = std::clamp(ev.pitch + transpose_semitones, 0, 127);
    if (ev.isNoteOn && ev.velocity > 0) {
      // Monophonic stack logic
      auto it = std::find(impl_->note_stack.begin(), impl_->note_stack.end(),
                          pitch);
      if (it != impl_->note_stack.end()) {
        impl_->note_stack.erase(it);
      }
      impl_->note_stack.push_back(pitch);

      double note_freq = 440.0 * std::pow(2.0, (pitch - 69.0) / 12.0);
      impl_->target_freq = note_freq;
      impl_->current_pitch = pitch;

      // Retrigger envelope only if it's the first note in stack
      if (impl_->note_stack.size() == 1) {
        impl_->current_freq = note_freq;
        impl_->env_val = 1.0;
        impl_->note_active = true;
      } else {
        // Legato transition: if Slide is OFF, set current_freq instantly!
        bool slide_on = impl_->params[PARAM_SLIDE] >= 0.5;
        if (!slide_on) {
          impl_->current_freq = note_freq;
        }
      }
      impl_->accent_active =
          (ev.velocity > 0.8f) || (impl_->params[PARAM_ACCENT_SWITCH] >= 0.5);
    } else {
      // Note Off
      auto it = std::find(impl_->note_stack.begin(), impl_->note_stack.end(),
                          pitch);
      if (it != impl_->note_stack.end()) {
        impl_->note_stack.erase(it);
      }

      if (impl_->note_stack.empty()) {
        impl_->note_active = false;
        impl_->current_pitch = -1;
        impl_->accent_active = false;
      } else {
        // Legato switch to previous note in stack
        int prev_pitch = impl_->note_stack.back();
        double note_freq = 440.0 * std::pow(2.0, (prev_pitch - 69.0) / 12.0);
        impl_->target_freq = note_freq;
        impl_->current_pitch = prev_pitch;

        // If Slide is OFF, set current_freq instantly!
        bool slide_on = impl_->params[PARAM_SLIDE] >= 0.5;
        if (!slide_on) {
          impl_->current_freq = note_freq;
        }
      }
    }
  }

  // Sync real-time accent override switch
  if (impl_->note_active) {
    if (impl_->params[PARAM_ACCENT_SWITCH] >= 0.5) {
      impl_->accent_active = true;
    }
  } else {
    impl_->accent_active = false;
  }

  float* out_l = outputs[0];
  float* out_r = outputs[1];

  // Initialize output buffers to silence
  for (int i = 0; i < num_samples; ++i) {
    out_l[i] = 0.0f;
    out_r[i] = 0.0f;
  }

  double master_vol = impl_->params[PARAM_VOLUME];
  double resonance = impl_->params[PARAM_RESONANCE];
  double env_mod = impl_->params[PARAM_ENV_MOD];
  double overdrive = impl_->params[PARAM_OVERDRIVE];
  bool is_square = impl_->params[PARAM_WAVEFORM] >= 0.5;

  double cutoff_hz = normToCutoffHz(impl_->params[PARAM_CUTOFF]);
  double decay_s = normToDecayS(impl_->params[PARAM_DECAY]);

  // Adjust decay time based on accent
  if (impl_->accent_active) {
    decay_s = std::clamp(decay_s * 0.5, 0.05, 3.0);  // accents decay faster
  }

  // Filter envelope decay coefficient
  double env_decay_rate = std::exp(-1.0 / (decay_s * impl_->sample_rate));

  // Amplitude envelope coefficients
  double amp_attack_coeff =
      1.0 - std::exp(-1.0 / (0.002 * impl_->sample_rate));  // 2ms attack
  double amp_release_coeff =
      1.0 - std::exp(-1.0 / (0.08 * impl_->sample_rate));  // 80ms release

  // Glide coefficient (~80ms portamento)
  double glide_coeff = 1.0 - std::exp(-1.0 / (0.08 * impl_->sample_rate));

  // Limit feedback to avoid blowups
  double res_fb = resonance * 3.8;

  for (int i = 0; i < num_samples; ++i) {
    // Smooth glide frequency
    if (impl_->note_active) {
      bool slide_on = impl_->params[PARAM_SLIDE] >= 0.5;
      if (slide_on) {
        impl_->current_freq +=
            (impl_->target_freq - impl_->current_freq) * glide_coeff;
      } else {
        impl_->current_freq = impl_->target_freq;
      }
    }

    double freq = impl_->current_freq;
    if (freq < 1.0) freq = 1.0;

    // Phase increment
    double dt = freq / impl_->sample_rate;
    impl_->osc_phase += dt;
    if (impl_->osc_phase >= 1.0) {
      impl_->osc_phase -= 1.0;
    }

    // PolyBLEP band-limited oscillator
    double saw = 2.0 * impl_->osc_phase - 1.0 - polyblep(impl_->osc_phase, dt);
    double osc_out = saw;
    if (is_square) {
      double phase_offset = impl_->osc_phase + 0.5;
      if (phase_offset >= 1.0) phase_offset -= 1.0;
      double saw_offset = 2.0 * phase_offset - 1.0 - polyblep(phase_offset, dt);
      osc_out = saw - saw_offset;
    }

    // Update filter envelope
    impl_->env_val *= env_decay_rate;

    // Update amplifier envelope
    double target_amp = impl_->note_active ? 1.0 : 0.0;
    if (impl_->note_active) {
      impl_->amp_gain += (target_amp - impl_->amp_gain) * amp_attack_coeff;
    } else {
      impl_->amp_gain += (target_amp - impl_->amp_gain) * amp_release_coeff;
    }

    if (impl_->amp_gain < 0.0001 && !impl_->note_active) {
      impl_->amp_gain = 0.0;
      continue;
    }

    // Calculate cutoff frequency (modulated by envelope)
    double mod_depth_hz = env_mod * 4000.0;
    if (impl_->accent_active) {
      mod_depth_hz *= 1.4;  // Accent boosts envelope depth
    }
    double current_cutoff = cutoff_hz + impl_->env_val * mod_depth_hz;
    current_cutoff = std::clamp(current_cutoff, 40.0, 18000.0);

    // 2x oversampled 4-pole ZDF low-pass filter stage
    double x = osc_out;
    for (int step = 0; step < 2; ++step) {
      double g =
          std::tan(hibiki::pi * current_cutoff / (2.0 * impl_->sample_rate));
      g = std::clamp(g, 0.0, 0.999);

      // Feedback loop with soft-limiting tanh saturation
      double fb = impl_->s4 * res_fb;
      double input_fb = x - std::tanh(fb);

      double v1 = (input_fb - impl_->s1) * g / (1.0 + g);
      double o1 = impl_->s1 + v1;
      impl_->s1 = o1 + v1;

      double v2 = (o1 - impl_->s2) * g / (1.0 + g);
      double o2 = impl_->s2 + v2;
      impl_->s2 = o2 + v2;

      double v3 = (o2 - impl_->s3) * g / (1.0 + g);
      double o3 = impl_->s3 + v3;
      impl_->s3 = o3 + v3;

      double v4 = (o3 - impl_->s4) * g / (1.0 + g);
      double o4 = impl_->s4 + v4;
      impl_->s4 = o4 + v4;
    }

    // Filter output
    double filter_out = impl_->s4;

    // Saturation/Overdrive stage
    double drive = overdrive * 6.0;
    double sat_in = filter_out * (1.0 + drive);
    if (impl_->accent_active) {
      sat_in *= 1.3;  // Accent drives the saturation harder
    }
    double saturated = std::tanh(sat_in);

    // Final gain scaling
    double output_val = saturated * impl_->amp_gain * master_vol;
    if (impl_->accent_active) {
      output_val *= 1.2;
    }

    out_l[i] = (float)output_val;
    out_r[i] = (float)output_val;
  }
}

int BuiltinAcidBass::getParameterCount() const { return kTotalParams; }

bool BuiltinAcidBass::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {"Waveform", "Cutoff", "Resonance", "Env Mod",
                                "Decay",    "Accent", "Overdrive", "Volume",
                                "Transpose", "Slide", "Accent Switch"};
  static const double defaults[] = {0.0, 0.3, 0.6, 0.5, 0.2, 0.0, 0.1, 0.7,
                                    0.5, 0.0, 0.0};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinAcidBass::setParameterValue(uint32_t id, double value) {
  if (id < kTotalParams) {
    impl_->params[id] = std::clamp(value, 0.0, 1.0);
  }
}

double BuiltinAcidBass::getParameterValue(uint32_t id) const {
  if (id < kTotalParams) {
    return impl_->params[id];
  }
  return 0.0;
}

const std::string& BuiltinAcidBass::getName() const { return kAcidBassName; }

const std::string& BuiltinAcidBass::getPath() const { return kAcidBassPath; }

int BuiltinAcidBass::getPluginIndex() const { return 0; }

bool BuiltinAcidBass::isInstrument() const { return true; }

}  // namespace hibiki
