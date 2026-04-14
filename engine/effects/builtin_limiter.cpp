#include "engine/effects/builtin_limiter.hpp"

#include <algorithm>
#include <cmath>

namespace hibiki {

static const std::string kLimiterName = "Limiter";
static const std::string kLimiterPath = "builtin://limiter";

BuiltinLimiter::BuiltinLimiter() {
  params_[PARAM_CEILING] = 0.975;     // -0.3 dB
  params_[PARAM_RELEASE] = 0.3;       // ~100ms
  params_[PARAM_LOOKAHEAD] = 0.2;     // ~1ms
  params_[PARAM_GAIN] = 0.0;          // 0 dB
  params_[PARAM_LINK_STEREO] = 1.0;
  params_[PARAM_ENABLE] = 1.0;
  lookahead_l_.resize(kMaxLookahead, 0.0f);
  lookahead_r_.resize(kMaxLookahead, 0.0f);
}

bool BuiltinLimiter::load(const std::string& /*path*/, int /*plugin_index*/,
                          double sample_rate) {
  sample_rate_ = sample_rate;
  reset();
  return true;
}

void BuiltinLimiter::reset() {
  std::fill(lookahead_l_.begin(), lookahead_l_.end(), 0.0f);
  std::fill(lookahead_r_.begin(), lookahead_r_.end(), 0.0f);
  la_write_ = 0;
  envelope_ = 1.0f;
  gain_reduction_db_.store(0.0f);
}

// --- Parameter mapping ---

float BuiltinLimiter::normToCeilingDb(double norm) {
  // 0 → -12 dB, 1 → 0 dB
  return (float)(norm * 12.0 - 12.0);
}

float BuiltinLimiter::normToReleaseMs(double norm) {
  // 10ms to 1000ms, exponential
  return 10.0f * std::pow(100.0f, (float)norm);
}

float BuiltinLimiter::normToLookaheadMs(double norm) {
  // 0.1ms to 5ms, exponential
  return 0.1f * std::pow(50.0f, (float)norm);
}

float BuiltinLimiter::normToGainDb(double norm) {
  // 0 to 24 dB
  return (float)(norm * 24.0);
}

void BuiltinLimiter::process(float** inputs, float** outputs, int num_samples,
                             const HostProcessContext& /*context*/,
                             const std::vector<MidiNoteEvent>& /*events*/) {
  if (!enabled_) {
    for (int i = 0; i < num_samples; ++i) {
      outputs[0][i] = inputs[0][i];
      outputs[1][i] = inputs[1][i];
    }
    return;
  }

  float ceiling_db = normToCeilingDb(params_[PARAM_CEILING]);
  float ceiling_lin = std::pow(10.0f, ceiling_db / 20.0f);
  float release_ms = normToReleaseMs(params_[PARAM_RELEASE]);
  float la_ms = normToLookaheadMs(params_[PARAM_LOOKAHEAD]);
  float gain_db = normToGainDb(params_[PARAM_GAIN]);
  float gain_lin = std::pow(10.0f, gain_db / 20.0f);
  bool link_stereo = params_[PARAM_LINK_STEREO] >= 0.5;

  int la_samples = std::clamp(
      (int)(la_ms * 0.001f * (float)sample_rate_), 1, kMaxLookahead - 1);

  // Release coefficient: smooth envelope release
  float release_coeff = std::exp(-1.0f / (release_ms * 0.001f * (float)sample_rate_));

  int buf_size = (int)lookahead_l_.size();
  float max_gr_db = 0.0f;

  for (int i = 0; i < num_samples; ++i) {
    // Apply input gain
    float in_l = inputs[0][i] * gain_lin;
    float in_r = inputs[1][i] * gain_lin;

    // Write to lookahead buffer
    lookahead_l_[la_write_] = in_l;
    lookahead_r_[la_write_] = in_r;

    // Peak detection on current input
    float peak;
    if (link_stereo) {
      peak = std::max(std::abs(in_l), std::abs(in_r));
    } else {
      // For unlinked, we'd need separate envelopes. Simplified to max for now.
      peak = std::max(std::abs(in_l), std::abs(in_r));
    }

    // Target gain
    float target = (peak > ceiling_lin) ? ceiling_lin / peak : 1.0f;

    // Envelope: instant attack, smooth release
    if (target < envelope_) {
      envelope_ = target;  // instant attack
    } else {
      envelope_ = envelope_ * release_coeff + target * (1.0f - release_coeff);
    }

    // Read from lookahead (delayed signal)
    int la_read = (la_write_ - la_samples + buf_size) % buf_size;
    float del_l = lookahead_l_[la_read];
    float del_r = lookahead_r_[la_read];

    // Apply gain reduction
    outputs[0][i] = del_l * envelope_;
    outputs[1][i] = del_r * envelope_;

    la_write_ = (la_write_ + 1) % buf_size;

    // Track max GR for metering
    float gr_db = 20.0f * std::log10(std::max(envelope_, 1e-10f));
    if (gr_db < max_gr_db) max_gr_db = gr_db;
  }

  gain_reduction_db_.store(max_gr_db);
}

float BuiltinLimiter::getGainReductionDb() const {
  return gain_reduction_db_.load();
}

// --- IPlugin interface ---

int BuiltinLimiter::getParameterCount() const { return kTotalParams; }

bool BuiltinLimiter::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {
      "Ceiling", "Release", "Lookahead", "Gain", "Link Stereo", "Enable"};
  static const double defaults[] = {0.975, 0.3, 0.2, 0.0, 1.0, 1.0};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinLimiter::setParameterValue(uint32_t id, double value) {
  if (id < kTotalParams) {
    params_[id] = value;
    if (id == PARAM_ENABLE) enabled_ = value >= 0.5;
  }
}

double BuiltinLimiter::getParameterValue(uint32_t id) const {
  return id < kTotalParams ? params_[id] : 0.0;
}

const std::string& BuiltinLimiter::getName() const { return kLimiterName; }
const std::string& BuiltinLimiter::getPath() const { return kLimiterPath; }
int BuiltinLimiter::getPluginIndex() const { return 0; }
bool BuiltinLimiter::isInstrument() const { return false; }

}  // namespace hibiki
