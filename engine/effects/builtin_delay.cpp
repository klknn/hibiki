#include "engine/effects/builtin_delay.hpp"

#include <algorithm>
#include <cmath>

namespace hibiki {

static const std::string kDelayName = "Delay";
static const std::string kDelayPath = "builtin://delay";

BuiltinDelay::BuiltinDelay() {
  params_[PARAM_TIME_L] = 0.35;   // ~250ms
  params_[PARAM_TIME_R] = 0.35;   // ~250ms
  params_[PARAM_FEEDBACK] = 0.4;
  params_[PARAM_MIX] = 0.3;
  params_[PARAM_HP_FREQ] = 0.15;  // ~80Hz
  params_[PARAM_LP_FREQ] = 0.75;  // ~12kHz
  params_[PARAM_PING_PONG] = 0.0;
  params_[PARAM_ENABLE] = 1.0;
  buffer_l_.resize(kMaxDelaySamples, 0.0f);
  buffer_r_.resize(kMaxDelaySamples, 0.0f);
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
  write_pos_ = 0;
  hp_state_l_ = hp_state_r_ = 0;
  lp_state_l_ = lp_state_r_ = 0;
}

// --- Parameter mapping ---

float BuiltinDelay::normToTimeMs(double norm) {
  // 1ms to 2000ms, exponential
  return 1.0f * std::pow(2000.0f, (float)norm);
}

float BuiltinDelay::normToHpFreq(double norm) {
  // 20Hz to 2000Hz, exponential
  return 20.0f * std::pow(100.0f, (float)norm);
}

float BuiltinDelay::normToLpFreq(double norm) {
  // 1000Hz to 20000Hz, exponential
  return 1000.0f * std::pow(20.0f, (float)norm);
}

void BuiltinDelay::process(float** inputs, float** outputs, int num_samples,
                           const HostProcessContext& /*context*/,
                           const std::vector<MidiNoteEvent>& /*events*/) {
  if (!enabled_) {
    for (int i = 0; i < num_samples; ++i) {
      outputs[0][i] = inputs[0][i];
      outputs[1][i] = inputs[1][i];
    }
    return;
  }

  float time_l_ms = normToTimeMs(params_[PARAM_TIME_L]);
  float time_r_ms = normToTimeMs(params_[PARAM_TIME_R]);
  int delay_l = std::clamp((int)(time_l_ms * 0.001f * (float)sample_rate_),
                           1, kMaxDelaySamples - 1);
  int delay_r = std::clamp((int)(time_r_ms * 0.001f * (float)sample_rate_),
                           1, kMaxDelaySamples - 1);

  float feedback = std::clamp((float)params_[PARAM_FEEDBACK], 0.0f, 0.95f);
  float mix = std::clamp((float)params_[PARAM_MIX], 0.0f, 1.0f);
  bool ping_pong = params_[PARAM_PING_PONG] >= 0.5;

  // 1-pole filter coefficients
  float hp_freq = normToHpFreq(params_[PARAM_HP_FREQ]);
  float lp_freq = normToLpFreq(params_[PARAM_LP_FREQ]);
  float hp_coeff = 1.0f - std::exp(-2.0f * 3.14159f * hp_freq / (float)sample_rate_);
  float lp_coeff = 1.0f - std::exp(-2.0f * 3.14159f * lp_freq / (float)sample_rate_);

  int buf_size = (int)buffer_l_.size();

  for (int i = 0; i < num_samples; ++i) {
    float in_l = inputs[0][i];
    float in_r = inputs[1][i];

    // Read from delay buffers
    int read_l = (write_pos_ - delay_l + buf_size) % buf_size;
    int read_r = (write_pos_ - delay_r + buf_size) % buf_size;
    float del_l = buffer_l_[read_l];
    float del_r = buffer_r_[read_r];

    // Apply feedback filters (HP then LP)
    // HP filter (subtract DC component)
    hp_state_l_ += hp_coeff * (del_l - hp_state_l_);
    float filtered_l = del_l - hp_state_l_;
    hp_state_r_ += hp_coeff * (del_r - hp_state_r_);
    float filtered_r = del_r - hp_state_r_;

    // LP filter
    lp_state_l_ += lp_coeff * (filtered_l - lp_state_l_);
    filtered_l = lp_state_l_;
    lp_state_r_ += lp_coeff * (filtered_r - lp_state_r_);
    filtered_r = lp_state_r_;

    // Write to delay buffers with feedback
    if (ping_pong) {
      // Cross-feed: L input → R buffer, R input → L buffer
      buffer_l_[write_pos_] = in_l + filtered_r * feedback;
      buffer_r_[write_pos_] = in_r + filtered_l * feedback;
    } else {
      buffer_l_[write_pos_] = in_l + filtered_l * feedback;
      buffer_r_[write_pos_] = in_r + filtered_r * feedback;
    }

    // Mix dry + wet
    outputs[0][i] = in_l * (1.0f - mix) + del_l * mix;
    outputs[1][i] = in_r * (1.0f - mix) + del_r * mix;

    write_pos_ = (write_pos_ + 1) % buf_size;
  }
}

// --- IPlugin interface ---

int BuiltinDelay::getParameterCount() const { return kTotalParams; }

bool BuiltinDelay::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {
      "Time L", "Time R", "Feedback", "Mix",
      "HP Freq", "LP Freq", "Ping-Pong", "Enable"};
  static const double defaults[] = {0.35, 0.35, 0.4, 0.3, 0.15, 0.75, 0.0, 1.0};
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
