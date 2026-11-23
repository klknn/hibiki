#include "engine/effects/builtin_limiter.hpp"

namespace hibiki {

static const std::string kLimiterName = "Limiter";
static const std::string kLimiterPath = "builtin://limiter";

BuiltinLimiter::BuiltinLimiter() {
  params_[PARAM_CEILING] = 0.975;  // -0.3 dB
  params_[PARAM_RELEASE] = 0.3;    // ~100ms
  params_[PARAM_LOOKAHEAD] = 0.2;  // ~1ms
  params_[PARAM_GAIN] = 0.0;       // 0 dB
  params_[PARAM_LINK_STEREO] = 1.0;
  params_[PARAM_ENABLE] = 1.0;
  dsp_state_.reset();
}

bool BuiltinLimiter::load(const std::string& /*path*/, int /*plugin_index*/,
                          double sample_rate) {
  sample_rate_ = sample_rate;
  reset();
  return true;
}

void BuiltinLimiter::reset() { dsp_state_.reset(); }

// --- Parameter mapping ---

float BuiltinLimiter::normToCeilingDb(double norm) {
  return d_limiter_norm_to_ceiling_db(norm);
}

float BuiltinLimiter::normToReleaseMs(double norm) {
  return d_limiter_norm_to_release_ms(norm);
}

float BuiltinLimiter::normToLookaheadMs(double norm) {
  return d_limiter_norm_to_lookahead_ms(norm);
}

float BuiltinLimiter::normToGainDb(double norm) {
  return d_limiter_norm_to_gain_db(norm);
}

void BuiltinLimiter::process(float** inputs, float** outputs, int num_samples,
                             const HostProcessContext& /*context*/,
                             const std::vector<MidiNoteEvent>& /*events*/,
                             float** /*sidechain*/) {
  d_limiter_process(dsp_state_, inputs[0], inputs[1], outputs[0], outputs[1],
                    num_samples, sample_rate_, params_[PARAM_CEILING],
                    params_[PARAM_RELEASE], params_[PARAM_LOOKAHEAD],
                    params_[PARAM_GAIN], params_[PARAM_LINK_STEREO] >= 0.5,
                    enabled_);
}

float BuiltinLimiter::getGainReductionDb() const {
  return dsp_state_.gain_reduction_db;
}

// --- IPlugin interface ---

int BuiltinLimiter::getParameterCount() const { return kTotalParams; }

bool BuiltinLimiter::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {"Ceiling", "Release",     "Lookahead",
                                "Gain",    "Link Stereo", "Enable"};
  static const double defaults[] = {0.975, 0.3, 0.2, 0.0, 1.0, 1.0};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinLimiter::setParameterValue(uint32_t id, double value) {
  if (id < kTotalParams) {
    params_[id] = value;
    if (id == PARAM_ENABLE) enabled_ = value >= 0.5;
  }
}

double BuiltinLimiter::getParameterValue(uint32_t id) const {
  return id < kTotalParams ? params_[id] : 0.0;
}

const std::string& BuiltinLimiter::getName() const { return kLimiterName; }
const std::string& BuiltinLimiter::getPath() const { return kLimiterPath; }
int BuiltinLimiter::getPluginIndex() const { return 0; }
bool BuiltinLimiter::isInstrument() const { return false; }

}  // namespace hibiki
