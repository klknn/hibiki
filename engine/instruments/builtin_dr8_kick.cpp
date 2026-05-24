#include "engine/instruments/builtin_dr8_kick.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "engine/core/math.hpp"

namespace hibiki {

static const std::string kDr8KickName = "DR8 Kick";
static const std::string kDr8KickPath = "builtin://dr8_kick";

struct BuiltinDr8Kick::Impl {
  double params[kTotalParams] = {};
  double sample_rate = 44100.0;

  // Synthesizer state
  bool active = false;
  double osc_phase = 0.0;
  double env_amp = 0.0;
  double env_pitch = 0.0;
  double env_click = 0.0;
  float velocity = 0.0f;

  void reset() {
    active = false;
    osc_phase = 0.0;
    env_amp = 0.0;
    env_pitch = 0.0;
    env_click = 0.0;
    velocity = 0.0f;
  }
};

BuiltinDr8Kick::BuiltinDr8Kick() : impl_(std::make_unique<Impl>()) {
  impl_->params[PARAM_PITCH] = 0.25;           // ~50 Hz base
  impl_->params[PARAM_DECAY] = 0.4;            // ~400ms decay
  impl_->params[PARAM_PITCH_ENV_DECAY] = 0.3;  // ~50ms pitch sweep decay
  impl_->params[PARAM_PITCH_ENV_DEPTH] = 0.5;  // ~150 Hz depth
  impl_->params[PARAM_CLICK_LEVEL] = 0.2;      // moderate click
  impl_->params[PARAM_DISTORTION] = 0.1;       // light drive
  impl_->params[PARAM_VOLUME] = 0.7;           // master volume

  impl_->reset();
}

BuiltinDr8Kick::~BuiltinDr8Kick() = default;

bool BuiltinDr8Kick::load(const std::string& /*path*/, int /*plugin_index*/,
                          double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->reset();
  return true;
}

float BuiltinDr8Kick::normToPitchHz(double norm) {
  // Linear mapping: 40 Hz to 80 Hz
  return (float)(40.0 + norm * 40.0);
}

float BuiltinDr8Kick::normToDecayS(double norm) {
  // Exponential mapping: 0.05 seconds to 1.0 seconds
  return (float)(0.05 * std::pow(20.0, norm));
}

float BuiltinDr8Kick::normToPitchEnvDecayS(double norm) {
  // Linear mapping: 0.01 seconds to 0.15 seconds
  return (float)(0.01 + norm * 0.14);
}

float BuiltinDr8Kick::normToPitchEnvDepthHz(double norm) {
  // Linear mapping: 0 Hz to 300 Hz
  return (float)(norm * 300.0);
}

void BuiltinDr8Kick::process(float** /*inputs*/, float** outputs,
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
  double click_level = impl_->params[PARAM_CLICK_LEVEL];
  double distortion = impl_->params[PARAM_DISTORTION];
  double volume = impl_->params[PARAM_VOLUME];

  // Coefficients
  double amp_decay_coeff = std::exp(-1.0 / (decay_s * impl_->sample_rate));
  double pitch_decay_coeff =
      std::exp(-1.0 / (pitch_decay_s * impl_->sample_rate));
  double click_decay_coeff = std::exp(
      -1.0 / (0.005 * impl_->sample_rate));  // 5ms fixed click envelope

  for (int i = 0; i < num_samples; ++i) {
    if (impl_->env_amp < 0.0001) {
      impl_->active = false;
      impl_->reset();
      break;
    }

    // Dynamic frequency calculation
    double current_freq = base_freq + impl_->env_pitch * pitch_depth;
    current_freq = std::max(10.0, current_freq);

    // Oscillator phase step
    double dt = current_freq / impl_->sample_rate;
    impl_->osc_phase += dt;
    if (impl_->osc_phase >= 1.0) impl_->osc_phase -= 1.0;

    double sine_out = std::sin(2.0 * hibiki::pi * impl_->osc_phase);

    // Generate Click noise
    double noise = (((double)std::rand() / RAND_MAX) * 2.0 - 1.0);
    double click = noise * impl_->env_click * click_level;

    // Sum
    double raw = sine_out * impl_->env_amp * impl_->velocity + click;

    // Saturation
    double drive = distortion * 5.0;
    double saturated = std::tanh(raw * (1.0 + drive));

    // Scaling
    float out_val = (float)(saturated * volume);

    out_l[i] = out_val;
    out_r[i] = out_val;

    // Decay envelopes
    impl_->env_amp *= amp_decay_coeff;
    impl_->env_pitch *= pitch_decay_coeff;
    impl_->env_click *= click_decay_coeff;
  }
}

int BuiltinDr8Kick::getParameterCount() const { return kTotalParams; }

bool BuiltinDr8Kick::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {
      "Pitch",       "Decay",      "Pitch Env Decay", "Pitch Env Depth",
      "Click Level", "Distortion", "Volume"};
  static const double defaults[] = {0.25, 0.4, 0.3, 0.5, 0.2, 0.1, 0.7};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinDr8Kick::setParameterValue(uint32_t id, double value) {
  if (id < kTotalParams) {
    impl_->params[id] = std::clamp(value, 0.0, 1.0);
  }
}

double BuiltinDr8Kick::getParameterValue(uint32_t id) const {
  if (id < kTotalParams) {
    return impl_->params[id];
  }
  return 0.0;
}

const std::string& BuiltinDr8Kick::getName() const { return kDr8KickName; }

const std::string& BuiltinDr8Kick::getPath() const { return kDr8KickPath; }

int BuiltinDr8Kick::getPluginIndex() const { return 0; }

bool BuiltinDr8Kick::isInstrument() const { return true; }

}  // namespace hibiki
