#include "engine/instruments/builtin_dr8_rim.hpp"

#include <algorithm>
#include <cmath>

#include "engine/core/biquad_filter.hpp"
#include "engine/core/math.hpp"

namespace hibiki {

static const std::string kDr8RimName = "DR8 Rimshot";
static const std::string kDr8RimPath = "builtin://dr8_rim";

struct BuiltinDr8Rim::Impl {
  double params[kTotalParams] = {};
  double sample_rate = 44100.0;

  // Synthesizer state
  bool active = false;
  double osc_phase[3] = {};
  double env_amp[3] = {};
  float velocity = 0.0f;

  // Filters
  BiquadFilter bpf[3];

  void reset() {
    active = false;
    for (int i = 0; i < 3; ++i) {
      osc_phase[i] = 0.0;
      env_amp[i] = 0.0;
      bpf[i].reset();
    }
    velocity = 0.0f;
  }
};

BuiltinDr8Rim::BuiltinDr8Rim() : impl_(std::make_unique<Impl>()) {
  impl_->params[PARAM_PITCH] = 0.4;   // ~320 Hz base pitch default
  impl_->params[PARAM_DECAY] = 0.3;   // ~37ms decay default
  impl_->params[PARAM_VOLUME] = 0.7;  // master volume default

  impl_->reset();
}

BuiltinDr8Rim::~BuiltinDr8Rim() = default;

bool BuiltinDr8Rim::load(const std::string& /*path*/, int /*plugin_index*/,
                         double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->reset();
  return true;
}

float BuiltinDr8Rim::normToPitchHz(double norm) {
  // Linear mapping: 200 Hz to 500 Hz
  return (float)(200.0 + norm * 300.0);
}

float BuiltinDr8Rim::normToDecayS(double norm) {
  // Linear mapping: 0.01s to 0.1s
  return (float)(0.01 + norm * 0.09);
}

void BuiltinDr8Rim::process(float** /*inputs*/, float** outputs,
                            int num_samples, const HostProcessContext& context,
                            const std::vector<MidiNoteEvent>& events,
                            float** /*sidechain*/) {
  impl_->sample_rate = context.sampleRate;

  // Process MIDI events
  for (const auto& ev : events) {
    if (ev.isNoteOn && ev.velocity > 0) {
      impl_->active = true;
      for (int i = 0; i < 3; ++i) {
        impl_->osc_phase[i] = 0.0;
        impl_->env_amp[i] = 1.0;
      }
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
  double volume = impl_->params[PARAM_VOLUME];

  double f[3];
  f[0] = base_freq;
  f[1] = base_freq * 2.6;
  f[2] = base_freq * 4.6;

  // Clamp frequencies below Nyquist
  double nyquist = impl_->sample_rate * 0.49;
  for (int j = 0; j < 3; ++j) {
    if (f[j] > nyquist) f[j] = nyquist;
  }

  double dt[3];
  for (int j = 0; j < 3; ++j) {
    dt[j] = f[j] / impl_->sample_rate;
  }

  // Decay coefficients: higher components decay faster
  double decay_s_val[3];
  decay_s_val[0] = decay_s;
  decay_s_val[1] = decay_s * 0.5;
  decay_s_val[2] = decay_s * 0.25;

  double decay_coeff[3];
  for (int j = 0; j < 3; ++j) {
    decay_coeff[j] = std::exp(-1.0 / (decay_s_val[j] * impl_->sample_rate));
  }

  // Configure filters (resonant bandpass filters around each frequency)
  for (int j = 0; j < 3; ++j) {
    impl_->bpf[j].setParams(BiquadFilter::Type::BANDPASS, (float)f[j], 4.0f,
                            0.0f, (float)impl_->sample_rate);
  }

  for (int i = 0; i < num_samples; ++i) {
    if (impl_->env_amp[0] < 0.0001 && impl_->env_amp[1] < 0.0001 &&
        impl_->env_amp[2] < 0.0001) {
      impl_->active = false;
      impl_->reset();
      break;
    }

    double signal_sum = 0.0;

    // Component 1 (fundamental)
    impl_->osc_phase[0] += dt[0];
    if (impl_->osc_phase[0] >= 1.0) impl_->osc_phase[0] -= 1.0;
    double osc0 = std::sin(2.0 * hibiki::pi * impl_->osc_phase[0]);
    float filt0 = impl_->bpf[0].process((float)osc0);
    signal_sum += 0.4 * filt0 * impl_->env_amp[0];

    // Component 2 (2.6x harmonic)
    impl_->osc_phase[1] += dt[1];
    if (impl_->osc_phase[1] >= 1.0) impl_->osc_phase[1] -= 1.0;
    double osc1 = std::sin(2.0 * hibiki::pi * impl_->osc_phase[1]);
    float filt1 = impl_->bpf[1].process((float)osc1);
    signal_sum += 0.4 * filt1 * impl_->env_amp[1];

    // Component 3 (4.6x harmonic)
    impl_->osc_phase[2] += dt[2];
    if (impl_->osc_phase[2] >= 1.0) impl_->osc_phase[2] -= 1.0;
    double osc2 = std::sin(2.0 * hibiki::pi * impl_->osc_phase[2]);
    float filt2 = impl_->bpf[2].process((float)osc2);
    signal_sum += 0.2 * filt2 * impl_->env_amp[2];

    float out_val = (float)(signal_sum * impl_->velocity * volume);
    out_l[i] = out_val;
    out_r[i] = out_val;

    // Apply envelope decay
    for (int j = 0; j < 3; ++j) {
      impl_->env_amp[j] *= decay_coeff[j];
    }
  }
}

int BuiltinDr8Rim::getParameterCount() const { return kTotalParams; }

bool BuiltinDr8Rim::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {"Pitch", "Decay", "Volume"};
  static const double defaults[] = {0.4, 0.3, 0.7};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinDr8Rim::setParameterValue(uint32_t id, double value) {
  if (id < kTotalParams) {
    impl_->params[id] = std::clamp(value, 0.0, 1.0);
  }
}

double BuiltinDr8Rim::getParameterValue(uint32_t id) const {
  if (id < kTotalParams) {
    return impl_->params[id];
  }
  return 0.0;
}

const std::string& BuiltinDr8Rim::getName() const { return kDr8RimName; }

const std::string& BuiltinDr8Rim::getPath() const { return kDr8RimPath; }

int BuiltinDr8Rim::getPluginIndex() const { return 0; }

bool BuiltinDr8Rim::isInstrument() const { return true; }

}  // namespace hibiki
