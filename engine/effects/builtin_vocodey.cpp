#include "engine/effects/builtin_vocodey.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "engine/core/biquad_filter.hpp"

namespace hibiki {

static const std::string kVocodeyName = "Vocodey";
static const std::string kVocodeyPath = "builtin://vocodey";

// A single polyphonic voice for the internal carrier synthesizer
struct SynthVoice {
  bool active = false;
  int pitch = 0;
  double phase1 = 0.0;
  double phase2 = 0.0;
  double amp = 0.0;
};

// PolyBLEP helper for band-limited sawtooth oscillator
static inline double poly_blep(double t, double dt) {
  if (t < dt) {
    double x = t / dt;
    return 2.0 * x - x * x - 1.0;
  } else if (t > 1.0 - dt) {
    double x = (t - 1.0) / dt;
    return x * x + 2.0 * x + 1.0;
  }
  return 0.0;
}

struct BuiltinVocodey::Impl {
  double params[kTotalParams] = {};
  double sample_rate = 44100.0;

  // Synthesizer voices
  static constexpr int kMaxVoices = 16;
  SynthVoice voices[kMaxVoices] = {};

  // Vocoder bandpass filters and envelope states
  double band_freqs[24] = {};
  BiquadFilter mod_filter_l[24] = {};
  BiquadFilter mod_filter_r[24] = {};
  BiquadFilter car_filter_l[24] = {};
  BiquadFilter car_filter_r[24] = {};
  double env_mod_l[24] = {};
  double env_mod_r[24] = {};

  void reset() {
    for (int i = 0; i < kMaxVoices; ++i) {
      voices[i].active = false;
      voices[i].pitch = 0;
      voices[i].phase1 = 0.0;
      voices[i].phase2 = 0.0;
      voices[i].amp = 0.0;
    }
    for (int j = 0; j < 24; ++j) {
      mod_filter_l[j].reset();
      mod_filter_r[j].reset();
      car_filter_l[j].reset();
      car_filter_r[j].reset();
      env_mod_l[j] = 0.0;
      env_mod_r[j] = 0.0;
    }
  }

  void initBandFreqs() {
    double f_min = 80.0;
    double f_max = 12000.0;
    for (int j = 0; j < 24; ++j) {
      band_freqs[j] = f_min * std::pow(f_max / f_min, j / 23.0);
    }
  }
};

BuiltinVocodey::BuiltinVocodey() : impl_(std::make_unique<Impl>()) {
  impl_->params[PARAM_ATTACK] = 0.15;        // ~5ms attack
  impl_->params[PARAM_DECAY] = 0.25;         // ~150ms decay
  impl_->params[PARAM_BANDWIDTH] = 0.35;     // Q factor ~4.0
  impl_->params[PARAM_NOISE_BLEED] = 0.3;    // moderate noise bleed
  impl_->params[PARAM_SYNTH_DETUNE] = 0.15;  // unison detune depth
  impl_->params[PARAM_DRY] = 0.0;            // dry level 0
  impl_->params[PARAM_WET] = 1.0;            // wet level 1.0
  impl_->params[PARAM_VOLUME] = 0.7;         // master volume 0.7

  impl_->initBandFreqs();
  impl_->reset();
}

BuiltinVocodey::~BuiltinVocodey() = default;

bool BuiltinVocodey::load(const std::string& /*path*/, int /*plugin_index*/,
                          double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->reset();
  return true;
}

float BuiltinVocodey::normToAttackS(double norm) {
  // 0.001s to 0.1s
  return (float)(0.001 + norm * 0.099);
}

float BuiltinVocodey::normToDecayS(double norm) {
  // 0.01s to 2.0s
  return (float)(0.01 + norm * 1.99);
}

float BuiltinVocodey::normToBandwidthQ(double norm) {
  // 0.5 to 10.0
  return (float)(0.5 + norm * 9.5);
}

void BuiltinVocodey::process(float** inputs, float** outputs, int num_samples,
                             const HostProcessContext& context,
                             const std::vector<MidiNoteEvent>& events,
                             float** sidechain) {
  impl_->sample_rate = context.sampleRate;

  // Process incoming MIDI events for the internal synth carrier
  for (const auto& ev : events) {
    if (ev.isNoteOn) {
      if (ev.velocity > 0) {
        // Voice triggering: look for a voice playing this note, or free voice
        int free_idx = -1;
        for (int i = 0; i < Impl::kMaxVoices; ++i) {
          if (impl_->voices[i].active && impl_->voices[i].pitch == ev.pitch) {
            free_idx = i;
            break;
          }
        }
        if (free_idx == -1) {
          for (int i = 0; i < Impl::kMaxVoices; ++i) {
            if (!impl_->voices[i].active && impl_->voices[i].amp < 0.001) {
              free_idx = i;
              break;
            }
          }
        }
        // Voice stealing if full
        if (free_idx == -1) {
          free_idx = 0;
        }
        auto& v = impl_->voices[free_idx];
        v.active = true;
        v.pitch = ev.pitch;
        v.phase1 = 0.0;
        v.phase2 = 0.0;
        v.amp = ev.velocity;
      } else {
        // NoteOff: release voice
        for (int i = 0; i < Impl::kMaxVoices; ++i) {
          if (impl_->voices[i].pitch == ev.pitch) {
            impl_->voices[i].active = false;
          }
        }
      }
    }
  }

  float* out_l = outputs[0];
  float* out_r = outputs[1];

  float* in_l = inputs[0];
  float* in_r = inputs[1];

  double att_s = normToAttackS(impl_->params[PARAM_ATTACK]);
  double dec_s = normToDecayS(impl_->params[PARAM_DECAY]);
  double q = normToBandwidthQ(impl_->params[PARAM_BANDWIDTH]);
  double noise_bleed = impl_->params[PARAM_NOISE_BLEED];
  double detune_depth = impl_->params[PARAM_SYNTH_DETUNE];
  double dry_gain = impl_->params[PARAM_DRY];
  double wet_gain = impl_->params[PARAM_WET];
  double volume = impl_->params[PARAM_VOLUME];

  double att_coeff = std::exp(-1.0 / (att_s * impl_->sample_rate));
  double rel_coeff = std::exp(-1.0 / (dec_s * impl_->sample_rate));
  double synth_release_coeff =
      std::exp(-1.0 / (0.008 * impl_->sample_rate));  // 8ms release

  // Setup filters center frequencies and Q factor
  for (int j = 0; j < 24; ++j) {
    float freq = (float)impl_->band_freqs[j];
    impl_->mod_filter_l[j].setParams(BiquadFilter::Type::BANDPASS, freq,
                                     (float)q, 0.0f, (float)impl_->sample_rate);
    impl_->mod_filter_r[j].setParams(BiquadFilter::Type::BANDPASS, freq,
                                     (float)q, 0.0f, (float)impl_->sample_rate);
    impl_->car_filter_l[j].setParams(BiquadFilter::Type::BANDPASS, freq,
                                     (float)q, 0.0f, (float)impl_->sample_rate);
    impl_->car_filter_r[j].setParams(BiquadFilter::Type::BANDPASS, freq,
                                     (float)q, 0.0f, (float)impl_->sample_rate);
  }

  // Check if sidechain is active and has buffers
  bool use_sidechain = (sidechain != nullptr && sidechain[0] != nullptr);

  for (int i = 0; i < num_samples; ++i) {
    double modulator_l = in_l[i];
    double modulator_r = in_r[i];

    // 1. Determine carrier sample
    double carrier_l = 0.0;
    double carrier_r = 0.0;

    if (use_sidechain) {
      carrier_l = sidechain[0][i];
      carrier_r = (sidechain[1] != nullptr) ? sidechain[1][i] : sidechain[0][i];
    } else {
      // Run internal polyphonic synth
      double synth_l = 0.0;
      double synth_r = 0.0;

      for (int v_idx = 0; v_idx < Impl::kMaxVoices; ++v_idx) {
        auto& v = impl_->voices[v_idx];
        if (v.amp < 0.0001) {
          v.active = false;
          v.amp = 0.0;
          continue;
        }

        double midi_pitch = v.pitch;
        double f_base = 440.0 * std::pow(2.0, (midi_pitch - 69.0) / 12.0);

        // Calculate detuning for two oscillators: Left and Right
        double detune_semitones =
            detune_depth * 0.4;  // max 0.4 semitones detune
        double f1 = f_base * std::pow(2.0, -detune_semitones / 12.0);
        double f2 = f_base * std::pow(2.0, detune_semitones / 12.0);

        double dt1 = f1 / impl_->sample_rate;
        double dt2 = f2 / impl_->sample_rate;

        // Step phase and generate PolyBLEP sawtooth
        v.phase1 += dt1;
        if (v.phase1 >= 1.0) v.phase1 -= 1.0;
        double saw1 = 2.0 * v.phase1 - 1.0;
        saw1 -= poly_blep(v.phase1, dt1);

        v.phase2 += dt2;
        if (v.phase2 >= 1.0) v.phase2 -= 1.0;
        double saw2 = 2.0 * v.phase2 - 1.0;
        saw2 -= poly_blep(v.phase2, dt2);

        // Mix stereo voice output
        synth_l += saw1 * v.amp;
        synth_r += saw2 * v.amp;

        // Apply release envelope
        if (!v.active) {
          v.amp *= synth_release_coeff;
        }
      }

      // Soft clamp synth output to avoid digital clipping before filterbank
      carrier_l = std::tanh(synth_l);
      carrier_r = std::tanh(synth_r);
    }

    // 2. Process modulator and carrier through the 24-band filterbank
    double vocoded_sum_l = 0.0;
    double vocoded_sum_r = 0.0;

    // Sibilance noise generator
    double noise = (((double)std::rand() / RAND_MAX) * 2.0 - 1.0);

    // Gate noise bleed if using internal synth and no active notes are playing
    double active_noise_bleed = noise_bleed;
    if (!use_sidechain) {
      double total_amp = 0.0;
      for (int v_idx = 0; v_idx < Impl::kMaxVoices; ++v_idx) {
        total_amp += impl_->voices[v_idx].amp;
      }
      if (total_amp < 0.001) {
        active_noise_bleed = 0.0;
      } else {
        active_noise_bleed = noise_bleed * std::min(1.0, total_amp);
      }
    }

    for (int j = 0; j < 24; ++j) {
      // Modulator filter bank
      float mod_bp_l = impl_->mod_filter_l[j].process((float)modulator_l);
      float mod_bp_r = impl_->mod_filter_r[j].process((float)modulator_r);

      // Envelope followers with attack/release logic
      double rect_l = std::abs(mod_bp_l);
      double rect_r = std::abs(mod_bp_r);

      if (rect_l > impl_->env_mod_l[j]) {
        impl_->env_mod_l[j] =
            rect_l * (1.0 - att_coeff) + impl_->env_mod_l[j] * att_coeff;
      } else {
        impl_->env_mod_l[j] =
            rect_l * (1.0 - rel_coeff) + impl_->env_mod_l[j] * rel_coeff;
      }

      if (rect_r > impl_->env_mod_r[j]) {
        impl_->env_mod_r[j] =
            rect_r * (1.0 - att_coeff) + impl_->env_mod_r[j] * att_coeff;
      } else {
        impl_->env_mod_r[j] =
            rect_r * (1.0 - rel_coeff) + impl_->env_mod_r[j] * rel_coeff;
      }

      // Carrier filter bank
      double band_carrier_l = carrier_l;
      double band_carrier_r = carrier_r;

      // In upper bands, blend in white noise governed by active_noise_bleed to
      // make consonants clear
      if (j >= 16) {
        band_carrier_l =
            (1.0 - active_noise_bleed) * carrier_l + active_noise_bleed * noise;
        band_carrier_r =
            (1.0 - active_noise_bleed) * carrier_r + active_noise_bleed * noise;
      }

      float car_bp_l = impl_->car_filter_l[j].process((float)band_carrier_l);
      float car_bp_r = impl_->car_filter_r[j].process((float)band_carrier_r);

      // Scale carrier band by modulator envelope
      vocoded_sum_l += car_bp_l * impl_->env_mod_l[j];
      vocoded_sum_r += car_bp_r * impl_->env_mod_r[j];
    }

    // 3. Summer and Master Gain
    // Boost vocoded sum slightly (empirical scale 2.0 to make it match typical
    // signal levels)
    double wet_l = vocoded_sum_l * 2.0;
    double wet_r = vocoded_sum_r * 2.0;

    out_l[i] = (float)((dry_gain * modulator_l + wet_gain * wet_l) * volume);
    out_r[i] = (float)((dry_gain * modulator_r + wet_gain * wet_r) * volume);
  }
}

int BuiltinVocodey::getParameterCount() const { return kTotalParams; }

bool BuiltinVocodey::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {"Attack", "Decay", "Bandwidth", "Noise Bleed",
                                "Detune", "Dry",   "Wet",       "Volume"};
  static const double defaults[] = {0.15, 0.25, 0.35, 0.3, 0.15, 0.0, 1.0, 0.7};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinVocodey::setParameterValue(uint32_t id, double valueNormalized) {
  if (id < kTotalParams) {
    impl_->params[id] = std::clamp(valueNormalized, 0.0, 1.0);
  }
}

double BuiltinVocodey::getParameterValue(uint32_t id) const {
  if (id < kTotalParams) {
    return impl_->params[id];
  }
  return 0.0;
}

const std::string& BuiltinVocodey::getName() const { return kVocodeyName; }

const std::string& BuiltinVocodey::getPath() const { return kVocodeyPath; }

int BuiltinVocodey::getPluginIndex() const { return 0; }

bool BuiltinVocodey::isInstrument() const { return false; }

}  // namespace hibiki
