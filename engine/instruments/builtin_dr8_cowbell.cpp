#include "engine/instruments/builtin_dr8_cowbell.hpp"

#include <algorithm>
#include <cmath>

#include "engine/core/biquad_filter.hpp"

namespace hibiki {

static const std::string kDr8CowbellName = "DR8 Cowbell";
static const std::string kDr8CowbellPath = "builtin://dr8_cowbell";

struct BuiltinDr8Cowbell::Impl {
  double params[kTotalParams] = {};
  double sample_rate = 44100.0;

  // Synthesizer state
  bool active = false;
  double phase1 = 0.0;
  double phase2 = 0.0;
  double env_amp = 0.0;
  float velocity = 0.0f;

  // Filter state
  BiquadFilter bpf;

  void reset() {
    active = false;
    phase1 = 0.0;
    phase2 = 0.0;
    env_amp = 0.0;
    velocity = 0.0f;
    bpf.reset();
  }
};

BuiltinDr8Cowbell::BuiltinDr8Cowbell() : impl_(std::make_unique<Impl>()) {
  impl_->params[PARAM_PITCH] = 0.466667;  // ~540 Hz base pitch default
  impl_->params[PARAM_DECAY] = 0.3;       // ~150ms decay default
  impl_->params[PARAM_DETUNE] = 0.4;  // 1.48 detune ratio default (norm = 0.4)
  impl_->params[PARAM_VOLUME] = 0.7;  // master volume

  impl_->reset();
}

BuiltinDr8Cowbell::~BuiltinDr8Cowbell() = default;

bool BuiltinDr8Cowbell::load(const std::string& /*path*/, int /*plugin_index*/,
                             double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->reset();
  return true;
}

float BuiltinDr8Cowbell::normToPitchHz(double norm) {
  // Linear mapping: 400 Hz to 700 Hz
  return (float)(400.0 + norm * 300.0);
}

float BuiltinDr8Cowbell::normToDecayS(double norm) {
  // Exponential mapping: 0.05 seconds to 0.5 seconds
  return (float)(0.05 * std::pow(10.0, norm));
}

float BuiltinDr8Cowbell::normToDetuneRatio(double norm) {
  // Detuning frequency multiplier: 1.40 to 1.60
  return (float)(1.40 + norm * 0.20);
}

void BuiltinDr8Cowbell::process(float** /*inputs*/, float** outputs,
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
      impl_->env_amp = 1.0;
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
  double detune_ratio = normToDetuneRatio(impl_->params[PARAM_DETUNE]);
  double volume = impl_->params[PARAM_VOLUME];

  double f1 = base_freq;
  double f2 = base_freq * detune_ratio;

  double dt1 = f1 / impl_->sample_rate;
  double dt2 = f2 / impl_->sample_rate;

  double decay_coeff = std::exp(-1.0 / (decay_s * impl_->sample_rate));

  // Bandpass filter centered at base pitch to tone down square harmonics
  impl_->bpf.setParams(BiquadFilter::Type::BANDPASS, (float)base_freq, 2.0f,
                       0.0f, (float)impl_->sample_rate);

  for (int i = 0; i < num_samples; ++i) {
    if (impl_->env_amp < 0.0001) {
      impl_->active = false;
      impl_->reset();
      break;
    }

    // Step phases
    impl_->phase1 += dt1;
    if (impl_->phase1 >= 1.0) impl_->phase1 -= 1.0;
    impl_->phase2 += dt2;
    if (impl_->phase2 >= 1.0) impl_->phase2 -= 1.0;

    // Generate square waves
    double osc1 = (impl_->phase1 < 0.5 ? 1.0 : -1.0);
    double osc2 = (impl_->phase2 < 0.5 ? 1.0 : -1.0);

    // Sum and scale
    double sum = 0.5 * (osc1 + osc2);

    // Apply envelope
    double raw = sum * impl_->env_amp;

    // Filter
    float filtered = impl_->bpf.process((float)raw);

    float out_val = (float)(filtered * impl_->velocity * volume);
    out_l[i] = out_val;
    out_r[i] = out_val;

    // Decay
    impl_->env_amp *= decay_coeff;
  }
}

int BuiltinDr8Cowbell::getParameterCount() const { return kTotalParams; }

bool BuiltinDr8Cowbell::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {"Pitch", "Decay", "Detune", "Volume"};
  static const double defaults[] = {0.466667, 0.3, 0.4, 0.7};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinDr8Cowbell::setParameterValue(uint32_t id, double value) {
  if (id < kTotalParams) {
    impl_->params[id] = std::clamp(value, 0.0, 1.0);
  }
}

double BuiltinDr8Cowbell::getParameterValue(uint32_t id) const {
  if (id < kTotalParams) {
    return impl_->params[id];
  }
  return 0.0;
}

const std::string& BuiltinDr8Cowbell::getName() const {
  return kDr8CowbellName;
}

const std::string& BuiltinDr8Cowbell::getPath() const {
  return kDr8CowbellPath;
}

int BuiltinDr8Cowbell::getPluginIndex() const { return 0; }

bool BuiltinDr8Cowbell::isInstrument() const { return true; }

}  // namespace hibiki
