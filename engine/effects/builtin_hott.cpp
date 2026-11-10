#include "engine/effects/builtin_hott.hpp"

namespace hibiki {

static const std::string kHottName = "Hott";
static const std::string kHottPath = "builtin://hott";

struct HottDspState;

HottDspState* d_hott_create();
void d_hott_destroy(HottDspState* state);
void d_hott_reset(HottDspState* state);
void d_hott_set_parameter(HottDspState* state, uint32_t id, double value);
double d_hott_get_parameter(const HottDspState* state, uint32_t id);
float d_hott_get_band_gain_reduction(const HottDspState* state, int band);
float d_hott_get_input_db(const HottDspState* state);
float d_hott_get_output_db(const HottDspState* state);
void d_hott_process(HottDspState* state, const float* in_l, const float* in_r,
                    float* out_l, float* out_r, int num_samples,
                    double sample_rate, long continuous_time_samples);
float d_hott_norm_to_freq(double norm, float min_hz, float max_hz);
float d_hott_norm_to_db24(double norm);

struct BuiltinHott::Impl {
  HottDspState* state = nullptr;
  double sample_rate = 44100.0;

  Impl() : state(d_hott_create()) {}
  ~Impl() { d_hott_destroy(state); }
  void reset() { d_hott_reset(state); }
};

BuiltinHott::BuiltinHott() : impl_(std::make_unique<Impl>()) { impl_->reset(); }

BuiltinHott::~BuiltinHott() = default;

bool BuiltinHott::load(const std::string& /*path*/, int /*plugin_index*/,
                       double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->reset();
  return true;
}

void BuiltinHott::process(float** inputs, float** outputs, int num_samples,
                          const HostProcessContext& context,
                          const std::vector<MidiNoteEvent>& /*events*/,
                          float** /*sidechain*/) {
  const float* in_l = (inputs && inputs[0]) ? inputs[0] : outputs[0];
  const float* in_r = (inputs && inputs[1]) ? inputs[1] : outputs[1];
  d_hott_process(impl_->state, in_l, in_r, outputs[0], outputs[1], num_samples,
                 context.sampleRate, context.continuousTimeSamples);
}

int BuiltinHott::getParameterCount() const { return kTotalParams; }

bool BuiltinHott::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  info.id = index;
  static const char* names[] = {
      /* PARAM_LOW_CROSSOVER   */ "LowXover",
      /* PARAM_HIGH_CROSSOVER  */ "HighXover",
      /* PARAM_AMOUNT          */ "Amount",
      /* PARAM_TIME            */ "Time",
      /* PARAM_OUTPUT          */ "Output",
      /* PARAM_LOW_OUT         */ "LowOut",
      /* PARAM_MID_OUT         */ "MidOut",
      /* PARAM_HIGH_OUT        */ "HighOut",
      /* PARAM_ENABLE          */ "Enable",
      /* PARAM_LOW_DOWN_THRESH */ "LowDownThresh",
      /* PARAM_MID_DOWN_THRESH */ "MidDownThresh",
      /* PARAM_HIGH_DOWN_THRESH*/ "HighDownThresh",
      /* PARAM_LOW_UP_THRESH   */ "LowUpThresh",
      /* PARAM_MID_UP_THRESH   */ "MidUpThresh",
      /* PARAM_HIGH_UP_THRESH  */ "HighUpThresh",
      /* PARAM_SOFT_KNEE       */ "SoftKnee",
      /* PARAM_RMS_MODE        */ "RmsMode",
      /* PARAM_LOW_ATTACK      */ "LowAttack",
      /* PARAM_MID_ATTACK      */ "MidAttack",
      /* PARAM_HIGH_ATTACK     */ "HighAttack",
      /* PARAM_LOW_RELEASE     */ "LowRelease",
      /* PARAM_MID_RELEASE     */ "MidRelease",
      /* PARAM_HIGH_RELEASE    */ "HighRelease",
      /* PARAM_LOW_DOWN_RATIO  */ "LowDownRatio",
      /* PARAM_MID_DOWN_RATIO  */ "MidDownRatio",
      /* PARAM_HIGH_DOWN_RATIO */ "HighDownRatio",
      /* PARAM_LOW_UP_RATIO    */ "LowUpRatio",
      /* PARAM_MID_UP_RATIO    */ "MidUpRatio",
      /* PARAM_HIGH_UP_RATIO   */ "HighUpRatio",
      /* PARAM_LOW_IN          */ "LowIn",
      /* PARAM_MID_IN          */ "MidIn",
      /* PARAM_HIGH_IN         */ "HighIn",
  };
  info.name = names[index];
  info.defaultValue = d_hott_get_parameter(impl_->state, index);
  return true;
}

void BuiltinHott::setParameterValue(uint32_t id, double value) {
  d_hott_set_parameter(impl_->state, id, value);
}

double BuiltinHott::getParameterValue(uint32_t id) const {
  return d_hott_get_parameter(impl_->state, id);
}

const std::string& BuiltinHott::getName() const { return kHottName; }
const std::string& BuiltinHott::getPath() const { return kHottPath; }
int BuiltinHott::getPluginIndex() const { return 0; }
bool BuiltinHott::isInstrument() const { return false; }

float BuiltinHott::getBandGainReduction(int band) const {
  return d_hott_get_band_gain_reduction(impl_->state, band);
}

float BuiltinHott::getInputDb() const {
  return d_hott_get_input_db(impl_->state);
}

float BuiltinHott::getOutputDb() const {
  return d_hott_get_output_db(impl_->state);
}

float BuiltinHott::normToFreq(double norm, float min_hz, float max_hz) {
  return d_hott_norm_to_freq(norm, min_hz, max_hz);
}

float BuiltinHott::normToDb24(double norm) { return d_hott_norm_to_db24(norm); }

}  // namespace hibiki
