#include "engine/effects/builtin_phaser.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace hibiki {

static const std::string kPhaserName = "Phaser";
static const std::string kPhaserPath = "builtin://phaser";

BuiltinPhaser::BuiltinPhaser() {
  params_[PARAM_RATE] = 0.3;  // ~1Hz
  params_[PARAM_DEPTH] = 0.7;
  params_[PARAM_FEEDBACK] = 0.4;
  params_[PARAM_STAGES] = 0.33;  // ~4 stages
  params_[PARAM_MIX] = 0.5;
  params_[PARAM_STEREO] = 0.5;     // 90° offset
  params_[PARAM_MODE] = 0.0;       // Phaser
  params_[PARAM_LFO_SHAPE] = 0.0;  // Sine
  params_[PARAM_CENTER] = 0.5;
  params_[PARAM_SPREAD] = 0.5;
  params_[PARAM_SYNC] = 0.0;
  params_[PARAM_ENABLE] = 1.0;
  comb_buf_l_.resize(kMaxCombDelay, 0.0f);
  comb_buf_r_.resize(kMaxCombDelay, 0.0f);
}

bool BuiltinPhaser::load(const std::string& /*path*/, int /*plugin_index*/,
                         double sample_rate) {
  sample_rate_ = sample_rate;
  reset();
  return true;
}

void BuiltinPhaser::reset() {
  std::memset(ap_state_l_, 0, sizeof(ap_state_l_));
  std::memset(ap_state_r_, 0, sizeof(ap_state_r_));
  feedback_l_ = feedback_r_ = 0.0f;
  lfo_phase_ = 0.0;
  comb_write_ = 0;
  std::fill(comb_buf_l_.begin(), comb_buf_l_.end(), 0.0f);
  std::fill(comb_buf_r_.begin(), comb_buf_r_.end(), 0.0f);
  scope_write_ = 0;
  scope_l_.fill(0.0f);
  scope_r_.fill(0.0f);
}

// --- Parameter mapping ---

float BuiltinPhaser::normToRate(double norm) {
  // 0.01Hz to 20Hz, exponential
  return 0.01f * std::pow(2000.0f, (float)norm);
}

int BuiltinPhaser::normToStages(double norm) {
  // Maps to 2,4,6,8,10,12
  int s = (int)(norm * 5.0 + 0.5);  // 0..5
  return std::clamp(s * 2 + 2, 2, kMaxStages);
}

int BuiltinPhaser::normToMode(double norm) {
  return std::clamp((int)(norm * 4.0 + 0.5), 0, 4);
}

// --- LFO ---

float BuiltinPhaser::lfo(double phase, int shape) const {
  // phase ∈ [0, 1), output ∈ [-1, 1]
  float p = (float)std::fmod(phase, 1.0);
  if (p < 0) p += 1.0f;

  switch (shape) {
    case 0:  // Sine
      return std::sin(p * 2.0f * 3.14159265f);
    case 1:  // Triangle
      return (p < 0.5f) ? (4.0f * p - 1.0f) : (3.0f - 4.0f * p);
    case 2:  // Saw
      return 2.0f * p - 1.0f;
    case 3:  // Square
      return (p < 0.5f) ? 1.0f : -1.0f;
    default:
      return std::sin(p * 2.0f * 3.14159265f);
  }
}

// --- First-order allpass ---

float BuiltinPhaser::allpass(float input, float coeff, float& state) {
  float output = coeff * input + state;
  state = input - coeff * output;
  return output;
}

void BuiltinPhaser::process(float** inputs, float** outputs, int num_samples,
                            const HostProcessContext& context,
                            const std::vector<MidiNoteEvent>& /*events*/,
                            float** /*sidechain*/) {
  if (!enabled_) {
    for (int i = 0; i < num_samples; ++i) {
      outputs[0][i] = inputs[0][i];
      outputs[1][i] = inputs[1][i];
    }
    return;
  }

  float mix = std::clamp((float)params_[PARAM_MIX], 0.0f, 1.0f);
  float depth = std::clamp((float)params_[PARAM_DEPTH], 0.0f, 1.0f);
  float fb =
      std::clamp((float)params_[PARAM_FEEDBACK] * 2.0f - 1.0f, -0.95f, 0.95f);
  int stages = normToStages(params_[PARAM_STAGES]);
  float stereo_offset = (float)params_[PARAM_STEREO];  // 0–1 → 0–360°
  int mode = normToMode(params_[PARAM_MODE]);
  int lfo_shape = std::clamp((int)(params_[PARAM_LFO_SHAPE] * 3.0 + 0.5), 0, 3);
  float center = (float)params_[PARAM_CENTER];
  float spread = (float)params_[PARAM_SPREAD];
  bool sync = params_[PARAM_SYNC] >= 0.5;

  // LFO rate
  float rate_hz = normToRate(params_[PARAM_RATE]);
  double lfo_inc;
  if (sync && context.tempo > 0) {
    // Tempo sync: rate maps to beat divisions
    // At norm=0.5 → 1/4 note, scale accordingly
    float beats = 0.25f * std::pow(64.0f, (float)params_[PARAM_RATE]);
    double freq = context.tempo / (60.0 * beats);
    lfo_inc = freq / sample_rate_;
  } else {
    lfo_inc = rate_hz / sample_rate_;
  }

  // Center frequency range: 200Hz to 8000Hz
  float center_hz = 200.0f * std::pow(40.0f, center);

  float in_sum_sq = 0, out_sum_sq = 0;

  for (int i = 0; i < num_samples; ++i) {
    float in_l = inputs[0][i];
    float in_r = inputs[1][i];

    // LFO values for L and R (stereo offset)
    float lfo_l = lfo(lfo_phase_, lfo_shape);
    float lfo_r = lfo(lfo_phase_ + stereo_offset, lfo_shape);

    // Scale LFO by depth
    float mod_l = lfo_l * depth;
    float mod_r = lfo_r * depth;

    float wet_l = 0, wet_r = 0;

    switch (mode) {
      case MODE_PHASER: {
        // Cascade allpass filters with modulated coefficients
        float sig_l = in_l + feedback_l_ * fb;
        float sig_r = in_r + feedback_r_ * fb;

        for (int s = 0; s < stages; ++s) {
          // Distribute stage frequencies logarithmically around center
          float stage_ratio = (float)s / std::max(1, stages - 1);  // 0..1
          float freq =
              center_hz * std::pow(2.0f, (stage_ratio - 0.5f) * spread * 4.0f);
          // Modulate frequency
          float mod_freq_l = freq * std::pow(2.0f, mod_l * 2.0f);
          float mod_freq_r = freq * std::pow(2.0f, mod_r * 2.0f);
          // Convert to allpass coefficient
          float coeff_l =
              (std::tan(3.14159f * mod_freq_l / (float)sample_rate_) - 1.0f) /
              (std::tan(3.14159f * mod_freq_l / (float)sample_rate_) + 1.0f);
          float coeff_r =
              (std::tan(3.14159f * mod_freq_r / (float)sample_rate_) - 1.0f) /
              (std::tan(3.14159f * mod_freq_r / (float)sample_rate_) + 1.0f);
          sig_l = allpass(sig_l, coeff_l, ap_state_l_[s]);
          sig_r = allpass(sig_r, coeff_r, ap_state_r_[s]);
        }
        feedback_l_ = sig_l;
        feedback_r_ = sig_r;
        wet_l = sig_l;
        wet_r = sig_r;
        break;
      }

      case MODE_CHORUS:
      case MODE_FLANGER: {
        // Delay-based modulation
        float max_delay =
            (mode == MODE_CHORUS) ? 0.025f : 0.005f;  // 25ms / 5ms
        float delay_l = max_delay * (0.5f + 0.5f * mod_l) * (float)sample_rate_;
        float delay_r = max_delay * (0.5f + 0.5f * mod_r) * (float)sample_rate_;
        int buf_size = (int)comb_buf_l_.size();

        // Write to comb buffer with feedback
        comb_buf_l_[comb_write_] = in_l + feedback_l_ * fb;
        comb_buf_r_[comb_write_] = in_r + feedback_r_ * fb;

        // Read with fractional delay (linear interpolation)
        float read_l = (float)comb_write_ - delay_l;
        float read_r = (float)comb_write_ - delay_r;
        if (read_l < 0) read_l += buf_size;
        if (read_r < 0) read_r += buf_size;
        int idx_l = (int)read_l;
        int idx_r = (int)read_r;
        float frac_l = read_l - idx_l;
        float frac_r = read_r - idx_r;
        idx_l = idx_l % buf_size;
        idx_r = idx_r % buf_size;
        int next_l = (idx_l + 1) % buf_size;
        int next_r = (idx_r + 1) % buf_size;

        wet_l =
            comb_buf_l_[idx_l] * (1.0f - frac_l) + comb_buf_l_[next_l] * frac_l;
        wet_r =
            comb_buf_r_[idx_r] * (1.0f - frac_r) + comb_buf_r_[next_r] * frac_r;
        feedback_l_ = wet_l;
        feedback_r_ = wet_r;
        comb_write_ = (comb_write_ + 1) % buf_size;
        break;
      }

      case MODE_RINGMOD: {
        // Ring modulation: multiply signal with LFO
        wet_l = in_l * (0.5f + 0.5f * mod_l);
        wet_r = in_r * (0.5f + 0.5f * mod_r);
        break;
      }

      case MODE_DISPERSER: {
        // Fixed allpass cascade (no LFO modulation on freq, just fixed
        // phase rotation). The depth controls how much dispersion is applied.
        float sig_l = in_l;
        float sig_r = in_r;
        for (int s = 0; s < stages; ++s) {
          float stage_ratio = (float)s / std::max(1, stages - 1);
          float freq =
              center_hz * std::pow(2.0f, (stage_ratio - 0.5f) * spread * 4.0f);
          float coeff =
              (std::tan(3.14159f * freq / (float)sample_rate_) - 1.0f) /
              (std::tan(3.14159f * freq / (float)sample_rate_) + 1.0f);
          sig_l = allpass(sig_l, coeff, ap_state_l_[s]);
          sig_r = allpass(sig_r, coeff, ap_state_r_[s]);
        }
        // Blend between dry and dispersed based on depth
        wet_l = in_l * (1.0f - depth) + sig_l * depth;
        wet_r = in_r * (1.0f - depth) + sig_r * depth;
        break;
      }
    }

    // Mix dry + wet
    float out_l = in_l * (1.0f - mix) + wet_l * mix;
    float out_r = in_r * (1.0f - mix) + wet_r * mix;
    outputs[0][i] = out_l;
    outputs[1][i] = out_r;

    in_sum_sq += in_l * in_l + in_r * in_r;
    out_sum_sq += out_l * out_l + out_r * out_r;

    // Write to scope buffer
    scope_l_[scope_write_] = out_l;
    scope_r_[scope_write_] = out_r;
    scope_write_ = (scope_write_ + 1) % kScopeSize;

    lfo_phase_ += lfo_inc;
    if (lfo_phase_ >= 1.0) lfo_phase_ -= 1.0;
  }

  // Smoothed RMS metering
  float in_rms = std::sqrt(in_sum_sq / (2.0f * num_samples));
  float out_rms = std::sqrt(out_sum_sq / (2.0f * num_samples));
  constexpr float kSmooth = 0.15f;
  input_rms_ = input_rms_.load() * (1.0f - kSmooth) + in_rms * kSmooth;
  output_rms_ = output_rms_.load() * (1.0f - kSmooth) + out_rms * kSmooth;
}

// --- IPlugin interface ---

int BuiltinPhaser::getParameterCount() const { return kTotalParams; }

bool BuiltinPhaser::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {"Rate",   "Depth",  "Feedback", "Stages",
                                "Mix",    "Stereo", "Mode",     "LFO Shape",
                                "Center", "Spread", "Sync",     "Enable"};
  static const double defaults[] = {0.3, 0.7, 0.4, 0.33, 0.5, 0.5,
                                    0.0, 0.0, 0.5, 0.5,  0.0, 1.0};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinPhaser::setParameterValue(uint32_t id, double value) {
  if (id < (uint32_t)kTotalParams) {
    params_[id] = value;
    if (id == PARAM_ENABLE) enabled_ = value >= 0.5;
  }
}

double BuiltinPhaser::getParameterValue(uint32_t id) const {
  return id < (uint32_t)kTotalParams ? params_[id] : 0.0;
}

const std::string& BuiltinPhaser::getName() const { return kPhaserName; }
const std::string& BuiltinPhaser::getPath() const { return kPhaserPath; }
int BuiltinPhaser::getPluginIndex() const { return 0; }
bool BuiltinPhaser::isInstrument() const { return false; }

float BuiltinPhaser::getInputDb() const {
  float rms = input_rms_.load();
  return rms > 0 ? 20.0f * std::log10(rms) : -100.0f;
}

float BuiltinPhaser::getOutputDb() const {
  float rms = output_rms_.load();
  return rms > 0 ? 20.0f * std::log10(rms) : -100.0f;
}

void BuiltinPhaser::getScopeData(float* left, float* right, int size) const {
  if (size > kScopeSize) size = kScopeSize;
  // Copy from ring buffer, oldest first
  int read_pos = (scope_write_ - size + kScopeSize) % kScopeSize;
  for (int i = 0; i < size; ++i) {
    int idx = (read_pos + i) % kScopeSize;
    left[i] = scope_l_[idx];
    right[i] = scope_r_[idx];
  }
}

}  // namespace hibiki
