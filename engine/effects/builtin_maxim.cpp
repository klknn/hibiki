#include "engine/effects/builtin_maxim.hpp"

#include <algorithm>
#include <cmath>

namespace hibiki {

static const std::string kMaximName = "Maxim";
static const std::string kMaximPath = "builtin://maxim";

// --- BandProcessor Definition & Implementation ---

struct BuiltinMaxim::BandProcessor {
  double sample_rate = 44100.0;
  float envelope_db = 0.0f;
  float limiter_envelope = 1.0f;
  std::vector<float> la_l, la_r;
  int la_write = 0;
  float max_gr_db = 0.0f;
  float peak_in_db = -200.0f;
  float peak_out_db = -200.0f;

  void init(double sample_rate) {
    this->sample_rate = sample_rate;
    la_l.resize(kMaxLookahead, 0.0f);
    la_r.resize(kMaxLookahead, 0.0f);
    reset();
  }

  void reset() {
    envelope_db = 0.0f;
    limiter_envelope = 1.0f;
    std::fill(la_l.begin(), la_l.end(), 0.0f);
    std::fill(la_r.begin(), la_r.end(), 0.0f);
    la_write = 0;
    max_gr_db = 0.0f;
    peak_in_db = -200.0f;
    peak_out_db = -200.0f;
  }

  void process(float* in_l, float* in_r, float* out_l, float* out_r,
               int num_samples, float pre_gain_db, float post_gain_db,
               float sat_amount, float sat_threshold_db, float thresh_db,
               float ratio, float knee_db, float attack_ms, float release_ms,
               float ceiling_db, float lookahead_ms);

  static float saturate(float x, float threshold, float amount);
  static float computeGainReduction(float input_db, float threshold,
                                    float ratio, float knee_db);
};

void BuiltinMaxim::BandProcessor::process(
    float* in_l, float* in_r, float* out_l, float* out_r, int num_samples,
    float pre_gain_db, float post_gain_db, float sat_amount,
    float sat_threshold_db, float thresh_db, float ratio, float knee_db,
    float attack_ms, float release_ms, float ceiling_db, float lookahead_ms) {
  float pre_gain_lin = std::pow(10.0f, pre_gain_db / 20.0f);
  float post_gain_lin = std::pow(10.0f, post_gain_db / 20.0f);
  float sat_thresh_lin = std::pow(10.0f, sat_threshold_db / 20.0f);
  float ceiling_lin = std::pow(10.0f, ceiling_db / 20.0f);

  float att_coeff =
      std::exp(-1.0f / (attack_ms * 0.001f * (float)sample_rate));
  float rel_coeff =
      std::exp(-1.0f / (release_ms * 0.001f * (float)sample_rate));

  // Faster release for limiter to avoid prolonged pumping
  float lim_release_ms = std::max(5.0f, release_ms * 0.5f);
  float lim_release_coeff =
      std::exp(-1.0f / (lim_release_ms * 0.001f * (float)sample_rate));

  int la_samples = std::clamp(
      (int)(lookahead_ms * 0.001f * (float)sample_rate), 0, kMaxLookahead - 1);
  int buf_size = (int)la_l.size();

  float current_max_gr = 0.0f;
  float current_peak_in = -200.0f;
  float current_peak_out = -200.0f;

  for (int i = 0; i < num_samples; ++i) {
    // 1. Pre-Gain
    float x_l = in_l[i] * pre_gain_lin;
    float x_r = in_r[i] * pre_gain_lin;

    float in_abs = std::max(std::abs(x_l), std::abs(x_r));
    float in_db = (in_abs > 1e-10f) ? 20.0f * std::log10(in_abs) : -200.0f;
    if (in_db > current_peak_in) current_peak_in = in_db;

    // 2. Saturation
    x_l = saturate(x_l, sat_thresh_lin, sat_amount);
    x_r = saturate(x_r, sat_thresh_lin, sat_amount);

    // 3. Compressor
    float comp_in_abs = std::max(std::abs(x_l), std::abs(x_r));
    float comp_in_db =
        (comp_in_abs > 1e-10f) ? 20.0f * std::log10(comp_in_abs) : -200.0f;

    float gr_db = computeGainReduction(comp_in_db, thresh_db, ratio, knee_db);
    if (gr_db < envelope_db) {
      envelope_db = att_coeff * envelope_db + (1.0f - att_coeff) * gr_db;
    } else {
      envelope_db = rel_coeff * envelope_db + (1.0f - rel_coeff) * gr_db;
    }

    float comp_gain = std::pow(10.0f, envelope_db / 20.0f);
    x_l *= comp_gain;
    x_r *= comp_gain;

    // 4. Lookahead Ceiling Limiter
    la_l[la_write] = x_l;
    la_r[la_write] = x_r;

    float peak = std::max(std::abs(x_l), std::abs(x_r));
    float target = (peak > ceiling_lin) ? ceiling_lin / peak : 1.0f;

    if (target < limiter_envelope) {
      limiter_envelope = target;
    } else {
      limiter_envelope = limiter_envelope * lim_release_coeff +
                         target * (1.0f - lim_release_coeff);
    }

    int la_read = (la_write - la_samples + buf_size) % buf_size;
    float out_sample_l = la_l[la_read] * limiter_envelope;
    float out_sample_r = la_r[la_read] * limiter_envelope;

    la_write = (la_write + 1) % buf_size;

    // 5. Post-Gain
    out_sample_l *= post_gain_lin;
    out_sample_r *= post_gain_lin;

    out_l[i] = out_sample_l;
    out_r[i] = out_sample_r;

    // Peak tracking
    float out_abs = std::max(std::abs(out_sample_l), std::abs(out_sample_r));
    float out_db = (out_abs > 1e-10f) ? 20.0f * std::log10(out_abs) : -200.0f;
    if (out_db > current_peak_out) current_peak_out = out_db;

    float lim_gr_db = 20.0f * std::log10(std::max(limiter_envelope, 1e-10f));
    float total_gr_db = envelope_db + lim_gr_db;
    if (total_gr_db < current_max_gr) current_max_gr = total_gr_db;
  }

  max_gr_db = current_max_gr;
  peak_in_db = current_peak_in;
  peak_out_db = current_peak_out;
}

float BuiltinMaxim::BandProcessor::saturate(float x, float threshold,
                                            float amount) {
  if (amount <= 0.001f) return x;
  float abs_x = std::abs(x);
  if (abs_x <= threshold) return x;
  float sign = (x > 0.0f) ? 1.0f : -1.0f;
  float excess = abs_x - threshold;
  float clip =
      threshold + (1.0f - threshold) *
                      std::tanh(excess / std::max(1.0f - threshold, 0.0001f));
  return x * (1.0f - amount) + sign * clip * amount;
}

float BuiltinMaxim::BandProcessor::computeGainReduction(float input_db,
                                                        float threshold,
                                                        float ratio,
                                                        float knee_db) {
  if (input_db <= -100.0f) return 0.0f;
  float half_knee = knee_db / 2.0f;
  float gr_db = 0.0f;

  if (knee_db <= 0.01f) {
    if (input_db > threshold) {
      gr_db = (threshold - input_db) * (1.0f - 1.0f / ratio);
    }
  } else {
    float lower = threshold - half_knee;
    float upper = threshold + half_knee;
    if (input_db <= lower) {
      // no reduction
    } else if (input_db >= upper) {
      gr_db = (threshold - input_db) * (1.0f - 1.0f / ratio);
    } else {
      float x = input_db - lower;
      gr_db = -(1.0f - 1.0f / ratio) * x * x / (2.0f * knee_db);
    }
  }
  return gr_db;
}

// --- BuiltinMaxim Implementation ---

BuiltinMaxim::BuiltinMaxim() {
  for (int b = 0; b < 4; ++b) {
    bands_[b] = std::make_unique<BandProcessor>();
  }
  reset();
}

BuiltinMaxim::~BuiltinMaxim() = default;

bool BuiltinMaxim::load(const std::string& /*path*/, int /*plugin_index*/,
                        double sample_rate) {
  sample_rate_ = sample_rate;
  for (int b = 0; b < 4; ++b) {
    bands_[b]->init(sample_rate_);
  }
  reset();
  return true;
}

void BuiltinMaxim::reset() {
  // Set global defaults
  params_[PARAM_LOW_XOVER] = 0.461;   // ~88.3Hz
  params_[PARAM_HIGH_XOVER] = 0.436;  // ~2.5kHz
  params_[PARAM_LOOKAHEAD] = 0.2;     // ~1ms
  params_[PARAM_ENABLE] = 1.0;
  enabled_ = true;

  // Set band defaults
  for (int b = 0; b < 4; ++b) {
    int offset = 4 + b * 10;
    params_[offset + 0] = 0.5;  // Pre-gain: 0 dB
    params_[offset + 1] = 0.5;  // Post-gain: 0 dB
    params_[offset + 2] = 0.0;  // Sat Amount: 0
    params_[offset + 3] = 1.0;  // Sat Threshold: 0 dB
    params_[offset + 4] = 1.0;  // Compressor Threshold: 0 dB (off)
    params_[offset + 5] = 0.0;  // Ratio: 1:1
    params_[offset + 6] = 0.0;  // Knee: 0 dB
    params_[offset + 7] = 0.3;  // Attack: ~10ms
    params_[offset + 8] = 0.3;  // Release: ~100ms
    // Ceiling: Master defaults to -0.3 dB, others to 0 dB
    params_[offset + 9] = (b == 3) ? 0.975 : 1.0;
  }

  // Clear crossover states
  for (int s = 0; s < 2; ++s) {
    lp1_L_[s].reset();
    lp1_R_[s].reset();
    hp1_L_[s].reset();
    hp1_R_[s].reset();
    lp2_L_[s].reset();
    lp2_R_[s].reset();
    hp2_L_[s].reset();
    hp2_R_[s].reset();
    lp2_low_L_[s].reset();
    lp2_low_R_[s].reset();
    hp2_low_L_[s].reset();
    hp2_low_R_[s].reset();
  }

  for (int b = 0; b < 4; ++b) {
    bands_[b]->reset();
  }

  updateCrossoverCoeffs();
}

void BuiltinMaxim::updateCrossoverCoeffs() {
  float low_freq = normToFreq(params_[PARAM_LOW_XOVER], 20.0f, 500.0f);
  float high_freq = normToFreq(params_[PARAM_HIGH_XOVER], 500.0f, 20000.0f);
  for (int i = 0; i < 2; ++i) {
    lp1_L_[i].setParams(BiquadFilter::Type::LOWPASS, low_freq, 0.7071f, 0.0f,
                        (float)sample_rate_);
    lp1_R_[i].setParams(BiquadFilter::Type::LOWPASS, low_freq, 0.7071f, 0.0f,
                        (float)sample_rate_);
    hp1_L_[i].setParams(BiquadFilter::Type::HIGHPASS, low_freq, 0.7071f, 0.0f,
                        (float)sample_rate_);
    hp1_R_[i].setParams(BiquadFilter::Type::HIGHPASS, low_freq, 0.7071f, 0.0f,
                        (float)sample_rate_);

    lp2_L_[i].setParams(BiquadFilter::Type::LOWPASS, high_freq, 0.7071f, 0.0f,
                        (float)sample_rate_);
    lp2_R_[i].setParams(BiquadFilter::Type::LOWPASS, high_freq, 0.7071f, 0.0f,
                        (float)sample_rate_);
    hp2_L_[i].setParams(BiquadFilter::Type::HIGHPASS, high_freq, 0.7071f, 0.0f,
                        (float)sample_rate_);
    hp2_R_[i].setParams(BiquadFilter::Type::HIGHPASS, high_freq, 0.7071f, 0.0f,
                        (float)sample_rate_);

    lp2_low_L_[i].setParams(BiquadFilter::Type::LOWPASS, high_freq, 0.7071f,
                            0.0f, (float)sample_rate_);
    lp2_low_R_[i].setParams(BiquadFilter::Type::LOWPASS, high_freq, 0.7071f,
                            0.0f, (float)sample_rate_);
    hp2_low_L_[i].setParams(BiquadFilter::Type::HIGHPASS, high_freq, 0.7071f,
                            0.0f, (float)sample_rate_);
    hp2_low_R_[i].setParams(BiquadFilter::Type::HIGHPASS, high_freq, 0.7071f,
                            0.0f, (float)sample_rate_);
  }
}

void BuiltinMaxim::process(float** inputs, float** outputs, int num_samples,
                           const HostProcessContext& context,
                           const std::vector<MidiNoteEvent>& /*events*/,
                           float** /*sidechain*/) {
  if (sample_rate_ != context.sampleRate) {
    sample_rate_ = context.sampleRate;
    updateCrossoverCoeffs();
    for (int b = 0; b < 4; ++b) {
      bands_[b]->sample_rate = sample_rate_;
    }
  }

  // Handle transport reset
  if (last_time_samples_ >= 0 &&
      context.continuousTimeSamples < last_time_samples_) {
    for (int s = 0; s < 2; ++s) {
      lp1_L_[s].reset();
      lp1_R_[s].reset();
      hp1_L_[s].reset();
      hp1_R_[s].reset();
      lp2_L_[s].reset();
      lp2_R_[s].reset();
      hp2_L_[s].reset();
      hp2_R_[s].reset();
      lp2_low_L_[s].reset();
      lp2_low_R_[s].reset();
      hp2_low_L_[s].reset();
      hp2_low_R_[s].reset();
    }
    for (int b = 0; b < 4; ++b) {
      bands_[b]->reset();
    }
  }
  last_time_samples_ = context.continuousTimeSamples + num_samples;

  float* out_l = outputs[0];
  float* out_r = outputs[1];
  if (inputs && inputs != outputs) {
    for (int i = 0; i < num_samples; ++i) {
      out_l[i] = inputs[0][i];
      out_r[i] = inputs[1][i];
    }
  }

  if (!enabled_) return;

  updateCrossoverCoeffs();

  // Temporary buffers for band outputs
  std::vector<float> band_buf(kNumBands * 2 * num_samples, 0.0f);
  float* band_l[kNumBands];
  float* band_r[kNumBands];
  for (int b = 0; b < kNumBands; ++b) {
    band_l[b] = &band_buf[(b * 2) * num_samples];
    band_r[b] = &band_buf[(b * 2 + 1) * num_samples];
  }

  // 1. Crossover Split into 3 bands (Low, Mid, High)
  for (int i = 0; i < num_samples; ++i) {
    float in_l_samp = out_l[i];
    float in_r_samp = out_r[i];

    // Low split
    float lp1_l = lp1_L_[1].process(lp1_L_[0].process(in_l_samp));
    float hp1_l = hp1_L_[1].process(hp1_L_[0].process(in_l_samp));
    float lp1_r = lp1_R_[1].process(lp1_R_[0].process(in_r_samp));
    float hp1_r = hp1_R_[1].process(hp1_R_[0].process(in_r_samp));

    // High split
    float lp2_l = lp2_L_[1].process(lp2_L_[0].process(hp1_l));
    float hp2_l = hp2_L_[1].process(hp2_L_[0].process(hp1_l));
    float lp2_r = lp2_R_[1].process(lp2_R_[0].process(hp1_r));
    float hp2_r = hp2_R_[1].process(hp2_R_[0].process(hp1_r));

    // Align Low band phase by passing it through LP2 + HP2 at high_freq
    float lp2_low_l = lp2_low_L_[1].process(lp2_low_L_[0].process(lp1_l));
    float hp2_low_l = hp2_low_L_[1].process(hp2_low_L_[0].process(lp1_l));
    float lp2_low_r = lp2_low_R_[1].process(lp2_low_R_[0].process(lp1_r));
    float hp2_low_r = hp2_low_R_[1].process(hp2_low_R_[0].process(lp1_r));

    band_l[0][i] = lp2_low_l + hp2_low_l;  // Low (phase aligned)
    band_r[0][i] = lp2_low_r + hp2_low_r;
    band_l[1][i] = lp2_l;  // Mid
    band_r[1][i] = lp2_r;
    band_l[2][i] = hp2_l;  // High
    band_r[2][i] = hp2_r;
  }

  // 2. Process each split band
  float lookahead_ms = normToLookaheadMs(params_[PARAM_LOOKAHEAD]);
  for (int b = 0; b < kNumBands; ++b) {
    int offset = 4 + b * 10;
    bands_[b]->process(
        band_l[b], band_r[b], band_l[b], band_r[b], num_samples,
        normToGainDb(params_[offset + 0]), normToGainDb(params_[offset + 1]),
        (float)params_[offset + 2], normToSatThreshDb(params_[offset + 3]),
        normToThreshDb(params_[offset + 4]), normToRatio(params_[offset + 5]),
        normToKneeDb(params_[offset + 6]), normToAttackMs(params_[offset + 7]),
        normToReleaseMs(params_[offset + 8]),
        normToCeilingDb(params_[offset + 9]), lookahead_ms);
  }

  // 3. Sum band outputs
  std::vector<float> sum_l(num_samples, 0.0f);
  std::vector<float> sum_r(num_samples, 0.0f);
  for (int i = 0; i < num_samples; ++i) {
    for (int b = 0; b < kNumBands; ++b) {
      sum_l[i] += band_l[b][i];
      sum_r[i] += band_r[b][i];
    }
  }

  // 4. Process Master Band on summed signals
  int offset = 4 + 3 * 10;
  bands_[3]->process(
      sum_l.data(), sum_r.data(), out_l, out_r, num_samples,
      normToGainDb(params_[offset + 0]), normToGainDb(params_[offset + 1]),
      (float)params_[offset + 2], normToSatThreshDb(params_[offset + 3]),
      normToThreshDb(params_[offset + 4]), normToRatio(params_[offset + 5]),
      normToKneeDb(params_[offset + 6]), normToAttackMs(params_[offset + 7]),
      normToReleaseMs(params_[offset + 8]),
      normToCeilingDb(params_[offset + 9]), lookahead_ms);
}

int BuiltinMaxim::getParameterCount() const { return kTotalParams; }

bool BuiltinMaxim::getParameterInfo(int index, VstParamInfo& info) const {
  if (index < 0 || index >= kTotalParams) return false;
  info.id = index;

  if (index == PARAM_LOW_XOVER) {
    info.name = "LowXover";
    info.defaultValue = 0.461;
  } else if (index == PARAM_HIGH_XOVER) {
    info.name = "HighXover";
    info.defaultValue = 0.436;
  } else if (index == PARAM_LOOKAHEAD) {
    info.name = "Lookahead";
    info.defaultValue = 0.2;
  } else if (index == PARAM_ENABLE) {
    info.name = "Enable";
    info.defaultValue = 1.0;
  } else {
    int b = (index - 4) / 10;
    int p = (index - 4) % 10;
    std::string prefix;
    if (b == 0)
      prefix = "Low";
    else if (b == 1)
      prefix = "Mid";
    else if (b == 2)
      prefix = "High";
    else
      prefix = "Master";

    static const char* param_suffixes[] = {
        "PreGain", "PostGain", "SatAmount", "SatThresh", "Thresh",
        "Ratio",   "Knee",     "Attack",    "Release",   "Ceiling"};
    static const double defaults[] = {0.5, 0.5, 0.0, 1.0, 1.0,
                                      0.0, 0.0, 0.3, 0.3, 1.0};

    info.name = prefix + param_suffixes[p];
    info.defaultValue = (b == 3 && p == 9) ? 0.975 : defaults[p];
  }
  return true;
}

void BuiltinMaxim::setParameterValue(uint32_t id, double value) {
  if (id < kTotalParams) {
    params_[id] = value;
    if (id == PARAM_ENABLE) enabled_ = value >= 0.5;
  }
}

double BuiltinMaxim::getParameterValue(uint32_t id) const {
  return id < kTotalParams ? params_[id] : 0.0;
}

const std::string& BuiltinMaxim::getName() const { return kMaximName; }
const std::string& BuiltinMaxim::getPath() const { return kMaximPath; }
int BuiltinMaxim::getPluginIndex() const { return 0; }
bool BuiltinMaxim::isInstrument() const { return false; }

// --- Parameter Mappings ---

float BuiltinMaxim::normToFreq(double norm, float min_hz, float max_hz) {
  return min_hz * std::pow(max_hz / min_hz, (float)norm);
}

float BuiltinMaxim::normToGainDb(double norm) {
  return (float)(norm * 48.0 - 24.0);
}

float BuiltinMaxim::normToSatThreshDb(double norm) {
  return (float)(norm * 30.0 - 30.0);
}

float BuiltinMaxim::normToThreshDb(double norm) {
  return (float)(norm * 60.0 - 60.0);
}

float BuiltinMaxim::normToRatio(double norm) {
  if (norm >= 0.999) return 1000.0f;
  return 1.0f / (1.0f - (float)norm);
}

float BuiltinMaxim::normToKneeDb(double norm) { return (float)(norm * 24.0); }

float BuiltinMaxim::normToAttackMs(double norm) {
  return 0.1f * std::pow(1000.0f, (float)norm);
}

float BuiltinMaxim::normToReleaseMs(double norm) {
  return 10.0f * std::pow(100.0f, (float)norm);
}

float BuiltinMaxim::normToCeilingDb(double norm) {
  return (float)(norm * 12.0 - 12.0);
}

float BuiltinMaxim::normToLookaheadMs(double norm) {
  return 0.1f * std::pow(100.0f, (float)norm);  // 0.1 to 10ms
}

// --- Metering Getters ---

float BuiltinMaxim::getBandGainReductionDb(int band) const {
  if (band < 0 || band >= 4) return 0.0f;
  return bands_[band]->max_gr_db;
}

float BuiltinMaxim::getBandInputDb(int band) const {
  if (band < 0 || band >= 4) return -200.0f;
  return bands_[band]->peak_in_db;
}

float BuiltinMaxim::getBandOutputDb(int band) const {
  if (band < 0 || band >= 4) return -200.0f;
  return bands_[band]->peak_out_db;
}

}  // namespace hibiki
