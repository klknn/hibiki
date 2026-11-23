#include "engine/effects/builtin_chorus.hpp"

#include <algorithm>

namespace hibiki {

static const std::string kChorusName = "Chorus";
static const std::string kChorusPath = "builtin://chorus";

struct ChorusDspState {
  double lfo_phase = 0.0;
  float delay_buf_l[16384] = {};
  float delay_buf_r[16384] = {};
  int write_idx = 0;
  void reset();
};

extern float d_chorus_norm_to_rate_hz(double norm);
extern float d_chorus_norm_to_depth_ms(double norm);
extern float d_chorus_norm_to_delay_ms(double norm);
extern float d_chorus_norm_to_feedback(double norm);
extern void d_chorus_process(ChorusDspState& state, const float* in_l,
                             const float* in_r, float* out_l, float* out_r,
                             int num_samples, double sample_rate,
                             double rate_norm, double depth_norm,
                             double delay_norm, double feedback_norm,
                             double wet_dry_norm, bool enabled);

struct BuiltinChorus::Impl {
  double params[kTotalParams] = {};
  double sample_rate = 44100.0;
  ChorusDspState dsp_state;

  void reset() { dsp_state.reset(); }
};

BuiltinChorus::BuiltinChorus() : impl_(std::make_unique<Impl>()) {
  impl_->params[PARAM_RATE] = 0.2;
  impl_->params[PARAM_DEPTH] = 0.5;
  impl_->params[PARAM_DELAY] = 0.4;
  impl_->params[PARAM_FEEDBACK] = 0.2;
  impl_->params[PARAM_WET_DRY] = 0.5;
  impl_->params[PARAM_ENABLE] = 1.0;
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
  return d_chorus_norm_to_rate_hz(norm);
}

float BuiltinChorus::normToDepthMs(double norm) {
  return d_chorus_norm_to_depth_ms(norm);
}

float BuiltinChorus::normToDelayMs(double norm) {
  return d_chorus_norm_to_delay_ms(norm);
}

float BuiltinChorus::normToFeedback(double norm) {
  return d_chorus_norm_to_feedback(norm);
}

void BuiltinChorus::process(float** inputs, float** outputs, int num_samples,
                            const HostProcessContext& /*context*/,
                            const std::vector<MidiNoteEvent>& /*events*/,
                            float** /*sidechain*/) {
  bool enabled = impl_->params[PARAM_ENABLE] >= 0.5;
  d_chorus_process(impl_->dsp_state, inputs[0], inputs[1], outputs[0],
                   outputs[1], num_samples, impl_->sample_rate,
                   impl_->params[PARAM_RATE], impl_->params[PARAM_DEPTH],
                   impl_->params[PARAM_DELAY], impl_->params[PARAM_FEEDBACK],
                   impl_->params[PARAM_WET_DRY], enabled);
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
