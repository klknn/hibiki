#include "engine/effects/builtin_maxim.hpp"

namespace hibiki {

static const std::string kMaximName = "Maxim";
static const std::string kMaximPath = "builtin://maxim";

struct MaximDspState;

MaximDspState* d_maxim_create(double sample_rate);
void d_maxim_destroy(MaximDspState* state);
void d_maxim_reset(MaximDspState* state);
void d_maxim_set_parameter(MaximDspState* state, uint32_t id, double value);
double d_maxim_get_parameter(const MaximDspState* state, uint32_t id);
float d_maxim_get_band_gain_reduction_db(const MaximDspState* state, int band);
float d_maxim_get_band_input_db(const MaximDspState* state, int band);
float d_maxim_get_band_output_db(const MaximDspState* state, int band);
void d_maxim_process(MaximDspState* state, const float* in_l, const float* in_r,
                     float* out_l, float* out_r, int num_samples,
                     double sample_rate, long continuous_time_samples);

float d_maxim_norm_to_freq(double norm, float min_hz, float max_hz);
float d_maxim_norm_to_gain_db(double norm);
float d_maxim_norm_to_sat_thresh_db(double norm);
float d_maxim_norm_to_thresh_db(double norm);
float d_maxim_norm_to_ratio(double norm);
float d_maxim_norm_to_knee_db(double norm);
float d_maxim_norm_to_attack_ms(double norm);
float d_maxim_norm_to_release_ms(double norm);
float d_maxim_norm_to_ceiling_db(double norm);
float d_maxim_norm_to_lookahead_ms(double norm);

struct BuiltinMaxim::Impl {
  MaximDspState* state = nullptr;
  double sample_rate = 44100.0;

  explicit Impl(double sr) : state(d_maxim_create(sr)), sample_rate(sr) {}
  ~Impl() { d_maxim_destroy(state); }
  void reset() { d_maxim_reset(state); }
};

BuiltinMaxim::BuiltinMaxim() : impl_(std::make_unique<Impl>(44100.0)) {
  impl_->reset();
}

BuiltinMaxim::~BuiltinMaxim() = default;

bool BuiltinMaxim::load(const std::string& /*path*/, int /*plugin_index*/,
                        double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->reset();
  return true;
}

void BuiltinMaxim::process(float** inputs, float** outputs, int num_samples,
                           const HostProcessContext& context,
                           const std::vector<MidiNoteEvent>& /*events*/,
                           float** /*sidechain*/) {
  const float* in_l = (inputs && inputs[0]) ? inputs[0] : outputs[0];
  const float* in_r = (inputs && inputs[1]) ? inputs[1] : outputs[1];
  d_maxim_process(impl_->state, in_l, in_r, outputs[0], outputs[1], num_samples,
                  context.sampleRate, context.continuousTimeSamples);
}

int BuiltinMaxim::getParameterCount() const { return kTotalParams; }

bool BuiltinMaxim::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  info.id = index;

  if (index == PARAM_LOW_XOVER) {
    info.name = "LowXover";
    info.defaultValue = 0.461;
  } else if (index == PARAM_HIGH_XOVER) {
    info.name = "HighXover";
    info.defaultValue = 0.436;
  } else if (index == PARAM_LOOKAHEAD) {
    info.name = "Lookahead";
    info.defaultValue = 0.2;
  } else if (index == PARAM_ENABLE) {
    info.name = "Enable";
    info.defaultValue = 1.0;
  } else {
    int b = (index - 4) / 10;
    int p = (index - 4) % 10;
    std::string prefix;
    if (b == 0)
      prefix = "Low";
    else if (b == 1)
      prefix = "Mid";
    else if (b == 2)
      prefix = "High";
    else
      prefix = "Master";

    static const char* param_suffixes[] = {
        "PreGain", "PostGain", "SatAmount", "SatThresh", "Thresh",
        "Ratio",   "Knee",     "Attack",    "Release",   "Ceiling"};
    static const double defaults[] = {0.5, 0.5, 0.0, 1.0, 1.0,
                                      0.0, 0.0, 0.3, 0.3, 1.0};

    info.name = prefix + param_suffixes[p];
    info.defaultValue = (b == 3 && p == 9) ? 0.975 : defaults[p];
  }
  return true;
}

void BuiltinMaxim::setParameterValue(uint32_t id, double value) {
  d_maxim_set_parameter(impl_->state, id, value);
}

double BuiltinMaxim::getParameterValue(uint32_t id) const {
  return d_maxim_get_parameter(impl_->state, id);
}

const std::string& BuiltinMaxim::getName() const { return kMaximName; }
const std::string& BuiltinMaxim::getPath() const { return kMaximPath; }
int BuiltinMaxim::getPluginIndex() const { return 0; }
bool BuiltinMaxim::isInstrument() const { return false; }

float BuiltinMaxim::normToFreq(double norm, float min_hz, float max_hz) {
  return d_maxim_norm_to_freq(norm, min_hz, max_hz);
}

float BuiltinMaxim::normToGainDb(double norm) {
  return d_maxim_norm_to_gain_db(norm);
}

float BuiltinMaxim::normToSatThreshDb(double norm) {
  return d_maxim_norm_to_sat_thresh_db(norm);
}

float BuiltinMaxim::normToThreshDb(double norm) {
  return d_maxim_norm_to_thresh_db(norm);
}

float BuiltinMaxim::normToRatio(double norm) {
  return d_maxim_norm_to_ratio(norm);
}

float BuiltinMaxim::normToKneeDb(double norm) {
  return d_maxim_norm_to_knee_db(norm);
}

float BuiltinMaxim::normToAttackMs(double norm) {
  return d_maxim_norm_to_attack_ms(norm);
}

float BuiltinMaxim::normToReleaseMs(double norm) {
  return d_maxim_norm_to_release_ms(norm);
}

float BuiltinMaxim::normToCeilingDb(double norm) {
  return d_maxim_norm_to_ceiling_db(norm);
}

float BuiltinMaxim::normToLookaheadMs(double norm) {
  return d_maxim_norm_to_lookahead_ms(norm);
}

float BuiltinMaxim::getBandGainReductionDb(int band) const {
  return d_maxim_get_band_gain_reduction_db(impl_->state, band);
}

float BuiltinMaxim::getBandInputDb(int band) const {
  return d_maxim_get_band_input_db(impl_->state, band);
}

float BuiltinMaxim::getBandOutputDb(int band) const {
  return d_maxim_get_band_output_db(impl_->state, band);
}

}  // namespace hibiki
