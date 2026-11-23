#include "engine/effects/builtin_delay.hpp"

#include <algorithm>
#include <cmath>

namespace hibiki {

static const std::string kDelayName = "Delay";
static const std::string kDelayPath = "builtin://delay";

BuiltinDelay::BuiltinDelay() {
  params_[PARAM_TIME_L] = 0.6;  // 1/8 D when synced, ~250ms when free
  params_[PARAM_TIME_R] = 0.6;  // 1/8 D when synced, ~250ms when free
  params_[PARAM_FEEDBACK] = 0.4;
  params_[PARAM_MIX] = 0.3;
  params_[PARAM_HP_FREQ] = 0.15;   // ~80Hz
  params_[PARAM_LP_FREQ] = 0.75;   // ~12kHz
  params_[PARAM_PING_PONG] = 1.0;  // Ping-pong ON by default
  params_[PARAM_ENABLE] = 1.0;
  params_[PARAM_SYNC] = 1.0;      // Tempo sync ON by default
  params_[PARAM_SYNC_DIV] = 0.6;  // Legacy, unused when L/R are independent
  buffer_l_.resize(kMaxDelaySamples, 0.0f);
  buffer_r_.resize(kMaxDelaySamples, 0.0f);
  dsp_state_.reset();
}

bool BuiltinDelay::load(const std::string& /*path*/, int /*plugin_index*/,
                        double sample_rate) {
  sample_rate_ = sample_rate;
  reset();
  return true;
}

void BuiltinDelay::reset() {
  std::fill(buffer_l_.begin(), buffer_l_.end(), 0.0f);
  std::fill(buffer_r_.begin(), buffer_r_.end(), 0.0f);
  dsp_state_.reset();
}

// --- Parameter mapping ---

float BuiltinDelay::normToTimeMs(double norm) {
  return d_delay_norm_to_time_ms(norm);
}

float BuiltinDelay::normToHpFreq(double norm) {
  return d_delay_norm_to_hp_freq(norm);
}

float BuiltinDelay::normToLpFreq(double norm) {
  return d_delay_norm_to_lp_freq(norm);
}

float BuiltinDelay::normToDivisionBeats(double norm) {
  return d_delay_norm_to_division_beats(norm);
}

const char* BuiltinDelay::normToDivisionLabel(double norm) {
  return d_delay_norm_to_division_label(norm);
}

void BuiltinDelay::process(float** inputs, float** outputs, int num_samples,
                           const HostProcessContext& context,
                           const std::vector<MidiNoteEvent>& /*events*/,
                           float** /*sidechain*/) {
  d_delay_process(
      dsp_state_, buffer_l_.data(), buffer_r_.data(), (int)buffer_l_.size(),
      inputs[0], inputs[1], outputs[0], outputs[1], num_samples, sample_rate_,
      context.tempo, params_[PARAM_TIME_L], params_[PARAM_TIME_R],
      params_[PARAM_FEEDBACK], params_[PARAM_MIX], params_[PARAM_HP_FREQ],
      params_[PARAM_LP_FREQ], params_[PARAM_PING_PONG],
      params_[PARAM_SYNC] >= 0.5, enabled_);
}

float BuiltinDelay::getInputDb() const {
  if (dsp_state_.input_rms < 1e-6f) return -120.0f;
  return 20.0f * std::log10(dsp_state_.input_rms);
}

float BuiltinDelay::getOutputDb() const {
  if (dsp_state_.output_rms < 1e-6f) return -120.0f;
  return 20.0f * std::log10(dsp_state_.output_rms);
}

// --- IPlugin interface ---

int BuiltinDelay::getParameterCount() const { return kTotalParams; }

bool BuiltinDelay::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {
      "Time L",  "Time R",    "Feedback", "Mix",  "HP Freq",
      "LP Freq", "Ping Pong", "Enable",   "Sync", "Division",
  };
  static const double defaults[] = {0.6,  0.6, 0.4, 0.3, 0.15,
                                    0.75, 1.0, 1.0, 1.0, 0.6};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinDelay::setParameterValue(uint32_t id, double value) {
  if (id < kTotalParams) {
    params_[id] = value;
    if (id == PARAM_ENABLE) enabled_ = value >= 0.5;
  }
}

double BuiltinDelay::getParameterValue(uint32_t id) const {
  return id < kTotalParams ? params_[id] : 0.0;
}

const std::string& BuiltinDelay::getName() const { return kDelayName; }
const std::string& BuiltinDelay::getPath() const { return kDelayPath; }
int BuiltinDelay::getPluginIndex() const { return 0; }
bool BuiltinDelay::isInstrument() const { return false; }

}  // namespace hibiki
