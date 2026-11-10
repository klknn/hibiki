#include "engine/instruments/builtin_dr8_rim.hpp"

#include <vector>

namespace hibiki {

static const std::string kDr8RimName = "DR8 Rimshot";
static const std::string kDr8RimPath = "builtin://dr8_rim";

struct Dr8RimDspState;

struct Dr8RimMidiEvent {
  int pitch;
  float velocity;
  bool is_note_on;
};

Dr8RimDspState* d_dr8_rim_create();
void d_dr8_rim_destroy(Dr8RimDspState* state);
void d_dr8_rim_reset(Dr8RimDspState* state);
void d_dr8_rim_set_parameter(Dr8RimDspState* state, uint32_t id, double value);
double d_dr8_rim_get_parameter(const Dr8RimDspState* state, uint32_t id);
void d_dr8_rim_process(Dr8RimDspState* state, float* out_l, float* out_r,
                       int num_samples, double sample_rate,
                       const Dr8RimMidiEvent* events, int num_events);

float d_dr8_rim_norm_to_pitch_hz(double norm);
float d_dr8_rim_norm_to_decay_s(double norm);

struct BuiltinDr8Rim::Impl {
  Dr8RimDspState* state = nullptr;
  double sample_rate = 44100.0;

  Impl() : state(d_dr8_rim_create()) {}
  ~Impl() { d_dr8_rim_destroy(state); }
  void reset() { d_dr8_rim_reset(state); }
};

BuiltinDr8Rim::BuiltinDr8Rim() : impl_(std::make_unique<Impl>()) {
  impl_->reset();
}

BuiltinDr8Rim::~BuiltinDr8Rim() = default;

bool BuiltinDr8Rim::load(const std::string& /*path*/, int /*plugin_index*/,
                         double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->reset();
  return true;
}

float BuiltinDr8Rim::normToPitchHz(double norm) {
  return d_dr8_rim_norm_to_pitch_hz(norm);
}

float BuiltinDr8Rim::normToDecayS(double norm) {
  return d_dr8_rim_norm_to_decay_s(norm);
}

void BuiltinDr8Rim::process(float** /*inputs*/, float** outputs,
                            int num_samples, const HostProcessContext& context,
                            const std::vector<MidiNoteEvent>& events,
                            float** /*sidechain*/) {
  impl_->sample_rate = context.sampleRate;

  std::vector<Dr8RimMidiEvent> d_events;
  d_events.reserve(events.size());
  for (const auto& ev : events) {
    Dr8RimMidiEvent dev;
    dev.pitch = ev.pitch;
    dev.velocity = ev.velocity;
    dev.is_note_on = ev.isNoteOn;
    d_events.push_back(dev);
  }

  d_dr8_rim_process(impl_->state, outputs[0], outputs[1], num_samples,
                    context.sampleRate, d_events.data(),
                    static_cast<int>(d_events.size()));
}

int BuiltinDr8Rim::getParameterCount() const { return kTotalParams; }

bool BuiltinDr8Rim::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {"Pitch", "Decay", "Volume"};
  static const double defaults[] = {0.4, 0.3, 0.7};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinDr8Rim::setParameterValue(uint32_t id, double value) {
  d_dr8_rim_set_parameter(impl_->state, id, value);
}

double BuiltinDr8Rim::getParameterValue(uint32_t id) const {
  return d_dr8_rim_get_parameter(impl_->state, id);
}

const std::string& BuiltinDr8Rim::getName() const { return kDr8RimName; }

const std::string& BuiltinDr8Rim::getPath() const { return kDr8RimPath; }

int BuiltinDr8Rim::getPluginIndex() const { return 0; }

bool BuiltinDr8Rim::isInstrument() const { return true; }

}  // namespace hibiki
