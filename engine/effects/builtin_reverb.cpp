#include "engine/effects/builtin_reverb.hpp"

namespace hibiki {

static const std::string kReverbName = "Reverb";
static const std::string kReverbPath = "builtin://reverb";

struct ReverbDspState;

ReverbDspState* d_reverb_create(double sample_rate);
void d_reverb_destroy(ReverbDspState* state);
void d_reverb_reset(ReverbDspState* state);
void d_reverb_init_buffers(ReverbDspState* state, double sample_rate);
void d_reverb_set_parameter(ReverbDspState* state, uint32_t id, double value);
double d_reverb_get_parameter(const ReverbDspState* state, uint32_t id);
float d_reverb_get_input_db(const ReverbDspState* state);
float d_reverb_get_output_db(const ReverbDspState* state);
void d_reverb_process(ReverbDspState* state, const float* in_l,
                      const float* in_r, float* out_l, float* out_r,
                      int num_samples);
float d_reverb_norm_to_predelay_ms(double norm);

struct BuiltinReverb::Impl {
  ReverbDspState* state = nullptr;
  double sample_rate = 44100.0;

  explicit Impl(double sr) : state(d_reverb_create(sr)), sample_rate(sr) {}
  ~Impl() { d_reverb_destroy(state); }
  void reset() { d_reverb_reset(state); }
};

BuiltinReverb::BuiltinReverb() : impl_(std::make_unique<Impl>(44100.0)) {
  impl_->reset();
}

BuiltinReverb::~BuiltinReverb() = default;

bool BuiltinReverb::load(const std::string& /*path*/, int /*plugin_index*/,
                         double sample_rate) {
  impl_->sample_rate = sample_rate;
  d_reverb_init_buffers(impl_->state, sample_rate);
  return true;
}

float BuiltinReverb::normToPreDelayMs(double norm) {
  return d_reverb_norm_to_predelay_ms(norm);
}

void BuiltinReverb::process(float** inputs, float** outputs, int num_samples,
                            const HostProcessContext& /*context*/,
                            const std::vector<MidiNoteEvent>& /*events*/,
                            float** /*sidechain*/) {
  const float* in_l = (inputs && inputs[0]) ? inputs[0] : outputs[0];
  const float* in_r = (inputs && inputs[1]) ? inputs[1] : outputs[1];
  d_reverb_process(impl_->state, in_l, in_r, outputs[0], outputs[1],
                   num_samples);
}

int BuiltinReverb::getParameterCount() const { return kTotalParams; }

bool BuiltinReverb::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {"Room Size", "Damping", "Mix",   "Pre-Delay",
                                "HP Freq",   "LP Freq", "Width", "Enable"};
  static const double defaults[] = {0.5, 0.5, 0.3, 0.1, 0.15, 0.85, 1.0, 1.0};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinReverb::setParameterValue(uint32_t id, double value) {
  d_reverb_set_parameter(impl_->state, id, value);
}

double BuiltinReverb::getParameterValue(uint32_t id) const {
  return d_reverb_get_parameter(impl_->state, id);
}

const std::string& BuiltinReverb::getName() const { return kReverbName; }
const std::string& BuiltinReverb::getPath() const { return kReverbPath; }
int BuiltinReverb::getPluginIndex() const { return 0; }
bool BuiltinReverb::isInstrument() const { return false; }

float BuiltinReverb::getInputDb() const {
  return d_reverb_get_input_db(impl_->state);
}

float BuiltinReverb::getOutputDb() const {
  return d_reverb_get_output_db(impl_->state);
}

}  // namespace hibiki
