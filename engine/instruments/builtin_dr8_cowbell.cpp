#include "engine/instruments/builtin_dr8_cowbell.hpp"

#include <vector>

namespace hibiki {

static const std::string kDr8CowbellName = "DR8 Cowbell";
static const std::string kDr8CowbellPath = "builtin://dr8_cowbell";

struct Dr8CowbellDspState;

struct Dr8CowbellMidiEvent {
  int pitch;
  float velocity;
  bool is_note_on;
};

Dr8CowbellDspState* d_dr8_cowbell_create();
void d_dr8_cowbell_destroy(Dr8CowbellDspState* state);
void d_dr8_cowbell_reset(Dr8CowbellDspState* state);
void d_dr8_cowbell_set_parameter(Dr8CowbellDspState* state, uint32_t id,
                                 double value);
double d_dr8_cowbell_get_parameter(const Dr8CowbellDspState* state,
                                   uint32_t id);
void d_dr8_cowbell_process(Dr8CowbellDspState* state, float* out_l,
                           float* out_r, int num_samples, double sample_rate,
                           const Dr8CowbellMidiEvent* events, int num_events);

float d_dr8_cowbell_norm_to_pitch_hz(double norm);
float d_dr8_cowbell_norm_to_decay_s(double norm);
float d_dr8_cowbell_norm_to_detune_ratio(double norm);

struct BuiltinDr8Cowbell::Impl {
  Dr8CowbellDspState* state = nullptr;
  double sample_rate = 44100.0;

  Impl() : state(d_dr8_cowbell_create()) {}
  ~Impl() { d_dr8_cowbell_destroy(state); }
  void reset() { d_dr8_cowbell_reset(state); }
};

BuiltinDr8Cowbell::BuiltinDr8Cowbell() : impl_(std::make_unique<Impl>()) {
  impl_->reset();
}

BuiltinDr8Cowbell::~BuiltinDr8Cowbell() = default;

bool BuiltinDr8Cowbell::load(const std::string& /*path*/, int /*plugin_index*/,
                             double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->reset();
  return true;
}

float BuiltinDr8Cowbell::normToPitchHz(double norm) {
  return d_dr8_cowbell_norm_to_pitch_hz(norm);
}

float BuiltinDr8Cowbell::normToDecayS(double norm) {
  return d_dr8_cowbell_norm_to_decay_s(norm);
}

float BuiltinDr8Cowbell::normToDetuneRatio(double norm) {
  return d_dr8_cowbell_norm_to_detune_ratio(norm);
}

void BuiltinDr8Cowbell::process(float** /*inputs*/, float** outputs,
                                int num_samples,
                                const HostProcessContext& context,
                                const std::vector<MidiNoteEvent>& events,
                                float** /*sidechain*/) {
  impl_->sample_rate = context.sampleRate;

  std::vector<Dr8CowbellMidiEvent> d_events;
  d_events.reserve(events.size());
  for (const auto& ev : events) {
    Dr8CowbellMidiEvent dev;
    dev.pitch = ev.pitch;
    dev.velocity = ev.velocity;
    dev.is_note_on = ev.isNoteOn;
    d_events.push_back(dev);
  }

  d_dr8_cowbell_process(impl_->state, outputs[0], outputs[1], num_samples,
                        context.sampleRate, d_events.data(),
                        static_cast<int>(d_events.size()));
}

int BuiltinDr8Cowbell::getParameterCount() const { return kTotalParams; }

bool BuiltinDr8Cowbell::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {"Pitch", "Decay", "Detune", "Volume"};
  static const double defaults[] = {0.466667, 0.3, 0.4, 0.7};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinDr8Cowbell::setParameterValue(uint32_t id, double value) {
  d_dr8_cowbell_set_parameter(impl_->state, id, value);
}

double BuiltinDr8Cowbell::getParameterValue(uint32_t id) const {
  return d_dr8_cowbell_get_parameter(impl_->state, id);
}

const std::string& BuiltinDr8Cowbell::getName() const {
  return kDr8CowbellName;
}

const std::string& BuiltinDr8Cowbell::getPath() const {
  return kDr8CowbellPath;
}

int BuiltinDr8Cowbell::getPluginIndex() const { return 0; }

bool BuiltinDr8Cowbell::isInstrument() const { return true; }

}  // namespace hibiki
