#include "engine/instruments/builtin_dr8_hat.hpp"

#include <vector>

namespace hibiki {

static const std::string kDr8HatName = "DR8 Hat";
static const std::string kDr8HatPath = "builtin://dr8_hat";

struct Dr8HatDspState;

struct Dr8HatMidiEvent {
  int pitch;
  float velocity;
  bool is_note_on;
};

Dr8HatDspState* d_dr8_hat_create();
void d_dr8_hat_destroy(Dr8HatDspState* state);
void d_dr8_hat_reset(Dr8HatDspState* state);
void d_dr8_hat_set_parameter(Dr8HatDspState* state, uint32_t id, double value);
double d_dr8_hat_get_parameter(const Dr8HatDspState* state, uint32_t id);
void d_dr8_hat_process(Dr8HatDspState* state, float* out_l, float* out_r,
                       int num_samples, double sample_rate,
                       const Dr8HatMidiEvent* events, int num_events);

float d_dr8_hat_norm_to_decay_s(double norm);
float d_dr8_hat_norm_to_hpf_hz(double norm);
float d_dr8_hat_norm_to_bpf_hz(double norm);

struct BuiltinDr8Hat::Impl {
  Dr8HatDspState* state = nullptr;
  double sample_rate = 44100.0;

  Impl() : state(d_dr8_hat_create()) {}
  ~Impl() { d_dr8_hat_destroy(state); }
  void reset() { d_dr8_hat_reset(state); }
};

BuiltinDr8Hat::BuiltinDr8Hat() : impl_(std::make_unique<Impl>()) {
  impl_->reset();
}

BuiltinDr8Hat::~BuiltinDr8Hat() = default;

bool BuiltinDr8Hat::load(const std::string& /*path*/, int /*plugin_index*/,
                         double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->reset();
  return true;
}

float BuiltinDr8Hat::normToDecayS(double norm) {
  return d_dr8_hat_norm_to_decay_s(norm);
}

float BuiltinDr8Hat::normToHpfHz(double norm) {
  return d_dr8_hat_norm_to_hpf_hz(norm);
}

float BuiltinDr8Hat::normToBpfHz(double norm) {
  return d_dr8_hat_norm_to_bpf_hz(norm);
}

void BuiltinDr8Hat::process(float** /*inputs*/, float** outputs,
                            int num_samples, const HostProcessContext& context,
                            const std::vector<MidiNoteEvent>& events,
                            float** /*sidechain*/) {
  impl_->sample_rate = context.sampleRate;

  std::vector<Dr8HatMidiEvent> devents;
  devents.reserve(events.size());
  for (const auto& ev : events) {
    Dr8HatMidiEvent dev;
    dev.pitch = ev.pitch;
    dev.velocity = ev.velocity;
    dev.is_note_on = ev.isNoteOn;
    devents.push_back(dev);
  }

  d_dr8_hat_process(impl_->state, outputs[0], outputs[1], num_samples,
                    context.sampleRate, devents.data(),
                    static_cast<int>(devents.size()));
}

int BuiltinDr8Hat::getParameterCount() const { return kTotalParams; }

bool BuiltinDr8Hat::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {"Decay", "HPF Freq", "BPF Freq", "Tension",
                                "Volume"};
  static const double defaults[] = {0.3, 0.5, 0.5, 0.4, 0.7};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinDr8Hat::setParameterValue(uint32_t id, double value) {
  d_dr8_hat_set_parameter(impl_->state, id, value);
}

double BuiltinDr8Hat::getParameterValue(uint32_t id) const {
  return d_dr8_hat_get_parameter(impl_->state, id);
}

const std::string& BuiltinDr8Hat::getName() const { return kDr8HatName; }

const std::string& BuiltinDr8Hat::getPath() const { return kDr8HatPath; }

int BuiltinDr8Hat::getPluginIndex() const { return 0; }

bool BuiltinDr8Hat::isInstrument() const { return true; }

}  // namespace hibiki
