#include "engine/instruments/builtin_dr8_conga.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "engine/core/biquad_filter.hpp"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace hibiki {

static const std::string kDr8CongaName = "DR8 Conga";
static const std::string kDr8CongaPath = "builtin://dr8_conga";

struct BuiltinDr8Conga::Impl {
  double params[kTotalParams] = {};
  double sample_rate = 44100.0;

  // Synthesizer state
  bool active = false;
  double osc_phase = 0.0;
  double env_amp = 0.0;
  double env_pitch = 0.0;
  double env_click = 0.0;
  float velocity = 0.0f;

  // Warm tone filter
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

BuiltinDr8Conga::BuiltinDr8Conga() : impl_(std::make_unique<Impl>()) {
  impl_->params[PARAM_PITCH] = 0.3;             // ~210 Hz base pitch default
  impl_->params[PARAM_DECAY] = 0.4;             // moderate amplitude decay
  impl_->params[PARAM_PITCH_ENV_DECAY] = 0.4;   // pitch decay
  impl_->params[PARAM_PITCH_ENV_DEPTH] = 0.33;  // ~40 Hz pitch sweep
  impl_->params[PARAM_VOLUME] = 0.7;            // master volume default

  impl_->reset();
}

BuiltinDr8Conga::~BuiltinDr8Conga() = default;

bool BuiltinDr8Conga::load(const std::string& /*path*/, int /*plugin_index*/,
                           double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->reset();
  return true;
}

float BuiltinDr8Conga::normToPitchHz(double norm) {
  // Linear mapping: 150 Hz to 350 Hz
  return (float)(150.0 + norm * 200.0);
}

float BuiltinDr8Conga::normToDecayS(double norm) {
  // Exponential mapping: 0.05s to 0.8s
  return (float)(0.05 * std::pow(16.0, norm));
}

float BuiltinDr8Conga::normToPitchEnvDecayS(double norm) {
  // Linear mapping: 0.01s to 0.15s
  return (float)(0.01 + norm * 0.14);
}

float BuiltinDr8Conga::normToPitchEnvDepthHz(double norm) {
  // Linear mapping: 0 Hz to 120 Hz
  return (float)(norm * 120.0);
}

void BuiltinDr8Conga::process(float** /*inputs*/, float** outputs,
                              int num_samples,
                              const HostProcessContext& context,
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
  double volume = impl_->params[PARAM_VOLUME];

  // Coefficients
  double amp_decay_coeff = std::exp(-1.0 / (decay_s * impl_->sample_rate));
  double pitch_decay_coeff =
      std::exp(-1.0 / (pitch_decay_s * impl_->sample_rate));
  double click_decay_coeff =
      std::exp(-1.0 / (0.003 * impl_->sample_rate));  // sharp 3ms click

  for (int i = 0; i < num_samples; ++i) {
    if (impl_->env_amp < 0.0001) {
      impl_->active = false;
      impl_->reset();
      break;
    }

    // Dynamic pitch sweep
    double current_freq = base_freq + impl_->env_pitch * pitch_depth;
    current_freq = std::max(20.0, current_freq);

    // Step phase
    double dt = current_freq / impl_->sample_rate;
    impl_->osc_phase += dt;
    if (impl_->osc_phase >= 1.0) impl_->osc_phase -= 1.0;

    double sine_out = std::sin(2.0 * M_PI * impl_->osc_phase);

    // Hardcoded noise click level (0.12) to emulate physical strike click
    double noise = (((double)std::rand() / RAND_MAX) * 2.0 - 1.0);
    double click = noise * impl_->env_click * 0.12;

    double raw = sine_out * impl_->env_amp * impl_->velocity + click;

    // Filter to warm up the sound and reduce high-frequency click harshness
    impl_->lpf.setParams(BiquadFilter::Type::LOWPASS,
                         (float)(current_freq * 2.5), 0.707f, 0.0f,
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

int BuiltinDr8Conga::getParameterCount() const { return kTotalParams; }

bool BuiltinDr8Conga::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {"Pitch", "Decay", "Pitch Env Decay",
                                "Pitch Env Depth", "Volume"};
  static const double defaults[] = {0.3, 0.4, 0.4, 0.33, 0.7};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinDr8Conga::setParameterValue(uint32_t id, double value) {
  if (id < kTotalParams) {
    impl_->params[id] = std::clamp(value, 0.0, 1.0);
  }
}

double BuiltinDr8Conga::getParameterValue(uint32_t id) const {
  if (id < kTotalParams) {
    return impl_->params[id];
  }
  return 0.0;
}

const std::string& BuiltinDr8Conga::getName() const { return kDr8CongaName; }

const std::string& BuiltinDr8Conga::getPath() const { return kDr8CongaPath; }

int BuiltinDr8Conga::getPluginIndex() const { return 0; }

bool BuiltinDr8Conga::isInstrument() const { return true; }

}  // namespace hibiki
