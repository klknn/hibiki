#include "engine/effects/builtin_phaser.hpp"

#include <string>

namespace hibiki {

static const std::string kPhaserName = "Phaser";
static const std::string kPhaserPath = "builtin://phaser";

struct BuiltinPhaser::Impl {
  PhaserDspState* state = nullptr;
  double sample_rate = 44100.0;

  Impl() : state(d_phaser_create()) {}
  ~Impl() { d_phaser_destroy(state); }
  void reset() { d_phaser_reset(state); }
};

BuiltinPhaser::BuiltinPhaser() : impl_(std::make_unique<Impl>()) { reset(); }

BuiltinPhaser::~BuiltinPhaser() = default;

bool BuiltinPhaser::load(const std::string& /*path*/, int /*plugin_index*/,
                         double sample_rate) {
  impl_->sample_rate = sample_rate;
  reset();
  return true;
}

void BuiltinPhaser::reset() { impl_->reset(); }

float BuiltinPhaser::normToRate(double norm) {
  return d_phaser_norm_to_rate(norm);
}

int BuiltinPhaser::normToStages(double norm) {
  return d_phaser_norm_to_stages(norm);
}

int BuiltinPhaser::normToMode(double norm) {
  return d_phaser_norm_to_mode(norm);
}

void BuiltinPhaser::process(float** inputs, float** outputs, int num_samples,
                            const HostProcessContext& context,
                            const std::vector<MidiNoteEvent>& /*events*/,
                            float** /*sidechain*/) {
  impl_->sample_rate = context.sampleRate;
  d_phaser_process(impl_->state, inputs[0], inputs[1], outputs[0], outputs[1],
                   num_samples, context.sampleRate, context.tempo);
}

int BuiltinPhaser::getParameterCount() const { return kTotalParams; }

bool BuiltinPhaser::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;

  info.id = index;
  switch (index) {
    case PARAM_RATE:
      info.name = "Rate";
      info.defaultValue = 0.3;
      break;
    case PARAM_DEPTH:
      info.name = "Depth";
      info.defaultValue = 0.7;
      break;
    case PARAM_FEEDBACK:
      info.name = "Feedback";
      info.defaultValue = 0.4;
      break;
    case PARAM_STAGES:
      info.name = "Poles";
      info.defaultValue = 0.33;
      break;
    case PARAM_MIX:
      info.name = "Mix";
      info.defaultValue = 0.5;
      break;
    case PARAM_STEREO:
      info.name = "Stereo";
      info.defaultValue = 0.5;
      break;
    case PARAM_MODE:
      info.name = "Mode";
      info.defaultValue = 0.0;
      break;
    case PARAM_LFO_SHAPE:
      info.name = "Shape";
      info.defaultValue = 0.0;
      break;
    case PARAM_CENTER:
      info.name = "Center";
      info.defaultValue = 0.5;
      break;
    case PARAM_SPREAD:
      info.name = "Spread";
      info.defaultValue = 0.5;
      break;
    case PARAM_SYNC:
      info.name = "Sync";
      info.defaultValue = 0.0;
      break;
    case PARAM_ENABLE:
      info.name = "Enable";
      info.defaultValue = 1.0;
      break;
    default:
      return false;
  }
  return true;
}

void BuiltinPhaser::setParameterValue(uint32_t id, double value) {
  d_phaser_set_parameter(impl_->state, id, value);
}

double BuiltinPhaser::getParameterValue(uint32_t id) const {
  return d_phaser_get_parameter(impl_->state, id);
}

const std::string& BuiltinPhaser::getName() const { return kPhaserName; }

const std::string& BuiltinPhaser::getPath() const { return kPhaserPath; }

int BuiltinPhaser::getPluginIndex() const { return 0; }
bool BuiltinPhaser::isInstrument() const { return false; }

float BuiltinPhaser::getInputDb() const {
  return d_phaser_get_input_db(impl_->state);
}

float BuiltinPhaser::getOutputDb() const {
  return d_phaser_get_output_db(impl_->state);
}

void BuiltinPhaser::getScopeData(float* left, float* right, int size) const {
  d_phaser_get_scope_data(impl_->state, left, right, size);
}

}  // namespace hibiki
