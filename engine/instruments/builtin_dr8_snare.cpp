#include "engine/instruments/builtin_dr8_snare.hpp"

#include <vector>

namespace hibiki {

static const std::string kDr8SnareName = "DR8 Snare";
static const std::string kDr8SnarePath = "builtin://dr8_snare";

struct Dr8SnareDspState;

struct Dr8SnareMidiEvent {
  int pitch;
  float velocity;
  bool is_note_on;
};

Dr8SnareDspState* d_dr8_snare_create();
void d_dr8_snare_destroy(Dr8SnareDspState* state);
void d_dr8_snare_reset(Dr8SnareDspState* state);
void d_dr8_snare_set_parameter(Dr8SnareDspState* state, uint32_t id,
                               double value);
double d_dr8_snare_get_parameter(const Dr8SnareDspState* state, uint32_t id);
void d_dr8_snare_process(Dr8SnareDspState* state, float* out_l, float* out_r,
                         int num_samples, double sample_rate,
                         const Dr8SnareMidiEvent* events, int num_events);

float d_dr8_snare_norm_to_pitch_hz(double norm);
float d_dr8_snare_norm_to_decay_s(double norm);
float d_dr8_snare_norm_to_noise_decay_s(double norm);
float d_dr8_snare_norm_to_noise_hpf_hz(double norm);

struct BuiltinDr8Snare::Impl {
  Dr8SnareDspState* state = nullptr;
  double sample_rate = 44100.0;

  Impl() : state(d_dr8_snare_create()) {}
  ~Impl() { d_dr8_snare_destroy(state); }
  void reset() { d_dr8_snare_reset(state); }
};

BuiltinDr8Snare::BuiltinDr8Snare() : impl_(std::make_unique<Impl>()) {
  impl_->reset();
}

BuiltinDr8Snare::~BuiltinDr8Snare() = default;

bool BuiltinDr8Snare::load(const std::string& /*path*/, int /*plugin_index*/,
                           double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->reset();
  return true;
}

float BuiltinDr8Snare::normToPitchHz(double norm) {
  return d_dr8_snare_norm_to_pitch_hz(norm);
}

float BuiltinDr8Snare::normToDecayS(double norm) {
  return d_dr8_snare_norm_to_decay_s(norm);
}

float BuiltinDr8Snare::normToNoiseDecayS(double norm) {
  return d_dr8_snare_norm_to_noise_decay_s(norm);
}

float BuiltinDr8Snare::normToNoiseHpfHz(double norm) {
  return d_dr8_snare_norm_to_noise_hpf_hz(norm);
}

void BuiltinDr8Snare::process(float** /*inputs*/, float** outputs,
                              int num_samples,
                              const HostProcessContext& context,
                              const std::vector<MidiNoteEvent>& events,
                              float** /*sidechain*/) {
  impl_->sample_rate = context.sampleRate;

  std::vector<Dr8SnareMidiEvent> devents;
  devents.reserve(events.size());
  for (const auto& ev : events) {
    Dr8SnareMidiEvent dev;
    dev.pitch = ev.pitch;
    dev.velocity = ev.velocity;
    dev.is_note_on = ev.isNoteOn;
    devents.push_back(dev);
  }

  d_dr8_snare_process(impl_->state, outputs[0], outputs[1], num_samples,
                      context.sampleRate, devents.data(),
                      static_cast<int>(devents.size()));
}

int BuiltinDr8Snare::getParameterCount() const { return kTotalParams; }

bool BuiltinDr8Snare::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;

  info.id = index;
  switch (index) {
    case PARAM_PITCH:
      info.name = "Pitch";
      info.defaultValue = 0.333333;
      break;
    case PARAM_DECAY:
      info.name = "Decay";
      info.defaultValue = 0.3;
      break;
    case PARAM_NOISE_LEVEL:
      info.name = "Snappy";
      info.defaultValue = 0.5;
      break;
    case PARAM_NOISE_DECAY:
      info.name = "Noise Decay";
      info.defaultValue = 0.4;
      break;
    case PARAM_NOISE_HPF:
      info.name = "Noise Filter";
      info.defaultValue = 0.4;
      break;
    case PARAM_TONE_NOISE_MIX:
      info.name = "Mix";
      info.defaultValue = 0.5;
      break;
    case PARAM_VOLUME:
      info.name = "Volume";
      info.defaultValue = 0.7;
      break;
    default:
      return false;
  }
  return true;
}

void BuiltinDr8Snare::setParameterValue(uint32_t id, double value) {
  d_dr8_snare_set_parameter(impl_->state, id, value);
}

double BuiltinDr8Snare::getParameterValue(uint32_t id) const {
  return d_dr8_snare_get_parameter(impl_->state, id);
}

const std::string& BuiltinDr8Snare::getName() const { return kDr8SnareName; }

const std::string& BuiltinDr8Snare::getPath() const { return kDr8SnarePath; }

int BuiltinDr8Snare::getPluginIndex() const { return 0; }

bool BuiltinDr8Snare::isInstrument() const { return true; }

}  // namespace hibiki
