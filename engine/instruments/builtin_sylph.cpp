#include "engine/instruments/builtin_sylph.hpp"

#include <cstdint>
#include <vector>

namespace hibiki {

static const std::string kSylphName = "Sylph";
static const std::string kSylphPath = "builtin://sylph";

struct SylphDspState;

struct SylphMidiEvent {
  int pitch;
  float velocity;
  bool is_note_on;
  float pan = 0.0f;
};

SylphDspState* d_sylph_create();
void d_sylph_destroy(SylphDspState* state);
void d_sylph_reset(SylphDspState* state);
int d_sylph_apply_preset(SylphDspState& state, int preset_idx);
double d_sylph_get_default_value(int id);
float d_sylph_get_param(const SylphDspState* state, uint32_t id);
void d_sylph_set_parameter(SylphDspState& state, uint32_t id, float value);
void d_sylph_process(SylphDspState& state, float* out_l, float* out_r,
                     int num_samples, double sample_rate,
                     const SylphMidiEvent* events, int num_events);

struct BuiltinSylph::Impl {
  SylphDspState* state = nullptr;
  double sample_rate = 44100.0;

  Impl() : state(d_sylph_create()) {}
  ~Impl() { d_sylph_destroy(state); }
  void reset() { d_sylph_reset(state); }
};

BuiltinSylph::BuiltinSylph() : impl_(std::make_unique<Impl>()) {
  impl_->reset();
}

BuiltinSylph::~BuiltinSylph() = default;

bool BuiltinSylph::load(const std::string& /*path*/, int /*plugin_index*/,
                        double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->reset();
  return true;
}

void BuiltinSylph::process(float** /*inputs*/, float** outputs, int num_samples,
                           const HostProcessContext& context,
                           const std::vector<MidiNoteEvent>& events,
                           float** /*sidechain*/) {
  impl_->sample_rate = context.sampleRate;

  std::vector<SylphMidiEvent> midi_events;
  midi_events.reserve(events.size());
  for (const auto& ev : events) {
    SylphMidiEvent mev;
    mev.pitch = ev.pitch;
    mev.velocity = ev.velocity;
    mev.pan = ev.pan;
    mev.is_note_on = ev.isNoteOn;
    midi_events.push_back(mev);
  }

  d_sylph_process(*impl_->state, outputs[0], outputs[1], num_samples,
                  impl_->sample_rate, midi_events.data(),
                  static_cast<int>(midi_events.size()));
}

int BuiltinSylph::getParameterCount() const { return kTotalParams; }

bool BuiltinSylph::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* kParamNames[kTotalParams] = {
      "Osc1 Wave",     "Osc1 Octave",   "Osc1 Semi",      "Osc1 Unison",
      "Osc1 Detune",   "Osc1 Stereo",   "Osc1 Retrig",    "Osc1 Vol",
      "Osc2 Wave",     "Osc2 Octave",   "Osc2 Semi",      "Osc2 Unison",
      "Osc2 Detune",   "Osc2 Stereo",   "Osc2 Retrig",    "Osc2 Vol",
      "Sub Wave",      "Sub Octave",    "Sub Vol",        "Click / Noise",
      "Pitch Env Amt", "Pitch Decay",   "Pitch Curve",    "Amp Attack",
      "Amp Decay",     "Amp Sustain",   "Amp Release",    "Play Mode",
      "Filter Type",   "Cutoff",        "Resonance",      "Filter Env Amt",
      "Filter Attack", "Filter Decay",  "Filter Sustain", "Filter Release",
      "Drive",         "LFO Rate",      "LFO -> Pitch",   "LFO -> Cutoff",
      "Chorus Mix",    "Delay Mix",     "Delay Time",     "Reverb Mix",
      "Glide",         "Master Volume",
  };
  info.id = index;
  info.name = kParamNames[index];
  info.defaultValue = d_sylph_get_default_value(index);
  return true;
}

void BuiltinSylph::setParameterValue(uint32_t id, double value) {
  if (impl_->state && id < kTotalParams) {
    d_sylph_set_parameter(*impl_->state, id, static_cast<float>(value));
  }
}

double BuiltinSylph::getParameterValue(uint32_t id) const {
  if (impl_->state && id < kTotalParams) {
    return d_sylph_get_param(impl_->state, id);
  }
  return 0.0;
}

void BuiltinSylph::applyPreset(int preset_index) {
  if (impl_->state) {
    d_sylph_apply_preset(*impl_->state, preset_index);
  }
}

const std::string& BuiltinSylph::getName() const { return kSylphName; }
const std::string& BuiltinSylph::getPath() const { return kSylphPath; }
int BuiltinSylph::getPluginIndex() const { return 0; }
bool BuiltinSylph::isInstrument() const { return true; }

}  // namespace hibiki
