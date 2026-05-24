#include "engine/instruments/builtin_dr8_snare.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "engine/core/math.hpp"

namespace hibiki {

static const std::string kDr8SnareName = "DR8 Snare";
static const std::string kDr8SnarePath = "builtin://dr8_snare";

struct BuiltinDr8Snare::Impl {
  double params[kTotalParams] = {};
  double sample_rate = 44100.0;

  // Synthesizer state
  bool active = false;
  double phase1 = 0.0;
  double phase2 = 0.0;
  double env_tone = 0.0;
  double env_noise = 0.0;
  float velocity = 0.0f;

  // Crossover/Filter state (1-pole highpass for noise wires)
  double noise_lp_state = 0.0;

  void reset() {
    active = false;
    phase1 = 0.0;
    phase2 = 0.0;
    env_tone = 0.0;
    env_noise = 0.0;
    velocity = 0.0f;
    noise_lp_state = 0.0;
  }
};

BuiltinDr8Snare::BuiltinDr8Snare() : impl_(std::make_unique<Impl>()) {
  impl_->params[PARAM_PITCH] = 0.333333;      // ~150 Hz default skin pitch
  impl_->params[PARAM_DECAY] = 0.3;           // ~150ms skin decay
  impl_->params[PARAM_NOISE_LEVEL] = 0.5;     // moderate noise level
  impl_->params[PARAM_NOISE_DECAY] = 0.4;     // ~250ms noise decay
  impl_->params[PARAM_NOISE_HPF] = 0.4;       // ~2000 Hz HPF
  impl_->params[PARAM_TONE_NOISE_MIX] = 0.5;  // 50/50 mix
  impl_->params[PARAM_VOLUME] = 0.7;          // master volume

  impl_->reset();
}

BuiltinDr8Snare::~BuiltinDr8Snare() = default;

bool BuiltinDr8Snare::load(const std::string& /*path*/, int /*plugin_index*/,
                           double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->reset();
  return true;
}

float BuiltinDr8Snare::normToPitchHz(double norm) {
  // Linear mapping: 100 Hz to 250 Hz
  return (float)(100.0 + norm * 150.0);
}

float BuiltinDr8Snare::normToDecayS(double norm) {
  // Exponential mapping: 0.05 seconds to 0.5 seconds
  return (float)(0.05 * std::pow(10.0, norm));
}

float BuiltinDr8Snare::normToNoiseDecayS(double norm) {
  // Exponential mapping: 0.05 seconds to 1.0 seconds
  return (float)(0.05 * std::pow(20.0, norm));
}

float BuiltinDr8Snare::normToNoiseHpfHz(double norm) {
  // Exponential mapping: 800 Hz to 8000 Hz
  return (float)(800.0 * std::pow(10.0, norm));
}

void BuiltinDr8Snare::process(float** /*inputs*/, float** outputs,
                              int num_samples,
                              const HostProcessContext& context,
                              const std::vector<MidiNoteEvent>& events,
                              float** /*sidechain*/) {
  impl_->sample_rate = context.sampleRate;

  // Process MIDI events
  for (const auto& ev : events) {
    if (ev.isNoteOn && ev.velocity > 0) {
      impl_->active = true;
      impl_->phase1 = 0.0;
      impl_->phase2 = 0.0;
      impl_->env_tone = 1.0;
      impl_->env_noise = 1.0;
      impl_->velocity = ev.velocity;
    }
  }

  float* out_l = outputs[0];
  float* out_r = outputs[1];

  for (int i = 0; i < num_samples; ++i) {
    out_l[i] = 0.0f;
    out_r[i] = 0.0f;
  }

  if (!impl_->active) return;

  double f1 = normToPitchHz(impl_->params[PARAM_PITCH]);
  double f2 = f1 * 1.6;  // resonant second drum mode
  double decay_s = normToDecayS(impl_->params[PARAM_DECAY]);
  double noise_level = impl_->params[PARAM_NOISE_LEVEL];
  double noise_decay_s = normToNoiseDecayS(impl_->params[PARAM_NOISE_DECAY]);
  double noise_hpf_fc = normToNoiseHpfHz(impl_->params[PARAM_NOISE_HPF]);
  double mix = impl_->params[PARAM_TONE_NOISE_MIX];
  double volume = impl_->params[PARAM_VOLUME];

  // Coefficients
  double tone_decay_coeff = std::exp(-1.0 / (decay_s * impl_->sample_rate));
  double noise_decay_coeff =
      std::exp(-1.0 / (noise_decay_s * impl_->sample_rate));

  double filter_theta = 2.0 * hibiki::pi * noise_hpf_fc / impl_->sample_rate;
  double filter_alpha = 1.0 - std::exp(-filter_theta);
  filter_alpha = std::clamp(filter_alpha, 0.0, 1.0);

  double dt1 = f1 / impl_->sample_rate;
  double dt2 = f2 / impl_->sample_rate;

  for (int i = 0; i < num_samples; ++i) {
    if (impl_->env_tone < 0.0001 && impl_->env_noise < 0.0001) {
      impl_->active = false;
      impl_->reset();
      break;
    }

    // Update phases
    impl_->phase1 += dt1;
    if (impl_->phase1 >= 1.0) impl_->phase1 -= 1.0;
    impl_->phase2 += dt2;
    if (impl_->phase2 >= 1.0) impl_->phase2 -= 1.0;

    // Resonant shell tone (sine + harmonic sine)
    double tone_out = (std::sin(2.0 * hibiki::pi * impl_->phase1) +
                       0.4 * std::sin(2.0 * hibiki::pi * impl_->phase2)) *
                      impl_->env_tone;

    // Snare wire noise
    double noise = (((double)std::rand() / RAND_MAX) * 2.0 - 1.0);

    // Apply highpass filter to noise
    impl_->noise_lp_state += filter_alpha * (noise - impl_->noise_lp_state);
    double filtered_noise = noise - impl_->noise_lp_state;

    double noise_out = filtered_noise * impl_->env_noise * noise_level;

    // Mix body tone and noise wires
    double mix_val = (1.0 - mix) * tone_out + mix * noise_out;

    float out_val = (float)(mix_val * impl_->velocity * volume);

    out_l[i] = out_val;
    out_r[i] = out_val;

    // Decay envelopes
    impl_->env_tone *= tone_decay_coeff;
    impl_->env_noise *= noise_decay_coeff;
  }
}

int BuiltinDr8Snare::getParameterCount() const { return kTotalParams; }

bool BuiltinDr8Snare::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {"Pitch",       "Decay",     "Noise Level",
                                "Noise Decay", "Noise HPF", "Mix",
                                "Volume"};
  static const double defaults[] = {0.333333, 0.3, 0.5, 0.4, 0.4, 0.5, 0.7};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinDr8Snare::setParameterValue(uint32_t id, double value) {
  if (id < kTotalParams) {
    impl_->params[id] = std::clamp(value, 0.0, 1.0);
  }
}

double BuiltinDr8Snare::getParameterValue(uint32_t id) const {
  if (id < kTotalParams) {
    return impl_->params[id];
  }
  return 0.0;
}

const std::string& BuiltinDr8Snare::getName() const { return kDr8SnareName; }

const std::string& BuiltinDr8Snare::getPath() const { return kDr8SnarePath; }

int BuiltinDr8Snare::getPluginIndex() const { return 0; }

bool BuiltinDr8Snare::isInstrument() const { return true; }

}  // namespace hibiki
