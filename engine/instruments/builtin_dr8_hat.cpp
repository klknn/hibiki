#include "engine/instruments/builtin_dr8_hat.hpp"

#include <algorithm>
#include <cmath>

#include "engine/core/biquad_filter.hpp"

namespace hibiki {

static const std::string kDr8HatName = "DR8 Hat";
static const std::string kDr8HatPath = "builtin://dr8_hat";

// Detuning ratios for the 6 square-wave oscillators in TR-808 hihat
static constexpr double kHatOscRatios[6] = {
    1.0,    // base
    1.315,  // Osc 2
    1.55,   // Osc 3
    1.715,  // Osc 4
    2.0,    // Osc 5
    2.25    // Osc 6
};

struct BuiltinDr8Hat::Impl {
  double params[kTotalParams] = {};
  double sample_rate = 44100.0;

  // Synthesizer state
  bool active = false;
  double osc_phase[6] = {};
  double env_amp = 0.0;
  float velocity = 0.0f;

  // Filters (one bandpass, one highpass)
  BiquadFilter bpf;
  BiquadFilter hpf;

  void reset() {
    active = false;
    std::fill(std::begin(osc_phase), std::end(osc_phase), 0.0);
    env_amp = 0.0;
    velocity = 0.0f;
    bpf.reset();
    hpf.reset();
  }
};

BuiltinDr8Hat::BuiltinDr8Hat() : impl_(std::make_unique<Impl>()) {
  impl_->params[PARAM_DECAY] = 0.3;     // moderate decay
  impl_->params[PARAM_HPF_FREQ] = 0.5;  // ~7.5 kHz high-pass
  impl_->params[PARAM_BPF_FREQ] = 0.5;  // ~10 kHz band-pass
  impl_->params[PARAM_TENSION] = 0.4;   // base frequency ~270 Hz
  impl_->params[PARAM_VOLUME] = 0.7;    // master volume

  impl_->reset();
}

BuiltinDr8Hat::~BuiltinDr8Hat() = default;

bool BuiltinDr8Hat::load(const std::string& /*path*/, int /*plugin_index*/,
                         double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->reset();
  return true;
}

float BuiltinDr8Hat::normToDecayS(double norm) {
  // Exponential mapping: 0.02 seconds to 0.8 seconds
  return (float)(0.02 * std::pow(40.0, norm));
}

float BuiltinDr8Hat::normToHpfHz(double norm) {
  // Linear mapping: 3000 Hz to 12000 Hz
  return (float)(3000.0 + norm * 9000.0);
}

float BuiltinDr8Hat::normToBpfHz(double norm) {
  // Linear mapping: 6000 Hz to 15000 Hz
  return (float)(6000.0 + norm * 9000.0);
}

void BuiltinDr8Hat::process(float** /*inputs*/, float** outputs,
                            int num_samples, const HostProcessContext& context,
                            const std::vector<MidiNoteEvent>& events,
                            float** /*sidechain*/) {
  impl_->sample_rate = context.sampleRate;

  // Process MIDI events
  for (const auto& ev : events) {
    if (ev.isNoteOn && ev.velocity > 0) {
      impl_->active = true;
      std::fill(std::begin(impl_->osc_phase), std::end(impl_->osc_phase), 0.0);
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
  double hpf_fc = normToHpfHz(impl_->params[PARAM_HPF_FREQ]);
  double bpf_fc = normToBpfHz(impl_->params[PARAM_BPF_FREQ]);
  double tension = impl_->params[PARAM_TENSION];
  double volume = impl_->params[PARAM_VOLUME];

  // Base frequency scale: 150 Hz to 450 Hz
  double base_freq = 150.0 + tension * 300.0;

  // Envelope decay coefficient
  double amp_decay_coeff = std::exp(-1.0 / (decay_s * impl_->sample_rate));

  // Configure filter parameters (re-setup each block or sample; biquads are
  // fast)
  impl_->bpf.setParams(BiquadFilter::Type::BANDPASS, (float)bpf_fc, 1.5f, 0.0f,
                       (float)impl_->sample_rate);
  impl_->hpf.setParams(BiquadFilter::Type::HIGHPASS, (float)hpf_fc, 0.707f,
                       0.0f, (float)impl_->sample_rate);

  // Pre-calculate step increments
  double dt[6];
  for (int d = 0; d < 6; ++d) {
    dt[d] = (base_freq * kHatOscRatios[d]) / impl_->sample_rate;
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

    // Filter cascade: Bandpass -> Highpass
    float bpf_out = impl_->bpf.process((float)metallic);
    float hpf_out = impl_->hpf.process(bpf_out);

    float out_val =
        (float)(hpf_out * impl_->env_amp * impl_->velocity * volume);

    out_l[i] = out_val;
    out_r[i] = out_val;

    // Decay amplitude envelope
    impl_->env_amp *= amp_decay_coeff;
  }
}

int BuiltinDr8Hat::getParameterCount() const { return kTotalParams; }

bool BuiltinDr8Hat::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {"Decay", "HPF Freq", "BPF Freq", "Tension",
                                "Volume"};
  static const double defaults[] = {0.3, 0.5, 0.5, 0.4, 0.7};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinDr8Hat::setParameterValue(uint32_t id, double value) {
  if (id < kTotalParams) {
    impl_->params[id] = std::clamp(value, 0.0, 1.0);
  }
}

double BuiltinDr8Hat::getParameterValue(uint32_t id) const {
  if (id < kTotalParams) {
    return impl_->params[id];
  }
  return 0.0;
}

const std::string& BuiltinDr8Hat::getName() const { return kDr8HatName; }

const std::string& BuiltinDr8Hat::getPath() const { return kDr8HatPath; }

int BuiltinDr8Hat::getPluginIndex() const { return 0; }

bool BuiltinDr8Hat::isInstrument() const { return true; }

}  // namespace hibiki
