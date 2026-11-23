#include "engine/instruments/builtin_dr8_tom.hpp"

#include <vector>

namespace hibiki {

static const std::string kDr8TomName = "DR8 Tom";
static const std::string kDr8TomPath = "builtin://dr8_tom";

struct Dr8TomDspState;

struct Dr8TomMidiEvent {
  int pitch;
  float velocity;
  bool is_note_on;
};

Dr8TomDspState* d_dr8_tom_create();
void d_dr8_tom_destroy(Dr8TomDspState* state);
void d_dr8_tom_reset(Dr8TomDspState* state);
void d_dr8_tom_set_parameter(Dr8TomDspState* state, uint32_t id, double value);
double d_dr8_tom_get_parameter(const Dr8TomDspState* state, uint32_t id);
void d_dr8_tom_process(Dr8TomDspState* state, float* out_l, float* out_r,
                       int num_samples, double sample_rate,
                       const Dr8TomMidiEvent* events, int num_events);

float d_dr8_tom_norm_to_pitch_hz(double norm);
float d_dr8_tom_norm_to_decay_s(double norm);
float d_dr8_tom_norm_to_pitch_env_decay_s(double norm);
float d_dr8_tom_norm_to_pitch_env_depth_hz(double norm);

struct BuiltinDr8Tom::Impl {
  Dr8TomDspState* state = nullptr;
  double sample_rate = 44100.0;

  Impl() : state(d_dr8_tom_create()) {}
  ~Impl() { d_dr8_tom_destroy(state); }
  void reset() { d_dr8_tom_reset(state); }
};

BuiltinDr8Tom::BuiltinDr8Tom() : impl_(std::make_unique<Impl>()) {
  impl_->reset();
}

BuiltinDr8Tom::~BuiltinDr8Tom() = default;

bool BuiltinDr8Tom::load(const std::string& /*path*/, int /*plugin_index*/,
                         double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->reset();
  return true;
}

float BuiltinDr8Tom::normToPitchHz(double norm) {
  return d_dr8_tom_norm_to_pitch_hz(norm);
}

float BuiltinDr8Tom::normToDecayS(double norm) {
  return d_dr8_tom_norm_to_decay_s(norm);
}

float BuiltinDr8Tom::normToPitchEnvDecayS(double norm) {
  return d_dr8_tom_norm_to_pitch_env_decay_s(norm);
}

float BuiltinDr8Tom::normToPitchEnvDepthHz(double norm) {
  return d_dr8_tom_norm_to_pitch_env_depth_hz(norm);
}

void BuiltinDr8Tom::process(float** /*inputs*/, float** outputs,
                            int num_samples, const HostProcessContext& context,
                            const std::vector<MidiNoteEvent>& events,
                            float** /*sidechain*/) {
  impl_->sample_rate = context.sampleRate;

  std::vector<Dr8TomMidiEvent> d_events;
  d_events.reserve(events.size());
  for (const auto& ev : events) {
    Dr8TomMidiEvent dev;
    dev.pitch = ev.pitch;
    dev.velocity = ev.velocity;
    dev.is_note_on = ev.isNoteOn;
    d_events.push_back(dev);
  }

  d_dr8_tom_process(impl_->state, outputs[0], outputs[1], num_samples,
                    context.sampleRate, d_events.data(),
                    static_cast<int>(d_events.size()));
}

int BuiltinDr8Tom::getParameterCount() const { return kTotalParams; }

bool BuiltinDr8Tom::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {"Pitch",         "Decay",       "PitchEnvDecay",
                                "PitchEnvDepth", "NoiseAttack", "Volume"};
  static const double defaults[] = {0.3, 0.4, 0.4, 0.4, 0.15, 0.7};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinDr8Tom::setParameterValue(uint32_t id, double value) {
  d_dr8_tom_set_parameter(impl_->state, id, value);
}

double BuiltinDr8Tom::getParameterValue(uint32_t id) const {
  return d_dr8_tom_get_parameter(impl_->state, id);
}

const std::string& BuiltinDr8Tom::getName() const { return kDr8TomName; }

const std::string& BuiltinDr8Tom::getPath() const { return kDr8TomPath; }

int BuiltinDr8Tom::getPluginIndex() const { return 0; }

bool BuiltinDr8Tom::isInstrument() const { return true; }

}  // namespace hibiki
