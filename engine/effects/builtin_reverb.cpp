#include "engine/effects/builtin_reverb.hpp"

#include <algorithm>
#include <cmath>

namespace hibiki {

static const std::string kReverbName = "Reverb";
static const std::string kReverbPath = "builtin://reverb";

// Freeverb comb filter lengths (tuned for 44100 Hz, scaled at load)
static constexpr int kCombLengths[8] = {1116, 1188, 1277, 1356,
                                        1422, 1491, 1557, 1617};
// Stereo spread: offset right channel
static constexpr int kStereoSpread = 23;
// Allpass lengths
static constexpr int kAllpassLengths[4] = {556, 441, 341, 225};
static constexpr float kAllpassFeedback = 0.5f;

static constexpr float kRoomScaleMin = 0.7f;
static constexpr float kRoomScaleRange = 0.28f;
static constexpr float kDampScaleMax = 0.4f;
static constexpr float kFixedGain = 0.015f;

BuiltinReverb::BuiltinReverb() {
  params_[PARAM_ROOM_SIZE] = 0.5;
  params_[PARAM_DAMPING] = 0.5;
  params_[PARAM_MIX] = 0.3;
  params_[PARAM_PRE_DELAY] = 0.1;  // ~10ms
  params_[PARAM_HP_FREQ] = 0.15;   // ~40Hz
  params_[PARAM_LP_FREQ] = 0.85;   // ~16kHz
  params_[PARAM_WIDTH] = 1.0;
  params_[PARAM_ENABLE] = 1.0;
}

bool BuiltinReverb::load(const std::string& /*path*/, int /*plugin_index*/,
                         double sample_rate) {
  sample_rate_ = sample_rate;
  initBuffers();
  reset();
  return true;
}

void BuiltinReverb::initBuffers() {
  double scale = sample_rate_ / 44100.0;
  for (int i = 0; i < kNumCombs; ++i) {
    int len_l = (int)(kCombLengths[i] * scale);
    int len_r = (int)((kCombLengths[i] + kStereoSpread) * scale);
    combs_l_[i].buffer.resize(len_l, 0.0f);
    combs_l_[i].size = len_l;
    combs_r_[i].buffer.resize(len_r, 0.0f);
    combs_r_[i].size = len_r;
  }
  for (int i = 0; i < kNumAllpasses; ++i) {
    int len_l = (int)(kAllpassLengths[i] * scale);
    int len_r = (int)((kAllpassLengths[i] + kStereoSpread) * scale);
    allpasses_l_[i].buffer.resize(len_l, 0.0f);
    allpasses_l_[i].size = len_l;
    allpasses_r_[i].buffer.resize(len_r, 0.0f);
    allpasses_r_[i].size = len_r;
  }
  int max_pre = (int)(0.1 * sample_rate_);  // 100ms max
  predelay_l_.resize(max_pre + 1, 0.0f);
  predelay_r_.resize(max_pre + 1, 0.0f);
}

void BuiltinReverb::reset() {
  for (int i = 0; i < kNumCombs; ++i) {
    std::fill(combs_l_[i].buffer.begin(), combs_l_[i].buffer.end(), 0.0f);
    combs_l_[i].pos = 0;
    combs_l_[i].filter_store = 0;
    std::fill(combs_r_[i].buffer.begin(), combs_r_[i].buffer.end(), 0.0f);
    combs_r_[i].pos = 0;
    combs_r_[i].filter_store = 0;
  }
  for (int i = 0; i < kNumAllpasses; ++i) {
    std::fill(allpasses_l_[i].buffer.begin(), allpasses_l_[i].buffer.end(),
              0.0f);
    allpasses_l_[i].pos = 0;
    std::fill(allpasses_r_[i].buffer.begin(), allpasses_r_[i].buffer.end(),
              0.0f);
    allpasses_r_[i].pos = 0;
  }
  std::fill(predelay_l_.begin(), predelay_l_.end(), 0.0f);
  std::fill(predelay_r_.begin(), predelay_r_.end(), 0.0f);
  predelay_pos_ = 0;
  hp_state_l_ = hp_state_r_ = 0;
  lp_state_l_ = lp_state_r_ = 0;
}

void BuiltinReverb::updateParams() {
  float room = (float)params_[PARAM_ROOM_SIZE];
  float damp = (float)params_[PARAM_DAMPING];

  float feedback = kRoomScaleMin + room * kRoomScaleRange;
  float damp1 = damp * kDampScaleMax;
  float damp2 = 1.0f - damp1;

  for (int i = 0; i < kNumCombs; ++i) {
    combs_l_[i].feedback = feedback;
    combs_l_[i].damp1 = damp1;
    combs_l_[i].damp2 = damp2;
    combs_r_[i].feedback = feedback;
    combs_r_[i].damp1 = damp1;
    combs_r_[i].damp2 = damp2;
  }
}

float BuiltinReverb::processComb(CombFilter& c, float input) {
  float output = c.buffer[c.pos];
  c.filter_store = output * c.damp2 + c.filter_store * c.damp1;
  c.buffer[c.pos] = input + c.filter_store * c.feedback;
  if (++c.pos >= c.size) c.pos = 0;
  return output;
}

float BuiltinReverb::processAllpass(AllpassFilter& a, float input) {
  float buffered = a.buffer[a.pos];
  float output = -input + buffered;
  a.buffer[a.pos] = input + buffered * kAllpassFeedback;
  if (++a.pos >= a.size) a.pos = 0;
  return output;
}

float BuiltinReverb::normToPreDelayMs(double norm) {
  return (float)(norm * 100.0);  // 0-100ms
}

void BuiltinReverb::process(float** inputs, float** outputs, int num_samples,
                            const HostProcessContext& /*context*/,
                            const std::vector<MidiNoteEvent>& /*events*/) {
  if (!enabled_) {
    for (int i = 0; i < num_samples; ++i) {
      outputs[0][i] = inputs[0][i];
      outputs[1][i] = inputs[1][i];
    }
    return;
  }

  updateParams();

  float mix = std::clamp((float)params_[PARAM_MIX], 0.0f, 1.0f);
  float width = std::clamp((float)params_[PARAM_WIDTH], 0.0f, 1.0f);
  float wet1 = width * 0.5f + 0.5f;
  float wet2 = (1.0f - width) * 0.5f;

  float pre_delay_ms = normToPreDelayMs(params_[PARAM_PRE_DELAY]);
  int pre_delay_samples =
      std::clamp((int)(pre_delay_ms * 0.001f * (float)sample_rate_), 0,
                 (int)predelay_l_.size() - 1);

  // Filter coefficients
  float hp_freq =
      20.0f * std::pow(25.0f, (float)params_[PARAM_HP_FREQ]);  // 20-500Hz
  float lp_freq =
      2000.0f * std::pow(10.0f, (float)params_[PARAM_LP_FREQ]);  // 2k-20kHz
  float hp_coeff =
      1.0f - std::exp(-2.0f * 3.14159f * hp_freq / (float)sample_rate_);
  float lp_coeff =
      1.0f - std::exp(-2.0f * 3.14159f * lp_freq / (float)sample_rate_);

  int pd_size = (int)predelay_l_.size();

  for (int i = 0; i < num_samples; ++i) {
    float in_l = inputs[0][i];
    float in_r = inputs[1][i];

    // Write to pre-delay
    predelay_l_[predelay_pos_] = in_l;
    predelay_r_[predelay_pos_] = in_r;
    int pd_read = (predelay_pos_ - pre_delay_samples + pd_size) % pd_size;
    float pd_l = predelay_l_[pd_read];
    float pd_r = predelay_r_[pd_read];
    predelay_pos_ = (predelay_pos_ + 1) % pd_size;

    float input_mixed = (pd_l + pd_r) * kFixedGain;

    // Parallel comb filters
    float out_l = 0, out_r = 0;
    for (int j = 0; j < kNumCombs; ++j) {
      out_l += processComb(combs_l_[j], input_mixed);
      out_r += processComb(combs_r_[j], input_mixed);
    }

    // Series allpass filters
    for (int j = 0; j < kNumAllpasses; ++j) {
      out_l = processAllpass(allpasses_l_[j], out_l);
      out_r = processAllpass(allpasses_r_[j], out_r);
    }

    // Width processing (mid/side)
    float wet_l = out_l * wet1 + out_r * wet2;
    float wet_r = out_r * wet1 + out_l * wet2;

    // HP filter on wet signal
    hp_state_l_ += hp_coeff * (wet_l - hp_state_l_);
    wet_l -= hp_state_l_;
    hp_state_r_ += hp_coeff * (wet_r - hp_state_r_);
    wet_r -= hp_state_r_;

    // LP filter on wet signal
    lp_state_l_ += lp_coeff * (wet_l - lp_state_l_);
    wet_l = lp_state_l_;
    lp_state_r_ += lp_coeff * (wet_r - lp_state_r_);
    wet_r = lp_state_r_;

    // Mix
    outputs[0][i] = in_l * (1.0f - mix) + wet_l * mix;
    outputs[1][i] = in_r * (1.0f - mix) + wet_r * mix;
  }
}

// --- IPlugin interface ---

int BuiltinReverb::getParameterCount() const { return kTotalParams; }

bool BuiltinReverb::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  static const char* names[] = {"Room Size", "Damping", "Mix",   "Pre-Delay",
                                "HP Freq",   "LP Freq", "Width", "Enable"};
  static const double defaults[] = {0.5, 0.5, 0.3, 0.1, 0.15, 0.85, 1.0, 1.0};
  info.id = index;
  info.name = names[index];
  info.defaultValue = defaults[index];
  return true;
}

void BuiltinReverb::setParameterValue(uint32_t id, double value) {
  if (id < kTotalParams) {
    params_[id] = value;
    if (id == PARAM_ENABLE) enabled_ = value >= 0.5;
  }
}

double BuiltinReverb::getParameterValue(uint32_t id) const {
  return id < kTotalParams ? params_[id] : 0.0;
}

const std::string& BuiltinReverb::getName() const { return kReverbName; }
const std::string& BuiltinReverb::getPath() const { return kReverbPath; }
int BuiltinReverb::getPluginIndex() const { return 0; }
bool BuiltinReverb::isInstrument() const { return false; }

}  // namespace hibiki
