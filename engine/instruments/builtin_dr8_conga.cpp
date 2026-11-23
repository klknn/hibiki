#include "engine/instruments/builtin_dr8_conga.hpp"

#include <vector>

namespace hibiki {

static const std::string kDr8CongaName = "DR8 Conga";
static const std::string kDr8CongaPath = "builtin://dr8_conga";

struct Dr8CongaDspState;

struct Dr8CongaMidiEvent {
  int pitch;
  float velocity;
  bool is_note_on;
};

Dr8CongaDspState* d_dr8_conga_create();
void d_dr8_conga_destroy(Dr8CongaDspState* state);
void d_dr8_conga_reset(Dr8CongaDspState* state);
void d_dr8_conga_set_parameter(Dr8CongaDspState* state, uint32_t id,
                               double value);
double d_dr8_conga_get_parameter(const Dr8CongaDspState* state, uint32_t id);
void d_dr8_conga_process(Dr8CongaDspState* state, float* out_l, float* out_r,
                         int num_samples, double sample_rate,
                         const Dr8CongaMidiEvent* events, int num_events);

float d_dr8_conga_norm_to_pitch_hz(double norm);
float d_dr8_conga_norm_to_decay_s(double norm);
float d_dr8_conga_norm_to_pitch_env_decay_s(double norm);
float d_dr8_conga_norm_to_pitch_env_depth_hz(double norm);

struct BuiltinDr8Conga::Impl {
  Dr8CongaDspState* state = nullptr;
  double sample_rate = 44100.0;

  Impl() : state(d_dr8_conga_create()) {}
  ~Impl() { d_dr8_conga_destroy(state); }
  void reset() { d_dr8_conga_reset(state); }
};

BuiltinDr8Conga::BuiltinDr8Conga() : impl_(std::make_unique<Impl>()) {
  impl_->reset();
}

BuiltinDr8Conga::~BuiltinDr8Conga() = default;

bool BuiltinDr8Conga::load(const std::string& /*path*/, int /*plugin_index*/,
                           double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->reset();
  return true;
}

float BuiltinDr8Conga::normToPitchHz(double norm) {
  return d_dr8_conga_norm_to_pitch_hz(norm);
}

float BuiltinDr8Conga::normToDecayS(double norm) {
  return d_dr8_conga_norm_to_decay_s(norm);
}

float BuiltinDr8Conga::normToPitchEnvDecayS(double norm) {
  return d_dr8_conga_norm_to_pitch_env_decay_s(norm);
}

float BuiltinDr8Conga::normToPitchEnvDepthHz(double norm) {
  return d_dr8_conga_norm_to_pitch_env_depth_hz(norm);
}

void BuiltinDr8Conga::process(float** /*inputs*/, float** outputs,
                              int num_samples,
                              const HostProcessContext& context,
                              const std::vector<MidiNoteEvent>& events,
                              float** /*sidechain*/) {
  impl_->sample_rate = context.sampleRate;

  std::vector<Dr8CongaMidiEvent> d_events;
  d_events.reserve(events.size());
  for (const auto& ev : events) {
    Dr8CongaMidiEvent dev;
    dev.pitch = ev.pitch;
    dev.velocity = ev.velocity;
    dev.is_note_on = ev.isNoteOn;
    d_events.push_back(dev);
  }

  d_dr8_conga_process(impl_->state, outputs[0], outputs[1], num_samples,
                      context.sampleRate, d_events.data(),
                      static_cast<int>(d_events.size()));
}

int BuiltinDr8Conga::getParameterCount() const { return kTotalParams; }

bool BuiltinDr8Conga::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {"Pitch", "Decay", "Pitch Env Decay",
                                "Pitch Env Depth", "Volume"};
  static const double defaults[] = {0.3, 0.4, 0.4, 0.33, 0.7};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinDr8Conga::setParameterValue(uint32_t id, double value) {
  d_dr8_conga_set_parameter(impl_->state, id, value);
}

double BuiltinDr8Conga::getParameterValue(uint32_t id) const {
  return d_dr8_conga_get_parameter(impl_->state, id);
}

const std::string& BuiltinDr8Conga::getName() const { return kDr8CongaName; }

const std::string& BuiltinDr8Conga::getPath() const { return kDr8CongaPath; }

int BuiltinDr8Conga::getPluginIndex() const { return 0; }

bool BuiltinDr8Conga::isInstrument() const { return true; }

}  // namespace hibiki
