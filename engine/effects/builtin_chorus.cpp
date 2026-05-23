#include "engine/effects/builtin_chorus.hpp"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace hibiki {

static const std::string kChorusName = "Chorus";
static const std::string kChorusPath = "builtin://chorus";

struct BuiltinChorus::Impl {
  double params[kTotalParams] = {};
  double sample_rate = 44100.0;

  // LFO state
  double lfo_phase = 0.0;

  // Delay lines (circular buffers)
  std::vector<float> delay_buf_l;
  std::vector<float> delay_buf_r;
  int write_idx = 0;

  void reset() {
    lfo_phase = 0.0;
    std::fill(delay_buf_l.begin(), delay_buf_l.end(), 0.0f);
    std::fill(delay_buf_r.begin(), delay_buf_r.end(), 0.0f);
    write_idx = 0;
  }
};

BuiltinChorus::BuiltinChorus() : impl_(std::make_unique<Impl>()) {
  impl_->params[PARAM_RATE] =
      0.2;  // 0.2 maps to ~1.0 Hz: (1.0 - 0.1) / 9.9 = 0.9 / 9.9 ~= 0.09
  impl_->params[PARAM_DEPTH] = 0.5;     // 50% depth
  impl_->params[PARAM_DELAY] = 0.4;     // ~15 ms
  impl_->params[PARAM_FEEDBACK] = 0.2;  // 20% feedback
  impl_->params[PARAM_WET_DRY] = 0.5;   // 50% mix (classic chorus)
  impl_->params[PARAM_ENABLE] = 1.0;    // enabled

  impl_->delay_buf_l.resize(16384, 0.0f);
  impl_->delay_buf_r.resize(16384, 0.0f);
  impl_->reset();
}

BuiltinChorus::~BuiltinChorus() = default;

bool BuiltinChorus::load(const std::string& /*path*/, int /*plugin_index*/,
                         double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->reset();
  return true;
}

float BuiltinChorus::normToRateHz(double norm) {
  // Linear mapping: 0.1 Hz to 10.0 Hz
  return (float)(0.1 + norm * 9.9);
}

float BuiltinChorus::normToDepthMs(double norm) {
  // Max modulation depth: 0.0 ms to 5.0 ms
  return (float)(norm * 5.0);
}

float BuiltinChorus::normToDelayMs(double norm) {
  // Base delay: 5.0 ms to 30.0 ms
  return (float)(5.0 + norm * 25.0);
}

float BuiltinChorus::normToFeedback(double norm) {
  // Feedback mapping: 0.0 to 0.95
  return (float)(norm * 0.95);
}

void BuiltinChorus::process(float** inputs, float** outputs, int num_samples,
                            const HostProcessContext& /*context*/,
                            const std::vector<MidiNoteEvent>& /*events*/,
                            float** /*sidechain*/) {
  bool enabled = impl_->params[PARAM_ENABLE] >= 0.5;
  if (!enabled) {
    for (int i = 0; i < num_samples; ++i) {
      outputs[0][i] = inputs[0][i];
      outputs[1][i] = inputs[1][i];
    }
    return;
  }

  float rate_hz = normToRateHz(impl_->params[PARAM_RATE]);
  float depth_ms = normToDepthMs(impl_->params[PARAM_DEPTH]);
  float delay_ms = normToDelayMs(impl_->params[PARAM_DELAY]);
  float feedback = normToFeedback(impl_->params[PARAM_FEEDBACK]);
  float wet = (float)impl_->params[PARAM_WET_DRY];
  float dry = 1.0f - wet;

  float sample_rate_f = (float)impl_->sample_rate;
  float base_delay_samples = delay_ms * 0.001f * sample_rate_f;
  float depth_samples = depth_ms * 0.001f * sample_rate_f;

  double phase_inc = 2.0 * M_PI * rate_hz / impl_->sample_rate;
  int buf_size = (int)impl_->delay_buf_l.size();

  for (int i = 0; i < num_samples; ++i) {
    float in_l = inputs[0][i];
    float in_r = inputs[1][i];

    // Increment and wrap LFO phase
    impl_->lfo_phase += phase_inc;
    if (impl_->lfo_phase >= 2.0 * M_PI) {
      impl_->lfo_phase -= 2.0 * M_PI;
    }

    // LFO outputs with 90-degree phase offset for stereo width
    float lfo_l = (float)std::sin(impl_->lfo_phase);
    float lfo_r =
        (float)std::cos(impl_->lfo_phase);  // sin(phase + pi/2) = cos(phase)

    // Compute modulated delay times
    float delay_samples_l = base_delay_samples + depth_samples * lfo_l;
    float delay_samples_r = base_delay_samples + depth_samples * lfo_r;

    // Linear interpolation for Left channel read pointer
    double read_pos_l = (double)impl_->write_idx - delay_samples_l;
    if (read_pos_l < 0.0) read_pos_l += buf_size;
    int idx_l1 = (int)std::floor(read_pos_l) % buf_size;
    int idx_l2 = (idx_l1 + 1) % buf_size;
    float frac_l = (float)(read_pos_l - std::floor(read_pos_l));
    float delayed_l = (1.0f - frac_l) * impl_->delay_buf_l[idx_l1] +
                      frac_l * impl_->delay_buf_l[idx_l2];

    // Linear interpolation for Right channel read pointer
    double read_pos_r = (double)impl_->write_idx - delay_samples_r;
    if (read_pos_r < 0.0) read_pos_r += buf_size;
    int idx_r1 = (int)std::floor(read_pos_r) % buf_size;
    int idx_r2 = (idx_r1 + 1) % buf_size;
    float frac_r = (float)(read_pos_r - std::floor(read_pos_r));
    float delayed_r = (1.0f - frac_r) * impl_->delay_buf_r[idx_r1] +
                      frac_r * impl_->delay_buf_r[idx_r2];

    // Write to circular delay lines
    impl_->delay_buf_l[impl_->write_idx] = in_l + delayed_l * feedback;
    impl_->delay_buf_r[impl_->write_idx] = in_r + delayed_r * feedback;
    impl_->write_idx = (impl_->write_idx + 1) % buf_size;

    // Dry/Wet Mix
    outputs[0][i] = delayed_l * wet + in_l * dry;
    outputs[1][i] = delayed_r * wet + in_r * dry;
  }
}

int BuiltinChorus::getParameterCount() const { return kTotalParams; }

bool BuiltinChorus::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {"Rate",     "Depth",   "Delay",
                                "Feedback", "Wet/Dry", "Enable"};
  static const double defaults[] = {0.090909, 0.5, 0.4, 0.2, 0.5, 1.0};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinChorus::setParameterValue(uint32_t id, double value) {
  if (id < kTotalParams) {
    impl_->params[id] = std::clamp(value, 0.0, 1.0);
  }
}

double BuiltinChorus::getParameterValue(uint32_t id) const {
  if (id < kTotalParams) {
    return impl_->params[id];
  }
  return 0.0;
}

const std::string& BuiltinChorus::getName() const { return kChorusName; }

const std::string& BuiltinChorus::getPath() const { return kChorusPath; }

int BuiltinChorus::getPluginIndex() const { return 0; }

bool BuiltinChorus::isInstrument() const { return false; }

}  // namespace hibiki
