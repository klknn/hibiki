#include "engine/instruments/builtin_3xosc.hpp"

#include <vector>

namespace hibiki {

static const std::string k3xOscName = "3xOsc";
static const std::string k3xOscPath = "builtin://3xosc";

struct ThreeXOscDspState;

struct ThreeXOscMidiEvent {
  int pitch;
  float velocity;
  bool is_note_on;
};

ThreeXOscDspState* d_3xosc_create();
void d_3xosc_destroy(ThreeXOscDspState* state);
void d_3xosc_reset(ThreeXOscDspState* state);
double d_3xosc_get_default_value(int id);
float d_3xosc_get_param(const ThreeXOscDspState* state, uint32_t id);
void d_3xosc_set_parameter(ThreeXOscDspState& state, uint32_t id, float value);
void d_3xosc_process(ThreeXOscDspState& state, float* out_l, float* out_r,
                     int num_samples, double sample_rate,
                     const ThreeXOscMidiEvent* events, int num_events);

struct Builtin3xOsc::Impl {
  ThreeXOscDspState* state = nullptr;
  double sample_rate = 44100.0;

  Impl() : state(d_3xosc_create()) {}
  ~Impl() { d_3xosc_destroy(state); }
  void reset() { d_3xosc_reset(state); }
};

Builtin3xOsc::Builtin3xOsc() : impl_(std::make_unique<Impl>()) {
  impl_->reset();
}

Builtin3xOsc::~Builtin3xOsc() = default;

bool Builtin3xOsc::load(const std::string& /*path*/, int /*plugin_index*/,
                        double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->reset();
  return true;
}

void Builtin3xOsc::process(float** /*inputs*/, float** outputs, int num_samples,
                           const HostProcessContext& context,
                           const std::vector<MidiNoteEvent>& events,
                           float** /*sidechain*/) {
  impl_->sample_rate = context.sampleRate;

  std::vector<ThreeXOscMidiEvent> midi_events;
  midi_events.reserve(events.size());
  for (const auto& ev : events) {
    ThreeXOscMidiEvent mev;
    mev.pitch = ev.pitch;
    mev.velocity = ev.velocity;
    mev.is_note_on = ev.isNoteOn;
    midi_events.push_back(mev);
  }

  d_3xosc_process(*impl_->state, outputs[0], outputs[1], num_samples,
                  impl_->sample_rate, midi_events.data(),
                  (int)midi_events.size());
}

int Builtin3xOsc::getParameterCount() const { return kTotalParams; }

bool Builtin3xOsc::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  info.id = index;
  info.defaultValue = d_3xosc_get_default_value(index);
  static const char* osc_names[] = {"Waveform", "Coarse", "Fine", "Volume",
                                    "Pan"};
  if (index < kOscParams) {
    int osc = index / kParamsPerOsc + 1;
    int param = index % kParamsPerOsc;
    info.name = "Osc" + std::to_string(osc) + " " + osc_names[param];
  } else {
    static const char* global_names[] = {
        "Gain Attack",  "Gain Decay",     "Gain Sustain",     "Gain Release",
        "Filter Type",  "Filter Cutoff",  "Filter Resonance", "Filter Attack",
        "Filter Decay", "Filter Sustain", "Filter Release",   "Filter Depth",
        "Volume",       "Enable"};
    info.name = global_names[index - kOscParams];
  }
  return true;
}

void Builtin3xOsc::setParameterValue(uint32_t id, double value) {
  if (impl_->state && id < kTotalParams) {
    d_3xosc_set_parameter(*impl_->state, id, (float)value);
  }
}

double Builtin3xOsc::getParameterValue(uint32_t id) const {
  if (impl_->state && id < kTotalParams) {
    return d_3xosc_get_param(impl_->state, id);
  }
  return 0.0;
}

const std::string& Builtin3xOsc::getName() const { return k3xOscName; }
const std::string& Builtin3xOsc::getPath() const { return k3xOscPath; }
int Builtin3xOsc::getPluginIndex() const { return 0; }
bool Builtin3xOsc::isInstrument() const { return true; }

}  // namespace hibiki
