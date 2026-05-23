#include "engine/instruments/builtin_dr8_clap.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "engine/core/biquad_filter.hpp"

namespace hibiki {

static const std::string kDr8ClapName = "DR8 Clap";
static const std::string kDr8ClapPath = "builtin://dr8_clap";

struct BuiltinDr8Clap::Impl {
  double params[kTotalParams] = {};
  double sample_rate = 44100.0;

  // Synthesizer state
  bool active = false;
  int pre_clap_index = -1;  // 0, 1, 2 for pre-claps, 3 for main tail
  int sample_since_last_trigger = 0;
  double env_amp = 0.0;
  float velocity = 0.0f;

  // Filter state
  BiquadFilter bpf;

  void reset() {
    active = false;
    pre_clap_index = -1;
    sample_since_last_trigger = 0;
    env_amp = 0.0;
    velocity = 0.0f;
    bpf.reset();
  }
};

BuiltinDr8Clap::BuiltinDr8Clap() : impl_(std::make_unique<Impl>()) {
  impl_->params[PARAM_DECAY] = 0.3;          // ~180ms decay default
  impl_->params[PARAM_FILTER_CUTOFF] = 0.3;  // ~1000 Hz filter cutoff default
  impl_->params[PARAM_SPREAD] = 0.466667;    // ~12ms spread default
  impl_->params[PARAM_VOLUME] = 0.7;         // master volume

  impl_->reset();
}

BuiltinDr8Clap::~BuiltinDr8Clap() = default;

bool BuiltinDr8Clap::load(const std::string& /*path*/, int /*plugin_index*/,
                          double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->reset();
  return true;
}

float BuiltinDr8Clap::normToDecayS(double norm) {
  // Exponential mapping: 0.05 seconds to 1.0 seconds
  return (float)(0.05 * std::pow(20.0, norm));
}

float BuiltinDr8Clap::normToCutoffHz(double norm) {
  // Exponential mapping: 500 Hz to 3000 Hz
  return (float)(500.0 * std::pow(6.0, norm));
}

float BuiltinDr8Clap::normToSpreadS(double norm) {
  // Linear mapping: 5ms to 20ms
  return (float)(0.005 + norm * 0.015);
}

void BuiltinDr8Clap::process(float** /*inputs*/, float** outputs,
                             int num_samples, const HostProcessContext& context,
                             const std::vector<MidiNoteEvent>& events,
                             float** /*sidechain*/) {
  impl_->sample_rate = context.sampleRate;

  // Process MIDI events
  for (const auto& ev : events) {
    if (ev.isNoteOn && ev.velocity > 0) {
      impl_->active = true;
      impl_->pre_clap_index = 0;
      impl_->sample_since_last_trigger = 0;
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

  double decay_s = normToDecayS(impl_->params[PARAM_DECAY]);
  double cutoff_fc = normToCutoffHz(impl_->params[PARAM_FILTER_CUTOFF]);
  double spread_s = normToSpreadS(impl_->params[PARAM_SPREAD]);
  double volume = impl_->params[PARAM_VOLUME];

  int spread_samples = (int)(spread_s * impl_->sample_rate);
  spread_samples = std::max(1, spread_samples);

  // Envelope decay coefficients
  // Pre-claps decay fast (around 10ms constant decay time)
  double pre_clap_decay_coeff = std::exp(-1.0 / (0.010 * impl_->sample_rate));
  double main_decay_coeff = std::exp(-1.0 / (decay_s * impl_->sample_rate));

  // Configure BPF: Q factor around 1.0 to 1.5 for moderate resonant throatiness
  impl_->bpf.setParams(BiquadFilter::Type::BANDPASS, (float)cutoff_fc, 1.2f,
                       0.0f, (float)impl_->sample_rate);

  for (int i = 0; i < num_samples; ++i) {
    // If in pre-claps phase and time to trigger next clap
    if (impl_->pre_clap_index >= 0 && impl_->pre_clap_index < 3) {
      if (impl_->sample_since_last_trigger >= spread_samples) {
        impl_->pre_clap_index++;
        impl_->sample_since_last_trigger = 0;
        impl_->env_amp = 1.0;  // re-trigger
      }
    }

    if (impl_->pre_clap_index >= 3 && impl_->env_amp < 0.0001) {
      impl_->active = false;
      impl_->reset();
      break;
    }

    // Generate white noise
    double noise = (((double)std::rand() / RAND_MAX) * 2.0 - 1.0);

    // Apply envelope
    double raw = noise * impl_->env_amp;

    // Filter
    float filtered = impl_->bpf.process((float)raw);

    float out_val = (float)(filtered * impl_->velocity * volume);
    out_l[i] = out_val;
    out_r[i] = out_val;

    // Decay the envelope
    if (impl_->pre_clap_index < 3) {
      impl_->env_amp *= pre_clap_decay_coeff;
    } else {
      impl_->env_amp *= main_decay_coeff;
    }

    impl_->sample_since_last_trigger++;
  }
}

int BuiltinDr8Clap::getParameterCount() const { return kTotalParams; }

bool BuiltinDr8Clap::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {"Decay", "Filter Cutoff", "Spread", "Volume"};
  static const double defaults[] = {0.3, 0.3, 0.466667, 0.7};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinDr8Clap::setParameterValue(uint32_t id, double value) {
  if (id < kTotalParams) {
    impl_->params[id] = std::clamp(value, 0.0, 1.0);
  }
}

double BuiltinDr8Clap::getParameterValue(uint32_t id) const {
  if (id < kTotalParams) {
    return impl_->params[id];
  }
  return 0.0;
}

const std::string& BuiltinDr8Clap::getName() const { return kDr8ClapName; }

const std::string& BuiltinDr8Clap::getPath() const { return kDr8ClapPath; }

int BuiltinDr8Clap::getPluginIndex() const { return 0; }

bool BuiltinDr8Clap::isInstrument() const { return true; }

}  // namespace hibiki
