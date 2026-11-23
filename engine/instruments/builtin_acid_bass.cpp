#include "engine/instruments/builtin_acid_bass.hpp"

#include <algorithm>
#include <vector>

namespace hibiki {

static const std::string kAcidBassName = "Acid Bass";
static const std::string kAcidBassPath = "builtin://acid_bass";

struct AcidMidiEvent {
  int pitch;
  float velocity;
  bool is_note_on;
};

struct AcidBassDspState {
  int note_stack[32];
  int note_stack_count;
  bool note_active;
  int current_pitch;
  double target_freq;
  double current_freq;
  double osc_phase;
  double env_val;
  double amp_gain;
  bool accent_active;
  double s1, s2, s3, s4;

  void reset();
};

float d_acid_norm_to_cutoff_hz(double norm);
float d_acid_norm_to_decay_s(double norm);
void d_acid_bass_process(AcidBassDspState& state, float* out_l, float* out_r,
                         int num_samples, double sample_rate,
                         const AcidMidiEvent* events, int num_events,
                         double waveform_norm, double cutoff_norm,
                         double resonance_norm, double env_mod_norm,
                         double decay_norm, double accent_norm,
                         double overdrive_norm, double volume_norm,
                         double transpose_norm, double slide_norm,
                         double accent_switch_norm);

struct BuiltinAcidBass::Impl {
  double params[kTotalParams] = {};
  double sample_rate = 44100.0;
  AcidBassDspState dsp_state{};

  void reset() { dsp_state.reset(); }
};

BuiltinAcidBass::BuiltinAcidBass() : impl_(std::make_unique<Impl>()) {
  impl_->params[PARAM_WAVEFORM] = 0.0;       // Sawtooth default
  impl_->params[PARAM_CUTOFF] = 0.3;         // moderate cutoff
  impl_->params[PARAM_RESONANCE] = 0.6;      // high resonance
  impl_->params[PARAM_ENV_MOD] = 0.5;        // env mod amount
  impl_->params[PARAM_DECAY] = 0.2;          // moderate decay
  impl_->params[PARAM_ACCENT] = 0.0;         // no accent
  impl_->params[PARAM_OVERDRIVE] = 0.1;      // light overdrive
  impl_->params[PARAM_VOLUME] = 0.7;         // output volume
  impl_->params[PARAM_TRANSPOSE] = 0.5;      // 0 octaves (middle)
  impl_->params[PARAM_SLIDE] = 0.0;          // Slide off
  impl_->params[PARAM_ACCENT_SWITCH] = 0.0;  // Accent switch off

  impl_->reset();
}

BuiltinAcidBass::~BuiltinAcidBass() = default;

bool BuiltinAcidBass::load(const std::string& /*path*/, int /*plugin_index*/,
                           double sample_rate) {
  impl_->sample_rate = sample_rate;
  impl_->reset();
  return true;
}

float BuiltinAcidBass::normToCutoffHz(double norm) {
  return d_acid_norm_to_cutoff_hz(norm);
}

float BuiltinAcidBass::normToDecayS(double norm) {
  return d_acid_norm_to_decay_s(norm);
}

void BuiltinAcidBass::process(float** /*inputs*/, float** outputs,
                              int num_samples,
                              const HostProcessContext& context,
                              const std::vector<MidiNoteEvent>& events,
                              float** /*sidechain*/) {
  impl_->sample_rate = context.sampleRate;

  std::vector<AcidMidiEvent> acid_events;
  acid_events.reserve(events.size());
  for (const auto& ev : events) {
    AcidMidiEvent aev;
    aev.pitch = ev.pitch;
    aev.velocity = ev.velocity;
    aev.is_note_on = ev.isNoteOn;
    acid_events.push_back(aev);
  }

  d_acid_bass_process(
      impl_->dsp_state, outputs[0], outputs[1], num_samples, impl_->sample_rate,
      acid_events.data(), (int)acid_events.size(),
      impl_->params[PARAM_WAVEFORM], impl_->params[PARAM_CUTOFF],
      impl_->params[PARAM_RESONANCE], impl_->params[PARAM_ENV_MOD],
      impl_->params[PARAM_DECAY], impl_->params[PARAM_ACCENT],
      impl_->params[PARAM_OVERDRIVE], impl_->params[PARAM_VOLUME],
      impl_->params[PARAM_TRANSPOSE], impl_->params[PARAM_SLIDE],
      impl_->params[PARAM_ACCENT_SWITCH]);
}

int BuiltinAcidBass::getParameterCount() const { return kTotalParams; }

bool BuiltinAcidBass::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {
      "Waveform",  "Cutoff", "Resonance", "Env Mod", "Decay",        "Accent",
      "Overdrive", "Volume", "Transpose", "Slide",   "Accent Switch"};
  static const double defaults[] = {0.0, 0.3, 0.6, 0.5, 0.2, 0.0,
                                    0.1, 0.7, 0.5, 0.0, 0.0};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinAcidBass::setParameterValue(uint32_t id, double value) {
  if (id < kTotalParams) {
    impl_->params[id] = std::clamp(value, 0.0, 1.0);
  }
}

double BuiltinAcidBass::getParameterValue(uint32_t id) const {
  if (id < kTotalParams) {
    return impl_->params[id];
  }
  return 0.0;
}

const std::string& BuiltinAcidBass::getName() const { return kAcidBassName; }

const std::string& BuiltinAcidBass::getPath() const { return kAcidBassPath; }

int BuiltinAcidBass::getPluginIndex() const { return 0; }

bool BuiltinAcidBass::isInstrument() const { return true; }

}  // namespace hibiki
