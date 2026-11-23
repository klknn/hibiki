#include "engine/instruments/builtin_dr8_crash.hpp"

#include <vector>

namespace hibiki {

static const std::string kDr8CrashName = "DR8 Crash";
static const std::string kDr8CrashPath = "builtin://dr8_crash";

struct Dr8CrashDspState;

struct Dr8CrashMidiEvent {
  int pitch;
  float velocity;
  bool is_note_on;
};

Dr8CrashDspState* d_dr8_crash_create();
void d_dr8_crash_destroy(Dr8CrashDspState* state);
void d_dr8_crash_reset(Dr8CrashDspState* state);
void d_dr8_crash_set_parameter(Dr8CrashDspState* state, uint32_t id,
                               double value);
double d_dr8_crash_get_parameter(const Dr8CrashDspState* state, uint32_t id);
void d_dr8_crash_process(Dr8CrashDspState* state, float* out_l, float* out_r,
                         int num_samples, double sample_rate,
                         const Dr8CrashMidiEvent* events, int num_events);

float d_dr8_crash_norm_to_decay_s(double norm);

struct BuiltinDr8Crash::Impl {
  Dr8CrashDspState* state = nullptr;
  double sample_rate = 44100.0;

  Impl() : state(d_dr8_crash_create()) {}
  ~Impl() { d_dr8_crash_destroy(state); }
  void reset() { d_dr8_crash_reset(state); }
};

BuiltinDr8Crash::BuiltinDr8Crash() : impl_(std::make_unique<Impl>()) {
  impl_->reset();
}

BuiltinDr8Crash::~BuiltinDr8Crash() = default;

bool BuiltinDr8Crash::load(const std::string& /*path*/, int /*plugin_index*/,
                           double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->reset();
  return true;
}

float BuiltinDr8Crash::normToDecayS(double norm) {
  return d_dr8_crash_norm_to_decay_s(norm);
}

void BuiltinDr8Crash::process(float** /*inputs*/, float** outputs,
                              int num_samples,
                              const HostProcessContext& context,
                              const std::vector<MidiNoteEvent>& events,
                              float** /*sidechain*/) {
  impl_->sample_rate = context.sampleRate;

  std::vector<Dr8CrashMidiEvent> d_events;
  d_events.reserve(events.size());
  for (const auto& ev : events) {
    Dr8CrashMidiEvent dev;
    dev.pitch = ev.pitch;
    dev.velocity = ev.velocity;
    dev.is_note_on = ev.isNoteOn;
    d_events.push_back(dev);
  }

  d_dr8_crash_process(impl_->state, outputs[0], outputs[1], num_samples,
                      context.sampleRate, d_events.data(),
                      static_cast<int>(d_events.size()));
}

int BuiltinDr8Crash::getParameterCount() const { return kTotalParams; }

bool BuiltinDr8Crash::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {"Decay", "Tone", "Tension", "Volume"};
  static const double defaults[] = {0.4, 0.4, 0.3, 0.7};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinDr8Crash::setParameterValue(uint32_t id, double value) {
  d_dr8_crash_set_parameter(impl_->state, id, value);
}

double BuiltinDr8Crash::getParameterValue(uint32_t id) const {
  return d_dr8_crash_get_parameter(impl_->state, id);
}

const std::string& BuiltinDr8Crash::getName() const { return kDr8CrashName; }

const std::string& BuiltinDr8Crash::getPath() const { return kDr8CrashPath; }

int BuiltinDr8Crash::getPluginIndex() const { return 0; }

bool BuiltinDr8Crash::isInstrument() const { return true; }

}  // namespace hibiki
