#include "engine/effects/builtin_stereo_width.hpp"

#include <algorithm>

namespace hibiki {

static const std::string kStereoWidthName = "Stereo Width";
static const std::string kStereoWidthPath = "builtin://stereo_width";

struct StereoWidthDspState {
  double lp_state_l = 0.0;
  double lp_state_r = 0.0;
  float delay_buf_l[16384] = {};
  float delay_buf_r[16384] = {};
  int write_idx = 0;
  void reset();
};

extern float d_stereo_width_norm_to_delay_ms(double norm);
extern float d_stereo_width_norm_to_mono_freq(double norm);
extern float d_stereo_width_norm_to_width(double norm);
extern void d_stereo_width_process(StereoWidthDspState& state,
                                   const float* in_l, const float* in_r,
                                   float* out_l, float* out_r, int num_samples,
                                   double sample_rate, double delay_norm,
                                   double channel_norm, double mono_freq_norm,
                                   double width_norm, bool enabled);

struct BuiltinStereoWidth::Impl {
  double params[kTotalParams] = {};
  double sample_rate = 44100.0;
  StereoWidthDspState dsp_state;

  void reset() { dsp_state.reset(); }
};

BuiltinStereoWidth::BuiltinStereoWidth() : impl_(std::make_unique<Impl>()) {
  impl_->params[PARAM_DELAY] = 0.25;          // 10.0 ms default
  impl_->params[PARAM_CHANNEL] = 0.0;         // Delay Left channel by default
  impl_->params[PARAM_MONO_FREQ] = 0.222222;  // ~150 Hz crossover default
  impl_->params[PARAM_WIDTH] = 0.5;           // 1.0x width default
  impl_->params[PARAM_ENABLE] = 1.0;          // enabled
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
  return d_stereo_width_norm_to_delay_ms(norm);
}

float BuiltinStereoWidth::normToMonoFreq(double norm) {
  return d_stereo_width_norm_to_mono_freq(norm);
}

float BuiltinStereoWidth::normToWidth(double norm) {
  return d_stereo_width_norm_to_width(norm);
}

void BuiltinStereoWidth::process(float** inputs, float** outputs,
                                 int num_samples,
                                 const HostProcessContext& /*context*/,
                                 const std::vector<MidiNoteEvent>& /*events*/,
                                 float** /*sidechain*/) {
  bool enabled = impl_->params[PARAM_ENABLE] >= 0.5;
  d_stereo_width_process(
      impl_->dsp_state, inputs[0], inputs[1], outputs[0], outputs[1],
      num_samples, impl_->sample_rate, impl_->params[PARAM_DELAY],
      impl_->params[PARAM_CHANNEL], impl_->params[PARAM_MONO_FREQ],
      impl_->params[PARAM_WIDTH], enabled);
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
