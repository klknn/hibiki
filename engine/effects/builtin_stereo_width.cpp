#include "engine/effects/builtin_stereo_width.hpp"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace hibiki {

static const std::string kStereoWidthName = "Stereo Width";
static const std::string kStereoWidthPath = "builtin://stereo_width";

struct BuiltinStereoWidth::Impl {
  double params[kTotalParams] = {};
  double sample_rate = 44100.0;

  // Filter state for crossover (Lowpass)
  double lp_state_l = 0.0;
  double lp_state_r = 0.0;

  // Delay lines (circular buffers for high-pass signal)
  std::vector<float> delay_buf_l;
  std::vector<float> delay_buf_r;
  int write_idx = 0;

  void reset() {
    lp_state_l = 0.0;
    lp_state_r = 0.0;
    std::fill(delay_buf_l.begin(), delay_buf_l.end(), 0.0f);
    std::fill(delay_buf_r.begin(), delay_buf_r.end(), 0.0f);
    write_idx = 0;
  }
};

BuiltinStereoWidth::BuiltinStereoWidth() : impl_(std::make_unique<Impl>()) {
  impl_->params[PARAM_DELAY] = 0.25;          // 10.0 ms default
  impl_->params[PARAM_CHANNEL] = 0.0;         // Delay Left channel by default
  impl_->params[PARAM_MONO_FREQ] = 0.222222;  // ~150 Hz crossover default
  impl_->params[PARAM_WIDTH] = 0.5;           // 1.0x width default
  impl_->params[PARAM_ENABLE] = 1.0;          // enabled

  impl_->delay_buf_l.resize(16384, 0.0f);
  impl_->delay_buf_r.resize(16384, 0.0f);
  impl_->reset();
}

BuiltinStereoWidth::~BuiltinStereoWidth() = default;

bool BuiltinStereoWidth::load(const std::string& /*path*/, int /*plugin_index*/,
                              double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->reset();
  return true;
}

float BuiltinStereoWidth::normToDelayMs(double norm) {
  // Delay range: 0.0 to 40.0 ms
  return (float)(norm * 40.0);
}

float BuiltinStereoWidth::normToMonoFreq(double norm) {
  // Crossover frequency: 50.0 to 500.0 Hz
  return (float)(50.0 + norm * 450.0);
}

float BuiltinStereoWidth::normToWidth(double norm) {
  // Width: 0.0 (mono) to 2.0 (extra wide)
  return (float)(norm * 2.0);
}

void BuiltinStereoWidth::process(float** inputs, float** outputs,
                                 int num_samples,
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

  float delay_ms = normToDelayMs(impl_->params[PARAM_DELAY]);
  bool delay_left = impl_->params[PARAM_CHANNEL] < 0.5;
  float crossover_fc = normToMonoFreq(impl_->params[PARAM_MONO_FREQ]);
  float width = normToWidth(impl_->params[PARAM_WIDTH]);

  // Crossover filter coefficient (one-pole lowpass)
  double theta = 2.0 * M_PI * crossover_fc / impl_->sample_rate;
  double alpha = 1.0 - std::exp(-theta);
  alpha = std::clamp(alpha, 0.0, 1.0);

  int buf_size = (int)impl_->delay_buf_l.size();
  double delay_samples = delay_ms * 0.001 * impl_->sample_rate;

  for (int i = 0; i < num_samples; ++i) {
    float in_l = inputs[0][i];
    float in_r = inputs[1][i];

    // Crossover filter: Low-pass band
    impl_->lp_state_l += alpha * (in_l - impl_->lp_state_l);
    impl_->lp_state_r += alpha * (in_r - impl_->lp_state_r);

    float low_l = (float)impl_->lp_state_l;
    float low_r = (float)impl_->lp_state_r;

    // Keep low frequencies centered in mono
    float low_mono = 0.5f * (low_l + low_r);

    // High-pass band (subtraction)
    float high_l = in_l - low_l;
    float high_r = in_r - low_r;

    // Write high band into circular delay buffers
    impl_->delay_buf_l[impl_->write_idx] = high_l;
    impl_->delay_buf_r[impl_->write_idx] = high_r;

    float high_l_proc = high_l;
    float high_r_proc = high_r;

    // Apply Haas delay with linear interpolation to high-pass band
    if (delay_left) {
      double read_pos_l = (double)impl_->write_idx - delay_samples;
      if (read_pos_l < 0.0) read_pos_l += buf_size;
      int idx_l1 = (int)std::floor(read_pos_l) % buf_size;
      int idx_l2 = (idx_l1 + 1) % buf_size;
      float frac_l = (float)(read_pos_l - std::floor(read_pos_l));
      high_l_proc = (1.0f - frac_l) * impl_->delay_buf_l[idx_l1] +
                    frac_l * impl_->delay_buf_l[idx_l2];
    } else {
      double read_pos_r = (double)impl_->write_idx - delay_samples;
      if (read_pos_r < 0.0) read_pos_r += buf_size;
      int idx_r1 = (int)std::floor(read_pos_r) % buf_size;
      int idx_r2 = (idx_r1 + 1) % buf_size;
      float frac_r = (float)(read_pos_r - std::floor(read_pos_r));
      high_r_proc = (1.0f - frac_r) * impl_->delay_buf_r[idx_r1] +
                    frac_r * impl_->delay_buf_r[idx_r2];
    }

    // Advance write pointer
    impl_->write_idx = (impl_->write_idx + 1) % buf_size;

    // Mid/Side width processing on high-pass band
    float mid = 0.5f * (high_l_proc + high_r_proc);
    float side = 0.5f * (high_l_proc - high_r_proc) * width;

    float high_l_out = mid + side;
    float high_r_out = mid - side;

    // Sum back low band (mono) and high band (widened)
    outputs[0][i] = low_mono + high_l_out;
    outputs[1][i] = low_mono + high_r_out;
  }
}

int BuiltinStereoWidth::getParameterCount() const { return kTotalParams; }

bool BuiltinStereoWidth::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {"Delay", "Channel", "Mono Crossover", "Width",
                                "Enable"};
  static const double defaults[] = {0.25, 0.0, 0.222222, 0.5, 1.0};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinStereoWidth::setParameterValue(uint32_t id, double value) {
  if (id < kTotalParams) {
    impl_->params[id] = std::clamp(value, 0.0, 1.0);
  }
}

double BuiltinStereoWidth::getParameterValue(uint32_t id) const {
  if (id < kTotalParams) {
    return impl_->params[id];
  }
  return 0.0;
}

const std::string& BuiltinStereoWidth::getName() const {
  return kStereoWidthName;
}

const std::string& BuiltinStereoWidth::getPath() const {
  return kStereoWidthPath;
}

int BuiltinStereoWidth::getPluginIndex() const { return 0; }

bool BuiltinStereoWidth::isInstrument() const { return false; }

}  // namespace hibiki
