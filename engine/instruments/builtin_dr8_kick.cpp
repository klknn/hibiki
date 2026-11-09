#include "engine/instruments/builtin_dr8_kick.hpp"

#include <algorithm>
#include <vector>

namespace hibiki {

static const std::string kDr8KickName = "DR8 Kick";
static const std::string kDr8KickPath = "builtin://dr8_kick";

struct Dr8KickMidiEvent {
  float velocity;
  bool is_note_on;
};

struct Dr8KickDspState {
  bool active;
  double osc_phase;
  double env_amp;
  double env_pitch;
  double env_click;
  float velocity;
  uint32_t rng_state;

  void reset();
};

float d_dr8_kick_norm_to_pitch_hz(double norm);
float d_dr8_kick_norm_to_decay_s(double norm);
float d_dr8_kick_norm_to_pitch_env_decay_s(double norm);
float d_dr8_kick_norm_to_pitch_env_depth_hz(double norm);

void d_dr8_kick_process(Dr8KickDspState& state, float* out_l, float* out_r,
                        int num_samples, double sample_rate,
                        const Dr8KickMidiEvent* events, int num_events,
                        double pitch_norm, double decay_norm,
                        double pitch_env_decay_norm,
                        double pitch_env_depth_norm, double click_level_norm,
                        double distortion_norm, double volume_norm);

struct BuiltinDr8Kick::Impl {
  double params[kTotalParams] = {};
  double sample_rate = 44100.0;
  Dr8KickDspState dsp_state{};

  void reset() { dsp_state.reset(); }
};

BuiltinDr8Kick::BuiltinDr8Kick() : impl_(std::make_unique<Impl>()) {
  impl_->params[PARAM_PITCH] = 0.25;           // ~50 Hz base
  impl_->params[PARAM_DECAY] = 0.4;            // ~400ms decay
  impl_->params[PARAM_PITCH_ENV_DECAY] = 0.3;  // ~50ms pitch sweep decay
  impl_->params[PARAM_PITCH_ENV_DEPTH] = 0.5;  // ~150 Hz depth
  impl_->params[PARAM_CLICK_LEVEL] = 0.2;      // moderate click
  impl_->params[PARAM_DISTORTION] = 0.1;       // light drive
  impl_->params[PARAM_VOLUME] = 0.7;           // master volume

  impl_->reset();
}

BuiltinDr8Kick::~BuiltinDr8Kick() = default;

bool BuiltinDr8Kick::load(const std::string& /*path*/, int /*plugin_index*/,
                          double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->reset();
  return true;
}

float BuiltinDr8Kick::normToPitchHz(double norm) {
  return d_dr8_kick_norm_to_pitch_hz(norm);
}

float BuiltinDr8Kick::normToDecayS(double norm) {
  return d_dr8_kick_norm_to_decay_s(norm);
}

float BuiltinDr8Kick::normToPitchEnvDecayS(double norm) {
  return d_dr8_kick_norm_to_pitch_env_decay_s(norm);
}

float BuiltinDr8Kick::normToPitchEnvDepthHz(double norm) {
  return d_dr8_kick_norm_to_pitch_env_depth_hz(norm);
}

void BuiltinDr8Kick::process(float** /*inputs*/, float** outputs,
                             int num_samples, const HostProcessContext& context,
                             const std::vector<MidiNoteEvent>& events,
                             float** /*sidechain*/) {
  impl_->sample_rate = context.sampleRate;

  std::vector<Dr8KickMidiEvent> kick_events;
  kick_events.reserve(events.size());
  for (const auto& ev : events) {
    if (ev.isNoteOn && ev.velocity > 0) {
      Dr8KickMidiEvent kev;
      kev.velocity = ev.velocity;
      kev.is_note_on = true;
      kick_events.push_back(kev);
    }
  }

  d_dr8_kick_process(
      impl_->dsp_state, outputs[0], outputs[1], num_samples, impl_->sample_rate,
      kick_events.data(), (int)kick_events.size(), impl_->params[PARAM_PITCH],
      impl_->params[PARAM_DECAY], impl_->params[PARAM_PITCH_ENV_DECAY],
      impl_->params[PARAM_PITCH_ENV_DEPTH], impl_->params[PARAM_CLICK_LEVEL],
      impl_->params[PARAM_DISTORTION], impl_->params[PARAM_VOLUME]);
}

int BuiltinDr8Kick::getParameterCount() const { return kTotalParams; }

bool BuiltinDr8Kick::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {"Pitch", "Decay",       "Pitch Env Decay",
                                "Punch", "Click Level", "Distortion",
                                "Gain"};
  static const double defaults[] = {0.25, 0.4, 0.3, 0.5, 0.2, 0.1, 0.7};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinDr8Kick::setParameterValue(uint32_t id, double value) {
  if (id < kTotalParams) {
    impl_->params[id] = std::clamp(value, 0.0, 1.0);
  }
}

double BuiltinDr8Kick::getParameterValue(uint32_t id) const {
  if (id < kTotalParams) {
    return impl_->params[id];
  }
  return 0.0;
}

const std::string& BuiltinDr8Kick::getName() const { return kDr8KickName; }

const std::string& BuiltinDr8Kick::getPath() const { return kDr8KickPath; }

int BuiltinDr8Kick::getPluginIndex() const { return 0; }

bool BuiltinDr8Kick::isInstrument() const { return true; }

}  // namespace hibiki
