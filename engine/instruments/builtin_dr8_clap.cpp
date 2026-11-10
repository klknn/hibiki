#include "engine/instruments/builtin_dr8_clap.hpp"

#include <vector>

namespace hibiki {

static const std::string kDr8ClapName = "DR8 Clap";
static const std::string kDr8ClapPath = "builtin://dr8_clap";

struct Dr8ClapDspState;

struct Dr8ClapMidiEvent {
  int pitch;
  float velocity;
  bool is_note_on;
};

Dr8ClapDspState* d_dr8_clap_create();
void d_dr8_clap_destroy(Dr8ClapDspState* state);
void d_dr8_clap_reset(Dr8ClapDspState* state);
void d_dr8_clap_set_parameter(Dr8ClapDspState* state, uint32_t id,
                              double value);
double d_dr8_clap_get_parameter(const Dr8ClapDspState* state, uint32_t id);
void d_dr8_clap_process(Dr8ClapDspState* state, float* out_l, float* out_r,
                        int num_samples, double sample_rate,
                        const Dr8ClapMidiEvent* events, int num_events);

float d_dr8_clap_norm_to_decay_s(double norm);
float d_dr8_clap_norm_to_cutoff_hz(double norm);
float d_dr8_clap_norm_to_spread_s(double norm);

struct BuiltinDr8Clap::Impl {
  Dr8ClapDspState* state = nullptr;
  double sample_rate = 44100.0;

  Impl() : state(d_dr8_clap_create()) {}
  ~Impl() { d_dr8_clap_destroy(state); }
  void reset() { d_dr8_clap_reset(state); }
};

BuiltinDr8Clap::BuiltinDr8Clap() : impl_(std::make_unique<Impl>()) {
  impl_->reset();
}

BuiltinDr8Clap::~BuiltinDr8Clap() = default;

bool BuiltinDr8Clap::load(const std::string& /*path*/, int /*plugin_index*/,
                          double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->reset();
  return true;
}

float BuiltinDr8Clap::normToDecayS(double norm) {
  return d_dr8_clap_norm_to_decay_s(norm);
}

float BuiltinDr8Clap::normToCutoffHz(double norm) {
  return d_dr8_clap_norm_to_cutoff_hz(norm);
}

float BuiltinDr8Clap::normToSpreadS(double norm) {
  return d_dr8_clap_norm_to_spread_s(norm);
}

void BuiltinDr8Clap::process(float** /*inputs*/, float** outputs,
                             int num_samples, const HostProcessContext& context,
                             const std::vector<MidiNoteEvent>& events,
                             float** /*sidechain*/) {
  impl_->sample_rate = context.sampleRate;

  std::vector<Dr8ClapMidiEvent> devents;
  devents.reserve(events.size());
  for (const auto& ev : events) {
    Dr8ClapMidiEvent dev;
    dev.pitch = ev.pitch;
    dev.velocity = ev.velocity;
    dev.is_note_on = ev.isNoteOn;
    devents.push_back(dev);
  }

  d_dr8_clap_process(impl_->state, outputs[0], outputs[1], num_samples,
                     context.sampleRate, devents.data(),
                     static_cast<int>(devents.size()));
}

int BuiltinDr8Clap::getParameterCount() const { return kTotalParams; }

bool BuiltinDr8Clap::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {"Decay", "Filter Cutoff", "Spread", "Volume"};
  static const double defaults[] = {0.3, 0.3, 0.466667, 0.7};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinDr8Clap::setParameterValue(uint32_t id, double value) {
  d_dr8_clap_set_parameter(impl_->state, id, value);
}

double BuiltinDr8Clap::getParameterValue(uint32_t id) const {
  return d_dr8_clap_get_parameter(impl_->state, id);
}

const std::string& BuiltinDr8Clap::getName() const { return kDr8ClapName; }

const std::string& BuiltinDr8Clap::getPath() const { return kDr8ClapPath; }

int BuiltinDr8Clap::getPluginIndex() const { return 0; }

bool BuiltinDr8Clap::isInstrument() const { return true; }

}  // namespace hibiki
