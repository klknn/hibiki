#include "engine/instruments/builtin_dr8_crash.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "engine/core/biquad_filter.hpp"

namespace hibiki {

static const std::string kDr8CrashName = "DR8 Crash";
static const std::string kDr8CrashPath = "builtin://dr8_crash";

// TR-808 cymbal frequency ratios relative to base freq
static constexpr double kCymbalOscRatios[6] = {1.0,  1.8,  1.48,
                                               2.54, 3.89, 4.15};

struct BuiltinDr8Crash::Impl {
  double params[kTotalParams] = {};
  double sample_rate = 44100.0;

  // Synthesizer state
  bool active = false;
  double osc_phase[6] = {};
  double env_amp = 0.0;
  float velocity = 0.0f;

  // Filters
  BiquadFilter sizzle_hpf;
  BiquadFilter bpf1;
  BiquadFilter bpf2;

  void reset() {
    active = false;
    for (int i = 0; i < 6; ++i) osc_phase[i] = 0.0;
    env_amp = 0.0;
    velocity = 0.0f;
    sizzle_hpf.reset();
    bpf1.reset();
    bpf2.reset();
  }
};

BuiltinDr8Crash::BuiltinDr8Crash() : impl_(std::make_unique<Impl>()) {
  impl_->params[PARAM_DECAY] = 0.4;    // moderate decay default
  impl_->params[PARAM_TONE] = 0.4;     // blend of metal/sizzle default
  impl_->params[PARAM_TENSION] = 0.3;  // base tension default
  impl_->params[PARAM_VOLUME] = 0.7;   // master volume

  impl_->reset();
}

BuiltinDr8Crash::~BuiltinDr8Crash() = default;

bool BuiltinDr8Crash::load(const std::string& /*path*/, int /*plugin_index*/,
                           double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->reset();
  return true;
}

float BuiltinDr8Crash::normToDecayS(double norm) {
  // Exponential mapping: 0.2 seconds to 3.0 seconds
  return (float)(0.2 * std::pow(15.0, norm));
}

void BuiltinDr8Crash::process(float** /*inputs*/, float** outputs,
                              int num_samples,
                              const HostProcessContext& context,
                              const std::vector<MidiNoteEvent>& events,
                              float** /*sidechain*/) {
  impl_->sample_rate = context.sampleRate;

  // Process MIDI events
  for (const auto& ev : events) {
    if (ev.isNoteOn && ev.velocity > 0) {
      impl_->active = true;
      for (int i = 0; i < 6; ++i) impl_->osc_phase[i] = 0.0;
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
  double tone = impl_->params[PARAM_TONE];
  double tension = impl_->params[PARAM_TENSION];
  double volume = impl_->params[PARAM_VOLUME];

  // Base frequency scale: 150 Hz to 400 Hz
  double base_freq = 150.0 + tension * 250.0;

  double decay_coeff = std::exp(-1.0 / (decay_s * impl_->sample_rate));

  // Configure filters
  // Noise sizzle highpass filter (Q=0.707, tuned around 7 kHz)
  impl_->sizzle_hpf.setParams(BiquadFilter::Type::HIGHPASS, 7000.0f, 0.707f,
                              0.0f, (float)impl_->sample_rate);
  // Parallel bandpass filters centered around 800 Hz and 1.2 kHz
  impl_->bpf1.setParams(BiquadFilter::Type::BANDPASS, 800.0f, 1.0f, 0.0f,
                        (float)impl_->sample_rate);
  impl_->bpf2.setParams(BiquadFilter::Type::BANDPASS, 1200.0f, 1.2f, 0.0f,
                        (float)impl_->sample_rate);

  // Pre-calculate step increments
  double dt[6];
  for (int d = 0; d < 6; ++d) {
    dt[d] = (base_freq * kCymbalOscRatios[d]) / impl_->sample_rate;
  }

  for (int i = 0; i < num_samples; ++i) {
    if (impl_->env_amp < 0.0001) {
      impl_->active = false;
      impl_->reset();
      break;
    }

    // Accumulate the 6 detuned square waves
    double osc_sum = 0.0;
    for (int d = 0; d < 6; ++d) {
      impl_->osc_phase[d] += dt[d];
      if (impl_->osc_phase[d] >= 1.0) impl_->osc_phase[d] -= 1.0;
      osc_sum += (impl_->osc_phase[d] < 0.5 ? 1.0 : -1.0);
    }
    double metallic = osc_sum / 6.0;

    // Generate white noise sizzle
    double noise = (((double)std::rand() / RAND_MAX) * 2.0 - 1.0);
    float sizzle = impl_->sizzle_hpf.process((float)noise);

    // Blend metallic oscillators and high-pass noise
    double raw = (1.0 - tone) * metallic + tone * sizzle;

    // Pass through parallel bandpass filters
    float f1 = impl_->bpf1.process((float)raw);
    float f2 = impl_->bpf2.process((float)raw);
    float filtered = 0.5f * (f1 + f2);

    float out_val =
        (float)(filtered * impl_->env_amp * impl_->velocity * volume);
    out_l[i] = out_val;
    out_r[i] = out_val;

    // Decay the envelope
    impl_->env_amp *= decay_coeff;
  }
}

int BuiltinDr8Crash::getParameterCount() const { return kTotalParams; }

bool BuiltinDr8Crash::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {"Decay", "Tone", "Tension", "Volume"};
  static const double defaults[] = {0.4, 0.4, 0.3, 0.7};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinDr8Crash::setParameterValue(uint32_t id, double value) {
  if (id < kTotalParams) {
    impl_->params[id] = std::clamp(value, 0.0, 1.0);
  }
}

double BuiltinDr8Crash::getParameterValue(uint32_t id) const {
  if (id < kTotalParams) {
    return impl_->params[id];
  }
  return 0.0;
}

const std::string& BuiltinDr8Crash::getName() const { return kDr8CrashName; }

const std::string& BuiltinDr8Crash::getPath() const { return kDr8CrashPath; }

int BuiltinDr8Crash::getPluginIndex() const { return 0; }

bool BuiltinDr8Crash::isInstrument() const { return true; }

}  // namespace hibiki
