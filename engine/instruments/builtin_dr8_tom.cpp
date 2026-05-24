#include "engine/instruments/builtin_dr8_tom.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "engine/core/biquad_filter.hpp"
#include "engine/core/math.hpp"

namespace hibiki {

static const std::string kDr8TomName = "DR8 Tom";
static const std::string kDr8TomPath = "builtin://dr8_tom";

struct BuiltinDr8Tom::Impl {
  double params[kTotalParams] = {};
  double sample_rate = 44100.0;

  // Synthesizer state
  bool active = false;
  double osc_phase = 0.0;
  double env_amp = 0.0;
  double env_pitch = 0.0;
  double env_click = 0.0;
  float velocity = 0.0f;

  // Woody tone filter
  BiquadFilter lpf;

  void reset() {
    active = false;
    osc_phase = 0.0;
    env_amp = 0.0;
    env_pitch = 0.0;
    env_click = 0.0;
    velocity = 0.0f;
    lpf.reset();
  }
};

BuiltinDr8Tom::BuiltinDr8Tom() : impl_(std::make_unique<Impl>()) {
  impl_->params[PARAM_PITCH] = 0.3;            // ~100 Hz base pitch
  impl_->params[PARAM_DECAY] = 0.4;            // ~400ms decay
  impl_->params[PARAM_PITCH_ENV_DECAY] = 0.4;  // ~100ms pitch decay
  impl_->params[PARAM_PITCH_ENV_DEPTH] = 0.4;  // ~40 Hz pitch sweep
  impl_->params[PARAM_NOISE_ATTACK] = 0.15;    // slight noise click
  impl_->params[PARAM_VOLUME] = 0.7;           // master volume

  impl_->reset();
}

BuiltinDr8Tom::~BuiltinDr8Tom() = default;

bool BuiltinDr8Tom::load(const std::string& /*path*/, int /*plugin_index*/,
                         double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->reset();
  return true;
}

float BuiltinDr8Tom::normToPitchHz(double norm) {
  // Linear mapping: 70 Hz to 200 Hz
  return (float)(70.0 + norm * 130.0);
}

float BuiltinDr8Tom::normToDecayS(double norm) {
  // Exponential mapping: 0.1 seconds to 1.5 seconds
  return (float)(0.1 * std::pow(15.0, norm));
}

float BuiltinDr8Tom::normToPitchEnvDecayS(double norm) {
  // Linear mapping: 0.02 seconds to 0.3 seconds
  return (float)(0.02 + norm * 0.28);
}

float BuiltinDr8Tom::normToPitchEnvDepthHz(double norm) {
  // Linear mapping: 0 Hz to 100 Hz
  return (float)(norm * 100.0);
}

void BuiltinDr8Tom::process(float** /*inputs*/, float** outputs,
                            int num_samples, const HostProcessContext& context,
                            const std::vector<MidiNoteEvent>& events,
                            float** /*sidechain*/) {
  impl_->sample_rate = context.sampleRate;

  // Process MIDI events
  for (const auto& ev : events) {
    if (ev.isNoteOn && ev.velocity > 0) {
      impl_->active = true;
      impl_->env_amp = 1.0;
      impl_->env_pitch = 1.0;
      impl_->env_click = 1.0;
      impl_->osc_phase = 0.0;
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

  double base_freq = normToPitchHz(impl_->params[PARAM_PITCH]);
  double decay_s = normToDecayS(impl_->params[PARAM_DECAY]);
  double pitch_decay_s =
      normToPitchEnvDecayS(impl_->params[PARAM_PITCH_ENV_DECAY]);
  double pitch_depth =
      normToPitchEnvDepthHz(impl_->params[PARAM_PITCH_ENV_DEPTH]);
  double click_level = impl_->params[PARAM_NOISE_ATTACK];
  double volume = impl_->params[PARAM_VOLUME];

  // Coefficients
  double amp_decay_coeff = std::exp(-1.0 / (decay_s * impl_->sample_rate));
  double pitch_decay_coeff =
      std::exp(-1.0 / (pitch_decay_s * impl_->sample_rate));
  double click_decay_coeff =
      std::exp(-1.0 / (0.004 * impl_->sample_rate));  // 4ms attack click

  for (int i = 0; i < num_samples; ++i) {
    if (impl_->env_amp < 0.0001) {
      impl_->active = false;
      impl_->reset();
      break;
    }

    // Dynamic pitch
    double current_freq = base_freq + impl_->env_pitch * pitch_depth;
    current_freq = std::max(20.0, current_freq);

    // Step phase
    double dt = current_freq / impl_->sample_rate;
    impl_->osc_phase += dt;
    if (impl_->osc_phase >= 1.0) impl_->osc_phase -= 1.0;

    double sine_out = std::sin(2.0 * hibiki::pi * impl_->osc_phase);

    // Click transient
    double noise = (((double)std::rand() / RAND_MAX) * 2.0 - 1.0);
    double click = noise * impl_->env_click * click_level;

    double raw = sine_out * impl_->env_amp * impl_->velocity + click;

    // Filter to keep the tom woody and warm
    impl_->lpf.setParams(BiquadFilter::Type::LOWPASS,
                         (float)(current_freq * 2.2), 0.707f, 0.0f,
                         (float)impl_->sample_rate);
    float filtered = impl_->lpf.process((float)raw);

    float out_val = (float)(filtered * volume);

    out_l[i] = out_val;
    out_r[i] = out_val;

    // Decay envelopes
    impl_->env_amp *= amp_decay_coeff;
    impl_->env_pitch *= pitch_decay_coeff;
    impl_->env_click *= click_decay_coeff;
  }
}

int BuiltinDr8Tom::getParameterCount() const { return kTotalParams; }

bool BuiltinDr8Tom::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {"Pitch",           "Decay",
                                "Pitch Env Decay", "Pitch Env Depth",
                                "Noise Attack",    "Volume"};
  static const double defaults[] = {0.3, 0.4, 0.4, 0.4, 0.15, 0.7};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinDr8Tom::setParameterValue(uint32_t id, double value) {
  if (id < kTotalParams) {
    impl_->params[id] = std::clamp(value, 0.0, 1.0);
  }
}

double BuiltinDr8Tom::getParameterValue(uint32_t id) const {
  if (id < kTotalParams) {
    return impl_->params[id];
  }
  return 0.0;
}

const std::string& BuiltinDr8Tom::getName() const { return kDr8TomName; }

const std::string& BuiltinDr8Tom::getPath() const { return kDr8TomPath; }

int BuiltinDr8Tom::getPluginIndex() const { return 0; }

bool BuiltinDr8Tom::isInstrument() const { return true; }

}  // namespace hibiki
