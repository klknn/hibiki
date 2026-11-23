#include "engine/effects/builtin_vocodey.hpp"

#include <vector>

namespace hibiki {

static const std::string kVocodeyName = "Vocodey";
static const std::string kVocodeyPath = "builtin://vocodey";

struct VocodeyDspState;

struct VocodeyMidiEvent {
  int pitch;
  float velocity;
  bool is_note_on;
};

VocodeyDspState* d_vocodey_create();
void d_vocodey_destroy(VocodeyDspState* state);
void d_vocodey_reset(VocodeyDspState* state);
void d_vocodey_set_parameter(VocodeyDspState* state, uint32_t id, double value);
double d_vocodey_get_parameter(const VocodeyDspState* state, uint32_t id);
void d_vocodey_process(VocodeyDspState* state, const float* in_l,
                       const float* in_r, float* out_l, float* out_r,
                       int num_samples, double sample_rate,
                       const VocodeyMidiEvent* events, int num_events,
                       const float* sc_l, const float* sc_r);

float d_vocodey_norm_to_attack_s(double norm);
float d_vocodey_norm_to_decay_s(double norm);
float d_vocodey_norm_to_bandwidth_q(double norm);

struct BuiltinVocodey::Impl {
  VocodeyDspState* state = nullptr;
  double sample_rate = 44100.0;

  Impl() : state(d_vocodey_create()) {}
  ~Impl() { d_vocodey_destroy(state); }
  void reset() { d_vocodey_reset(state); }
};

BuiltinVocodey::BuiltinVocodey() : impl_(std::make_unique<Impl>()) {
  impl_->reset();
}

BuiltinVocodey::~BuiltinVocodey() = default;

bool BuiltinVocodey::load(const std::string& /*path*/, int /*plugin_index*/,
                          double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->reset();
  return true;
}

float BuiltinVocodey::normToAttackS(double norm) {
  return d_vocodey_norm_to_attack_s(norm);
}

float BuiltinVocodey::normToDecayS(double norm) {
  return d_vocodey_norm_to_decay_s(norm);
}

float BuiltinVocodey::normToBandwidthQ(double norm) {
  return d_vocodey_norm_to_bandwidth_q(norm);
}

void BuiltinVocodey::process(float** inputs, float** outputs, int num_samples,
                             const HostProcessContext& context,
                             const std::vector<MidiNoteEvent>& events,
                             float** sidechain) {
  impl_->sample_rate = context.sampleRate;

  std::vector<VocodeyMidiEvent> d_events;
  d_events.reserve(events.size());
  for (const auto& ev : events) {
    VocodeyMidiEvent dev;
    dev.pitch = ev.pitch;
    dev.velocity = ev.velocity;
    dev.is_note_on = ev.isNoteOn;
    d_events.push_back(dev);
  }

  const float* in_l = (inputs && inputs[0]) ? inputs[0] : outputs[0];
  const float* in_r = (inputs && inputs[1]) ? inputs[1] : outputs[1];
  const float* sc_l = (sidechain && sidechain[0]) ? sidechain[0] : nullptr;
  const float* sc_r = (sidechain && sidechain[1]) ? sidechain[1] : nullptr;

  d_vocodey_process(impl_->state, in_l, in_r, outputs[0], outputs[1],
                    num_samples, context.sampleRate, d_events.data(),
                    static_cast<int>(d_events.size()), sc_l, sc_r);
}

int BuiltinVocodey::getParameterCount() const { return kTotalParams; }

bool BuiltinVocodey::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {"Attack", "Decay", "Bandwidth", "Noise Bleed",
                                "Detune", "Dry",   "Wet",       "Volume"};
  static const double defaults[] = {0.15, 0.25, 0.35, 0.3, 0.15, 0.0, 1.0, 0.7};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinVocodey::setParameterValue(uint32_t id, double valueNormalized) {
  d_vocodey_set_parameter(impl_->state, id, valueNormalized);
}

double BuiltinVocodey::getParameterValue(uint32_t id) const {
  return d_vocodey_get_parameter(impl_->state, id);
}

const std::string& BuiltinVocodey::getName() const { return kVocodeyName; }

const std::string& BuiltinVocodey::getPath() const { return kVocodeyPath; }

int BuiltinVocodey::getPluginIndex() const { return 0; }

bool BuiltinVocodey::isInstrument() const { return false; }

}  // namespace hibiki
