#include "engine/effects/builtin_aux.hpp"

namespace hibiki {

static const std::string kAuxName = "Aux";
static const std::string kAuxPath = "builtin://aux";

struct AuxDspState;

AuxDspState* d_aux_create();
void d_aux_destroy(AuxDspState* state);
void d_aux_reset(AuxDspState* state);
void d_aux_set_parameter(AuxDspState* state, uint32_t id, double value);
double d_aux_get_parameter(const AuxDspState* state, uint32_t id);
void d_aux_process(AuxDspState* state, const float* in_l, const float* in_r,
                   float* out_l, float* out_r, int num_samples);

struct BuiltinAux::Impl {
  AuxDspState* state = nullptr;
  double sample_rate = 44100.0;

  Impl() : state(d_aux_create()) {}
  ~Impl() { d_aux_destroy(state); }
  void reset() { d_aux_reset(state); }
};

BuiltinAux::BuiltinAux() : impl_(std::make_unique<Impl>()) { impl_->reset(); }

BuiltinAux::~BuiltinAux() = default;

bool BuiltinAux::load(const std::string& /*path*/, int /*plugin_index*/,
                      double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->reset();
  return true;
}

void BuiltinAux::process(float** inputs, float** outputs, int num_samples,
                         const HostProcessContext& /*context*/,
                         const std::vector<MidiNoteEvent>& /*events*/,
                         float** /*sidechain*/) {
  const float* in_l = (inputs && inputs[0]) ? inputs[0] : outputs[0];
  const float* in_r = (inputs && inputs[1]) ? inputs[1] : outputs[1];
  d_aux_process(impl_->state, in_l, in_r, outputs[0], outputs[1], num_samples);
}

// --- IPlugin interface ---

int BuiltinAux::getParameterCount() const { return kTotalParams; }

bool BuiltinAux::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {"Gain", "Pan"};
  static const double defaults[] = {0.7, 0.5};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinAux::setParameterValue(uint32_t id, double value) {
  d_aux_set_parameter(impl_->state, id, value);
}

double BuiltinAux::getParameterValue(uint32_t id) const {
  return d_aux_get_parameter(impl_->state, id);
}

const std::string& BuiltinAux::getName() const { return kAuxName; }
const std::string& BuiltinAux::getPath() const { return kAuxPath; }
int BuiltinAux::getPluginIndex() const { return 0; }
bool BuiltinAux::isInstrument() const { return false; }

}  // namespace hibiki
