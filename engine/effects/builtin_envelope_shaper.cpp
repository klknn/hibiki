#include "engine/effects/builtin_envelope_shaper.hpp"

#include <string>

namespace hibiki {

static const std::string kEnvShaperName = "EnvShaper";
static const std::string kEnvShaperPath = "builtin://envelope_shaper";

struct BuiltinEnvelopeShaper::Impl {
  EnvShaperDspState* state = nullptr;
  double sample_rate = 44100.0;

  Impl() : state(d_env_shaper_create()) {}
  ~Impl() { d_env_shaper_destroy(state); }
  void reset() { d_env_shaper_reset(state); }
};

BuiltinEnvelopeShaper::BuiltinEnvelopeShaper()
    : impl_(std::make_unique<Impl>()) {
  reset();
}

BuiltinEnvelopeShaper::~BuiltinEnvelopeShaper() = default;

bool BuiltinEnvelopeShaper::load(const std::string& /*path*/,
                                 int /*plugin_index*/, double sample_rate) {
  impl_->sample_rate = sample_rate;
  reset();
  return true;
}

void BuiltinEnvelopeShaper::reset() { impl_->reset(); }

float BuiltinEnvelopeShaper::normToRateBeats(double norm) {
  return d_env_shaper_norm_to_rate_beats(norm);
}

const char* BuiltinEnvelopeShaper::normToRateLabel(double norm) {
  return d_env_shaper_norm_to_rate_label(norm);
}

void BuiltinEnvelopeShaper::process(
    float** inputs, float** outputs, int num_samples,
    const HostProcessContext& context,
    const std::vector<MidiNoteEvent>& /*events*/, float** /*sidechain*/) {
  impl_->sample_rate = context.sampleRate;
  d_env_shaper_process(impl_->state, inputs[0], inputs[1], outputs[0],
                       outputs[1], num_samples, context.sampleRate,
                       context.tempo, context.projectTimeMusic);
}

int BuiltinEnvelopeShaper::getParameterCount() const { return kTotalParams; }

bool BuiltinEnvelopeShaper::getParameterInfo(int index,
                                             VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;

  info.id = index;
  switch (index) {
    case PARAM_MIX:
      info.name = "Mix";
      info.defaultValue = 1.0;
      break;
    case PARAM_RATE:
      info.name = "Rate";
      info.defaultValue = 0.43;
      break;
    case PARAM_SMOOTH:
      info.name = "Smooth";
      info.defaultValue = 0.3;
      break;
    case PARAM_MODE:
      info.name = "Mode";
      info.defaultValue = 0.0;
      break;
    case PARAM_ENABLE:
      info.name = "Enable";
      info.defaultValue = 1.0;
      break;
    case PARAM_NUM_POINTS:
      info.name = "Points";
      info.defaultValue = 0.25;
      break;
    default:
      if (index >= PARAM_POINT_Y_0 && index < PARAM_POINT_Y_0 + kMaxPoints) {
        int pt = index - PARAM_POINT_Y_0;
        info.name = "Pt " + std::to_string(pt);
        info.defaultValue = (pt < 6) ? getParameterValue(index) : 1.0;
      }
      break;
  }
  return true;
}

void BuiltinEnvelopeShaper::setParameterValue(uint32_t id, double value) {
  d_env_shaper_set_parameter(impl_->state, id, value);
}

double BuiltinEnvelopeShaper::getParameterValue(uint32_t id) const {
  return d_env_shaper_get_parameter(impl_->state, id);
}

const std::string& BuiltinEnvelopeShaper::getName() const {
  return kEnvShaperName;
}

const std::string& BuiltinEnvelopeShaper::getPath() const {
  return kEnvShaperPath;
}

int BuiltinEnvelopeShaper::getPluginIndex() const { return 0; }
bool BuiltinEnvelopeShaper::isInstrument() const { return false; }

float BuiltinEnvelopeShaper::getInputDb() const {
  return d_env_shaper_get_input_db(impl_->state);
}

float BuiltinEnvelopeShaper::getOutputDb() const {
  return d_env_shaper_get_output_db(impl_->state);
}

float BuiltinEnvelopeShaper::getEnvelopePhase() const {
  return d_env_shaper_get_current_phase(impl_->state);
}

}  // namespace hibiki
